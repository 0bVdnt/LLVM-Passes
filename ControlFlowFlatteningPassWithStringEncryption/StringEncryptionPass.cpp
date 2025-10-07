#include "StringEncryptionPass.h"

#include "ChakravyuhaReport.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <random>
#include <string>
#include <vector>

using namespace llvm;

static std::vector<char> encryptString(StringRef S, uint8_t key) {
  std::vector<char> Encrypted(S.begin(), S.end());
  for (char &C : Encrypted)
    C ^= key;
  return Encrypted;
}

static FunctionCallee injectDecryptionStub(Module &M, uint8_t key) {
  LLVMContext &Ctx = M.getContext();
  Type *Int8PtrTy = PointerType::get(Ctx, 0);
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  Type *VoidTy = Type::getVoidTy(Ctx);
  FunctionType *DecryptFTy =
      FunctionType::get(VoidTy, {Int8PtrTy, Int8PtrTy, Int32Ty}, false);

  Function *DecryptF = M.getFunction("chakravyuha_decrypt_string");
  if (!DecryptF) {
    DecryptF = Function::Create(DecryptFTy, GlobalValue::PrivateLinkage,
                                "chakravyuha_decrypt_string", M);
    DecryptF->setCallingConv(CallingConv::C);
    DecryptF->addFnAttr(Attribute::NoInline);
    DecryptF->addFnAttr(Attribute::NoUnwind);

    Function::arg_iterator ArgIt = DecryptF->arg_begin();
    Argument *DestPtr = ArgIt++;
    DestPtr->setName("dest_ptr");
    Argument *SrcPtr = ArgIt++;
    SrcPtr->setName("src_ptr");
    Argument *Length = ArgIt++;
    Length->setName("length");

    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", DecryptF);
    IRBuilder<> Builder(EntryBB);
    BasicBlock *LoopHeader = BasicBlock::Create(Ctx, "loop_header", DecryptF);
    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop_body", DecryptF);
    BasicBlock *LoopExit = BasicBlock::Create(Ctx, "loop_exit", DecryptF);

    Builder.CreateBr(LoopHeader);
    Builder.SetInsertPoint(LoopHeader);
    PHINode *IndexPhi = Builder.CreatePHI(Int32Ty, 2, "index");
    IndexPhi->addIncoming(Builder.getInt32(0), EntryBB);
    Value *LoopCondition = Builder.CreateICmpSLT(IndexPhi, Length, "loop_cond");
    Builder.CreateCondBr(LoopCondition, LoopBody, LoopExit);

    Builder.SetInsertPoint(LoopBody);
    Value *SrcCharPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), SrcPtr,
                                          IndexPhi, "src_char_ptr");
    Value *LoadedByte =
        Builder.CreateLoad(Type::getInt8Ty(Ctx), SrcCharPtr, "loaded_byte");
    Value *DecryptedByte =
        Builder.CreateXor(LoadedByte, Builder.getInt8(key), "decrypted_byte");
    Value *DestCharPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), DestPtr,
                                           IndexPhi, "dest_char_ptr");
    Builder.CreateStore(DecryptedByte, DestCharPtr);
    Value *NextIndex =
        Builder.CreateAdd(IndexPhi, Builder.getInt32(1), "next_index");
    IndexPhi->addIncoming(NextIndex, LoopBody);
    Builder.CreateBr(LoopHeader);

    Builder.SetInsertPoint(LoopExit);
    Builder.CreateRetVoid();
  }

  return FunctionCallee(DecryptF->getFunctionType(), DecryptF);
}

llvm::PreservedAnalyses StringEncryptionPass::run(Module &M,
                                                  ModuleAnalysisManager &) {
  bool Changed = false;
  auto &R = chakravyuha::ReportData::get();
  R.enableStringEncryption = true;
  R.passesRun.push_back("StringEncrypt");

  std::vector<GlobalVariable *> StringGlobalsToEncrypt;
  unsigned int encryptedStringsCount = 0;
  uint64_t originalStringDataSize = 0;
  uint64_t encryptedStringDataSize = 0;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(1, 255);
  uint8_t randomKey = (uint8_t)distrib(gen);

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.isConstant() || !GV.hasInitializer())
      continue;
    if (!GV.getInitializer()->getType()->isAggregateType())
      continue;
    if (auto *CDA = dyn_cast<ConstantDataArray>(GV.getInitializer())) {
      if (CDA->isString()) {
        StringGlobalsToEncrypt.push_back(&GV);
      }
    }
  }

  if (StringGlobalsToEncrypt.empty()) {
    return llvm::PreservedAnalyses::all();
  }

  FunctionCallee DecryptFunc = injectDecryptionStub(M, randomKey);
  LLVMContext &Ctx = M.getContext();
  Type *Int8PtrTy = PointerType::get(Ctx, 0);
  for (GlobalVariable *GV : StringGlobalsToEncrypt) {
    StringRef OriginalStringRef =
        cast<ConstantDataArray>(GV->getInitializer())->getAsString();
    if (OriginalStringRef.empty())
      continue;

    encryptedStringsCount++;
    originalStringDataSize += OriginalStringRef.size();

    std::vector<char> EncryptedBytes =
        encryptString(OriginalStringRef, randomKey);
    if (!EncryptedBytes.empty())
      EncryptedBytes.back() = '\0' ^ randomKey;
    encryptedStringDataSize += EncryptedBytes.size();

    ArrayType *ArrTy =
        ArrayType::get(Type::getInt8Ty(Ctx), EncryptedBytes.size());
    Constant *EncryptedConst =
        ConstantDataArray::get(Ctx, ArrayRef<char>(EncryptedBytes));
    auto *EncryptedGV =
        new GlobalVariable(M, ArrTy, true, GlobalValue::PrivateLinkage,
                           EncryptedConst, GV->getName() + ".enc");

    // Ensure not dropped by DCE
    SmallVector<GlobalValue *, 1> Keep;
    Keep.push_back(EncryptedGV);
    appendToCompilerUsed(M, Keep);

    std::vector<Use *> UsesToReplace;
    for (Use &U : GV->uses()) {
      UsesToReplace.push_back(&U);
    }

    for (Use *U : UsesToReplace) {
      auto *InsertionPoint = dyn_cast<Instruction>(U->getUser());
      if (!InsertionPoint)
        continue;

      IRBuilder<> Builder(InsertionPoint);
      Value *Zero64 = Builder.getInt64(0);

      Value *EncryptedBasePtr = Builder.CreateInBoundsGEP(
          ArrTy, EncryptedGV, {Zero64, Zero64}, "encryptedPtr");
      Value *EncryptedArgPtr = Builder.CreateBitCast(
          EncryptedBasePtr, Int8PtrTy, "encryptedPtrCast");

      Value *DecryptedStringAlloca =
          Builder.CreateAlloca(ArrTy, nullptr, GV->getName() + ".dec.alloca");
      Value *DecryptedAllocaPtr = Builder.CreateBitCast(
          DecryptedStringAlloca, Int8PtrTy, "decryptedAllocaPtrCast");

      Builder.CreateCall(DecryptFunc,
                         {DecryptedAllocaPtr, EncryptedArgPtr,
                          Builder.getInt32((int)EncryptedBytes.size())});

      U->set(DecryptedAllocaPtr);
      Changed = true;
    }

    GV->eraseFromParent();
  }

  R.stringsEncrypted += encryptedStringsCount;
  R.originalIRStringDataSize += originalStringDataSize;
  R.obfuscatedIRStringDataSize += encryptedStringDataSize;
  R.stringMethod = "XOR with dynamic per-run key";

  if (Changed)
    return llvm::PreservedAnalyses::none();
  return llvm::PreservedAnalyses::all();
}
