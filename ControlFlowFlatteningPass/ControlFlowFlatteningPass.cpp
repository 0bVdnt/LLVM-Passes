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

static void demoteValuesToMemory(Function &F) {
  BasicBlock &Entry = F.getEntryBlock();
  IRBuilder<> AllocaBuilder(&Entry, Entry.getFirstInsertionPt());

  DenseMap<Value *, AllocaInst *> ValueToAlloca;

  std::vector<PHINode *> PhisToRemove;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (auto *PN = dyn_cast<PHINode>(&I)) {
        PhisToRemove.push_back(PN);
      }
    }
  }

  for (PHINode *PN : PhisToRemove) {
    AllocaInst *Alloca = AllocaBuilder.CreateAlloca(
        PN->getType(), nullptr, PN->getName() + ".phialloca");
    ValueToAlloca[PN] = Alloca;

    for (unsigned i = 0; i < PN->getNumIncomingValues(); ++i) {
      Value *IncomingVal = PN->getIncomingValue(i);
      BasicBlock *IncomingBB = PN->getIncomingBlock(i);

      IRBuilder<> StoreBuilder(IncomingBB->getTerminator());
      StoreBuilder.CreateStore(IncomingVal, Alloca);
    }

    std::vector<Use *> UsesToReplace;
    for (Use &U : PN->uses()) {
      UsesToReplace.push_back(&U);
    }

    for (Use *U : UsesToReplace) {
      if (auto *UserInst = dyn_cast<Instruction>(U->getUser())) {
        IRBuilder<> LoadBuilder(UserInst);
        LoadInst *Load = LoadBuilder.CreateLoad(PN->getType(), Alloca,
                                                PN->getName() + ".reload");
        U->set(Load);
      }
    }

    PN->eraseFromParent();
  }

  std::vector<Instruction *> ToDemote;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (I.isTerminator() || isa<AllocaInst>(I) || isa<PHINode>(I))
        continue;

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

  for (Instruction *I : ToDemote) {
    AllocaInst *Alloca = AllocaBuilder.CreateAlloca(I->getType(), nullptr,
                                                    I->getName() + ".alloca");

    IRBuilder<> StoreBuilder(I);
    StoreBuilder.SetInsertPoint(I->getParent(), ++I->getIterator());
    StoreBuilder.CreateStore(I, Alloca);

    std::vector<Use *> UsesToReplace;
    for (Use &U : I->uses()) {
      UsesToReplace.push_back(&U);
    }

    for (Use *U : UsesToReplace) {
      if (auto *UserInst = dyn_cast<Instruction>(U->getUser())) {
        if (auto *SI = dyn_cast<StoreInst>(UserInst)) {
          if (SI->getPointerOperand() == Alloca)
            continue;
        }

        IRBuilder<> LoadBuilder(UserInst);
        LoadInst *Load = LoadBuilder.CreateLoad(I->getType(), Alloca,
                                                I->getName() + ".reload");
        U->set(Load);
      }
    }
  }
}

static Value *buildNextStateForTerm(IRBuilder<> &B, Instruction *T,
                                    DenseMap<BasicBlock *, unsigned> &Id) {
  if (auto *Br = dyn_cast<BranchInst>(T)) {
    if (Br->isUnconditional()) {
      auto It = Id.find(Br->getSuccessor(0));
      if (It != Id.end()) {
        return B.getInt32(It->second);
      }
      return nullptr;
    }

    auto It1 = Id.find(Br->getSuccessor(0));
    auto It2 = Id.find(Br->getSuccessor(1));
    if (It1 != Id.end() && It2 != Id.end()) {
      Value *TState = B.getInt32(It1->second);
      Value *FState = B.getInt32(It2->second);
      return B.CreateSelect(Br->getCondition(), TState, FState, "cff.next");
    }
    return nullptr;
  }

  if (auto *Sw = dyn_cast<SwitchInst>(T)) {
    auto DefaultIt = Id.find(Sw->getDefaultDest());
    if (DefaultIt == Id.end())
      return nullptr;

    Value *Cond = Sw->getCondition();
    Value *NS = B.getInt32(DefaultIt->second);

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
      errs() << "CFF: Skipping function '" << F.getName()
             << "' - contains exception handling\n";
      return true;
    }

    Instruction *T = BB.getTerminator();
    if (!isSupportedTerminator(T)) {
      if (isa<IndirectBrInst>(T)) {
        errs() << "CFF: Skipping function '" << F.getName()
               << "' - contains indirect branch\n";
      } else if (isa<CallBrInst>(T)) {
        errs() << "CFF: Skipping function '" << F.getName()
               << "' - contains callbr instruction\n";
      } else if (isa<InvokeInst>(T)) {
        errs() << "CFF: Skipping function '" << F.getName()
               << "' - contains invoke instruction\n";
      } else {
        errs() << "CFF: Skipping function '" << F.getName()
               << "' - contains unsupported terminator: " << T->getOpcodeName()
               << "\n";
      }
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

        if (verifyFunction(F, &errs())) {
          errs() << "CFF ERROR: Function verification failed for "
                 << F.getName() << "\n";
        }
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

    demoteValuesToMemory(F);

    BasicBlock *Entry = &F.getEntryBlock();

    SmallVector<BasicBlock *, 32> OriginalBlocks;
    for (BasicBlock &BB : F) {
      OriginalBlocks.push_back(&BB);
    }

    DenseMap<BasicBlock *, unsigned> BlockId;
    SmallVector<BasicBlock *, 32> FlattenTargets;
    unsigned NextId = 1;

    for (BasicBlock *BB : OriginalBlocks) {
      if (BB == Entry)
        continue;

      Instruction *Term = BB->getTerminator();
      if ((isa<ReturnInst>(Term) || isa<UnreachableInst>(Term)) &&
          BB->size() == 1) {
        continue;
      }

      BlockId[BB] = NextId++;
      FlattenTargets.push_back(BB);
    }

    if (FlattenTargets.empty())
      return false;

    IRBuilder<> EntryBuilder(Ctx);
    EntryBuilder.SetInsertPoint(Entry, Entry->getFirstInsertionPt());
    AllocaInst *StateVar =
        EntryBuilder.CreateAlloca(Type::getInt32Ty(Ctx), nullptr, "cff.state");

    BasicBlock *Dispatcher = BasicBlock::Create(Ctx, "cff.dispatch", &F);
    BasicBlock *DefaultBlock = BasicBlock::Create(Ctx, "cff.default", &F);
    IRBuilder<> DefaultBuilder(DefaultBlock);
    DefaultBuilder.CreateUnreachable();

    Instruction *EntryTerm = Entry->getTerminator();
    Value *InitialState = nullptr;
    {
      IRBuilder<> InitBuilder(EntryTerm);
      InitialState = buildNextStateForTerm(InitBuilder, EntryTerm, BlockId);
      if (InitialState) {
        InitBuilder.CreateStore(InitialState, StateVar);
      } else {
        return false;
      }
    }

    EntryTerm->eraseFromParent();
    IRBuilder<> NewEntryBuilder(Entry);
    NewEntryBuilder.CreateBr(Dispatcher);

    IRBuilder<> DispatchBuilder(Dispatcher);
    Value *CurrentState =
        DispatchBuilder.CreateLoad(Type::getInt32Ty(Ctx), StateVar, "cff.cur");
    SwitchInst *DispatchSwitch = DispatchBuilder.CreateSwitch(
        CurrentState, DefaultBlock, FlattenTargets.size());

    for (BasicBlock *BB : FlattenTargets) {
      DispatchSwitch->addCase(DispatchBuilder.getInt32(BlockId[BB]), BB);
    }

    for (BasicBlock *BB : FlattenTargets) {
      Instruction *Term = BB->getTerminator();

      if (isa<ReturnInst>(Term) || isa<UnreachableInst>(Term))
        continue;

      IRBuilder<> TermBuilder(Term);
      Value *NextState = buildNextStateForTerm(TermBuilder, Term, BlockId);

      if (NextState) {
        TermBuilder.CreateStore(NextState, StateVar);
        TermBuilder.CreateBr(Dispatcher);
        Term->eraseFromParent();
      } else {
        // This terminator goes to a non-flattened block (like a return block)
        // Leave it as-is
      }
    }

    removeUnreachableBlocks(F);
    return true;
  }
};

} // namespace

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
