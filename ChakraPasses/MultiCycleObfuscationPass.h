#pragma once
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

struct MultiCycleObfuscationPass
    : public llvm::PassInfoMixin<MultiCycleObfuscationPass> {
  unsigned NumCycles;

  MultiCycleObfuscationPass(unsigned Cycles = 1) : NumCycles(Cycles) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};
