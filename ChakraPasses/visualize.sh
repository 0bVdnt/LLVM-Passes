#!/bin/bash

# --- Configuration ---
RESULTS_DIR="test_results"
LL_DIR="$RESULTS_DIR/ll_files"
DOT_DIR="$RESULTS_DIR/dot_files"
VIZ_DIR="$RESULTS_DIR/visualizations"

ORIG_DOT_DIR="$DOT_DIR/original"
OBF_DOT_DIR="$DOT_DIR/obfuscated"
ORIG_VIZ_DIR="$VIZ_DIR/original"
OBF_VIZ_DIR="$VIZ_DIR/obfuscated"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# --- Step 1: Check for prerequisites ---
echo -e "${YELLOW}Checking for required tools...${NC}"
if ! command -v opt &>/dev/null; then
    echo -e "${RED}Error: 'opt' not found.${NC}"
    exit 1
fi
if ! command -v dot &>/dev/null; then
    echo -e "${RED}Error: 'dot' (Graphviz) not found.${NC}"
    exit 1
fi
if ! command -v python3 &>/dev/null; then
    echo -e "${RED}Error: 'python3' not found.${NC}"
    exit 1
fi
echo -e "${GREEN}All tools found.${NC}"
echo "----------------------------------------------------"

# --- Step 2: Set up visualization directories ---
echo -e "${YELLOW}Creating visualization directories...${NC}"
mkdir -p "$ORIG_DOT_DIR" "$OBF_DOT_DIR" "$ORIG_VIZ_DIR" "$OBF_VIZ_DIR"
rm -f .*.dot

# --- Step 3 & 4: Generate .dot files ---
echo -e "${YELLOW}Generating CFG .dot files...${NC}"
# Original code
for ll_file in "$LL_DIR"/test_*.ll; do
    if [[ "$ll_file" == *"_cff.ll" || "$ll_file" == *"_string.ll" || "$ll_file" == *"_fake.ll" || "$ll_file" == *"_full.ll" ]]; then continue; fi
    test_name=$(basename "$ll_file" .ll)
    echo "  Original: $test_name"
    opt -passes='dot-cfg' "$ll_file" -o /dev/null 2>/dev/null
    for dot_file in .*.dot; do if [ -f "$dot_file" ]; then
        func_name="${dot_file#.}"
        func_name="${func_name%.dot}"
        mv "$dot_file" "$ORIG_DOT_DIR/${test_name}_${func_name}.dot"
    fi; done
done
# Obfuscated code ("_full" versions)
for ll_file in "$LL_DIR"/test_*_full.ll; do
    if [ ! -f "$ll_file" ]; then continue; fi
    base_name=$(basename "$ll_file" .ll)
    test_name="${base_name%_full}"
    echo "  Obfuscated: $base_name"
    opt -passes='dot-cfg' "$ll_file" -o /dev/null 2>/dev/null
    for dot_file in .*.dot; do if [ -f "$dot_file" ]; then
        func_name="${dot_file#.}"
        func_name="${func_name%.dot}"
        mv "$dot_file" "$OBF_DOT_DIR/${test_name}_${func_name}.dot"
    fi; done
done

# --- Step 5: Convert .dot files to PNG images ---
echo -e "${YELLOW}Rendering CFG images...${NC}"
for dot_file in "$ORIG_DOT_DIR"/*.dot; do if [ -f "$dot_file" ]; then
    png_file="$ORIG_VIZ_DIR/$(basename "$dot_file" .dot).png"
    dot -Tpng "$dot_file" -o "$png_file" 2>/dev/null
fi; done
for dot_file in "$OBF_DOT_DIR"/*.dot; do if [ -f "$dot_file" ]; then
    png_file="$OBF_VIZ_DIR/$(basename "$dot_file" .dot).png"
    dot -Tpng "$dot_file" -o "$png_file" 2>/dev/null
fi; done
echo -e "${GREEN}Image rendering complete.${NC}"
echo "----------------------------------------------------"

# --- Step 6: Generate the final HTML comparison report ---
echo -e "${YELLOW}Generating interactive HTML report...${NC}"
if [ ! -f "create_comparison.py" ]; then
    echo -e "${RED}Error: 'create_comparison.py' script not found.${NC}"
    exit 1
fi
python3 create_comparison.py

# --- Step 7: Final instructions with automatic opening ---
HTML_REPORT_PATH="$VIZ_DIR/comparison/index.html"
if [ -f "$HTML_REPORT_PATH" ]; then
    echo "----------------------------------------------------"
    echo -e "${GREEN}Visualization complete!${NC}"

    # Get the absolute Linux path to the file
    full_linux_path=$(realpath "$HTML_REPORT_PATH")

    # Check if running in WSL by looking for the WSL environment variable
    if [ -n "$WSL_DISTRO_NAME" ]; then
        echo "WSL environment detected. Opening report in your Windows browser..."
        # Convert the Linux path to a Windows path and use explorer.exe
        windows_path=$(wslpath -w "$full_linux_path")
        explorer.exe "$windows_path"
    # Check for macOS
    elif command -v open &>/dev/null; then
        echo "macOS environment detected. Opening report in your default browser..."
        open "$full_linux_path"
    # Fallback to standard Linux
    elif command -v xdg-open &>/dev/null; then
        echo "Linux environment detected. Opening report in your default browser..."
        xdg-open "$full_linux_path"
    # If no opener is found, just print the path
    else
        echo "Could not detect a command to automatically open the report."
        echo "To view it, open this file in your web browser:"
        echo -e "${CYAN}file://$full_path${NC}"
    fi
else
    echo -e "${RED}HTML report generation failed. Check output from the python script.${NC}"
fi
