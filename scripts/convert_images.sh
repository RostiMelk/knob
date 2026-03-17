#!/usr/bin/env bash
# Converts PNG logos to LVGL C arrays for firmware embedding.
# Run from project root: ./scripts/convert_images.sh
#
# Logos: RGB565A8 + LZ4 compression + premultiplied alpha → C arrays
# Backgrounds: solid colors at runtime (no image files needed)
#
# Prerequisites: pip3 install pypng lz4

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LVGL_IMAGE="$PROJECT_DIR/managed_components/lvgl__lvgl/scripts/LVGLImage.py"
if [ ! -f "$LVGL_IMAGE" ]; then
    LVGL_IMAGE="$SCRIPT_DIR/LVGLImage.py"
fi
OUTPUT_DIR="$PROJECT_DIR/main/ui/images"

if ! python3 -c "import png" 2>/dev/null; then
    echo "ERROR: pypng not installed. Run: pip3 install pypng lz4"
    exit 1
fi

if [ ! -f "$LVGL_IMAGE" ]; then
    echo "ERROR: LVGLImage.py not found. Run 'idf.py build' first to download LVGL,"
    echo "       or: curl -o scripts/LVGLImage.py https://raw.githubusercontent.com/lvgl/lvgl/master/scripts/LVGLImage.py"
    exit 1
fi

# Preserve the hand-written header
HEADER="$OUTPUT_DIR/images.h"
HEADER_BAK=""
if [ -f "$HEADER" ]; then
    HEADER_BAK=$(mktemp)
    cp "$HEADER" "$HEADER_BAK"
fi

# Remove old generated .c files (keep images.h)
find "$OUTPUT_DIR" -name '*.c' -delete 2>/dev/null || true

echo "Converting logos (RGB565A8 + LZ4 + premultiply)..."
python3 "$LVGL_IMAGE" --cf RGB565A8 --compress LZ4 --premultiply --ofmt C -o "$OUTPUT_DIR" "$PROJECT_DIR/assets/logos/"

# Remove background artifacts picked up from bg/ subfolder
rm -f "$OUTPUT_DIR"/*_bg*

# Restore header
if [ -n "$HEADER_BAK" ]; then
    cp "$HEADER_BAK" "$HEADER"
    rm -f "$HEADER_BAK"
fi

echo ""
echo "Done! Generated C arrays:"
ls -la "$OUTPUT_DIR"/*.c 2>/dev/null
TOTAL=$(du -sh "$OUTPUT_DIR" | cut -f1)
echo ""
echo "Total size: $TOTAL (compiled into firmware binary)"
echo ""
echo "Remember to:"
echo "  1. Add any new .c files to main/CMakeLists.txt SRCS"
echo "  2. Add LV_IMAGE_DECLARE() to main/ui/images/images.h"
echo "  3. Add &symbol to s_logos[] in main/ui/ui.cpp"
