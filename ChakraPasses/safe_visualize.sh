#!/bin/bash

# safe_visualize.sh - Safe visualization with timeout and error handling

RESULTS_DIR="test_results"
DOT_DIR="$RESULTS_DIR/dot_files"
VIZ_DIR="$RESULTS_DIR/visualizations"
TIMEOUT_SEC=5

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Safe Visualization Script${NC}"
echo "======================================"

# Check for dot command
if ! command -v dot &>/dev/null; then
    echo -e "${RED}Error: 'dot' (Graphviz) not found${NC}"
    exit 1
fi

# Create directories
mkdir -p "$VIZ_DIR/original" "$VIZ_DIR/obfuscated"

# Function to safely render a DOT file
render_dot() {
    local dot_file="$1"
    local output_file="$2"
    local file_type="${3:-original}"

    if [ ! -f "$dot_file" ]; then
        return 1
    fi

    local base_name=$(basename "$dot_file")
    echo -e "  ${CYAN}Processing $file_type: $base_name${NC}"

    # Check if DOT file is valid
    if ! dot -Tsvg "$dot_file" -o /dev/null 2>/dev/null; then
        echo -e "    ${RED}✗ Invalid DOT file: $base_name${NC}"
        return 1
    fi

    # Render with timeout
    if timeout $TIMEOUT_SEC dot -Tpng "$dot_file" -o "$output_file" 2>/dev/null; then
        echo -e "    ${GREEN}✓ Success${NC}"
        return 0
    else
        echo -e "    ${RED}✗ Failed or timed out${NC}"
        return 1
    fi
}

# Count files
orig_count=$(ls -1 "$DOT_DIR/original"/*.dot 2>/dev/null | wc -l)
obf_count=$(ls -1 "$DOT_DIR/obfuscated"/*.dot 2>/dev/null | wc -l)

echo -e "${CYAN}Found $orig_count original and $obf_count obfuscated DOT files${NC}"
echo ""

# Process original DOT files
echo -e "${YELLOW}Rendering original CFGs...${NC}"
success_count=0
fail_count=0

for dot_file in "$DOT_DIR/original"/*.dot; do
    if [ -f "$dot_file" ]; then
        base_name=$(basename "$dot_file" .dot)
        output_file="$VIZ_DIR/original/${base_name}.png"

        if render_dot "$dot_file" "$output_file" "original"; then
            ((success_count++))
        else
            ((fail_count++))
        fi
    fi
done

echo -e "${GREEN}Original: $success_count succeeded, $fail_count failed${NC}"
echo ""

# Process obfuscated DOT files
echo -e "${YELLOW}Rendering obfuscated CFGs...${NC}"
success_count=0
fail_count=0

for dot_file in "$DOT_DIR/obfuscated"/*.dot; do
    if [ -f "$dot_file" ]; then
        base_name=$(basename "$dot_file" .dot)
        output_file="$VIZ_DIR/obfuscated/${base_name}.png"

        if render_dot "$dot_file" "$output_file" "obfuscated"; then
            ((success_count++))
        else
            ((fail_count++))
        fi
    fi
done

echo -e "${GREEN}Obfuscated: $success_count succeeded, $fail_count failed${NC}"
echo ""

# Generate HTML report
echo -e "${YELLOW}Generating HTML report...${NC}"
if python3 create_comparison.py; then
    echo -e "${GREEN}✓ HTML report generated successfully${NC}"
else
    echo -e "${RED}✗ Failed to generate HTML report${NC}"
fi

echo ""
echo -e "${GREEN}Visualization complete!${NC}"
echo -e "View the report at: file://$(realpath "$VIZ_DIR/comparison/index.html")"
