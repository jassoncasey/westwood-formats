#!/bin/sh
# Test runner for mix-tool
# Runs Python test suite and reports results

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/impl/build"

export MIX_TOOL="$BUILD_DIR/mix-tool"
export MIX_LIB="$BUILD_DIR/libmix.dylib"
export TESTDATA="$PROJECT_DIR/testdata"
export FIXTURES="$SCRIPT_DIR/fixtures"

if [ ! -x "$MIX_TOOL" ]; then
    echo "Error: mix-tool not found at $MIX_TOOL" >&2
    echo "Run: cd impl && cmake -B build && cmake --build build" >&2
    exit 1
fi

exec python3 "$SCRIPT_DIR/test_mix.py" "$@"
