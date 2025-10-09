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
	@mkdir -p $(RESULTS_DIR)
	@mkdir -p $(RESULTS_DIR)/ll_files
	@mkdir -p $(RESULTS_DIR)/logs
	@mkdir -p $(RESULTS_DIR)/binaries
	@mkdir -p $(RESULTS_DIR)/reports
	@mkdir -p $(RESULTS_DIR)/outputs
	@mkdir -p $(RESULTS_DIR)/dot_files
	@mkdir -p $(RESULTS_DIR)/dot_files/original
	@mkdir -p $(RESULTS_DIR)/dot_files/obfuscated
	@mkdir -p $(RESULTS_DIR)/visualizations
	@mkdir -p $(RESULTS_DIR)/visualizations/original
	@mkdir -p $(RESULTS_DIR)/visualizations/obfuscated
	@mkdir -p $(RESULTS_DIR)/visualizations/comparison

.PHONY: clean
clean:
	@echo "$(YELLOW)Cleaning all build and test artifacts...$(NC)"
	@rm -rf $(BUILD_DIR) $(RESULTS_DIR) .*.dot
	@echo "$(GREEN)Clean complete!$(NC)"

.PHONY: clean-tests
clean-tests:
	@echo "$(YELLOW)Cleaning test results...$(NC)"
	@rm -rf $(RESULTS_DIR) .*.dot

# --- Run all tests with different passes ---
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

.PHONY: test-multi-3 test-multi-5
test-multi-3: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},multi3,'chakravyuha-multi-cycle-3'); done
	@$(MAKE) --no-print-directory summary TYPE=multi3

test-multi-5: build setup
	@for test in $(TEST_NAMES); do $(call run_test,$${test},multi5,'chakravyuha-multi-cycle-5'); done
	@$(MAKE) --no-print-directory summary TYPE=multi5

# --- Individual test targets ---
# Pattern rules for running individual tests with different pass configurations
.PHONY: $(TEST_NAMES)
$(TEST_NAMES): build setup
	@echo "$(YELLOW)Running all pass configurations for $@...$(NC)"
	@$(call run_test,$@,full,$(PASSES_FULL))
	@$(call run_test,$@,cff,$(PASSES_CFF))
	@$(call run_test,$@,string,$(PASSES_STRING))
	@$(call run_test,$@,fake,$(PASSES_FAKE))
	@echo "\n$(GREEN)Completed all configurations for $@$(NC)"

# Specific pass configurations for individual tests
.PHONY: $(foreach test,$(TEST_NAMES),$(test)-full)
$(foreach test,$(TEST_NAMES),$(test)-full): %-full: build setup
	@$(call run_test,$*,full,$(PASSES_FULL))

.PHONY: $(foreach test,$(TEST_NAMES),$(test)-cff)
$(foreach test,$(TEST_NAMES),$(test)-cff): %-cff: build setup
	@$(call run_test,$*,cff,$(PASSES_CFF))

.PHONY: $(foreach test,$(TEST_NAMES),$(test)-string)
$(foreach test,$(TEST_NAMES),$(test)-string): %-string: build setup
	@$(call run_test,$*,string,$(PASSES_STRING))

.PHONY: $(foreach test,$(TEST_NAMES),$(test)-fake)
$(foreach test,$(TEST_NAMES),$(test)-fake): %-fake: build setup
	@$(call run_test,$*,fake,$(PASSES_FAKE))

# --- Test execution function ---
define run_test
	echo "\n$(YELLOW)Testing $(1) (Obfuscation: $(2))...$(NC)"; \
	if [ -f $(TEST_DIR)/$(1).c ]; then \
		echo "$(CYAN)  [1/8] Compiling to LLVM IR...$(NC)"; \
		$(CC) -O0 -emit-llvm -S $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/ll_files/$(1).ll 2>/dev/null; \
		echo "$(CYAN)  [2/8] Generating original CFG...$(NC)"; \
		$(OPT) -passes='dot-cfg' $(RESULTS_DIR)/ll_files/$(1).ll -o /dev/null 2>/dev/null; \
		for dot in .*.dot; do [ -f "$$dot" ] && mv "$$dot" "$(RESULTS_DIR)/dot_files/original/$(1)_$${dot#.}" 2>/dev/null || true; done; \
		echo "$(CYAN)  [3/8] Applying passes ($(2)) and emitting report...$(NC)"; \
		TEMP_REPORT=$$(mktemp); \
		$(OPT) -load-pass-plugin=$(PASS_PLUGIN) -passes='$(3),chakravyuha-emit-report,dot-cfg' \
			$(RESULTS_DIR)/ll_files/$(1).ll \
			-S -o $(RESULTS_DIR)/ll_files/$(1)_$(2).ll \
			1>$$TEMP_REPORT \
			2>$(RESULTS_DIR)/logs/$(1)_$(2).log; \
		if [ ! -f $(RESULTS_DIR)/ll_files/$(1)_$(2).ll ]; then \
			echo "$(RED)  ✗ FATAL: Obfuscated LLVM IR file was not created! Pass crashed.$(NC)"; \
			echo "      Check log: $(RESULTS_DIR)/logs/$(1)_$(2).log"; \
			rm -f $$TEMP_REPORT; \
			exit 1; \
		fi; \
		echo "$(CYAN)  [4/8] Saving obfuscated CFG...$(NC)"; \
		for dot in .*.dot; do [ -f "$$dot" ] && mv "$$dot" "$(RESULTS_DIR)/dot_files/obfuscated/$(1)_$${dot#.}" 2>/dev/null || true; done; \
		echo "$(CYAN)  [5/8] Compiling binaries...$(NC)"; \
		$(CC) $(RESULTS_DIR)/ll_files/$(1)_$(2).ll -o $(RESULTS_DIR)/binaries/$(1)_$(2) 2>/dev/null; \
		if [ ! -f $(RESULTS_DIR)/binaries/$(1)_original ]; then \
			$(CC) $(TEST_DIR)/$(1).c -o $(RESULTS_DIR)/binaries/$(1)_original 2>/dev/null; \
		fi; \
		echo "$(CYAN)  [6/8] Measuring binary sizes...$(NC)"; \
		ORIG_SIZE=$$(stat -c%s $(RESULTS_DIR)/binaries/$(1)_original 2>/dev/null || echo 0); \
		OBF_SIZE=$$(stat -c%s $(RESULTS_DIR)/binaries/$(1)_$(2) 2>/dev/null || echo 0); \
		echo "$(CYAN)  [7/8] Running and comparing outputs...$(NC)"; \
		if [ ! -f $(RESULTS_DIR)/outputs/$(1)_original.out ]; then \
			$(RESULTS_DIR)/binaries/$(1)_original > $(RESULTS_DIR)/outputs/$(1)_original.out 2>&1; \
		fi; \
		$(RESULTS_DIR)/binaries/$(1)_$(2) > $(RESULTS_DIR)/outputs/$(1)_$(2).out 2>&1; \
		echo "$(CYAN)  [8/8] Finalizing report with binary sizes...$(NC)"; \
		jq --arg orig "$$ORIG_SIZE" --arg obf "$$OBF_SIZE" \
			'. + {outputAttributes: (.outputAttributes + {originalBinarySize: ($$orig | tonumber), obfuscatedBinarySize: ($$obf | tonumber)})}' \
			$$TEMP_REPORT > $(RESULTS_DIR)/reports/$(1)_$(2).json 2>/dev/null || \
			mv $$TEMP_REPORT $(RESULTS_DIR)/reports/$(1)_$(2).json; \
		rm -f $$TEMP_REPORT; \
		if diff -q $(RESULTS_DIR)/outputs/$(1)_original.out $(RESULTS_DIR)/outputs/$(1)_$(2).out >/dev/null; then \
			echo "$(GREEN)  ✓ Test passed - outputs match!$(NC)"; \
			echo "      Original size: $$ORIG_SIZE bytes, Obfuscated: $$OBF_SIZE bytes"; \
			echo "      Report: $(RESULTS_DIR)/reports/$(1)_$(2).json"; \
		else \
			echo "$(RED)  ✗ Test failed - outputs differ!$(NC)"; \
		fi; \
	else \
		echo "$(RED)  Test file not found: $(TEST_DIR)/$(1).c$(NC)"; \
	fi
endef

# --- Visualization targets ---
.PHONY: visualize view

visualize:
	@echo "$(YELLOW)Generating all visualizations...$(NC)"
	@if ! command -v $(DOT) >/dev/null 2>&1; then \
		echo "$(RED)Error: 'dot' (Graphviz) not found. Cannot generate images.$(NC)"; \
		exit 1; \
	fi
	@echo "$(CYAN)  Checking for DOT files...$(NC)"
	@orig_count=$$(ls -1 $(RESULTS_DIR)/dot_files/original/*.dot 2>/dev/null | wc -l); \
	obf_count=$$(ls -1 $(RESULTS_DIR)/dot_files/obfuscated/*.dot 2>/dev/null | wc -l); \
	echo "    Found $$orig_count original and $$obf_count obfuscated DOT files"
	@echo "$(CYAN)  Rendering original images...$(NC)"
	@for dot_file in $(RESULTS_DIR)/dot_files/original/*.dot; do \
		if [ -f "$$dot_file" ]; then \
			base_name=$$(basename "$$dot_file" .dot); \
			echo "    Processing: $$base_name"; \
			if ! $(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/original/$${base_name}.png" 2>/dev/null; then \
				echo "$(RED)    Failed to render: $$dot_file$(NC)"; \
			fi; \
		fi; \
	done
	@echo "$(CYAN)  Rendering obfuscated images...$(NC)"
	@for dot_file in $(RESULTS_DIR)/dot_files/obfuscated/*.dot; do \
		if [ -f "$$dot_file" ]; then \
			base_name=$$(basename "$$dot_file" .dot); \
			echo "    Processing: $$base_name"; \
			if ! $(DOT) -Tpng "$$dot_file" -o "$(RESULTS_DIR)/visualizations/obfuscated/$${base_name}.png" 2>/dev/null; then \
				echo "$(RED)    Failed to render: $$dot_file$(NC)"; \
				echo "    Checking file validity..."; \
				head -n 5 "$$dot_file" | cat -A; \
			fi; \
		fi; \
	done
	@echo "$(CYAN)  Generating HTML report...$(NC)"
	@$(PYTHON) create_comparison.py || echo "$(RED)Failed to generate HTML report$(NC)"

view: visualize
	@echo "$(YELLOW)Opening comparison report in browser...$(NC)"
	@report_file="$(RESULTS_DIR)/visualizations/comparison/index.html"; \
	if [ ! -f "$$report_file" ]; then \
		echo "$(RED)Error: Report file not found. Run 'make visualize' first.$(NC)"; \
		exit 1; \
	fi; \
	full_path=$$(realpath "$$report_file" 2>/dev/null || echo "$$report_file"); \
	if [ -n "$$WSL_DISTRO_NAME" ]; then \
		echo "WSL environment detected. Opening in Windows browser..."; \
		windows_path=$$(wslpath -w "$$full_path" 2>/dev/null) && \
		explorer.exe "$$windows_path" 2>/dev/null || \
		echo "Could not open automatically. Please open: $$full_path"; \
	elif command -v open > /dev/null 2>&1; then \
		echo "macOS environment detected. Opening in default browser..."; \
		open "$$full_path" 2>/dev/null || \
		echo "Could not open automatically. Please open: file://$$full_path"; \
	elif command -v xdg-open > /dev/null 2>&1; then \
		echo "Linux environment detected. Opening in default browser..."; \
		xdg-open "$$full_path" 2>/dev/null || \
		echo "Could not open automatically. Please open: file://$$full_path"; \
	else \
		echo "Could not detect a command to automatically open the report."; \
		echo "Please open this file in your web browser:"; \
		echo "  file://$$full_path"; \
	fi && \
	echo "$(GREEN)✓ Report is ready at: file://$$full_path$(NC)" || true

.PHONY: check-dots
check-dots:
	@echo "$(YELLOW)Checking DOT files...$(NC)"
	@echo "$(CYAN)Original DOT files:$(NC)"
	@for dot in $(RESULTS_DIR)/dot_files/original/*.dot; do \
		if [ -f "$$dot" ]; then \
			echo "  $$dot: $$(wc -l < $$dot) lines"; \
			if ! dot -Tsvg "$$dot" -o /dev/null 2>/dev/null; then \
				echo "$(RED)    INVALID DOT FILE$(NC)"; \
			fi; \
		fi; \
	done
	@echo "$(CYAN)Obfuscated DOT files:$(NC)"
	@for dot in $(RESULTS_DIR)/dot_files/obfuscated/*.dot; do \
		if [ -f "$$dot" ]; then \
			echo "  $$dot: $$(wc -l < $$dot) lines"; \
			if ! dot -Tsvg "$$dot" -o /dev/null 2>/dev/null; then \
				echo "$(RED)    INVALID DOT FILE$(NC)"; \
				echo "    First few lines:"; \
				head -n 10 "$$dot"; \
			fi; \
		fi; \
	done

# Clean just visualization files
.PHONY: clean-viz
clean-viz:
	@echo "$(YELLOW)Cleaning visualization files...$(NC)"
	@rm -rf $(RESULTS_DIR)/visualizations
	@rm -rf $(RESULTS_DIR)/dot_files
	@mkdir -p $(RESULTS_DIR)/dot_files/original $(RESULTS_DIR)/dot_files/obfuscated
	@mkdir -p $(RESULTS_DIR)/visualizations/original $(RESULTS_DIR)/visualizations/obfuscated $(RESULTS_DIR)/visualizations/comparison

.PHONY: fast-viz
fast-viz:
	@echo "$(YELLOW)Fast parallel visualization...$(NC)"
	@chmod +x parallel_visualize.sh
	@./parallel_visualize.sh
	@echo "$(GREEN)✓ Visualization complete$(NC)"

.PHONY: summary list-tests help

summary:
	@echo "\n$(YELLOW)=========================================="
	@echo "Test Summary (Type: $(TYPE))"
	@echo "==========================================$(NC)"
	@passed=0; failed=0; total=0; \
	for out in $(RESULTS_DIR)/outputs/*_original.out; do \
		if [ -f "$$out" ]; then \
			test_name=$$(basename "$$out" _original.out); \
			obf_out="$(RESULTS_DIR)/outputs/$${test_name}_$(TYPE).out"; \
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
	@echo "\n$(CYAN)Run individual tests:$(NC)"
	@echo "  $(GREEN)make <test_name>$(NC)        - Run all passes on a specific test"
	@echo "  $(GREEN)make <test_name>-full$(NC)   - Run full obfuscation on a specific test"
	@echo "  $(GREEN)make <test_name>-cff$(NC)    - Run only CFF on a specific test"
	@echo "  $(GREEN)make <test_name>-string$(NC) - Run only string encryption on a specific test"
	@echo "  $(GREEN)make <test_name>-fake$(NC)   - Run only fake code insertion on a specific test"
	@echo "\n$(CYAN)Examples:$(NC)"
	@echo "  make test_simple         - Run all passes on test_simple"
	@echo "  make test_complex-cff    - Run only CFF on test_complex"

help:
	@echo "$(YELLOW)Chakravyuha LLVM Obfuscator - Comprehensive Makefile$(NC)"
	@echo "=========================================================="
	@echo "\n$(CYAN)Main Commands:$(NC)"
	@echo "  $(GREEN)make build$(NC)              - Build the LLVM pass plugin."
	@echo "  $(GREEN)make test$(NC)               - Run all obfuscation passes on all tests."
	@echo "  $(GREEN)make test-full$(NC)          - Run all passes on all tests."
	@echo "  $(GREEN)make test-cff$(NC)           - Run only the Control Flow Flattening pass."
	@echo "  $(GREEN)make test-string$(NC)        - Run only the String Encryption pass."
	@echo "  $(GREEN)make test-fake$(NC)          - Run only the Fake Code Insertion pass."
	@echo "\n$(CYAN)Individual Test Commands:$(NC)"
	@echo "  $(GREEN)make <test_name>$(NC)        - Run all passes on a specific test"
	@echo "  $(GREEN)make <test_name>-<pass>$(NC) - Run specific pass on a specific test"
	@echo "    where <pass> can be: full, cff, string, or fake"
	@echo "\n$(CYAN)Visualization Commands:$(NC)"
	@echo "  $(GREEN)make visualize$(NC)          - Generate all CFG images and the HTML report."
	@echo "  $(GREEN)make view$(NC)               - Generate visualizations and automatically open the report."
	@echo "\n$(CYAN)Utility Commands:$(NC)"
	@echo "  $(GREEN)make clean$(NC)              - Clean all build and test artifacts."
	@echo "  $(GREEN)make clean-tests$(NC)        - Clean only test results."
	@echo "  $(GREEN)make list-tests$(NC)         - List all discovered test cases."
	@echo "  $(GREEN)make summary TYPE=<type>$(NC) - Show pass/fail summary for a given test type."
	@echo "\n$(CYAN)Examples:$(NC)"
	@echo "  make test_simple         - Run all passes on test_simple.c"
	@echo "  make test_loops-cff      - Run only CFF on test_loops.c"
	@echo "  make test_switch-full    - Run full obfuscation on test_switch.c"
