#include "MultiCycleObfuscationPass.h"
#include "ControlFlowFlatteningPass.cpp"

using namespace llvm;

PreservedAnalyses MultiCycleObfuscationPass::run(Module &M,
                                                 ModuleAnalysisManager &MAM) {
  auto &R = chakravyuha::ReportData::get();
  R.cyclesRequested = NumCycles;
  R.startTimer();

  errs() << "Starting multi-cycle obfuscation with " << NumCycles
         << " cycles\n";

  for (unsigned Cycle = 0; Cycle < NumCycles; ++Cycle) {
    errs() << "  Cycle " << (Cycle + 1) << "/" << NumCycles << "\n";

    // Run string encryption
    if (Cycle == 0) { // Only encrypt strings in first cycle
      StringEncryptionPass SEP;
      SEP.run(M, MAM);
    }

    // Run control flow flattening
    // Note: You'll need to expose the ControlFlowFlatteningPass class
    // or create it here again

    // Run fake code insertion
    FakeCodeInsertionPass FCP;
    FCP.run(M, MAM);

    R.cyclesCompleted = Cycle + 1;
  }

  return PreservedAnalyses::none();
}
