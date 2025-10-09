#pragma once
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace chakravyuha {

struct ReportData {
  std::string inputFile;
  std::string outputFile = "obfuscated.ll";
  std::string targetPlatform;
  std::string obfuscationLevel = "medium";
  bool enableStringEncryption = false;
  bool enableControlFlowFlattening = false;
  bool enableFakeCodeInsertion = false;
  unsigned cyclesCompleted = 1;
  unsigned cyclesRequested = 1;

  // String encryption
  unsigned stringsEncrypted = 0;
  uint64_t originalIRStringDataSize = 0;
  uint64_t obfuscatedIRStringDataSize = 0;
  std::string stringMethod;

  // Control flow flattening
  unsigned flattenedFunctions = 0;
  unsigned flattenedBlocks = 0;
  unsigned skippedFunctions = 0;

  // Enhanced fake code metrics
  unsigned fakeCodeBlocksInserted = 0;
  unsigned fakeLoopsInserted = 0;
  unsigned fakeConditionalsInserted = 0;
  unsigned totalBogusInstructions = 0;

  // Binary size tracking
  uint64_t originalBinarySize = 0;
  uint64_t obfuscatedBinarySize = 0;

  // Performance metrics
  double compilationTimeSeconds = 0.0;
  std::time_t startTime;
  std::time_t endTime;

  // Method details
  std::vector<std::string> passesRun;
  std::vector<std::string> obfuscationMethods;

  static ReportData &get() {
    static ReportData R;
    return R;
  }

  void startTimer() { startTime = std::time(nullptr); }

  void endTimer() {
    endTime = std::time(nullptr);
    compilationTimeSeconds = std::difftime(endTime, startTime);
  }
};

inline std::string esc(const std::string &S) {
  std::string T;
  T.reserve(S.size());
  for (char c : S) {
    if (c == '\\')
      T += "\\\\";
    else if (c == '"')
      T += "\\\"";
    else
      T += c;
  }
  return T;
}

inline std::string formatBytes(uint64_t bytes) {
  if (bytes == 0)
    return "0 B";

  const char *units[] = {"B", "KB", "MB", "GB"};
  int unitIndex = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024.0 && unitIndex < 3) {
    size /= 1024.0;
    unitIndex++;
  }

  std::stringstream ss;
  ss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
  return ss.str();
}

inline std::string formatPercentage(double value) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2) << value << "%";
  return ss.str();
}

inline std::string nowUtcIso8601() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char b[24];
  std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return b;
}

inline void finalizeDefaults(llvm::Module &M) {
  auto &R = ReportData::get();

  if (R.inputFile.empty()) {
    const std::string &SF = M.getSourceFileName();
    R.inputFile = SF.empty() ? std::string("<stdin>") : SF;
  }

  if (R.targetPlatform.empty()) {
    llvm::Triple T(M.getTargetTriple());
    R.targetPlatform = T.isOSWindows() ? "windows" : "linux";
  }

  // Populate obfuscation methods used
  R.obfuscationMethods.clear();
  if (R.enableStringEncryption)
    R.obfuscationMethods.push_back("String Encryption (XOR)");
  if (R.enableControlFlowFlattening)
    R.obfuscationMethods.push_back("Control Flow Flattening");
  if (R.enableFakeCodeInsertion) {
    R.obfuscationMethods.push_back("Fake Code Insertion");
    if (R.fakeLoopsInserted > 0)
      R.obfuscationMethods.push_back("Fake Loop Insertion");
    if (R.fakeConditionalsInserted > 0)
      R.obfuscationMethods.push_back("Fake Conditional Insertion");
  }
}

inline void emitReportJSON(llvm::Module &M) {
  finalizeDefaults(M);
  auto &R = ReportData::get();
  R.endTimer();

  // Calculate change percentages
  double stringChangePct = 0.0;
  if (R.originalIRStringDataSize != 0) {
    stringChangePct =
        (double)(R.obfuscatedIRStringDataSize - R.originalIRStringDataSize) /
        (double)R.originalIRStringDataSize * 100.0;
  }

  double binaryChangePct = 0.0;
  if (R.originalBinarySize > 0) {
    binaryChangePct = (double)(R.obfuscatedBinarySize - R.originalBinarySize) /
                      (double)R.originalBinarySize * 100.0;
  }

  // Format percentages as strings
  std::string stringChangeStr = formatPercentage(stringChangePct);
  std::string binaryChangeStr = formatPercentage(binaryChangePct);

  llvm::outs() << "{\n";
  llvm::outs() << "  \"timestamp\": \"" << nowUtcIso8601() << "\",\n";
  llvm::outs() << "  \"inputFile\": \"" << esc(R.inputFile) << "\",\n";
  llvm::outs() << "  \"outputFile\": \"" << esc(R.outputFile) << "\",\n";

  // Input parameters
  llvm::outs() << "  \"inputParameters\": {\n";
  llvm::outs() << "    \"obfuscationLevel\": \"" << esc(R.obfuscationLevel)
               << "\",\n";
  llvm::outs() << "    \"targetPlatform\": \"" << esc(R.targetPlatform)
               << "\",\n";
  llvm::outs() << "    \"requestedCycles\": " << R.cyclesRequested << ",\n";
  llvm::outs() << "    \"enableStringEncryption\": "
               << (R.enableStringEncryption ? "true" : "false") << ",\n";
  llvm::outs() << "    \"enableControlFlowFlattening\": "
               << (R.enableControlFlowFlattening ? "true" : "false") << ",\n";
  llvm::outs() << "    \"enableFakeCodeInsertion\": "
               << (R.enableFakeCodeInsertion ? "true" : "false") << "\n";
  llvm::outs() << "  },\n";

  // Output attributes
  llvm::outs() << "  \"outputAttributes\": {\n";
  llvm::outs() << "    \"originalBinarySize\": \""
               << formatBytes(R.originalBinarySize) << "\",\n";
  llvm::outs() << "    \"obfuscatedBinarySize\": \""
               << formatBytes(R.obfuscatedBinarySize) << "\",\n";
  llvm::outs() << "    \"binarySizeIncrease\": \"" << binaryChangeStr
               << "\",\n";
  llvm::outs() << "    \"originalIRStringDataSize\": \""
               << R.originalIRStringDataSize << " bytes\",\n";
  llvm::outs() << "    \"obfuscatedIRStringDataSize\": \""
               << R.obfuscatedIRStringDataSize << " bytes\",\n";
  llvm::outs() << "    \"stringDataSizeChange\": \"" << stringChangeStr
               << "\",\n";
  llvm::outs() << "    \"compilationTimeSeconds\": " << R.compilationTimeSeconds
               << ",\n";
  llvm::outs() << "    \"obfuscationMethods\": [";
  for (size_t i = 0; i < R.obfuscationMethods.size(); ++i) {
    llvm::outs() << "\"" << esc(R.obfuscationMethods[i]) << "\"";
    if (i + 1 < R.obfuscationMethods.size())
      llvm::outs() << ", ";
  }
  llvm::outs() << "]\n";
  llvm::outs() << "  },\n";

  // Obfuscation metrics
  llvm::outs() << "  \"obfuscationMetrics\": {\n";
  llvm::outs() << "    \"cyclesCompleted\": " << R.cyclesCompleted << ",\n";
  llvm::outs() << "    \"passesRun\": [";
  for (size_t i = 0; i < R.passesRun.size(); ++i) {
    llvm::outs() << "\"" << esc(R.passesRun[i]) << "\"";
    if (i + 1 < R.passesRun.size())
      llvm::outs() << ", ";
  }
  llvm::outs() << "],\n";

  // String encryption metrics
  llvm::outs() << "    \"stringEncryption\": {\n";
  llvm::outs() << "      \"count\": " << R.stringsEncrypted << ",\n";
  llvm::outs() << "      \"method\": \""
               << esc(R.stringMethod.empty() ? "N/A" : R.stringMethod)
               << "\"\n";
  llvm::outs() << "    },\n";

  // Control flow flattening metrics
  llvm::outs() << "    \"controlFlowFlattening\": {\n";
  llvm::outs() << "      \"flattenedFunctions\": " << R.flattenedFunctions
               << ",\n";
  llvm::outs() << "      \"flattenedBlocks\": " << R.flattenedBlocks << ",\n";
  llvm::outs() << "      \"skippedFunctions\": " << R.skippedFunctions << "\n";
  llvm::outs() << "    },\n";

  // Fake code insertion metrics
  llvm::outs() << "    \"fakeCodeInsertion\": {\n";
  llvm::outs() << "      \"totalBogusInstructions\": "
               << R.totalBogusInstructions << ",\n";
  llvm::outs() << "      \"fakeBlocks\": " << R.fakeCodeBlocksInserted << ",\n";
  llvm::outs() << "      \"fakeLoops\": " << R.fakeLoopsInserted << ",\n";
  llvm::outs() << "      \"fakeConditionals\": " << R.fakeConditionalsInserted
               << "\n";
  llvm::outs() << "    }\n";
  llvm::outs() << "  }\n";
  llvm::outs() << "}\n";
}

} // namespace chakravyuha
