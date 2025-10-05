#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"

#include <optional>
#include <set>
#include <vector>

using namespace llvm;

namespace {

// Simpler demotion that doesn't try to split edges
static void demoteValuesToMemory(Function &F) {
  BasicBlock &Entry = F.getEntryBlock();
  IRBuilder<> AllocaBuilder(&Entry, Entry.getFirstInsertionPt());

  // Map from original value to its stack slot
  DenseMap<Value *, AllocaInst *> ValueToAlloca;

  // First, collect all PHI nodes
  std::vector<PHINode *> PhisToRemove;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (auto *PN = dyn_cast<PHINode>(&I)) {
        PhisToRemove.push_back(PN);
      }
    }
  }

  // Demote each PHI node with a simpler approach
  for (PHINode *PN : PhisToRemove) {
    // Create alloca for this PHI
    AllocaInst *Alloca = AllocaBuilder.CreateAlloca(
        PN->getType(), nullptr, PN->getName() + ".phialloca");

    // Initialize with undef to handle cases where no store executes
    IRBuilder<> InitBuilder(Entry.getTerminator());
    InitBuilder.CreateStore(UndefValue::get(PN->getType()), Alloca);

    // For each incoming value, store it at the END of the incoming block
    // This ensures the store happens only when we actually take that path
    for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i) {
      Value *IncomingVal = PN->getIncomingValue(i);
      BasicBlock *IncomingBB = PN->getIncomingBlock(i);

      // Insert store right before the terminator
      IRBuilder<> StoreBuilder(IncomingBB->getTerminator());
      StoreBuilder.CreateStore(IncomingVal, Alloca);
    }

    // Replace all uses of PHI with loads from the alloca
    std::vector<Use *> UsesToReplace;
    for (Use &U : PN->uses()) {
      UsesToReplace.push_back(&U);
    }

    for (Use *U : UsesToReplace) {
      if (auto *UserInst = dyn_cast<Instruction>(U->getUser())) {
        // Insert load at the beginning of the PHI's block
        IRBuilder<> LoadBuilder(PN->getParent(),
                                PN->getParent()->getFirstInsertionPt());
        LoadInst *Load = LoadBuilder.CreateLoad(PN->getType(), Alloca,
                                                PN->getName() + ".reload");
        U->set(Load);
      }
    }

    // Remove the PHI
    PN->eraseFromParent();
  }

  // Now handle regular instructions that are used across basic blocks
  std::vector<Instruction *> ToDemote;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (I.isTerminator() || isa<AllocaInst>(I) || isa<PHINode>(I))
        continue;

      // Check if used outside its block
      bool UsedOutside = false;
      for (User *U : I.users()) {
        if (auto *UI = dyn_cast<Instruction>(U)) {
          if (UI->getParent() != &BB) {
            UsedOutside = true;
            break;
          }
        }
      }

      if (UsedOutside) {
        ToDemote.push_back(&I);
      }
    }
  }

  // Demote each instruction
  for (Instruction *I : ToDemote) {
    // Create alloca
    AllocaInst *Alloca = AllocaBuilder.CreateAlloca(I->getType(), nullptr,
                                                    I->getName() + ".alloca");

    // Store the value right after it's computed
    IRBuilder<> StoreBuilder(I);
    StoreBuilder.SetInsertPoint(I->getParent(), ++I->getIterator());
    StoreBuilder.CreateStore(I, Alloca);

    // Replace all uses with loads
    std::vector<Use *> UsesToReplace;
    for (Use &U : I->uses()) {
      UsesToReplace.push_back(&U);
    }

    for (Use *U : UsesToReplace) {
      if (auto *UserInst = dyn_cast<Instruction>(U->getUser())) {
        // Skip the store we just created
        if (auto *SI = dyn_cast<StoreInst>(UserInst)) {
          if (SI->getPointerOperand() == Alloca)
            continue;
        }

        // Insert load right before the user
        IRBuilder<> LoadBuilder(UserInst);
        LoadInst *Load = LoadBuilder.CreateLoad(I->getType(), Alloca,
                                                I->getName() + ".reload");
        U->set(Load);
      }
    }
  }
}

static Value *buildNextStateForTerm(IRBuilder<> &B, Instruction *T,
                                    DenseMap<BasicBlock *, unsigned> &Id,
                                    unsigned DefaultState = 0) {
  if (auto *Br = dyn_cast<BranchInst>(T)) {
    if (Br->isUnconditional()) {
      auto It = Id.find(Br->getSuccessor(0));
      if (It != Id.end()) {
        return B.getInt32(It->second);
      }
      return nullptr; // Successor not in flattened blocks
    }

    auto It1 = Id.find(Br->getSuccessor(0));
    auto It2 = Id.find(Br->getSuccessor(1));

    if (It1 != Id.end() && It2 != Id.end()) {
      // Both successors are flattened
      Value *TState = B.getInt32(It1->second);
      Value *FState = B.getInt32(It2->second);
      return B.CreateSelect(Br->getCondition(), TState, FState, "cff.next");
    } else if (It1 != Id.end() && It2 == Id.end()) {
      // Only true successor is flattened, need to handle this specially
      // Can't just return the state because we might not take that branch
      return nullptr;
    } else if (It1 == Id.end() && It2 != Id.end()) {
      // Only false successor is flattened
      return nullptr;
    }

    return nullptr;
  }

  if (auto *Sw = dyn_cast<SwitchInst>(T)) {
    // First check if any successor is flattened
    bool HasFlattenedSuccessor = false;
    auto DefaultIt = Id.find(Sw->getDefaultDest());
    if (DefaultIt != Id.end()) {
      HasFlattenedSuccessor = true;
    }

    for (auto &C : Sw->cases()) {
      if (Id.find(C.getCaseSuccessor()) != Id.end()) {
        HasFlattenedSuccessor = true;
        break;
      }
    }

    if (!HasFlattenedSuccessor) {
      return nullptr;
    }

    // Build switch expression using selects
    Value *Cond = Sw->getCondition();
    Value *NS = (DefaultIt != Id.end()) ? B.getInt32(DefaultIt->second)
                                        : B.getInt32(DefaultState);

    for (auto &C : Sw->cases()) {
      auto CaseIt = Id.find(C.getCaseSuccessor());
      if (CaseIt != Id.end()) {
        Value *Is = B.CreateICmpEQ(Cond, C.getCaseValue());
        Value *S = B.getInt32(CaseIt->second);
        NS = B.CreateSelect(Is, S, NS, "cff.case.select");
      }
    }
    return NS;
  }

  return nullptr;
}

static bool isSupportedTerminator(Instruction *T) {
  return isa<BranchInst>(T) || isa<SwitchInst>(T) || isa<ReturnInst>(T) ||
         isa<UnreachableInst>(T);
}

static bool hasUnsupportedControlFlow(Function &F) {
  for (BasicBlock &BB : F) {
    if (BB.isEHPad() || BB.isLandingPad()) {
      return true;
    }

    Instruction *T = BB.getTerminator();
    if (!isSupportedTerminator(T)) {
      return true;
    }
  }
  return false;
}

struct ControlFlowFlatteningPass
    : public PassInfoMixin<ControlFlowFlatteningPass> {

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool Changed = false;
    unsigned int flattenedFunctions = 0;
    unsigned int flattenedBlocks = 0;
    unsigned int skippedFunctions = 0;

    for (Function &F : M) {
      if (F.isDeclaration() || F.isIntrinsic())
        continue;
      if (F.size() < 2)
        continue;

      if (hasUnsupportedControlFlow(F)) {
        skippedFunctions++;
        continue;
      }

      unsigned blocksBefore = F.size();
      if (flattenFunction(F)) {
        Changed = true;
        flattenedFunctions++;
        flattenedBlocks += blocksBefore - 1;
      }
    }

    if (Changed || skippedFunctions > 0) {
      errs() << "CFF_METRICS:{\"flattenedFunctions\":" << flattenedFunctions
             << ",\"flattenedBlocks\":" << flattenedBlocks
             << ",\"skippedFunctions\":" << skippedFunctions << "}\n";
      if (Changed)
        return PreservedAnalyses::none();
    }
    return PreservedAnalyses::all();
  }

private:
  bool flattenFunction(Function &F) {
    if (F.isDeclaration() || F.isIntrinsic() || F.size() < 2)
      return false;

    if (hasUnsupportedControlFlow(F))
      return false;

    LLVMContext &Ctx = F.getContext();

    // 1) Demote all SSA values to memory before CFG transformation
    demoteValuesToMemory(F);

    BasicBlock *Entry = &F.getEntryBlock();

    // 2) Collect all blocks except entry that should be flattened
    SmallVector<BasicBlock *, 32> OriginalBlocks;
    for (BasicBlock &BB : F) {
      OriginalBlocks.push_back(&BB);
    }

    DenseMap<BasicBlock *, unsigned> BlockId;
    SmallVector<BasicBlock *, 32> FlattenTargets;
    unsigned NextId = 1;

    // Only flatten blocks that are not pure returns/unreachables
    for (BasicBlock *BB : OriginalBlocks) {
      if (BB == Entry)
        continue;

      // Include all non-entry blocks in flattening
      BlockId[BB] = NextId++;
      FlattenTargets.push_back(BB);
    }

    if (FlattenTargets.empty())
      return false;

    // 3) Create state variable
    IRBuilder<> EntryBuilder(Ctx);
    EntryBuilder.SetInsertPoint(Entry, Entry->getFirstInsertionPt());
    AllocaInst *StateVar =
        EntryBuilder.CreateAlloca(Type::getInt32Ty(Ctx), nullptr, "cff.state");

    // 4) Create dispatcher and default blocks
    BasicBlock *Dispatcher = BasicBlock::Create(Ctx, "cff.dispatch", &F);
    BasicBlock *DefaultBlock = BasicBlock::Create(Ctx, "cff.default", &F);
    IRBuilder<> DefaultBuilder(DefaultBlock);
    DefaultBuilder.CreateUnreachable();

    // 5) Set initial state based on entry's original successors
    Instruction *EntryTerm = Entry->getTerminator();
    {
      IRBuilder<> InitBuilder(EntryTerm);

      // Handle the initial state more carefully
      if (auto *Br = dyn_cast<BranchInst>(EntryTerm)) {
        if (Br->isUnconditional()) {
          auto It = BlockId.find(Br->getSuccessor(0));
          if (It != BlockId.end()) {
            InitBuilder.CreateStore(InitBuilder.getInt32(It->second), StateVar);
          } else {
            return false; // Entry goes to non-flattened block
          }
        } else {
          // Conditional branch from entry
          auto It1 = BlockId.find(Br->getSuccessor(0));
          auto It2 = BlockId.find(Br->getSuccessor(1));
          if (It1 != BlockId.end() && It2 != BlockId.end()) {
            Value *InitState = InitBuilder.CreateSelect(
                Br->getCondition(), InitBuilder.getInt32(It1->second),
                InitBuilder.getInt32(It2->second), "cff.init");
            InitBuilder.CreateStore(InitState, StateVar);
          } else {
            return false; // Entry has non-flattened successor
          }
        }
      } else {
        Value *InitialState =
            buildNextStateForTerm(InitBuilder, EntryTerm, BlockId);
        if (InitialState) {
          InitBuilder.CreateStore(InitialState, StateVar);
        } else {
          return false;
        }
      }
    }

    // Replace entry terminator
    EntryTerm->eraseFromParent();
    IRBuilder<> NewEntryBuilder(Entry);
    NewEntryBuilder.CreateBr(Dispatcher);

    // 6) Build dispatcher switch
    IRBuilder<> DispatchBuilder(Dispatcher);
    Value *CurrentState =
        DispatchBuilder.CreateLoad(Type::getInt32Ty(Ctx), StateVar, "cff.cur");
    SwitchInst *DispatchSwitch = DispatchBuilder.CreateSwitch(
        CurrentState, DefaultBlock, FlattenTargets.size());

    for (BasicBlock *BB : FlattenTargets) {
      DispatchSwitch->addCase(DispatchBuilder.getInt32(BlockId[BB]), BB);
    }

    // 7) Rewrite terminators in flattened blocks
    for (BasicBlock *BB : FlattenTargets) {
      Instruction *Term = BB->getTerminator();

      // Keep returns and unreachables as-is
      if (isa<ReturnInst>(Term) || isa<UnreachableInst>(Term))
        continue;

      IRBuilder<> TermBuilder(Term);

      // Handle different terminator types
      if (auto *Br = dyn_cast<BranchInst>(Term)) {
        if (Br->isUnconditional()) {
          auto It = BlockId.find(Br->getSuccessor(0));
          if (It != BlockId.end()) {
            TermBuilder.CreateStore(TermBuilder.getInt32(It->second), StateVar);
            TermBuilder.CreateBr(Dispatcher);
            Term->eraseFromParent();
          }
          // If successor is not flattened, leave branch as-is
        } else {
          // Conditional branch
          auto It1 = BlockId.find(Br->getSuccessor(0));
          auto It2 = BlockId.find(Br->getSuccessor(1));

          if (It1 != BlockId.end() && It2 != BlockId.end()) {
            // Both successors are flattened
            Value *NextState = TermBuilder.CreateSelect(
                Br->getCondition(), TermBuilder.getInt32(It1->second),
                TermBuilder.getInt32(It2->second), "cff.next");
            TermBuilder.CreateStore(NextState, StateVar);
            TermBuilder.CreateBr(Dispatcher);
            Term->eraseFromParent();
          }
          // If only one or neither successor is flattened, leave branch as-is
        }
      } else if (auto *Sw = dyn_cast<SwitchInst>(Term)) {
        Value *NextState = buildNextStateForTerm(TermBuilder, Term, BlockId);
        if (NextState) {
          TermBuilder.CreateStore(NextState, StateVar);
          TermBuilder.CreateBr(Dispatcher);
          Term->eraseFromParent();
        }
      }
    }

    // 8) Clean up
    removeUnreachableBlocks(F);

    return true;
  }
};

} // end anonymous namespace

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
