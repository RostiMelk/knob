#!/usr/bin/env bash
# test.sh — Local build + lint check. Run before pushing.
#
# Usage:
#   ./test.sh          Build firmware (ESP-IDF)
#   ./test.sh --sim    Build simulator (SDL2) instead
#   ./test.sh --all    Build both firmware + simulator
#   ./test.sh --lint   Run clang-tidy lint only (requires prior build)
#
# Exit codes:
#   0 = all checks passed
#   1 = build or lint failed

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

cd "$(dirname "$0")"

MODE="firmware"
LINT=false

for arg in "$@"; do
  case "$arg" in
    --sim)  MODE="sim" ;;
    --all)  MODE="all" ;;
    --lint) LINT=true ;;
    -h|--help)
      echo "Usage: ./test.sh [--sim] [--all] [--lint]"
      echo "  (default)  Build ESP-IDF firmware"
      echo "  --sim      Build SDL2 simulator"
      echo "  --all      Build both"
      echo "  --lint     Run clang-tidy (requires prior build)"
      exit 0
      ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

FAIL=0

# ── Firmware build ──
build_firmware() {
  echo -e "${BOLD}▸ Building firmware (ESP-IDF)...${NC}"
  if ! command -v idf.py &>/dev/null; then
    echo -e "${YELLOW}  idf.py not found — source ESP-IDF first:${NC}"
    echo "    source ~/esp/esp-idf/export.sh"
    return 1
  fi
  idf.py set-target esp32s3 2>/dev/null || true
  if idf.py build; then
    echo -e "${GREEN}  ✓ Firmware build passed${NC}"
    echo ""
    idf.py size 2>/dev/null | grep -E "^(Total|Used)" || true
  else
    echo -e "${RED}  ✗ Firmware build failed${NC}"
    return 1
  fi
}

# ── Simulator build ──
build_sim() {
  echo -e "${BOLD}▸ Building simulator (SDL2)...${NC}"
  if ! command -v cmake &>/dev/null; then
    echo -e "${RED}  cmake not found${NC}"
    return 1
  fi
  cmake -B build -S sim 2>/dev/null
  if cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"; then
    echo -e "${GREEN}  ✓ Simulator build passed${NC}"
  else
    echo -e "${RED}  ✗ Simulator build failed${NC}"
    return 1
  fi
}

# ── Lint ──
run_lint() {
  echo -e "${BOLD}▸ Running clang-tidy lint...${NC}"
  if [ ! -f build/compile_commands.json ]; then
    echo -e "${YELLOW}  No compile_commands.json — build first${NC}"
    return 1
  fi
  if ./scripts/lint.sh; then
    echo -e "${GREEN}  ✓ Lint passed${NC}"
  else
    echo -e "${RED}  ✗ Lint issues found${NC}"
    return 1
  fi
}

# ── Run ──
case "$MODE" in
  firmware) build_firmware || FAIL=1 ;;
  sim)      build_sim || FAIL=1 ;;
  all)
    build_firmware || FAIL=1
    echo ""
    build_sim || FAIL=1
    ;;
esac

if $LINT || [ "$MODE" = "firmware" ] || [ "$MODE" = "all" ]; then
  echo ""
  run_lint || FAIL=1
fi

echo ""
if [ "$FAIL" -eq 0 ]; then
  echo -e "${GREEN}${BOLD}All checks passed ✓${NC}"
else
  echo -e "${RED}${BOLD}Some checks failed ✗${NC}"
  exit 1
fi
