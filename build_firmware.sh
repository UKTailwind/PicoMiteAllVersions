#!/bin/bash
# build_firmware.sh — Build PicoMite firmware for any device target.
#
# Mirrors .github/workflows/firmware.yml so local builds exercise the
# same gate CI enforces on main: same Pico SDK 2.2.0 at $PICO_SDK_PATH,
# same CMakeLists.txt COMPILE switching, same artifacts. The SDK is
# never mutated — the historical gpio.c/gpio.h patches were eliminated
# on the sdk-patch-removal branch. If this script touches the SDK tree,
# it's a bug.
#
# Targets:
#   rp2040        PICO          PicoCalc RP2040 (console + SD)
#   rp2350        PICORP2350    PicoCalc RP2350 (pimoroni_pga2350)
#
# WEB variants (COMPILE=WEB, WEBRP2350) are not supported on this fork.
# Two orthogonal blockers:
#
#   1. GUI.c is compiled for WEB targets but references Option.MaxCtrls
#      unconditionally. Upstream works around this by #defining
#      GUICONTROLS for WEBRP2350 as a special case; we mirror that fix
#      in CMakeLists.txt, but that alone isn't enough to link.
#
#   2. The bytecode VM (bc_vm.c, vm_sys_*.c, bc_runtime.c) references
#      PICOMITE-only symbols: FASTGFX bridge (bc_fastgfx_*), ShadowBuf /
#      fb_dma_chan, mergerunning / mergetimer, LocalKeyDown, and the
#      multicore FIFO. These only exist under #if defined(PICOMITE).
#      On WEB builds the VM links against nothing and fails.
#
# Making WEB variants build is a separate effort (gate the VM surface on
# PICOMITE, or stub out FASTGFX for non-PICOMITE). Not this branch's job.
#
# Usage:
#   ./build_firmware.sh                        Build both (rp2040 + rp2350)
#   ./build_firmware.sh rp2040                 Build one target
#   ./build_firmware.sh rp2350                 Build the other
#   PICO_SDK_PATH=... ./build_firmware.sh      Override SDK path (default $HOME/pico/pico-sdk)
#
# Outputs:
#   build/PicoMite.uf2              (rp2040)
#   build2350/PicoMite.uf2          (rp2350)
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

target_to_compile() {
    case "$1" in
        rp2040)  echo PICO        ;;
        rp2350)  echo PICORP2350  ;;
        *)       echo "" ;;
    esac
}

target_to_dir() {
    case "$1" in
        rp2040)  echo build      ;;
        rp2350)  echo build2350  ;;
        *)       echo "" ;;
    esac
}

# --- args -------------------------------------------------------------------

TARGETS=()
if [ $# -eq 0 ]; then
    TARGETS=(rp2040 rp2350)
else
    for arg in "$@"; do
        if [ -z "$(target_to_compile "$arg")" ]; then
            echo "error: unknown target '$arg' (want: rp2040, rp2350)" >&2
            exit 2
        fi
        TARGETS+=("$arg")
    done
fi

# --- per-target build ------------------------------------------------------
#
# CMakeLists.txt's `if(NOT DEFINED COMPILE) set(COMPILE PICO)` default lets
# us pick a target by passing -DCOMPILE=<variant> to cmake — no source-tree
# rewriting needed. (An earlier version of this script sed-rewrote the
# active set(COMPILE …) line in CMakeLists.txt; the per-port decascade
# moved that line inside an if() guard which broke the rewrite.)

build_one() {
    local name="$1" compile build_dir stamp prev jobs
    compile=$(target_to_compile "$name")
    build_dir=$(target_to_dir "$name")

    echo
    echo "=== Building $name (COMPILE=$compile, dir=$build_dir) ==="

    # Pico SDK caches PICO_PLATFORM in the build dir's CMakeCache; wipe
    # if the active target has changed since the last configure.
    stamp="$build_dir/.compile_target"
    prev=""
    [ -f "$stamp" ] && prev=$(cat "$stamp")
    if [ "$prev" != "$compile" ]; then
        echo "  cleaning $build_dir (target was '${prev:-<none>}', now '$compile')"
        rm -rf "$build_dir"
        mkdir "$build_dir"
    else
        mkdir -p "$build_dir"
    fi

    (cd "$build_dir" && cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" -DCOMPILE="$compile" ..)
    echo "$compile" > "$stamp"

    jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    make -C "$build_dir" -j"$jobs"

    local uf2="$build_dir/PicoMite.uf2"
    local elf="$build_dir/PicoMite.elf"
    if [ ! -f "$uf2" ]; then
        echo "error: no .uf2 produced for $name at $uf2" >&2
        exit 1
    fi

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
