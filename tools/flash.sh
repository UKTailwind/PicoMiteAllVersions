#!/bin/bash
# tools/flash.sh — Build and flash PicoMite firmware to connected device
#
# Usage:
#   ./tools/flash.sh              Build RP2350 (default) and flash
#   ./tools/flash.sh --build-only Just build, don't flash
#   ./tools/flash.sh --flash-only Flash existing build, don't rebuild
#
# Requires: picotool (brew install picotool), arm-none-eabi-gcc

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Always use the correct SDK — ignore any stale PICO_SDK_PATH in the environment
PICO_SDK_PATH="$HOME/pico/pico-sdk"
BUILD_DIR="build2350"

BUILD=1
FLASH=1

for arg in "$@"; do
    case "$arg" in
        --build-only) FLASH=0 ;;
        --flash-only) BUILD=0 ;;
        *) echo "Unknown arg: $arg"; exit 1 ;;
    esac
done

if [ "$BUILD" -eq 1 ]; then
    echo "=== Building RP2350 firmware ==="
    rm -rf "$BUILD_DIR"
    mkdir "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" ..
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    cd ..
    echo ""
    ls -lh "$BUILD_DIR/PicoMite.uf2"
    echo ""
fi

if [ "$FLASH" -eq 1 ]; then
    UF2="$BUILD_DIR/PicoMite.uf2"
    if [ ! -f "$UF2" ]; then
        echo "Error: $UF2 not found. Run without --flash-only first."
        exit 1
    fi

    echo "=== Flashing ==="
    picotool load "$UF2" -f
    picotool reboot
    echo ""
    echo "Done. Device rebooted into new firmware."
fi
