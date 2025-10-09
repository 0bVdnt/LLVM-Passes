# --- Compiler and Tools ---
CXX := clang++
CC := clang
OPT := opt
CMAKE := cmake
DOT := dot
PYTHON := python3

# --- Directories ---
BUILD_DIR := build
TEST_DIR := tests
RESULTS_DIR := test_results
PASS_PLUGIN := $(BUILD_DIR)/lib/ChakravyuhaControlFlowFlatteningPass.so

# --- Test Discovery ---
TEST_SOURCES := $(wildcard $(TEST_DIR)/test_*.c)
TEST_NAMES   := $(basename $(notdir $(TEST_SOURCES)))

# --- Pass Pipelines ---
PASSES_FULL   := 'chakravyuha-string-encrypt,chakravyuha-control-flow-flatten,chakravyuha-fake-code-insertion'
PASSES_CFF    := 'chakravyuha-control-flow-flatten'
PASSES_STRING := 'chakravyuha-string-encrypt'
PASSES_FAKE   := 'chakravyuha-fake-code-insertion'

# --- Output Colors ---
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
CYAN := \033[0;36m
NC := \033[0m

.PHONY: all
all: build test

.DEFAULT_GOAL := help

# --- Build & Clean ---
.PHONY: build
build:
	@echo "$(YELLOW)Building the pass...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. && make
	@echo "$(GREEN)Build complete!$(NC)"

.PHONY: setup
setup:
	@echo "Setting up test environment..."
	@mkdir -p $(RESULTS_DIR)/{ll_files,logs,binaries,reports,dot_files/original,dot_files/obfuscated,visualizations/original,visualizations/obfuscated,visualizations/comparison}

.PHONY: clean
clean:
	@echo "$(YELLOW)Cleaning all build and test artifacts...$(NC)"
	@rm -rf $(BUILD_DIR) $(RESULTS_DIR) .*.dot
	@echo "$(GREEN)Clean complete!$(NC)"

.PHONY: clean-tests
clean-tests:
	@echo "$(YELLOW)Cleaning test results...$(NC)"
	@rm -rf $(RESULTS_DIR) .*.dot

.PHONY: test test-full test-cff test-string test-fake
test: test-full

test-full: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},full,$(PASSES_FULL)); done
	@$(MAKE) --no-print-directory summary TYPE=full

test-cff: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},cff,$(PASSES_CFF)); done
	@$(MAKE) --no-print-directory summary TYPE=cff

test-string: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},string,$(PASSES_STRING)); done
	@$(MAKE) --no-print-directory summary TYPE=string

test-fake: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},fake,$(PASSES_FAKE)); done
	@$(MAKE) --no-print-directory summary TYPE=fake


define run_test
	@echo "\n$(YELLOW)Testing $(1) (Obfuscation: $(2))...$(NC)"; \
	if [ -f $(TEST_DIR)/$(1).c ]; then \
		echo "$(CYAN)  [1/7] Compiling to LLVM IR...$(NC)"; \
		$(CC) -O0 -emit-llvm -S $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/ll_files/$(1).ll 2>/dev/null; \
		echo "$(CYAN)  [2/7] Generating original CFG...$(NC)"; \
		$(OPT) -passes='dot-cfg' $(RESULTS_DIR)/ll_files/$(1).ll -o /dev/null 2>/dev/null; \
		for dot in .*.dot; do [ -f "$$dot" ] && mv "$$dot" "$(RESULTS_DIR)/dot_files/original/$(1)_$${dot#.}" 2>/dev/null || true; done; \
		echo "$(CYAN)  [3/7] Applying passes ($(2)) and emitting report...$(NC)"; \
		$(OPT) -load-pass-plugin=$(PASS_PLUGIN) -passes='$(3),chakravyuha-emit-report,dot-cfg' \
			$(RESULTS_DIR)/ll_files/$(1).ll \
			-S -o $(RESULTS_DIR)/ll_files/$(1)_$(2).ll \
			1>$(RESULTS_DIR)/reports/$(1)_$(2).json \
			2>$(RESULTS_DIR)/logs/$(1)_$(2).log; \
		if [ ! -f $(RESULTS_DIR)/ll_files/$(1)_$(2).ll ]; then \
			echo "$(RED)  ✗ FATAL: Obfuscated LLVM IR file was not created! Pass crashed.$(NC)"; \
			echo "      Check log: $(RESULTS_DIR)/logs/$(1)_$(2).log"; \
			exit 1; \
		fi; \
		echo "$(CYAN)  [4/7] Saving obfuscated CFG...$(NC)"; \
		for dot in .*.dot; do [ -f "$$dot" ] && mv "$$dot" "$(RESULTS_DIR)/dot_files/obfuscated/$(1)_$${dot#.}" 2>/dev/null || true; done; \
		echo "$(CYAN)  [5/7] Compiling binaries...$(NC)"; \
		$(CC) $(RESULTS_DIR)/ll_files/$(1)_$(2).ll -o $(RESULTS_DIR)/binaries/$(1)_$(2); \
		$(CC) $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/binaries/$(1)_original; \
		echo "$(CYAN)  [6/7] Running and comparing outputs...$(NC)"; \
		$(RESULTS_DIR)/binaries/$(1)_original > $(RESULTS_DIR)/binaries/$(1)_original.out 2>&1; \
		$(RESULTS_DIR)/binaries/$(1)_$(2) > $(RESULTS_DIR)/binaries/$(1)_$(2).out 2>&1; \
		if diff -q $(RESULTS_DIR)/$(1)_original.out $(RESULTS_DIR)/$(1)_$(2).out >/dev/null; then \
			echo "$(GREEN)  ✓ Test passed - outputs match!$(NC)"; \
			echo "      Report: $(RESULTS_DIR)/reports/$(1)_$(2).json"; \
		else \
			echo "$(RED)  ✗ Test failed - outputs differ!$(NC)"; \
		fi; \
	else \
		echo "$(RED)  Test file not found: $(TEST_DIR)/$(1).c$(NC)"; \
	fi
endef	

.PHONY: visualize view

visualize:
	@echo "$(YELLOW)Generating all visualizations...$(NC)"; \
	if ! command -v $(DOT) >/dev/null 2>&1; then \
		echo "$(RED)Error: 'dot' (Graphviz) not found. Cannot generate images.${NC}"; exit 1; \
	fi; \
	echo "$(CYAN)  Rendering original images...$(NC)"; \
	for dot_file in $(RESULTS_DIR)/dot_files/original/*.dot; do \
		if [ -f "$$dot_file" ]; then \
			base_name=$$(basename "$$dot_file" .dot); \
			$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/original/$${base_name}.png" 2>/dev/null; \
		fi; \
	done; \
	echo "$(CYAN)  Rendering obfuscated images...$(NC)"; \
	for dot_file in $(RESULTS_DIR)/dot_files/obfuscated/*.dot; do \
		if [ -f "$$dot_file" ]; then \
			base_name=$$(basename "$$dot_file" .dot); \
			$(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/obfuscated/$${base_name}.png" 2>/dev/null; \
		fi; \
	done; \
	echo "$(CYAN)  Generating HTML report...$(NC)"; \
	$(PYTHON) create_comparison.py

view: visualize
	@echo "$(YELLOW)Opening comparison report in browser...$(NC)"; \
	report_file="$(RESULTS_DIR)/visualizations/comparison/index.html"; \
	if [ ! -f "$$report_file" ]; then \
		echo "$(RED)Error: Report file not found. Run 'make visualize' first.${NC}"; exit 1; \
	fi; \
	full_path=$$(realpath "$$report_file"); \
	if [ -n "$$WSL_DISTRO_NAME" ]; then \
		windows_path=$$(wslpath -w "$$full_path"); \
		explorer.exe "$$windows_path"; \
	elif command -v open > /dev/null; then \
		open "$$full_path"; \
	elif command -v xdg-open > /dev/null; then \
		xdg-open "$$full_path"; \
	else \
		echo "Could not auto-open. Please open this file in your browser: file://$$full_path"; \
	fi

.PHONY: summary list-tests help
summary:
	@echo "\n$(YELLOW)=========================================="
	@echo "Test Summary (Type: $(TYPE))"
	@echo "==========================================$(NC)"
	@passed=0; failed=0; total=0; \
	for out in $(RESULTS_DIR)/*_original.out; do \
		if [ -f "$$out" ]; then \
			test_name=$$(basename "$$out" _original.out); \
			obf_out="$(RESULTS_DIR)/$${test_name}_$(TYPE).out"; \
			if [ -f "$$obf_out" ]; then \
				total=$$((total + 1)); \
				if diff -q "$$out" "$$obf_out" >/dev/null; then \
					echo "$(GREEN)✓ $$test_name$(NC)"; passed=$$((passed + 1)); \
				else \
					echo "$(RED)✗ $$test_name$(NC)"; failed=$$((failed + 1)); \
				fi; \
			fi; \
		fi; \
	done; \
	echo ""; \
	echo "Total run for type '$(TYPE)': $$total | $(GREEN)Passed: $$passed$(NC) | $(RED)Failed: $$failed$(NC)"
list-tests:
	@echo "$(YELLOW)Available tests:$(NC)"
	@for test in $(TEST_NAMES); do echo "  $${test}"; done
help:
	@echo "$(YELLOW)Chakravyuha LLVM Obfuscator - Comprehensive Makefile$(NC)"
	@echo "=========================================================="
	@echo "\n$(CYAN)Main Commands:$(NC)"
	@echo "  $(GREEN)make build$(NC)           - Build the LLVM pass plugin."
	@echo "  $(GREEN)make test$(NC)            - (Default) Run all obfuscation passes on all tests."
	@echo "  $(GREEN)make test-full$(NC)      - Run all passes on all tests."
	@echo "  $(GREEN)make test-cff$(NC)       - Run only the Control Flow Flattening pass."
	@echo "  $(GREEN)make test-string$(NC)    - Run only the String Encryption pass."
	@echo "  $(GREEN)make test-fake$(NC)       - Run only the Fake Code Insertion pass."
	@echo "  $(GREEN)make visualize$(NC)       - Generate all CFG images and the HTML report."
	@echo "  $(GREEN)make view$(NC)            - Generate visualizations and automatically open the report."
	@echo "\n$(CYAN)Utility Commands:$(NC)"
	@echo "  $(GREEN)make clean$(NC)           - Clean all build and test artifacts."
	@echo "  $(GREEN)make clean-tests$(NC)   - Clean only test results."
	@echo "  $(GREEN)make list-tests$(NC)      - List all discovered test cases."
	@echo "  $(GREEN)make summary TYPE=<type>$(NC) - Show pass/fail summary for a given test type."
