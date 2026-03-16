#!/usr/bin/env bash
# Converts PNG assets to LVGL binary format for SPIFFS embedding.
# Run from project root: ./scripts/convert_images.sh
#
# This is also run automatically by CMake during build, but you can run it
# manually to inspect the output or test changes.
#
# Logos: ARGB8888 (preserve alpha transparency)
# Backgrounds: RGB565 with Floyd-Steinberg dithering (fixes gradient banding)
#
# Prerequisites: pip3 install pypng lz4

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
LVGL_IMAGE="$SCRIPT_DIR/LVGLImage.py"
OUTPUT_DIR="$PROJECT_DIR/build/spiffs_data"

# Check prerequisites
if ! python3 -c "import png" 2>/dev/null; then
    echo "ERROR: pypng not installed. Run: pip3 install pypng lz4"
    exit 1
fi

if [ ! -f "$LVGL_IMAGE" ]; then
    echo "ERROR: LVGLImage.py not found at $LVGL_IMAGE"
    exit 1
fi

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/bg"

echo "Converting logos (ARGB8888)..."
python3 "$LVGL_IMAGE" --cf ARGB8888 --ofmt BIN -o "$OUTPUT_DIR" "$PROJECT_DIR/assets/logos/"

# LVGLImage.py may convert bg/ subfolder contents with ARGB8888 — remove them
rm -f "$OUTPUT_DIR"/*_bg.bin

echo "Converting backgrounds (RGB565 + dithering)..."
python3 "$LVGL_IMAGE" --cf RGB565 --rgb565dither --ofmt BIN -o "$OUTPUT_DIR/bg" "$PROJECT_DIR/assets/logos/bg/"

echo ""
echo "Done! Converted files:"
ls -la "$OUTPUT_DIR"/*.bin "$OUTPUT_DIR/bg/"*.bin 2>/dev/null
TOTAL=$(du -sh "$OUTPUT_DIR" | cut -f1)
echo ""
echo "Total size: $TOTAL (SPIFFS partition: 9MB)"
