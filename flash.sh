#!/usr/bin/env bash
# flash.sh — Build, flash, and monitor helper for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8
#
# NOTE: GitHub does not preserve file permissions. After cloning, run:
#   chmod +x flash.sh
#
# BOARD QUIRK: This board has ONE USB-C port connected to a CH445P analog switch.
# The cable orientation determines which chip you talk to:
#   - One way  → ESP32-S3 (main MCU, used for flashing firmware)
#   - Flipped  → ESP32 co-processor
# If the wrong chip is detected, flip the USB-C cable and try again.
#
# Usage:
#   ./flash.sh               — build + flash
#   ./flash.sh --monitor     — build + flash + open serial monitor
#   ./flash.sh -m            — same as --monitor
#   ./flash.sh --build-only  — build only, no flash

set -e

# ─── Colors ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

ok()   { echo -e "  ${GREEN}✅ $*${NC}"; }
warn() { echo -e "  ${YELLOW}⚠️  $*${NC}"; }
err()  { echo -e "  ${RED}❌ $*${NC}"; }
info() { echo -e "  ${CYAN}$*${NC}"; }
step() { echo -e "\n${BOLD}$*${NC}"; }

# ─── Argument parsing ────────────────────────────────────────────────────────
MODE="flash"  # flash | build-only
MONITOR=false

for arg in "$@"; do
  case "$arg" in
    --monitor|-m)    MONITOR=true ;;
    --build-only)    MODE="build-only" ;;
    --help|-h)
      echo "Usage: ./flash.sh [--monitor|-m] [--build-only]"
      echo "  (no flags)     Build and flash firmware"
      echo "  --monitor, -m  Build, flash, then open serial monitor"
      echo "  --build-only   Build only, do not flash"
      exit 0
      ;;
    *)
      err "Unknown argument: $arg"
      echo "Run './flash.sh --help' for usage."
      exit 1
      ;;
  esac
done

# ─── 1. Prerequisites ────────────────────────────────────────────────────────
step "🔍 Checking prerequisites..."

PREREQ_OK=true

# ESP-IDF
if [ -n "$IDF_PATH" ] && [ -d "$IDF_PATH" ]; then
  ok "ESP-IDF found at \$IDF_PATH ($IDF_PATH)"
elif [ -d "$HOME/esp/esp-idf" ]; then
  ok "ESP-IDF found at ~/esp/esp-idf"
else
  err "ESP-IDF not found. Expected at ~/esp/esp-idf or \$IDF_PATH."
  err "Install it: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/"
  PREREQ_OK=false
fi

# Rust ESP toolchain
if command -v rustup &>/dev/null && rustup toolchain list 2>/dev/null | grep -q 'esp'; then
  ok "Rust ESP toolchain found"
else
  err "Rust ESP toolchain not found. Run: espup install"
  err "See: https://github.com/esp-rs/espup"
  PREREQ_OK=false
fi

# ldproxy
if command -v ldproxy &>/dev/null; then
  ok "ldproxy found"
else
  err "ldproxy not found. Run: cargo install ldproxy"
  PREREQ_OK=false
fi

# espup (optional — warn only)
if command -v espup &>/dev/null; then
  ok "espup found"
else
  warn "espup not found (optional, but needed to update the ESP Rust toolchain)"
  warn "Install: cargo install espup"
fi

if [ "$PREREQ_OK" = false ]; then
  err "Prerequisites missing. Fix the errors above and try again."
  exit 1
fi

# ─── 2. Source ESP-IDF ───────────────────────────────────────────────────────
if [ -z "$IDF_PATH" ]; then
  info "Sourcing ESP-IDF from ~/esp/esp-idf/export.sh ..."
  # shellcheck disable=SC1091
  source "$HOME/esp/esp-idf/export.sh" 2>/dev/null || {
    err "Failed to source ESP-IDF. Check your installation."
    exit 1
  }
fi

# ─── 3. Detect ESP32-S3 port ─────────────────────────────────────────────────
if [ "$MODE" != "build-only" ]; then
  step "🔌 Detecting ESP32-S3 port..."

  # Collect candidate ports
  PORTS=()
  if [[ "$(uname)" == "Darwin" ]]; then
    while IFS= read -r p; do PORTS+=("$p"); done < <(ls /dev/cu.usb* 2>/dev/null || true)
  else
    while IFS= read -r p; do PORTS+=("$p"); done < <(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
  fi

  if [ ${#PORTS[@]} -eq 0 ]; then
    err "No USB serial ports found."
    err "Connect the board via USB-C and try again."
    exit 1
  fi

  DETECTED_PORT=""
  WRONG_CHIP_PORT=""

  for port in "${PORTS[@]}"; do
    info "Probing $port ..."
    CHIP_OUTPUT=$(esptool.py --port "$port" --no-stub chip_id 2>&1 || true)

    if echo "$CHIP_OUTPUT" | grep -qi "ESP32-S3"; then
      ok "Found ESP32-S3 on $port"
      DETECTED_PORT="$port"
      break
    elif echo "$CHIP_OUTPUT" | grep -qi "ESP32"; then
      warn "Found ESP32 (co-processor) on $port — wrong chip!"
      WRONG_CHIP_PORT="$port"
    else
      warn "Could not identify chip on $port (may be in use or unresponsive)"
    fi
  done

  if [ -z "$DETECTED_PORT" ]; then
    if [ -n "$WRONG_CHIP_PORT" ]; then
      echo ""
      echo -e "${YELLOW}${BOLD}⚠️  USB-C cable orientation issue detected!${NC}"
      echo -e "${YELLOW}  The board's CH445P switch is routing to the ESP32 co-processor.${NC}"
      echo -e "${YELLOW}  👉 Flip the USB-C cable and run this script again.${NC}"
    else
      err "Could not find an ESP32-S3 on any port."
      err "Make sure the board is connected and drivers are installed."
    fi
    exit 1
  fi
fi

# ─── 4. Build ────────────────────────────────────────────────────────────────
step "🔨 Building firmware..."

if [ ! -d "build" ]; then
  info "No build directory found — setting target to esp32s3..."
  idf.py set-target esp32s3
fi

info "Target: esp32s3"
if idf.py build; then
  # Try to extract binary size from build output
  BIN=$(find build -maxdepth 1 -name '*.bin' | head -1)
  if [ -n "$BIN" ]; then
    SIZE=$(du -sh "$BIN" 2>/dev/null | cut -f1)
    ok "Build successful (binary: $BIN, size: $SIZE)"
  else
    ok "Build successful"
  fi
else
  err "Build failed. Check the output above for errors."
  exit 1
fi

if [ "$MODE" = "build-only" ]; then
  echo ""
  ok "Build-only mode — done!"
  exit 0
fi

# ─── 5. Flash ────────────────────────────────────────────────────────────────
step "⚡ Flashing..."
info "Port: $DETECTED_PORT"

FLASH_OUTPUT=$(idf.py -p "$DETECTED_PORT" flash 2>&1) && FLASH_EXIT=0 || FLASH_EXIT=$?

echo "$FLASH_OUTPUT"

if [ $FLASH_EXIT -ne 0 ]; then
  if echo "$FLASH_OUTPUT" | grep -qi "Failed to connect"; then
    echo ""
    echo -e "${YELLOW}${BOLD}⚠️  Failed to connect to the ESP32-S3!${NC}"
    echo -e "${YELLOW}  Try the following:${NC}"
    echo -e "${YELLOW}  1. Power cycle the board (unplug and replug USB-C)${NC}"
    echo -e "${YELLOW}  2. Hold the BOOT button while plugging in, then release${NC}"
    echo -e "${YELLOW}  3. Make sure the USB-C cable is in the correct orientation${NC}"
    echo -e "${YELLOW}     (flip it if you see the wrong chip)${NC}"
    echo -e "${YELLOW}  Then run: ./flash.sh${NC}"
  else
    err "Flash failed. Check the output above."
  fi
  exit 1
fi

ok "Flash complete!"

# ─── 6. Monitor (optional) ───────────────────────────────────────────────────
if [ "$MONITOR" = true ]; then
  step "📺 Starting monitor... (Ctrl+T then Ctrl+X to exit)"
  idf.py -p "$DETECTED_PORT" monitor
fi
