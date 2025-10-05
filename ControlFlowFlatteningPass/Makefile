# Compiler and tools
CXX := clang++
CC := clang
OPT := opt
CMAKE := cmake
DOT := dot

# Directories
BUILD_DIR := build
TEST_DIR := tests
RESULTS_DIR := test_results
PASS_PLUGIN := $(BUILD_DIR)/lib/ChakravyuhaControlFlowFlatteningPass.so

# Automatic test discovery
TEST_SOURCES := $(wildcard $(TEST_DIR)/test_*.c)
TEST_NAMES := $(basename $(notdir $(TEST_SOURCES)))
TEST_TARGETS := $(patsubst test_%,test-%,$(TEST_NAMES))

# Colors for output
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
BLUE := \033[0;34m
MAGENTA := \033[0;35m
CYAN := \033[0;36m
NC := \033[0m

# Default target
.PHONY: all
all: build test

# Setup directories
.PHONY: setup
setup:
	@echo "Setting up test environment..."
	@mkdir -p $(TEST_DIR)
	@mkdir -p $(RESULTS_DIR)/ll_files
	@mkdir -p $(RESULTS_DIR)/dot_files/original
	@mkdir -p $(RESULTS_DIR)/dot_files/obfuscated
	@mkdir -p $(RESULTS_DIR)/visualizations/original
	@mkdir -p $(RESULTS_DIR)/visualizations/obfuscated
	@mkdir -p $(RESULTS_DIR)/visualizations/comparison
	@mkdir -p $(RESULTS_DIR)/logs
	@mkdir -p $(RESULTS_DIR)/binaries

# Build the pass
.PHONY: build
build:
	@echo "$(YELLOW)Building the pass...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. && make
	@echo "$(GREEN)Build complete!$(NC)"

# Clean everything
.PHONY: clean
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(RESULTS_DIR)
	@rm -f .*.dot
	@echo "Clean complete!"

# Clean only test results
.PHONY: clean-tests
clean-tests:
	@echo "Cleaning test results..."
	@rm -rf $(RESULTS_DIR)
	@rm -f .*.dot

# Function to run and visualize a single test
define run_single_test
	@echo "$(YELLOW)Testing $(1)...$(NC)"
	@mkdir -p $(RESULTS_DIR)/{ll_files,logs,binaries}
	@mkdir -p $(RESULTS_DIR)/dot_files/{original,obfuscated}
	@mkdir -p $(RESULTS_DIR)/visualizations/{original,obfuscated,comparison}
	@if [ -f $(TEST_DIR)/$(1).c ]; then \
		echo "$(CYAN)  [1/7] Compiling to LLVM IR...$(NC)"; \
		$(CC) -O0 -emit-llvm -S $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/ll_files/$(1).ll 2>/dev/null; \
		echo "$(CYAN)  [2/7] Generating original CFG...$(NC)"; \
		$(OPT) -passes='dot-cfg' $(RESULTS_DIR)/ll_files/$(1).ll -o /dev/null 2>/dev/null; \
		for dot in .*.dot; do \
			if [ -f "$$dot" ]; then \
				mv "$$dot" "$(RESULTS_DIR)/dot_files/original/$(1)_$${dot#.}" 2>/dev/null || true; \
			fi; \
		done; \
		echo "$(CYAN)  [3/7] Applying control flow flattening...$(NC)"; \
		$(OPT) -load-pass-plugin=$(PASS_PLUGIN) \
			-passes='chakravyuha-control-flow-flatten,dot-cfg' \
			$(RESULTS_DIR)/ll_files/$(1).ll \
			-o $(RESULTS_DIR)/ll_files/$(1)_obfuscated.ll \
			2>$(RESULTS_DIR)/logs/$(1).log; \
		echo "$(CYAN)  [4/7] Saving obfuscated CFG...$(NC)"; \
		for dot in .*.dot; do \
			if [ -f "$$dot" ]; then \
				mv "$$dot" "$(RESULTS_DIR)/dot_files/obfuscated/$(1)_$${dot#.}" 2>/dev/null || true; \
			fi; \
		done; \
		echo "$(CYAN)  [5/7] Compiling binaries...$(NC)"; \
		$(CC) $(RESULTS_DIR)/ll_files/$(1)_obfuscated.ll \
			-o $(RESULTS_DIR)/binaries/$(1)_obfuscated 2>/dev/null; \
		$(CC) $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/binaries/$(1)_original 2>/dev/null; \
		echo "$(CYAN)  [6/7] Running and comparing outputs...$(NC)"; \
		$(RESULTS_DIR)/binaries/$(1)_original > $(RESULTS_DIR)/$(1)_original.out 2>&1; \
		$(RESULTS_DIR)/binaries/$(1)_obfuscated > $(RESULTS_DIR)/$(1)_obfuscated.out 2>&1; \
		if diff -q $(RESULTS_DIR)/$(1)_original.out $(RESULTS_DIR)/$(1)_obfuscated.out >/dev/null; then \
			echo "$(GREEN)  ✓ Test passed - outputs match!$(NC)"; \
			grep "CFF_METRICS" $(RESULTS_DIR)/logs/$(1).log 2>/dev/null | sed 's/.*CFF_METRICS:/  Metrics: CFF_METRICS:/' || true; \
		else \
			echo "$(RED)  ✗ Test failed - outputs differ!$(NC)"; \
			echo "  Expected:"; \
			cat $(RESULTS_DIR)/$(1)_original.out | sed 's/^/    /'; \
			echo "  Got:"; \
			cat $(RESULTS_DIR)/$(1)_obfuscated.out | sed 's/^/    /'; \
		fi; \
		echo "$(CYAN)  [7/7] Generating visualizations...$(NC)"; \
		$(MAKE) -s visualize-test TEST_NAME=$(1); \
	else \
		echo "$(RED)  Test file not found: $(TEST_DIR)/$(1).c$(NC)"; \
	fi
endef

# Pattern rule for individual test targets
.PHONY: test-%
test-%: build setup
	$(call run_single_test,test_$*)

# Run all tests
.PHONY: test
test: build setup $(TEST_TARGETS)
	@echo ""
	@$(MAKE) -s summary

# Pattern rule for individual test targets
.PHONY: test-%
test-%: 
	@$(call run_single_test,test_$*)
	@echo ""

.PHONY: list-tests
list-tests:
	@echo "$(YELLOW)Available tests:$(NC)"
	@for test in $(TEST_NAMES); do \
		echo "  $(GREEN)make test-$${test#test_}$(NC) - Run $$test"; \
	done

# Visualize specific test
.PHONY: visualize-test
visualize-test:
	@if command -v $(DOT) >/dev/null 2>&1; then \
		for dot_file in $(RESULTS_DIR)/dot_files/original/$(TEST_NAME)_*.dot; do \
			if [ -f "$$dot_file" ]; then \
				base_name=$$(basename "$$dot_file" .dot); \
				$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/original/$${base_name}.png" 2>/dev/null; \
			fi; \
		done; \
		for dot_file in $(RESULTS_DIR)/dot_files/obfuscated/$(TEST_NAME)_*.dot; do \
			if [ -f "$$dot_file" ]; then \
				base_name=$$(basename "$$dot_file" .dot); \
				$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/obfuscated/$${base_name}.png" 2>/dev/null; \
			fi; \
		done; \
	fi

# Visualize all CFGs
.PHONY: visualize
visualize:
	@echo "$(YELLOW)Generating all visualizations...$(NC)"
	@if command -v $(DOT) >/dev/null 2>&1; then \
		echo "$(CYAN)Processing original CFGs...$(NC)"; \
		for dot_file in $(RESULTS_DIR)/dot_files/original/*.dot; do \
			if [ -f "$$dot_file" ]; then \
				base_name=$$(basename "$$dot_file" .dot); \
				echo "  Generating $$base_name.png"; \
				$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/original/$${base_name}.png" 2>/dev/null; \
			fi; \
		done; \
		echo "$(CYAN)Processing obfuscated CFGs...$(NC)"; \
		for dot_file in $(RESULTS_DIR)/dot_files/obfuscated/*.dot; do \
			if [ -f "$$dot_file" ]; then \
				base_name=$$(basename "$$dot_file" .dot); \
				echo "  Generating $$base_name.png"; \
				$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/obfuscated/$${base_name}.png" 2>/dev/null; \
			fi; \
		done; \
		echo "$(GREEN)Visualizations complete!$(NC)"; \
		$(MAKE) -s create-comparison-html; \
	else \
		echo "$(RED)Graphviz not installed. Install it with: apt-get install graphviz$(NC)"; \
	fi

# Create comparison HTML
.PHONY: create-comparison-html
create-comparison-html:
	@echo "$(CYAN)Creating comparison HTML viewer...$(NC)"
	@python3 create_comparison.py || echo "Python script not found, skipping HTML generation"

# Compare specific test visually
.PHONY: compare-%
compare-%: test-%
	@echo "$(YELLOW)Creating visual comparison for test_$*...$(NC)"
	@if command -v $(DOT) >/dev/null 2>&1 && command -v convert >/dev/null 2>&1; then \
		for func in $(RESULTS_DIR)/visualizations/original/test_$*_*.png; do \
			if [ -f "$$func" ]; then \
				func_name=$$(basename "$$func" .png); \
				orig="$(RESULTS_DIR)/visualizations/original/$${func_name}.png"; \
				obf="$(RESULTS_DIR)/visualizations/obfuscated/$${func_name}.png"; \
				if [ -f "$$orig" ] && [ -f "$$obf" ]; then \
					convert "$$orig" "$$obf" +append "$(RESULTS_DIR)/visualizations/comparison/$${func_name}_comparison.png"; \
					echo "  Created comparison for $${func_name}"; \
				fi; \
			fi; \
		done; \
		echo "$(GREEN)Comparison images saved to $(RESULTS_DIR)/visualizations/comparison/$(NC)"; \
	else \
		echo "$(RED)ImageMagick not installed. Install it with: apt-get install imagemagick$(NC)"; \
	fi

# Show statistics
.PHONY: stats
stats:
	@echo "$(YELLOW)Obfuscation Statistics:$(NC)"
	@echo "=========================================="
	@for log in $(RESULTS_DIR)/logs/*.log; do \
		if [ -f "$$log" ]; then \
			test_name=$$(basename "$$log" .log); \
			echo "$(CYAN)$$test_name:$(NC)"; \
			grep "CFF_METRICS" "$$log" 2>/dev/null | sed 's/CFF_METRICS://g' || echo "  No metrics found"; \
			if grep -q "Skipping" "$$log" 2>/dev/null; then \
				grep "Skipping" "$$log" | sed 's/^/  /'; \
			fi; \
			echo ""; \
		fi; \
	done

# Summary of all tests
.PHONY: summary
summary:
	@echo "$(YELLOW)=========================================="
	@echo "Test Summary"
	@echo "==========================================$(NC)"
	@passed=0; \
	failed=0; \
	total=0; \
	for out in $(RESULTS_DIR)/*_original.out; do \
		if [ -f "$$out" ]; then \
			test_name=$$(basename "$$out" _original.out); \
			total=$$((total + 1)); \
			if [ -f "$(RESULTS_DIR)/$${test_name}_obfuscated.out" ]; then \
				if diff -q "$$out" "$(RESULTS_DIR)/$${test_name}_obfuscated.out" >/dev/null; then \
					echo "$(GREEN)✓ $$test_name$(NC)"; \
					passed=$$((passed + 1)); \
				else \
					echo "$(RED)✗ $$test_name$(NC)"; \
					failed=$$((failed + 1)); \
				fi; \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "Total: $$total | $(GREEN)Passed: $$passed$(NC) | $(RED)Failed: $$failed$(NC)"

# Open comparison in browser
.PHONY: view
view:
	@if [ -f "$(RESULTS_DIR)/visualizations/comparison/index.html" ]; then \
		echo "Opening comparison viewer in browser..."; \
		xdg-open "$(RESULTS_DIR)/visualizations/comparison/index.html" 2>/dev/null || \
		open "$(RESULTS_DIR)/visualizations/comparison/index.html" 2>/dev/null || \
		echo "Please open $(RESULTS_DIR)/visualizations/comparison/index.html in your browser"; \
	else \
		echo "No comparison HTML found. Run 'make visualize' first."; \
	fi

# Help
.PHONY: help
help:
	@echo "$(YELLOW)Control Flow Flattening Pass - Enhanced Test Suite$(NC)"
	@echo "======================================================"
	@echo "$(MAGENTA)Basic Commands:$(NC)"
	@echo "  $(GREEN)make build$(NC)         - Build the pass plugin"
	@echo "  $(GREEN)make test$(NC)          - Run all tests ($(words $(TEST_NAMES)) tests found)"
	@echo "  $(GREEN)make test-<name>$(NC)   - Run specific test (e.g., make test-simple)"
	@echo "  $(GREEN)make list-tests$(NC)    - List all available tests"
	@echo "  $(GREEN)make clean$(NC)         - Clean all artifacts"
	@echo ""
	@echo "$(MAGENTA)Visualization Commands:$(NC)"
	@echo "  $(GREEN)make visualize$(NC)     - Generate all CFG visualizations"
	@echo "  $(GREEN)make compare-<name>$(NC)- Create side-by-side comparison for specific test"
	@echo "  $(GREEN)make view$(NC)          - Open comparison viewer in browser"
	@echo ""
	@echo "$(MAGENTA)Analysis Commands:$(NC)"
	@echo "  $(GREEN)make stats$(NC)         - Show obfuscation statistics"
	@echo "  $(GREEN)make summary$(NC)       - Show test results summary"
	@echo ""
	@echo "$(CYAN)Example workflow:$(NC)"
	@echo "  1. make build"
	@echo "  2. make test           # Run all tests"
	@echo "  3. make test-complex   # Run specific test"
	@echo "  4. make visualize"
	@echo "  5. make view"

.DEFAULT_GOAL := help