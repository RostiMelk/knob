#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Find clang-tidy: prefer brew llvm, then PATH
if command -v /opt/homebrew/opt/llvm/bin/clang-tidy &>/dev/null; then
    CT=/opt/homebrew/opt/llvm/bin/clang-tidy
elif command -v clang-tidy &>/dev/null; then
    CT=clang-tidy
else
    echo "error: clang-tidy not found"
    echo "  brew install llvm"
    exit 1
fi

EXTRA_ARGS=()
if [[ "$(uname)" == "Darwin" ]] && xcrun --show-sdk-path &>/dev/null; then
    EXTRA_ARGS+=(--extra-arg="--sysroot=$(xcrun --show-sdk-path)")
fi

DB=build/compile_commands.json
if [ ! -f "$DB" ]; then
    echo "error: $DB not found — run idf.py build or cmake -B build -S sim first"
    exit 1
fi

# Only lint files under main/ that have an entry in compile_commands.json
mapfile -t SRCS < <(
    python3 -c "
import json, sys, os
root = os.getcwd()
with open('$DB') as f:
    db = json.load(f)
for entry in db:
    p = entry.get('file', '')
    # Normalise to relative path
    if p.startswith(root):
        p = p[len(root)+1:]
    if p.startswith('main/'):
        print(p)
" | sort -u
)

if [ ${#SRCS[@]} -eq 0 ]; then
    echo "no main/ source files found in $DB"
    exit 0
fi

echo "running $($CT --version | head -1)"
echo "linting ${#SRCS[@]} files..."
echo ""

FAIL=0
for f in "${SRCS[@]}"; do
    OUTPUT=$($CT -p build --quiet "${EXTRA_ARGS[@]}" "$f" 2>&1)
    RC=$?
    FILTERED=$(echo "$OUTPUT" | grep -v "warnings\? generated\.\|errors\? generated\." || true)
    if [ -n "$FILTERED" ]; then
        echo "$FILTERED"
    fi
    if [ "$RC" -ne 0 ]; then
        FAIL=1
    fi
done

if [ "$FAIL" -ne 0 ]; then
    echo ""
    echo "lint: issues found"
    exit 1
fi

echo "lint: clean"
