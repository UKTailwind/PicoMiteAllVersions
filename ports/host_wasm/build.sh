#!/bin/bash
# build.sh — Build the MMBasic WASM host target via emscripten.
#
# Transitional: the actual Makefile still lives at host/Makefile.wasm
# while host/ retirement is staged. This script is the port-wasm
# entry point that docs + CI should use going forward.
#
# Usage:
#   ./build.sh          Build (incremental)
#   ./build.sh clean    Clean artifacts
#   ./build.sh rebuild  Clean then build

set -e
cd "$(dirname "$0")/../../host"
exec ./build_wasm.sh "$@"
