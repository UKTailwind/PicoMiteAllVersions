#!/usr/bin/env bash
# Build the ESP32-S3 port. This is intentionally opt-in; ESP-IDF is
# heavyweight and is not part of the default host/device build gate.
#
# Usage:
#   ./buildesp32.sh              # run HAL purity, then idf.py build
#   ./buildesp32.sh fullclean    # pass arguments through to idf.py
#
# Environment:
#   IDF_PATH=/path/to/esp-idf    # defaults to ~/esp/esp-idf
#   SKIP_HAL_PURITY=1            # skip the source-level purity gate

set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
port_dir="$root/ports/esp32_s3"
idf_path="${IDF_PATH:-$HOME/esp/esp-idf}"

if [ "${SKIP_HAL_PURITY:-0}" != "1" ]; then
    printf '=== HAL purity gate ===\n'
    "$root/tools/check_hal_purity.sh"
fi

if ! command -v idf.py >/dev/null 2>&1; then
    if [ ! -f "$idf_path/export.sh" ]; then
        echo "ESP-IDF not found. Set IDF_PATH or install ESP-IDF at $idf_path." >&2
        exit 2
    fi
    # export.sh intentionally mutates PATH, IDF_PATH, and toolchain vars.
    # shellcheck disable=SC1090
    . "$idf_path/export.sh" >/dev/null
fi

cd "$port_dir"

if [ ! -f sdkconfig ]; then
    idf.py set-target esp32s3
fi

if [ "$#" -eq 0 ]; then
    set -- build
fi

idf.py "$@"
