#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <algorithm>
#include <map>
#include <random>

using namespace llvm;

struct ControlFlowFlatteningPass
    : public PassInfoMixin<ControlFlowFlatteningPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool Changed = false;
    unsigned int flattenedFunctions = 0;
    unsigned int flattenedBlocks = 0;

    for (Function &F : M) {
      // Skip declarations, intrinsics, and simple functions
      if (F.isDeclaration() || F.isIntrinsic() || F.size() < 3)
        continue;

      // Skip functions with exception handling or other complex features
      bool hasComplexFeatures = false;
      for (BasicBlock &BB : F) {
        if (BB.isEHPad() || BB.isLandingPad()) {
          hasComplexFeatures = true;
          break;
        }
      }
      if (hasComplexFeatures)
        continue;

      if (flattenFunction(F)) {
        Changed = true;
        flattenedFunctions++;
        flattenedBlocks += F.size() - 2; // Exclude entry and switch blocks
      }
    }

    // Output metrics to stderr instead of stdout
    if (Changed) {
      errs() << "CFF_METRICS:{\"flattenedFunctions\":" << flattenedFunctions
             << ",\"flattenedBlocks\":" << flattenedBlocks << "}\n";
      return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
  }

private:
  bool flattenFunction(Function &F) {
    // Collect all basic blocks except entry
    std::vector<BasicBlock *> blocks;
    BasicBlock *entryBlock = &F.getEntryBlock();

    for (BasicBlock &BB : F) {
      if (&BB != entryBlock) {
        blocks.push_back(&BB);
      }
    }

    if (blocks.size() < 2)
      return false;

    LLVMContext &Ctx = F.getContext();
    IRBuilder<> Builder(Ctx);

    // Create dispatch basic block and state variable
    BasicBlock *dispatchBlock =
        BasicBlock::Create(Ctx, "dispatch", &F, blocks[0]);

    // Create state variable at function entry
    Builder.SetInsertPoint(entryBlock, entryBlock->getFirstInsertionPt());
    AllocaInst *stateVar =
        Builder.CreateAlloca(Type::getInt32Ty(Ctx), nullptr, "state");

    // Insert state variable initialization at entry
    Builder.SetInsertPoint(entryBlock->getTerminator());
    Builder.CreateStore(Builder.getInt32(0), stateVar);

    // Replace entry block terminator with branch to dispatch
    entryBlock->getTerminator()->eraseFromParent();
    Builder.SetInsertPoint(entryBlock);
    Builder.CreateBr(dispatchBlock);

    // Create switch instruction in dispatch block
    Builder.SetInsertPoint(dispatchBlock);
    Value *stateVal =
        Builder.CreateLoad(Type::getInt32Ty(Ctx), stateVar, "state.val");

    // Create default case (should never be reached in normal flow)
    BasicBlock *defaultBlock = BasicBlock::Create(Ctx, "default", &F);

    // Create switch instruction while still in dispatch block
    SwitchInst *switchInst =
        Builder.CreateSwitch(stateVal, defaultBlock, blocks.size());

    // Now set up the default block
    Builder.SetInsertPoint(defaultBlock);
    Builder.CreateUnreachable();

    // Assign random state values to each block
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1000, 9999);
    std::map<BasicBlock *, int> blockStates;

    // First pass: assign state IDs to all blocks
    for (BasicBlock *BB : blocks) {
      int stateId = dist(gen);
      blockStates[BB] = stateId;
      switchInst->addCase(Builder.getInt32(stateId), BB);
    }

    // Second pass: update terminators
    for (BasicBlock *BB : blocks) {
      Instruction *term = BB->getTerminator();

      if (!term)
        continue;

      if (ReturnInst *ret = dyn_cast<ReturnInst>(term)) {
        // Return instructions remain unchanged
        continue;
      } else if (BranchInst *br = dyn_cast<BranchInst>(term)) {
        Builder.SetInsertPoint(term);

        if (br->isUnconditional()) {
          BasicBlock *successor = br->getSuccessor(0);
          if (blockStates.find(successor) != blockStates.end()) {
            Builder.CreateStore(Builder.getInt32(blockStates[successor]),
                                stateVar);
            Builder.CreateBr(dispatchBlock);
            term->eraseFromParent();
          }
        } else {
          // Handle conditional branches
          BasicBlock *trueBB = br->getSuccessor(0);
          BasicBlock *falseBB = br->getSuccessor(1);
          Value *condition = br->getCondition();

          // Create intermediate blocks for state updates
          BasicBlock *trueStateBlock =
              BasicBlock::Create(Ctx, "true.state", &F);
          BasicBlock *falseStateBlock =
              BasicBlock::Create(Ctx, "false.state", &F);

          // Replace the original branch with a branch to state blocks
          Builder.CreateCondBr(condition, trueStateBlock, falseStateBlock);

          // Set up true state block
          Builder.SetInsertPoint(trueStateBlock);
          if (blockStates.find(trueBB) != blockStates.end()) {
            Builder.CreateStore(Builder.getInt32(blockStates[trueBB]),
                                stateVar);
          } else if (trueBB == entryBlock) {
            Builder.CreateStore(Builder.getInt32(0), stateVar);
          }
          Builder.CreateBr(dispatchBlock);

          // Set up false state block
          Builder.SetInsertPoint(falseStateBlock);
          if (blockStates.find(falseBB) != blockStates.end()) {
            Builder.CreateStore(Builder.getInt32(blockStates[falseBB]),
                                stateVar);
          } else if (falseBB == entryBlock) {
            Builder.CreateStore(Builder.getInt32(0), stateVar);
          }
          Builder.CreateBr(dispatchBlock);

          // Erase the original branch
          term->eraseFromParent();
        }
      } else if (SwitchInst *sw = dyn_cast<SwitchInst>(term)) {
        // Handle switch instructions
        Builder.SetInsertPoint(term);

        // Create a new switch for dispatching
        SwitchInst *newSwitch = Builder.CreateSwitch(
            sw->getCondition(), nullptr, sw->getNumCases());

        // For each case, create an intermediate block
        for (auto &Case : sw->cases()) {
          BasicBlock *caseTarget = Case.getCaseSuccessor();
          BasicBlock *caseStateBlock = BasicBlock::Create(
              Ctx,
              "case.state." +
                  std::to_string(Case.getCaseValue()->getZExtValue()),
              &F);

          newSwitch->addCase(Case.getCaseValue(), caseStateBlock);

          Builder.SetInsertPoint(caseStateBlock);
          if (blockStates.find(caseTarget) != blockStates.end()) {
            Builder.CreateStore(Builder.getInt32(blockStates[caseTarget]),
                                stateVar);
          }
          Builder.CreateBr(dispatchBlock);
        }

        // Handle default case
        BasicBlock *defaultTarget = sw->getDefaultDest();
        BasicBlock *defaultStateBlock =
            BasicBlock::Create(Ctx, "default.state", &F);
        newSwitch->setDefaultDest(defaultStateBlock);

        Builder.SetInsertPoint(defaultStateBlock);
        if (blockStates.find(defaultTarget) != blockStates.end()) {
          Builder.CreateStore(Builder.getInt32(blockStates[defaultTarget]),
                              stateVar);
        }
        Builder.CreateBr(dispatchBlock);

        // Erase the original switch
        term->eraseFromParent();
      }
    }

    return true;
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ChakravyuhaControlFlowFlatteningPassPlugin",
          "v0.1", [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "chakravyuha-control-flow-flatten") {
                    MPM.addPass(ControlFlowFlatteningPass());
                    return true;
                  }
                  return false;
                });
          }};
}
