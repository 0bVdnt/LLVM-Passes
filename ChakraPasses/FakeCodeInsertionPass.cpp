#include "FakeCodeInsertionPass.h"
#include "ChakravyuhaReport.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

#include <random>
#include <vector>

using namespace llvm;

const unsigned MAX_FAKE_BLOCKS_PER_FUNCTION = 10;
const unsigned MAX_FAKE_LOOPS_PER_FUNCTION = 5;
const unsigned MAX_FAKE_CONDITIONALS_PER_FUNCTION = 8;
const unsigned MAX_FAKE_INSTRUCTIONS_PER_BLOCK = 20;

static std::mt19937 gen(std::random_device{}());

// Insert a fake loop that never executes
static void insertFakeLoop(BasicBlock *InsertAfter, AllocaInst *dummyVar) {
  Function *F = InsertAfter->getParent();
  LLVMContext &Ctx = F->getContext();

  auto &R = chakravyuha::ReportData::get();

  // Create loop blocks
  BasicBlock *FakeLoopEntry = BasicBlock::Create(Ctx, "fake.loop.entry", F);
  BasicBlock *FakeLoopHeader = BasicBlock::Create(Ctx, "fake.loop.header", F);
  BasicBlock *FakeLoopBody = BasicBlock::Create(Ctx, "fake.loop.body", F);
  BasicBlock *FakeLoopExit = BasicBlock::Create(Ctx, "fake.loop.exit", F);

  // Get the original successor
  BasicBlock *OrigSuccessor = InsertAfter->getTerminator()->getSuccessor(0);

  // Redirect to fake loop entry (but with always-false condition)
  InsertAfter->getTerminator()->eraseFromParent();
  IRBuilder<> EntryBuilder(InsertAfter);
  Value *FalseCondition = EntryBuilder.getFalse();
  EntryBuilder.CreateCondBr(FalseCondition, FakeLoopEntry, OrigSuccessor);

  // Build fake loop entry
  IRBuilder<> LoopEntryBuilder(FakeLoopEntry);
  LoopEntryBuilder.CreateBr(FakeLoopHeader);

  // Build fake loop header with PHI
  IRBuilder<> HeaderBuilder(FakeLoopHeader);
  PHINode *Counter =
      HeaderBuilder.CreatePHI(Type::getInt32Ty(Ctx), 2, "fake.counter");
  Counter->addIncoming(HeaderBuilder.getInt32(0), FakeLoopEntry);

  // Fake loop condition
  Value *LoopBound = HeaderBuilder.getInt32(10);
  Value *LoopCond =
      HeaderBuilder.CreateICmpSLT(Counter, LoopBound, "fake.cond");
  HeaderBuilder.CreateCondBr(LoopCond, FakeLoopBody, FakeLoopExit);

  // Build fake loop body with bogus operations
  IRBuilder<> BodyBuilder(FakeLoopBody);
  Value *Increment = BodyBuilder.getInt32(1);
  Value *NextCounter = BodyBuilder.CreateAdd(Counter, Increment, "fake.inc");

  // Add some bogus computations
  std::uniform_int_distribution<> instrDist(5, 15);
  unsigned numBogusInstr = instrDist(gen);
  Value *AccumValue = BodyBuilder.getInt32(1);

  for (unsigned i = 0; i < numBogusInstr; ++i) {
    std::uniform_int_distribution<> opDist(0, 3);
    Value *Constant = BodyBuilder.getInt32(i + 1);

    switch (opDist(gen)) {
    case 0:
      AccumValue = BodyBuilder.CreateAdd(AccumValue, Constant, "fake.add");
      break;
    case 1:
      AccumValue = BodyBuilder.CreateMul(AccumValue, Constant, "fake.mul");
      break;
    case 2:
      AccumValue = BodyBuilder.CreateXor(AccumValue, Constant, "fake.xor");
      break;
    case 3:
      AccumValue = BodyBuilder.CreateShl(AccumValue, BodyBuilder.getInt32(1),
                                         "fake.shl");
      break;
    }
    R.totalBogusInstructions++;
  }

  // Store to dummy variable (volatile to prevent optimization)
  BodyBuilder.CreateStore(AccumValue, dummyVar, true);
  Counter->addIncoming(NextCounter, FakeLoopBody);
  BodyBuilder.CreateBr(FakeLoopHeader);

  // Build loop exit
  IRBuilder<> ExitBuilder(FakeLoopExit);
  ExitBuilder.CreateBr(OrigSuccessor);

  R.fakeLoopsInserted++;
  R.totalBogusInstructions += 4; // Loop control instructions
}

// Insert a fake conditional block
static void insertFakeConditional(BasicBlock *InsertAfter,
                                  AllocaInst *dummyVar) {
  Function *F = InsertAfter->getParent();
  LLVMContext &Ctx = F->getContext();

  auto &R = chakravyuha::ReportData::get();

  // Create fake conditional blocks
  BasicBlock *FakeThen = BasicBlock::Create(Ctx, "fake.then", F);
  BasicBlock *FakeElse = BasicBlock::Create(Ctx, "fake.else", F);
  BasicBlock *FakeMerge = BasicBlock::Create(Ctx, "fake.merge", F);

  // Get original successor
  BasicBlock *OrigSuccessor = InsertAfter->getTerminator()->getSuccessor(0);

  // Create always-false condition
  InsertAfter->getTerminator()->eraseFromParent();
  IRBuilder<> Builder(InsertAfter);
  Value *FalseCondition = Builder.getFalse();
  Builder.CreateCondBr(FalseCondition, FakeThen, OrigSuccessor);

  // Populate fake then block
  IRBuilder<> ThenBuilder(FakeThen);
  std::uniform_int_distribution<> instrDist(3, 10);
  unsigned numInstr = instrDist(gen);
  Value *ThenValue = ThenBuilder.getInt32(42);

  for (unsigned i = 0; i < numInstr; ++i) {
    ThenValue = ThenBuilder.CreateAdd(ThenValue, ThenBuilder.getInt32(i),
                                      "fake.then.op");
    R.totalBogusInstructions++;
  }
  ThenBuilder.CreateStore(ThenValue, dummyVar, true);
  ThenBuilder.CreateBr(FakeMerge);

  // Populate fake else block
  IRBuilder<> ElseBuilder(FakeElse);
  Value *ElseValue = ElseBuilder.getInt32(24);
  for (unsigned i = 0; i < numInstr; ++i) {
    ElseValue = ElseBuilder.CreateMul(ElseValue, ElseBuilder.getInt32(i + 1),
                                      "fake.else.op");
    R.totalBogusInstructions++;
  }
  ElseBuilder.CreateStore(ElseValue, dummyVar, true);
  ElseBuilder.CreateBr(FakeMerge);

  // Merge block
  IRBuilder<> MergeBuilder(FakeMerge);
  MergeBuilder.CreateBr(OrigSuccessor);

  R.fakeConditionalsInserted++;
  R.totalBogusInstructions += 5; // Control flow instructions
}

// Original fake block insertion (simplified)
static void insertFakeBlock(BasicBlock *InsertAfter, AllocaInst *dummyVar) {
  Function *F = InsertAfter->getParent();
  LLVMContext &Ctx = F->getContext();

  auto &R = chakravyuha::ReportData::get();

  BasicBlock *FakeBlock = BasicBlock::Create(Ctx, "fake.block", F);
  BasicBlock *OrigSuccessor = InsertAfter->getTerminator()->getSuccessor(0);

  InsertAfter->getTerminator()->eraseFromParent();
  IRBuilder<> Builder(InsertAfter);
  Value *FalseCondition = Builder.getFalse();
  Builder.CreateCondBr(FalseCondition, FakeBlock, OrigSuccessor);

  // Populate fake block
  IRBuilder<> FakeBuilder(FakeBlock);
  std::uniform_int_distribution<> numInstDist(5,
                                              MAX_FAKE_INSTRUCTIONS_PER_BLOCK);
  unsigned numInstr = numInstDist(gen);

  Value *AccumValue = FakeBuilder.getInt32(1);
  for (unsigned i = 0; i < numInstr; ++i) {
    std::uniform_int_distribution<> opDist(0, 4);
    Value *Operand = FakeBuilder.getInt32(i + 1);

    switch (opDist(gen)) {
    case 0:
      AccumValue = FakeBuilder.CreateAdd(AccumValue, Operand, "fake.op");
      break;
    case 1:
      AccumValue = FakeBuilder.CreateSub(AccumValue, Operand, "fake.op");
      break;
    case 2:
      AccumValue = FakeBuilder.CreateMul(AccumValue, Operand, "fake.op");
      break;
    case 3:
      AccumValue = FakeBuilder.CreateXor(AccumValue, Operand, "fake.op");
      break;
    case 4:
      AccumValue =
          FakeBuilder.CreateShl(AccumValue, FakeBuilder.getInt32(1), "fake.op");
      break;
    }
    R.totalBogusInstructions++;
  }

  FakeBuilder.CreateStore(AccumValue, dummyVar, true);
  FakeBuilder.CreateBr(OrigSuccessor);

  R.fakeCodeBlocksInserted++;
  R.totalBogusInstructions += 2; // Branch and store
}

static bool addFakeCodeToFunction(Function &F) {
  if (F.isDeclaration() || F.hasAvailableExternallyLinkage() || F.empty()) {
    return false;
  }

  // Create dummy variable for side effects
  IRBuilder<> EntryBuilder(&F.getEntryBlock(),
                           F.getEntryBlock().getFirstInsertionPt());
  AllocaInst *dummyVar = EntryBuilder.CreateAlloca(
      Type::getInt32Ty(F.getContext()), nullptr, "dummy.var");

  // Collect candidate blocks
  std::vector<BasicBlock *> candidateBlocks;
  for (BasicBlock &BB : F) {
    if (BB.getTerminator()->getNumSuccessors() == 1) {
      BasicBlock *Successor = BB.getTerminator()->getSuccessor(0);
      if (!isa<PHINode>(Successor->front())) {
        candidateBlocks.push_back(&BB);
      }
    }
  }

  if (candidateBlocks.empty()) {
    return false;
  }

  bool changed = false;

  // Insert fake loops
  std::uniform_int_distribution<> loopDist(1, MAX_FAKE_LOOPS_PER_FUNCTION);
  unsigned numLoops =
      std::min((unsigned)loopDist(gen), (unsigned)candidateBlocks.size() / 3);

  for (unsigned i = 0; i < numLoops && !candidateBlocks.empty(); ++i) {
    std::uniform_int_distribution<size_t> picker(0, candidateBlocks.size() - 1);
    size_t idx = picker(gen);
    insertFakeLoop(candidateBlocks[idx], dummyVar);
    candidateBlocks.erase(candidateBlocks.begin() + idx);
    changed = true;
  }

  // Insert fake conditionals
  std::uniform_int_distribution<> condDist(1,
                                           MAX_FAKE_CONDITIONALS_PER_FUNCTION);
  unsigned numConds =
      std::min((unsigned)condDist(gen), (unsigned)candidateBlocks.size() / 2);

  for (unsigned i = 0; i < numConds && !candidateBlocks.empty(); ++i) {
    std::uniform_int_distribution<size_t> picker(0, candidateBlocks.size() - 1);
    size_t idx = picker(gen);
    insertFakeConditional(candidateBlocks[idx], dummyVar);
    candidateBlocks.erase(candidateBlocks.begin() + idx);
    changed = true;
  }

  // Insert remaining as simple fake blocks
  std::uniform_int_distribution<> blockDist(1, MAX_FAKE_BLOCKS_PER_FUNCTION);
  unsigned numBlocks =
      std::min((unsigned)blockDist(gen), (unsigned)candidateBlocks.size());

  for (unsigned i = 0; i < numBlocks && !candidateBlocks.empty(); ++i) {
    std::uniform_int_distribution<size_t> picker(0, candidateBlocks.size() - 1);
    size_t idx = picker(gen);
    insertFakeBlock(candidateBlocks[idx], dummyVar);
    candidateBlocks.erase(candidateBlocks.begin() + idx);
    changed = true;
  }

  return changed;
}

PreservedAnalyses FakeCodeInsertionPass::run(Module &M,
                                             ModuleAnalysisManager &) {
  bool Changed = false;

  auto &R = chakravyuha::ReportData::get();
  R.passesRun.push_back("FakeCodeInsertion");
  R.enableFakeCodeInsertion = true;

  for (Function &F : M) {
    if (addFakeCodeToFunction(F)) {
      Changed = true;
    }
  }

  if (Changed) {
    return PreservedAnalyses::none();
  }
  return PreservedAnalyses::all();
}
