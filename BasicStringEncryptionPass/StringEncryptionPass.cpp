// StringEncryptionPass.cpp (Fixed version)

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;

static const char XOR_KEY = 0xAB;

std::vector<char> encryptString(StringRef S) {
  std::vector<char> Encrypted(S.begin(), S.end());
  for (char &C : Encrypted) {
    C ^= XOR_KEY;
  }
  return Encrypted;
}

struct StringEncryptionPass : public PassInfoMixin<StringEncryptionPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool Changed = false;
    std::vector<GlobalVariable *> StringGlobalsToEncrypt;

    for (GlobalVariable &GV : M.globals()) {
      if (GV.isConstant() && GV.hasInitializer() &&
          GV.getInitializer()->getType()->isAggregateType()) {
        if (ConstantDataArray *CDA =
                dyn_cast<ConstantDataArray>(GV.getInitializer())) {
          if (CDA->isString()) {
            if (GV.getName().starts_with(".str.") ||
                GV.getName().starts_with(".str")) {
              StringGlobalsToEncrypt.push_back(&GV);
            }
          }
        }
      }
    }

    if (StringGlobalsToEncrypt.empty()) {
      return PreservedAnalyses::all();
    }

    FunctionCallee DecryptFunc = injectDecryptionStub(M);
    LLVMContext &Ctx = M.getContext();
    PointerType *Int8PtrTy = PointerType::get(Ctx, 0);
    Type *Int32Ty = Type::getInt32Ty(Ctx);

    for (GlobalVariable *GV : StringGlobalsToEncrypt) {
      StringRef OriginalStringRef =
          cast<ConstantDataArray>(GV->getInitializer())->getAsString();

      if (OriginalStringRef.empty())
        continue;

      errs() << "Chakravyuha StringEncrypt: Encrypting string -> "
             << OriginalStringRef << "\n";

      std::vector<char> EncryptedBytes = encryptString(OriginalStringRef);
      // Keep the null terminator
      EncryptedBytes.back() = '\0' ^ XOR_KEY; // Encrypt the null terminator too

      ArrayType *ArrTy =
          ArrayType::get(Type::getInt8Ty(Ctx), EncryptedBytes.size());
      Constant *EncryptedConst =
          ConstantDataArray::get(Ctx, ArrayRef<char>(EncryptedBytes));

      GlobalVariable *EncryptedGV =
          new GlobalVariable(M, ArrTy, true, GlobalValue::PrivateLinkage,
                             EncryptedConst, GV->getName() + ".enc");
      appendToCompilerUsed(M, {EncryptedGV});

      std::vector<Use *> UsesToReplace;
      for (Use &U : GV->uses()) {
        UsesToReplace.push_back(&U);
      }

      for (Use *U : UsesToReplace) {
        User *CurrentUser = U->getUser();
        Instruction *InsertionPoint = nullptr;

        if (Instruction *Inst = dyn_cast<Instruction>(CurrentUser)) {
          InsertionPoint = Inst;
        } else if (Constant *C = dyn_cast<Constant>(CurrentUser)) {
          errs() << "Chakravyuha StringEncrypt: Warning - Constant user of GV "
                    "not handled, skipping: "
                 << *C << "\n";
          continue;
        } else {
          errs() << "Chakravyuha StringEncrypt: Warning - Unexpected user type "
                    "of GV, skipping: "
                 << *CurrentUser << "\n";
          continue;
        }

        if (!InsertionPoint)
          continue;

        IRBuilder<> Builder(InsertionPoint);

        Value *Zero = Builder.getInt64(0);
        Value *EncryptedBasePtr = Builder.CreateInBoundsGEP(
            ArrTy, EncryptedGV, {Zero, Zero}, "encryptedPtr");

        Value *EncryptedArgPtr = EncryptedBasePtr;
        if (EncryptedArgPtr->getType() != Int8PtrTy) {
          EncryptedArgPtr = Builder.CreateBitCast(EncryptedArgPtr, Int8PtrTy,
                                                  "encryptedPtrCast");
        }

        // Allocate space on the stack for the decrypted string
        Value *DecryptedStringAlloca = Builder.CreateAlloca(
            ArrTy,   // Allocate array type
            nullptr, // No explicit array size (ArrTy implies size)
            GV->getName() + ".dec.alloca");

        // Cast the alloca pointer to i8* for the decryption function
        Value *DecryptedAllocaPtr = DecryptedStringAlloca;
        if (DecryptedAllocaPtr->getType() != Int8PtrTy) {
          DecryptedAllocaPtr = Builder.CreateBitCast(
              DecryptedAllocaPtr, Int8PtrTy, "decryptedAllocaPtrCast");
        }

        // Call the decryption stub with:
        // 1. Destination buffer (decrypted alloca)
        // 2. Source buffer (encrypted string)
        // 3. Length (including null terminator)
        Builder.CreateCall(
            DecryptFunc,
            {DecryptedAllocaPtr, // Destination
             EncryptedArgPtr,    // Source (encrypted)
             Builder.getInt32(
                 EncryptedBytes.size())}, // Full size including null
            "");

        // Replace the original use with the pointer to the decrypted string
        U->set(DecryptedAllocaPtr);
        Changed = true;
      }

      if (!GV->user_empty()) {
        errs() << "Chakravyuha StringEncrypt: ERROR - Original GV still has "
                  "users after replacement: "
               << *GV << "\n";
        for (User *U_dbg : GV->users()) {
          errs() << "  Remaining user: " << *U_dbg << "\n";
        }
      }
      GV->eraseFromParent();
    }

    if (Changed) {
      return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
  }

  // Helper function to inject the decryption stub function into the module
  FunctionCallee injectDecryptionStub(Module &M) {
    LLVMContext &Ctx = M.getContext();
    Type *Int8PtrTy = PointerType::get(Ctx, 0);
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *VoidTy = Type::getVoidTy(Ctx);

    // Changed signature: void decrypt(char *dest, const char *src, int32_t
    // length)
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

      Value *LoopCondition =
          Builder.CreateICmpSLT(IndexPhi, Length, "loop_cond");
      Builder.CreateCondBr(LoopCondition, LoopBody, LoopExit);

      Builder.SetInsertPoint(LoopBody);

      // Read from source
      Value *SrcCharPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), SrcPtr,
                                            IndexPhi, "src_char_ptr");
      Value *LoadedByte =
          Builder.CreateLoad(Type::getInt8Ty(Ctx), SrcCharPtr, "loaded_byte");

      // Decrypt
      Value *DecryptedByte = Builder.CreateXor(
          LoadedByte, Builder.getInt8(XOR_KEY), "decrypted_byte");

      // Write to destination
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

  static StringRef name() { return "chakravyuha-string-encrypt"; }
  static bool isRequired() { return true; }
  bool skipFunction(const Function &F) const { return false; }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ChakravyuhaStringEncryptionPassPlugin",
          "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "chakravyuha-string-encrypt") {
                    MPM.addPass(StringEncryptionPass());
                    return true;
                  }
                  return false;
                });
          }};
}
