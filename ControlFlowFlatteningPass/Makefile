CC = clang
CXX = clang++
OPT = opt
LLVM_CONFIG = llvm-config

BUILD_DIR = build
TEST_DIR = tests
PLUGIN = $(BUILD_DIR)/lib/ChakravyuhaControlFlowFlatteningPass.so

# Test targets
.PHONY: all test clean run-tests build-plugin

all: build-plugin test

build-plugin:
	@echo "Building the pass plugin..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make

test: build-plugin
	@echo "Running tests..."
	@bash run_tests.sh

# Individual test targets
test-simple: build-plugin
	@echo "Testing simple if..."
	@clang -O0 -S -emit-llvm $(TEST_DIR)/test_simple_if.c -o $(BUILD_DIR)/simple.ll
	@opt -load-pass-plugin=$(PLUGIN) -passes=chakravyuha-control-flow-flatten -S $(BUILD_DIR)/simple.ll -o $(BUILD_DIR)/simple_flat.ll
	@echo "Original:"
	@clang $(BUILD_DIR)/simple.ll -o $(BUILD_DIR)/simple_orig && $(BUILD_DIR)/simple_orig
	@echo "Flattened:"
	@clang $(BUILD_DIR)/simple_flat.ll -o $(BUILD_DIR)/simple_flat && $(BUILD_DIR)/simple_flat

# Visualization target
visualize: build-plugin
	@echo "Generating CFG visualizations..."
	@for test in $(TEST_DIR)/test_*.c; do \
		name=$$(basename $$test .c); \
		echo "Processing $$name..."; \
		clang -O0 -S -emit-llvm $$test -o $(BUILD_DIR)/$$name.ll 2>/dev/null; \
		opt -load-pass-plugin=$(PLUGIN) -passes=chakravyuha-control-flow-flatten -S $(BUILD_DIR)/$$name.ll -o $(BUILD_DIR)/$${name}_flat.ll 2>/dev/null; \
		opt -dot-cfg $(BUILD_DIR)/$$name.ll -disable-output 2>/dev/null; \
		opt -dot-cfg $(BUILD_DIR)/$${name}_flat.ll -disable-output 2>/dev/null; \
	done
	@echo "CFG dot files generated. Use 'dot -Tpng file.dot -o file.png' to convert."

clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -f *.dot
	@rm -f test_report.txt

help:
	@echo "Available targets:"
	@echo "  make all          - Build plugin and run all tests"
	@echo "  make build-plugin - Build the pass plugin only"
	@echo "  make test         - Run all tests"
	@echo "  make test-simple  - Run simple if test only"
	@echo "  make visualize    - Generate CFG visualizations"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make help         - Show this help message"
