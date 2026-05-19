#!/bin/bash
# build_picocalc_firmware.sh — Build PicoMite firmware for PicoCalc variants.
#
# Mirrors .github/workflows/firmware.yml so local builds exercise the
# same gate CI enforces on main: same Pico SDK at $PICO_SDK_PATH, same
# explicit PORT switching, same artifacts. The SDK is never mutated.
#
# Targets:
#   rp2040        picocalc_rp2040       PicoCalc RP2040
#   rp2040-wifi   picocalc_wifi_rp2040  PicoCalc Pico W
#   rp2350        picocalc_rp2350       PicoCalc RP2350
#   rp2350-wifi   picocalc_wifi_rp2350  PicoCalc Pico 2 W
#
# Usage:
#   ./build_picocalc_firmware.sh                        Build all PicoCalc variants
#   ./build_picocalc_firmware.sh rp2040                 Build one target
#   ./build_picocalc_firmware.sh rp2350-wifi            Build one target
#   PICO_SDK_PATH=... ./build_picocalc_firmware.sh      Override SDK path (default $HOME/pico/pico-sdk)
#
# Outputs:
#   build_picocalc_rp2040/PicoMite.uf2
#   build_picocalc_wifi_rp2040/PicoMite.uf2
#   build_picocalc_rp2350/PicoMite.uf2
#   build_picocalc_wifi_rp2350/PicoMite.uf2
#
# Exit code: 0 only if every requested target produces a .uf2.

set -euo pipefail
cd "$(dirname "$0")"

export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico/pico-sdk}"

# --- preflight --------------------------------------------------------------

for cmd in arm-none-eabi-gcc cmake make; do
    command -v "$cmd" >/dev/null 2>&1 \
        || { echo "error: $cmd not on PATH" >&2; exit 2; }
done

[ -d "$PICO_SDK_PATH" ] \
    || { echo "error: PICO_SDK_PATH does not exist: $PICO_SDK_PATH" >&2; exit 2; }

# --- target registry --------------------------------------------------------

target_to_port() {
    case "$1" in
        rp2040)       echo picocalc_rp2040       ;;
        rp2040-wifi)  echo picocalc_wifi_rp2040  ;;
        rp2350)       echo picocalc_rp2350       ;;
        rp2350-wifi)  echo picocalc_wifi_rp2350  ;;
        *)            echo "" ;;
    esac
}

target_to_dir() {
    local port
    port=$(target_to_port "$1")
    [ -n "$port" ] && echo "build_$port"
}

# --- args -------------------------------------------------------------------

TARGETS=()
if [ $# -eq 0 ]; then
    TARGETS=(rp2040 rp2040-wifi rp2350 rp2350-wifi)
else
    for arg in "$@"; do
        if [ -z "$(target_to_port "$arg")" ]; then
            echo "error: unknown target '$arg' (want: rp2040, rp2040-wifi, rp2350, rp2350-wifi)" >&2
            exit 2
        fi
        TARGETS+=("$arg")
    done
fi

# --- per-target build ------------------------------------------------------
#
# Explicit PORT values select the PicoCalc hardware variant; no compiler
# flag is needed to opt into PicoCalc support.

build_one() {
    local name="$1" port build_dir stamp prev jobs
    port=$(target_to_port "$name")
    build_dir=$(target_to_dir "$name")

    echo
    echo "=== Building $name (PORT=$port, dir=$build_dir) ==="

    # Pico SDK caches PICO_PLATFORM in the build dir's CMakeCache; wipe
    # if the active target has changed since the last configure.
    stamp="$build_dir/.port_target"
    prev=""
    [ -f "$stamp" ] && prev=$(cat "$stamp")
    if [ "$prev" != "$port" ]; then
        echo "  cleaning $build_dir (target was '${prev:-<none>}', now '$port')"
        rm -rf "$build_dir"
        mkdir "$build_dir"
    else
        mkdir -p "$build_dir"
    fi

    (cd "$build_dir" && cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPORT="$port" ..)
    echo "$port" > "$stamp"

    jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    make -C "$build_dir" -j"$jobs"

    local uf2="$build_dir/PicoMite.uf2"
    local elf="$build_dir/PicoMite.elf"
    if [ ! -f "$uf2" ]; then
        echo "error: no .uf2 produced for $name at $uf2" >&2
        exit 1
    fi
    ./tools/check_flash_layout.sh --target "$port" --build-dir "$build_dir"

    local text
    text=$(arm-none-eabi-size "$elf" | awk 'NR==2 { print $1 }')
    local uf2_size
    uf2_size=$(wc -c < "$uf2" | tr -d ' ')
    echo "  OK: $uf2 (${uf2_size} bytes, text=${text})"
}

for t in "${TARGETS[@]}"; do
    build_one "$t"
done

echo
echo "=== All firmware targets built successfully ==="
