#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
TEST_DIR="tests"
PASS_PLUGIN="./build/lib/ChakravyuhaControlFlowFlatteningPass.so"
REPORT_FILE="test_report.txt"
CLANG_FLAGS="-O0 -Xclang -disable-O0-optnone"

# Check if pass plugin exists
if [ ! -f "$PASS_PLUGIN" ]; then
    echo -e "${RED}Error: Pass plugin not found at $PASS_PLUGIN${NC}"
    echo "Please build the plugin first"
    exit 1
fi

# Create necessary directories
mkdir -p "$BUILD_DIR/ir_original"
mkdir -p "$BUILD_DIR/ir_flattened"
mkdir -p "$BUILD_DIR/executables"

# Initialize report
echo "Control Flow Flattening Test Report" >"$REPORT_FILE"
echo "====================================" >>"$REPORT_FILE"
echo "Date: $(date)" >>"$REPORT_FILE"
echo "" >>"$REPORT_FILE"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# Function to run a single test
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .c)

    echo -e "\n${YELLOW}Testing: $test_name${NC}"
    echo -e "\n## Test: $test_name" >>"$REPORT_FILE"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    # Step 1: Compile to LLVM IR
    echo "  [1/5] Compiling to LLVM IR..."
    clang $CLANG_FLAGS -S -emit-llvm "$test_file" -o "$BUILD_DIR/ir_original/${test_name}.ll" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to compile to IR${NC}"
        echo "Status: FAILED (compilation error)" >>"$REPORT_FILE"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi

    # Step 2: Run the flattening pass
    echo "  [2/5] Running flattening pass..."
    opt_output=$(opt -load-pass-plugin="$PASS_PLUGIN" \
        -passes=chakravyuha-control-flow-flatten \
        -S "$BUILD_DIR/ir_original/${test_name}.ll" \
        -o "$BUILD_DIR/ir_flattened/${test_name}_flat.ll" 2>&1)

    # Check if function was skipped
    if echo "$opt_output" | grep -q "Skipping function"; then
        echo -e "${YELLOW}  Function skipped (contains unsupported constructs)${NC}"
        echo "Status: SKIPPED" >>"$REPORT_FILE"
        echo "$opt_output" | grep "Skipping" >>"$REPORT_FILE"
        SKIPPED_TESTS=$((SKIPPED_TESTS + 1))
        return 0
    fi

    # Check for errors
    if echo "$opt_output" | grep -q "LLVM ERROR"; then
        echo -e "${RED}  Flattening failed with error${NC}"
        echo "Status: FAILED (flattening error)" >>"$REPORT_FILE"
        echo "$opt_output" | grep -E "ERROR|error" >>"$REPORT_FILE"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi

    # Extract metrics
    if echo "$opt_output" | grep -q "CFF_METRICS"; then
        metrics=$(echo "$opt_output" | grep "CFF_METRICS")
        echo "  Metrics: $metrics"
        echo "Metrics: $metrics" >>"$REPORT_FILE"
    fi

    # Step 3: Compile original to executable
    echo "  [3/5] Compiling original to executable..."
    clang "$BUILD_DIR/ir_original/${test_name}.ll" -o "$BUILD_DIR/executables/${test_name}_original" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to compile original${NC}"
        echo "Status: FAILED (original compilation)" >>"$REPORT_FILE"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi

    # Step 4: Compile flattened to executable
    echo "  [4/5] Compiling flattened to executable..."
    clang "$BUILD_DIR/ir_flattened/${test_name}_flat.ll" -o "$BUILD_DIR/executables/${test_name}_flattened" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to compile flattened version${NC}"
        echo "Status: FAILED (flattened compilation)" >>"$REPORT_FILE"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi

    # Step 5: Compare outputs
    echo "  [5/5] Comparing outputs..."
    original_output=$("$BUILD_DIR/executables/${test_name}_original" 2>&1)
    flattened_output=$("$BUILD_DIR/executables/${test_name}_flattened" 2>&1)

    if [ "$original_output" = "$flattened_output" ]; then
        echo -e "${GREEN}  ✓ Test passed - outputs match${NC}"
        echo "Status: PASSED" >>"$REPORT_FILE"
        PASSED_TESTS=$((PASSED_TESTS + 1))

        # Count basic blocks for comparison
        original_blocks=$(grep -c "^[a-zA-Z0-9_]*:" "$BUILD_DIR/ir_original/${test_name}.ll")
        flattened_blocks=$(grep -c "^[a-zA-Z0-9_]*:" "$BUILD_DIR/ir_flattened/${test_name}_flat.ll")
        echo "  Basic blocks: $original_blocks -> $flattened_blocks"
        echo "Basic blocks: $original_blocks -> $flattened_blocks" >>"$REPORT_FILE"
    else
        echo -e "${RED}  ✗ Test failed - outputs differ${NC}"
        echo "Status: FAILED (output mismatch)" >>"$REPORT_FILE"
        echo "Expected output:" >>"$REPORT_FILE"
        echo "$original_output" >>"$REPORT_FILE"
        echo "Got output:" >>"$REPORT_FILE"
        echo "$flattened_output" >>"$REPORT_FILE"
        FAILED_TESTS=$((FAILED_TESTS + 1))

        # Show diff
        echo -e "${RED}  Expected:${NC}"
        echo "$original_output"
        echo -e "${RED}  Got:${NC}"
        echo "$flattened_output"
    fi

    return 0
}

# Main test execution
echo "Starting Control Flow Flattening Tests"
echo "======================================"

# Find and run all test files
for test_file in "$TEST_DIR"/test_*.c; do
    if [ -f "$test_file" ]; then
        run_test "$test_file"
    fi
done

# Summary
echo ""
echo "======================================"
echo "Test Summary"
echo "======================================"
echo -e "Total tests:   $TOTAL_TESTS"
echo -e "Passed:        ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:        ${RED}$FAILED_TESTS${NC}"
echo -e "Skipped:       ${YELLOW}$SKIPPED_TESTS${NC}"

echo -e "\n## Summary" >>"$REPORT_FILE"
echo "Total tests: $TOTAL_TESTS" >>"$REPORT_FILE"
echo "Passed: $PASSED_TESTS" >>"$REPORT_FILE"
echo "Failed: $FAILED_TESTS" >>"$REPORT_FILE"
echo "Skipped: $SKIPPED_TESTS" >>"$REPORT_FILE"

# Check for analysis tools
echo ""
echo "======================================"
echo "Additional Analysis"
echo "======================================"

# Check if any test has the dispatcher pattern
echo "Checking for dispatcher pattern in flattened code..."
for flat_ir in "$BUILD_DIR/ir_flattened"/*.ll; do
    if [ -f "$flat_ir" ]; then
        if grep -q "cff.dispatch" "$flat_ir"; then
            echo -e "${GREEN}  ✓ $(basename $flat_ir): Contains dispatcher${NC}"
        else
            echo -e "${YELLOW}  ! $(basename $flat_ir): No dispatcher found${NC}"
        fi
    fi
done

echo ""
echo "Full report saved to: $REPORT_FILE"

# Exit with appropriate code
if [ $FAILED_TESTS -gt 0 ]; then
    exit 1
else
    exit 0
fi
