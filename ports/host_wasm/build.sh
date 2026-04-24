#!/bin/bash
# build.sh — Build the MMBasic WASM host target via emscripten.
#
# Output: host/web/picomite.{mjs,wasm,data} (legacy location, kept
# stable while retirement completes).
#
# Assumes emscripten is on PATH. If not, sources ~/emsdk/emsdk_env.sh
# when present. Otherwise fails with an install hint.

set -e
cd "$(dirname "$0")"

if ! command -v emcc >/dev/null 2>&1; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh" >/dev/null
    fi
fi

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not found on PATH." >&2
    echo "Install emscripten: https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

case "${1:-}" in
    clean|rebuild)
        echo "Cleaning..."
        make clean
        echo "Building..."
        make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
        ;;
    *)
        make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
        ;;
esac

echo ""
echo "Build complete: host/web/picomite.{mjs,wasm,data}"
