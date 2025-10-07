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

# Colors
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
BLUE := \033[0;34m
MAGENTA := \033[0;35m
CYAN := \033[0;36m
NC := \033[0m

.PHONY: all
all: build test

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
	@mkdir -p $(RESULTS_DIR)/reports

.PHONY: build
build:
	@echo "$(YELLOW)Building the pass...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. && make
	@echo "$(GREEN)Build complete!$(NC)"

.PHONY: clean
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(RESULTS_DIR)
	@rm -f .*.dot
	@echo "Clean complete!"

.PHONY: clean-tests
clean-tests:
	@echo "Cleaning test results..."
	@rm -rf $(RESULTS_DIR)
	@rm -f .*.dot

define run_single_test
	@echo "$(YELLOW)Testing $(1)...$(NC)"
	@mkdir -p $(RESULTS_DIR)/{ll_files,logs,binaries,reports}
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
		echo "$(CYAN)  [3/7] Applying passes (StringEncrypt + CFF) and emitting report...$(NC)"; \
		$(OPT) -load-pass-plugin=$(PASS_PLUGIN) \
			-passes='chakravyuha-string-encrypt,chakravyuha-control-flow-flatten,chakravyuha-emit-report,dot-cfg' \
			$(RESULTS_DIR)/ll_files/$(1).ll \
			-o $(RESULTS_DIR)/ll_files/$(1)_obfuscated.ll \
			1>$(RESULTS_DIR)/reports/$(1).json \
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
			echo "  Report: $(RESULTS_DIR)/reports/$(1).json"; \
		else \
			echo "$(RED)  ✗ Test failed - outputs differ!$(NC)"; \
			echo "  Expected:"; \
			cat $(RESULTS_DIR)/$(1)_original.out | sed 's/^/    /'; \
			echo "  Got:"; \
			cat $(RESULTS_DIR)/$(1)_obfuscated.out | sed 's/^/    /'; \
			echo "  Report (may still exist): $(RESULTS_DIR)/reports/$(1).json"; \
		fi; \
		echo "$(CYAN)  [7/7] Generating visualizations...$(NC)"; \
		$(MAKE) -s visualize-test TEST_NAME=$(1); \
	else \
		echo "$(RED)  Test file not found: $(TEST_DIR)/$(1).c$(NC)"; \
	fi
endef

.PHONY: test-%
test-%: build setup
	$(call run_single_test,test_$*)

.PHONY: test
test: build setup $(TEST_TARGETS)
	@echo ""
	@$(MAKE) -s summary

.PHONY: list-tests
list-tests:
	@echo "$(YELLOW)Available tests:$(NC)"
	@for test in $(TEST_NAMES); do \
		echo "  $(GREEN)make test-$${test#test_}$(NC) - Run $$test"; \
	done

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

.PHONY: create-comparison-html
create-comparison-html:
	@echo "$(CYAN)Creating comparison HTML viewer...$(NC)"
	@python3 create_comparison.py || echo "Python script not found, skipping HTML generation"

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

.PHONY: view
view:
	@if [ -f "$(RESULTS_DIR)/visualizations/comparison/index.html" ]; then \
		echo "Opening comparison viewer in browser..."; \
		if command -v xdg-open > /dev/null; then \
			xdg-open "$(RESULTS_DIR)/visualizations/comparison/index.html" || true; \
		elif command -v open > /dev/null; then \
			open "$(RESULTS_DIR)/visualizations/comparison/index.html" || true; \
		else \
			echo "Please open: $(RESULTS_DIR)/visualizations/comparison/index.html"; \
		fi; \
	else \
		echo "No comparison HTML found. Run 'make visualize' first."; \
	fi

.DEFAULT_GOAL := help

.PHONY: help
help:
	@echo "$(YELLOW)Control Flow Flattening + String Encryption - Test Suite$(NC)"
	@echo "======================================================"
	@echo "$(MAGENTA)Basic Commands:$(NC)"
	@echo "  $(GREEN)make build$(NC)         - Build the pass plugin"
	@echo "  $(GREEN)make test$(NC)          - Run all tests ($(words $(TEST_NAMES)) tests found)"
	@echo "  $(GREEN)make test-<name>$(NC)   - Run a specific test (e.g., make test-simple)"
	@echo "  $(GREEN)make list-tests$(NC)    - List available tests"
	@echo "  $(GREEN)make clean$(NC)         - Clean all artifacts"
	@echo ""
	@echo "$(MAGENTA)Visualization Commands:$(NC)"
	@echo "  $(GREEN)make visualize$(NC)     - Generate all CFG visualizations"
	@echo "  $(GREEN)make compare-<name>$(NC)- Side-by-side comparison for specific test"
	@echo "  $(GREEN)make view$(NC)          - Open comparison viewer in browser"
	@echo ""
	@echo "$(MAGENTA)Analysis Commands:$(NC)"
	@echo "  $(GREEN)make stats$(NC)         - Show obfuscation statistics (CFF_METRICS)"
	@echo "  $(GREEN)make summary$(NC)       - Show test results summary"
	@echo ""
	@echo "$(CYAN)Example workflow:$(NC)"
	@echo "  1. make build"
	@echo "  2. make test"
	@echo "  3. make visualize"
	@echo "  4. make view"
