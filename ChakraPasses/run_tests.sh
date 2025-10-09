# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TEST_DIR="$SCRIPT_DIR/tests"
RESULTS_DIR="$SCRIPT_DIR/test_results"
PASS_PLUGIN="$BUILD_DIR/lib/ChakravyuhaControlFlowFlatteningPass.so"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

mkdir -p "$TEST_DIR" "$RESULTS_DIR" "$RESULTS_DIR/dot_files" "$RESULTS_DIR/ll_files" "$RESULTS_DIR/logs" "$RESULTS_DIR/reports"

if [ ! -f "$PASS_PLUGIN" ]; then
    echo -e "${YELLOW}Building the pass...${NC}"
    mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR" && cmake .. && make && cd "$SCRIPT_DIR"
fi

test_file() {
    local test_name=$1
    local test_file="$TEST_DIR/${test_name}.c"
    if [ ! -f "$test_file" ]; then
        echo -e "${RED}Missing: $test_file${NC}"
        return 1
    fi
    echo -e "${YELLOW}Testing: $test_name${NC}"

    clang -O0 -emit-llvm -S "$test_file" -o "$RESULTS_DIR/ll_files/${test_name}.ll" 2>/dev/null

    opt -passes='dot-cfg' "$RESULTS_DIR/ll_files/${test_name}.ll" -o /dev/null 2>/dev/null
    mv .*.dot "$RESULTS_DIR/dot_files/" 2>/dev/null

    opt -load-pass-plugin="$PASS_PLUGIN" \
        -passes='chakravyuha-string-encrypt,chakravyuha-control-flow-flatten,chakravyuha-emit-report' \
        "$RESULTS_DIR/ll_files/${test_name}.ll" \
        -S -o "$RESULTS_DIR/ll_files/${test_name}_obfuscated.ll" \
        1>"$RESULTS_DIR/reports/${test_name}.json" \
        2>"$RESULTS_DIR/logs/${test_name}.log"

    clang "$RESULTS_DIR/ll_files/${test_name}_obfuscated.ll" -o "$RESULTS_DIR/${test_name}_obfuscated" 2>/dev/null
    clang "$test_file" -o "$RESULTS_DIR/${test_name}_original" 2>/dev/null

    "$RESULTS_DIR/${test_name}_original" >"$RESULTS_DIR/${test_name}_original.out" 2>&1
    "$RESULTS_DIR/${test_name}_obfuscated" >"$RESULTS_DIR/${test_name}_obfuscated.out" 2>&1

    if diff -q "$RESULTS_DIR/${test_name}_original.out" "$RESULTS_DIR/${test_name}_obfuscated.out" >/dev/null; then
        echo -e "${GREEN}  ✓ Output matches original${NC}"
        grep "CFF_METRICS" "$RESULTS_DIR/logs/${test_name}.log" || true
        echo "  Report: $RESULTS_DIR/reports/${test_name}.json"
        return 0
    else
        echo -e "${RED}  ✗ Output differs${NC}"
        return 1
    fi
}
