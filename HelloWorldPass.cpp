#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"     // Required for FunctionAnalysisManager
#include "llvm/Passes/PassBuilder.h" // Required for PassInfoMixin and PassBuilder
#include "llvm/Passes/PassPlugin.h" // Required for PassPluginLibraryInfo
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// For the New Pass Manager, pass inherits from PassInfoMixin.
// This allows it to be used with the PassBuilder and other New Pass Manager
// (NPM) infrastructure.
struct DummyPass : public PassInfoMixin<DummyPass> {
  // The `run` method for a Function pass now takes a Function reference
  // and a FunctionAnalysisManager reference.
  // It returns PreservedAnalyses to indicate which analyses are still valid.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    errs() << "Hello World Pass (NPM): Analyzing function -> " << F.getName()
           << "\n";

    return PreservedAnalyses::all();
  }

  // Every NPM pass needs a static 'name' method. This is the string
  // that is used with `-passes=`.
  static StringRef name() { return "hello-world"; }
};

// This is the crucial entry point for a dynamically loaded LLVM Pass Plugin.
// It's a C-linkage function that LLVM's `opt` (or `clang`) will look for
// when loading a shared library as a plugin.
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HelloWorldPassPlugin", "v0.1",
          [](PassBuilder &PB) {
            // Register the pass with the PassBuilder.
            // This callback allows `opt` to recognize "hello-world"
            // when provided in a pipeline (e.g.,
            // `-passes=function(hello-world)` or just
            // `-passes=hello-world`).
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "hello-world") {
                    FPM.addPass(DummyPass());
                    return true;
                  }
                  return false;
                });
          }};
}
