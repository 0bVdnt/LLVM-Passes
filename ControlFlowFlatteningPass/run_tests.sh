# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TEST_DIR="$SCRIPT_DIR/tests"
RESULTS_DIR="$SCRIPT_DIR/test_results"
PASS_PLUGIN="$BUILD_DIR/lib/ChakravyuhaControlFlowFlatteningPass.so"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create directories
mkdir -p "$TEST_DIR"
mkdir -p "$RESULTS_DIR"
mkdir -p "$RESULTS_DIR/dot_files"
mkdir -p "$RESULTS_DIR/ll_files"
mkdir -p "$RESULTS_DIR/logs"

# Check if pass is built
if [ ! -f "$PASS_PLUGIN" ]; then
    echo -e "${RED}Error: Pass plugin not found at $PASS_PLUGIN${NC}"
    echo "Building the pass..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake ..
    make
    cd "$SCRIPT_DIR"

    if [ ! -f "$PASS_PLUGIN" ]; then
        echo -e "${RED}Build failed!${NC}"
        exit 1
    fi
fi

# Function to compile and test a single file
test_file() {
    local test_name=$1
    local test_file="$TEST_DIR/${test_name}.c"

    if [ ! -f "$test_file" ]; then
        echo -e "${RED}Test file not found: $test_file${NC}"
        return 1
    fi

    echo -e "${YELLOW}Testing: $test_name${NC}"

    # Compile to LLVM IR
    echo "  Compiling to LLVM IR..."
    clang -O0 -emit-llvm -S "$test_file" -o "$RESULTS_DIR/ll_files/${test_name}.ll" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to compile $test_name${NC}"
        return 1
    fi

    # Apply the pass
    echo "  Applying control flow flattening..."
    opt -load-pass-plugin="$PASS_PLUGIN" \
        -passes='chakravyuha-control-flow-flatten,dot-cfg' \
        "$RESULTS_DIR/ll_files/${test_name}.ll" \
        -o "$RESULTS_DIR/ll_files/${test_name}_obfuscated.ll" \
        2>"$RESULTS_DIR/logs/${test_name}.log"

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to apply pass to $test_name${NC}"
        return 1
    fi

    # Move generated dot files
    mv .*.dot "$RESULTS_DIR/dot_files/" 2>/dev/null

    # Compile obfuscated version to executable
    echo "  Compiling obfuscated version..."
    clang "$RESULTS_DIR/ll_files/${test_name}_obfuscated.ll" \
        -o "$RESULTS_DIR/${test_name}_obfuscated" 2>/dev/null

    if [ $? -ne 0 ]; then
        echo -e "${RED}  Failed to compile obfuscated version${NC}"
        return 1
    fi

    # Compile original for comparison
    clang "$test_file" -o "$RESULTS_DIR/${test_name}_original" 2>/dev/null

    # Run both versions and compare output
    echo "  Running tests..."
    "$RESULTS_DIR/${test_name}_original" >"$RESULTS_DIR/${test_name}_original.out" 2>&1
    "$RESULTS_DIR/${test_name}_obfuscated" >"$RESULTS_DIR/${test_name}_obfuscated.out" 2>&1

    # Compare outputs
    if diff -q "$RESULTS_DIR/${test_name}_original.out" "$RESULTS_DIR/${test_name}_obfuscated.out" >/dev/null; then
        echo -e "${GREEN}  ✓ Output matches original${NC}"

        # Extract metrics
        if grep -q "CFF_METRICS" "$RESULTS_DIR/logs/${test_name}.log"; then
            metrics=$(grep "CFF_METRICS" "$RESULTS_DIR/logs/${test_name}.log")
            echo "  Metrics: $metrics"
        fi

        return 0
    else
        echo -e "${RED}  ✗ Output differs from original${NC}"
        echo "  Check $RESULTS_DIR/${test_name}_*.out for details"
        return 1
    fi
}

# Function to run performance test
performance_test() {
    local test_name=$1
    local test_file="$RESULTS_DIR/${test_name}_original"
    local obf_file="$RESULTS_DIR/${test_name}_obfuscated"

    if [ ! -f "$test_file" ] || [ ! -f "$obf_file" ]; then
        return
    fi

    echo -e "${YELLOW}Performance comparison for $test_name:${NC}"

    # Time original
    original_time=$({ time -p "$test_file" >/dev/null 2>&1; } 2>&1 | grep real | awk '{print $2}')

    # Time obfuscated
    obfuscated_time=$({ time -p "$obf_file" >/dev/null 2>&1; } 2>&1 | grep real | awk '{print $2}')

    echo "  Original:   ${original_time}s"
    echo "  Obfuscated: ${obfuscated_time}s"

    # Calculate overhead
    if command -v bc &>/dev/null; then
        overhead=$(echo "scale=2; ($obfuscated_time - $original_time) / $original_time * 100" | bc)
        echo "  Overhead:   ${overhead}%"
    fi
}

# Function to generate visualization
generate_visualization() {
    local test_name=$1
    local dot_dir="$RESULTS_DIR/dot_files"

    if ! command -v dot &>/dev/null; then
        echo "  Graphviz not installed, skipping visualization"
        return
    fi

    for dot_file in "$dot_dir"/.*.dot; do
        if [ -f "$dot_file" ]; then
            base_name=$(basename "$dot_file" .dot)
            dot -Tpng "$dot_file" -o "$RESULTS_DIR/dot_files/${base_name}.png" 2>/dev/null
        fi
    done
}

# Main test execution
echo "=========================================="
echo "Control Flow Flattening Pass Test Suite"
echo "=========================================="
echo ""

# Create test files if they don't exist
echo "Creating test files..."
for test_content in test_simple test_loops test_switch test_complex test_recursive test_attributes test_phi; do
    if [ ! -f "$TEST_DIR/${test_content}.c" ]; then
        echo "  Creating ${test_content}.c"
    fi
done

# Run all tests
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

for test_file in "$TEST_DIR"/*.c; do
    if [ -f "$test_file" ]; then
        test_name=$(basename "$test_file" .c)
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        if test_file "$test_name"; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
            generate_visualization "$test_name"
        else
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi

        echo ""
    fi
done

# Performance tests
echo "=========================================="
echo "Performance Tests"
echo "=========================================="
performance_test "test_loops"
performance_test "test_complex"
echo ""

# Summary
echo "=========================================="
echo "Test Summary"
echo "=========================================="
echo -e "Total tests:  $TOTAL_TESTS"
echo -e "${GREEN}Passed:       $PASSED_TESTS${NC}"
echo -e "${RED}Failed:       $FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
