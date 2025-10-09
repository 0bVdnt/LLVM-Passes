#!/bin/bash

RESULTS_DIR="test_results"
DOT_DIR="$RESULTS_DIR/dot_files"
VIZ_DIR="$RESULTS_DIR/visualizations"
MAX_JOBS=8 # Adjust based on CPU cores

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}Parallel Visualization Script${NC}"
echo "Using $MAX_JOBS parallel jobs"

# Create directories
mkdir -p "$VIZ_DIR/original" "$VIZ_DIR/obfuscated"

# Function to render a single DOT file
render_single() {
    local dot_file="$1"
    local output_dir="$2"
    local base_name=$(basename "$dot_file" .dot)
    local output_file="$output_dir/${base_name}.png"

    # Use simpler layout for complex graphs
    if timeout 3 dot -Tpng -Gnslimit=2 -Gmaxiter=500 "$dot_file" -o "$output_file" 2>/dev/null; then
        echo "✓ $base_name"
    else
        # Try with simpler layout engine
        if timeout 2 neato -Tpng -Goverlap=false "$dot_file" -o "$output_file" 2>/dev/null; then
            echo "⚡ $base_name (neato)"
        else
            echo "✗ $base_name"
        fi
    fi
}

export -f render_single

echo -e "${YELLOW}Rendering original CFGs...${NC}"
find "$DOT_DIR/original" -name "*.dot" -print0 |
    xargs -0 -P $MAX_JOBS -I {} bash -c 'render_single "$@"' _ {} "$VIZ_DIR/original"

echo -e "${YELLOW}Rendering obfuscated CFGs...${NC}"
find "$DOT_DIR/obfuscated" -name "*.dot" -print0 |
    xargs -0 -P $MAX_JOBS -I {} bash -c 'render_single "$@"' _ {} "$VIZ_DIR/obfuscated"

echo -e "${YELLOW}Generating HTML report...${NC}"
python3 create_comparison.py

echo -e "${GREEN}Done!${NC}"
