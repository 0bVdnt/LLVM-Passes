// StringEncryptionPass.cpp
#include "llvm/IR/Constants.h" // For creating constant arrays, strings
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h" // For working with global strings
#include "llvm/IR/IRBuilder.h"      // For easily creating new instructions
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h" // For `appendToCompilerUsed`

#include <algorithm> // For std::reverse
#include <string>
#include <vector>

using namespace llvm;

// Define a simple XOR encryption key
static const char XOR_KEY = 0xAB;

// Helper function for encryption (can be more complex later)
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

    // 1. Identify global string constants
    for (GlobalVariable &GV : M.globals()) {
      if (GV.isConstant() && GV.hasInitializer() &&
          GV.getInitializer()->getType()->isAggregateType()) {
        if (ConstantDataArray *CDA =
                dyn_cast<ConstantDataArray>(GV.getInitializer())) {
          if (CDA->isString()) {
            // Exclude common LLVM debug info strings or known internal strings
            // FIX: Changed .startswith to .starts_with for StringRef
            if (GV.getName().starts_with(".str.") ||
                GV.getName().starts_with(
                    ".str")) { // Common string literal names
                               // This is a candidate for encryption
              StringGlobalsToEncrypt.push_back(&GV);
            }
          }
        }
      }
    }

    if (StringGlobalsToEncrypt.empty()) {
      return PreservedAnalyses::all(); // No strings to encrypt
    }

    // 2. Inject decryption stub function (only once)
    FunctionCallee DecryptFunc = injectDecryptionStub(M);

    // 3. Encrypt and replace uses for each identified string
    for (GlobalVariable *GV : StringGlobalsToEncrypt) {
      StringRef OriginalString =
          cast<ConstantDataArray>(GV->getInitializer())->getAsString();

      // Skip empty strings
      if (OriginalString.empty())
        continue;

      errs() << "Chakravyuha StringEncrypt: Encrypting string -> "
             << OriginalString << "\n";

      // Encrypt the string
      std::vector<char> EncryptedBytes = encryptString(OriginalString);
      EncryptedBytes.push_back('\0');
      // Create a new global variable for the encrypted bytes
      // +1 for null terminator, as LLVM's getAsString() usually includes it
      ArrayType *ArrTy = ArrayType::get(Type::getInt8Ty(M.getContext()),
                                        EncryptedBytes.size());
      Constant *EncryptedConst = ConstantDataArray::get(
          M.getContext(), ArrayRef<char>(EncryptedBytes));

      GlobalVariable *EncryptedGV = new GlobalVariable(
          M, ArrTy, true, GlobalValue::PrivateLinkage,
          ConstantAggregateZero::get(
              ArrTy), // Initialize with zeros, we'll fill it if needed, though
                      // EncryptedConst sets it
          GV->getName() + ".enc");
      EncryptedGV->setInitializer(EncryptedConst); // Set the encrypted content

      // Add the new encrypted global to the "llvm.compiler.used" list
      // to prevent it from being optimized out if no direct uses are found
      appendToCompilerUsed(M, {EncryptedGV});

      // Iterate over all uses of the original global variable
      std::vector<User *> Users(
          GV->user_begin(),
          GV->user_end()); // Copy users to avoid iterator invalidation
      for (User *U : Users) {
        if (Instruction *Inst = dyn_cast<Instruction>(U)) {
          IRBuilder<> Builder(
              Inst); // Create builder at the instruction's location

          // Get a pointer to the start of the encrypted data
          Value *EncryptedPtr = Builder.CreateConstGEP2_32(
              ArrTy, EncryptedGV, 0, 0, "encryptedPtr");

          // Call the decryption stub
          Value *DecryptedPtr = Builder.CreateCall(
              DecryptFunc,
              {EncryptedPtr,
               Builder.getInt32(
                   EncryptedBytes.size())}, // Pass encrypted ptr and length
              "decryptedPtr");

          // Replace all uses of the original GV (or its GEP) with the decrypted
          // pointer
          Inst->replaceUsesOfWith(GV, DecryptedPtr);
          Changed = true;
        }
      }
      // Now that all uses are replaced, we can erase the original
      // GlobalVariable
      GV->eraseFromParent();
    }

    if (Changed) {
      return PreservedAnalyses::none(); // We modified the IR significantly
    }
    return PreservedAnalyses::all();
  }

  // Helper to inject the decryption function into the module
  FunctionCallee injectDecryptionStub(Module &M) {
    LLVMContext &Ctx = M.getContext();
    Type *VoidTy = Type::getVoidTy(Ctx);
    // FIX: Changed Type::getInt8PtrTy(Ctx) to
    // Type::getInt8Ty(Ctx)->getPointerTo()
    Type *Int8PtrTy = Type::getInt8Ty(Ctx)->getPointerTo();
    Type *Int32Ty = Type::getInt32Ty(Ctx);

    // Define the function type: char* decrypt_string(char* encrypted_ptr, int
    // length)
    FunctionType *DecryptFTy = FunctionType::get(
        Int8PtrTy, {Int8PtrTy, Int32Ty}, false); // Returns char*

    // Get or create the function
    Function *DecryptF = M.getFunction("chakravyuha_decrypt_string");
    if (!DecryptF) {
      DecryptF = Function::Create(DecryptFTy, GlobalValue::PrivateLinkage,
                                  "chakravyuha_decrypt_string", M);
      DecryptF->setCallingConv(CallingConv::C);

      // Add attributes for better compatibility
      DecryptF->addFnAttr(
          Attribute::NoInline); // Prevents inlining, keeping stub separate
      DecryptF->addFnAttr(Attribute::NoUnwind);

      // Create the entry basic block
      BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", DecryptF);
      IRBuilder<> Builder(EntryBB);

      // Get arguments
      Argument *EncryptedPtr = DecryptF->arg_begin();
      EncryptedPtr->setName("encrypted_ptr");
      Argument *Length = DecryptF->arg_begin() + 1;
      Length->setName("length");

      // Loop through the encrypted bytes and XOR them
      Value *LoopVar = Builder.CreateAlloca(Int32Ty, nullptr, "loop_var");
      Builder.CreateStore(Builder.getInt32(0), LoopVar);

      BasicBlock *LoopCondBB = BasicBlock::Create(Ctx, "loop_cond", DecryptF);
      BasicBlock *LoopBodyBB = BasicBlock::Create(Ctx, "loop_body", DecryptF);
      BasicBlock *LoopEndBB = BasicBlock::Create(Ctx, "loop_end", DecryptF);

      Builder.CreateBr(LoopCondBB); // Jump to condition

      // Loop Condition
      Builder.SetInsertPoint(LoopCondBB);
      Value *CurrentIdx = Builder.CreateLoad(Int32Ty, LoopVar, "current_idx");
      Value *Condition = Builder.CreateICmpSLT(CurrentIdx, Length, "loop_cond");
      Builder.CreateCondBr(Condition, LoopBodyBB, LoopEndBB);

      // Loop Body
      Builder.SetInsertPoint(LoopBodyBB);
      Value *ElementPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), EncryptedPtr,
                                            CurrentIdx, "element_ptr");
      Value *LoadedByte =
          Builder.CreateLoad(Type::getInt8Ty(Ctx), ElementPtr, "loaded_byte");
      Value *DecryptedByte = Builder.CreateXor(
          LoadedByte, Builder.getInt8(XOR_KEY), "decrypted_byte");
      Builder.CreateStore(DecryptedByte,
                          ElementPtr); // Write back decrypted byte

      Value *NextIdx =
          Builder.CreateAdd(CurrentIdx, Builder.getInt32(1), "next_idx");
      Builder.CreateStore(NextIdx, LoopVar);
      Builder.CreateBr(LoopCondBB); // Jump back to condition

      // Loop End
      Builder.SetInsertPoint(LoopEndBB);
      Builder.CreateRet(
          EncryptedPtr); // Return pointer to the now-decrypted string
    }
    return FunctionCallee(DecryptF->getFunctionType(), DecryptF);
  }

  static StringRef name() { return "chakravyuha-string-encrypt"; }
  static bool isRequired() { return true; }
  bool skipFunction(const Function &F) const {
    return false;
  } // Always run on functions within module
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ChakravyuhaStringEncryptionPassPlugin",
          "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name,
                   ModulePassManager &MPM, // Changed to ModulePassManager
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "chakravyuha-string-encrypt") {
                    MPM.addPass(StringEncryptionPass()); // Add as a Module pass
                    return true;
                  }
                  return false;
                });
          }};
}
