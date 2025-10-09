#!/bin/bash

# --- Configuration ---
PASS_PLUGIN_NAME="ChakravyuhaControlFlowFlatteningPass.so"
BUILD_DIR="build"
TEST_DIR="tests"
RESULTS_DIR="test_results"
PASS_PLUGIN_PATH="$BUILD_DIR/lib/$PASS_PLUGIN_NAME"

ALL_PASSES_PIPELINE="chakravyuha-all"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# --- Step 1: Build the LLVM Pass Plugin ---
echo -e "${YELLOW}Building the pass plugin...${NC}"
mkdir -p "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S .
make -C "$BUILD_DIR"
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed. Aborting.${NC}"
    exit 1
fi
echo -e "${GREEN}Build complete!${NC}"
echo "----------------------------------------------------"

# --- Step 2: Set up the test results directory ---
echo -e "${YELLOW}Setting up test environment...${NC}"
mkdir -p "$RESULTS_DIR/ll_files"
mkdir -p "$RESULTS_DIR/binaries"
mkdir -p "$RESULTS_DIR/reports"
mkdir -p "$RESULTS_DIR/logs"
mkdir -p "$RESULTS_DIR/outputs"
echo "----------------------------------------------------"

# --- Step 3: Loop through all test files and run the full process ---
for test_file in "$TEST_DIR"/test_*.c; do
    if [ ! -f "$test_file" ]; then
        continue
    fi

    # Extract the base name of the test (e.g., "test_simple")
    test_name=$(basename "$test_file" .c)
    echo -e "${YELLOW}Executing full obfuscation pipeline on: $test_name${NC}"

    # Define file paths
    original_ll="$RESULTS_DIR/ll_files/${test_name}.ll"
    obfuscated_ll="$RESULTS_DIR/ll_files/${test_name}_full.ll"
    report_file="$RESULTS_DIR/reports/${test_name}_full.json"
    log_file="$RESULTS_DIR/logs/${test_name}_full.log"
    original_bin="$RESULTS_DIR/binaries/${test_name}_original"
    obfuscated_bin="$RESULTS_DIR/binaries/${test_name}_full"
    original_out="$RESULTS_DIR/outputs/${test_name}_original.out"
    obfuscated_out="$RESULTS_DIR/outputs/${test_name}_full.out"

    # 1. Compile C to LLVM IR
    echo -e "${CYAN}  [1/5] Compiling to LLVM IR...${NC}"
    clang -O0 -emit-llvm -S "$test_file" -o "$original_ll"

    # 2. Run 'chakravyuha-all' pass pipeline
    echo -e "${CYAN}  [2/5] Applying all obfuscation passes...${NC}"
    opt -load-pass-plugin="$PASS_PLUGIN_PATH" \
        -passes="$ALL_PASSES_PIPELINE" \
        "$original_ll" \
        -S -o "$obfuscated_ll" \
        1>"$report_file" \
        2>"$log_file"

    if [ ! -f "$obfuscated_ll" ]; then
        echo -e "${RED}  ✗ FATAL: Obfuscated LLVM IR file was not created! Your pass may have crashed.${NC}"
        echo "      Check log for details: $log_file"
        continue
    fi

    # 3. Compile both original and obfuscated IR to binaries
    echo -e "${CYAN}  [3/5] Compiling binaries...${NC}"
    clang "$original_ll" -o "$original_bin"
    clang "$obfuscated_ll" -o "$obfuscated_bin"

    # 4. Run both binaries and capture their output
    echo -e "${CYAN}  [4/5] Running binaries...${NC}"
    "$original_bin" >"$original_out" 2>&1
    "$obfuscated_bin" >"$obfuscated_out" 2>&1

    # 5. Compare the outputs and report the result
    echo -e "${CYAN}  [5/5] Comparing outputs...${NC}"
    if diff -q "$original_out" "$obfuscated_out" >/dev/null; then
        echo -e "${GREEN}  ✓ Test Passed: Outputs match!${NC}"
        echo "      Report generated at: $report_file"
    else
        echo -e "${RED}  ✗ Test FAILED: Outputs differ!${NC}"
        echo "      --- Expected Output ---"
        cat "$original_out" | sed 's/^/        /'
        echo "      --- Got Output ---"
        cat "$obfuscated_out" | sed 's/^/        /'
    fi
    echo "----------------------------------------------------"
done

echo -e "${GREEN}All tests complete.${NC}"
