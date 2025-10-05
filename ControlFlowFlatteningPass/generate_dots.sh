set -e

BUILD_DIR="build"
TEST_DIR="tests"
OPT="${OPT:-opt}"

echo "Generating CFG visualizations via helper script..."

find "$TEST_DIR" -maxdepth 1 -type f -name "test_*.c" | while read -r test_file; do
    name=$(basename "$test_file" .c)
    echo "Processing $name..."

    TMP_ORIG_DIR="${BUILD_DIR}/tmp_orig"
    TMP_FLAT_DIR="${BUILD_DIR}/tmp_flat"
    mkdir -p "$TMP_ORIG_DIR" "$TMP_FLAT_DIR"

    "$OPT" -passes='dot-cfg' -disable-output "${BUILD_DIR}/${name}.ll" --dot-cfg-output-dir="$TMP_ORIG_DIR" >/dev/null 2>&1
    for dotfile in "$TMP_ORIG_DIR"/*.dot; do
        if [ -f "$dotfile" ]; then
            mv "$dotfile" "${BUILD_DIR}/$(basename "$dotfile" .dot)_orig.dot"
        fi
    done

    "$OPT" -passes='dot-cfg' -disable-output "${BUILD_DIR}/${name}_flat.ll" --dot-cfg-output-dir="$TMP_FLAT_DIR" >/dev/null 2>&1
    for dotfile in "$TMP_FLAT_DIR"/*.dot; do
        if [ -f "$dotfile" ]; then
            mv "$dotfile" "${BUILD_DIR}/$(basename "$dotfile" .dot)_flat.dot"
        fi
    done

    rm -rf "$TMP_ORIG_DIR" "$TMP_FLAT_DIR"
done

echo "Dot file generation complete."
