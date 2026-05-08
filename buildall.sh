#!/usr/bin/env bash
# Build every device target from clean dirs. Any failure stops the
# script — green across all targets is the gate.
#
# Two target-name conventions:
#   - UPPERCASE entries (PICO, HDMIUSB, ...) are legacy COMPILE values.
#     buildall passes them as -DCOMPILE=<NAME>; CMakeLists.txt's shim
#     resolves them to the matching ports/<dir>.
#   - lowercase entries (e.g. mymite, my_board_v2) are direct port
#     directory names. buildall passes them as -DPORT=<dir>.
# New single-board ports should use the lowercase form.
#
# PICOCALC is enabled for every PICOMITE or PICOMITEWEB target that isn't a
# USB-keyboard variant — matches the legacy formula
#   #define PICOCALC ((defined(PICOMITE) || defined(PICOMITEWEB)) && !defined(USBKEYBOARD))
# from configuration.h. Without this, the I²C keypad driver isn't linked
# and OPTION RESET PICOCALC leaves the keyboard non-functional.
set -euo pipefail

TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
    VGAWIFIRP2350 DVIWIFIRP2350
)

root="$(cd "$(dirname "$0")" && pwd)"
fail=0

# HAL purity gate — runs first so source-level regressions fail fast before the
# ~10-minute 12-target build loop burns cycles. Skip with SKIP_HAL_PURITY=1.
if [ "${SKIP_HAL_PURITY:-0}" != "1" ]; then
    if [ -x "$root/tools/check_hal_purity.sh" ]; then
        printf '=== HAL purity gate ===\n'
        if ! "$root/tools/check_hal_purity.sh"; then
            echo "HAL purity gate FAILED — fix the ifdef leak before building."
            exit 1
        fi
    fi
fi

for t in "${TARGETS[@]}"; do
    d="$root/build_all/$t"
    rm -rf "$d" && mkdir -p "$d"
    printf '=== %s ===\n' "$t"

    # Enable PICOCALC for every non-USB-keyboard PICOMITE / PICOMITEWEB
    # variant — matches the legacy formula. The I²C keypad driver gets
    # compiled in; runtime OPTION RESET PICOCALC then activates it.
    picocalc_flag="false"
    case "$t" in
        PICO|WEB|PICORP2350|WEBRP2350) picocalc_flag="true" ;;
    esac

    # Uppercase target → legacy -DCOMPILE; lowercase → direct -DPORT.
    if [[ "$t" =~ ^[A-Z0-9_]+$ ]]; then
        select_arg="-DCOMPILE=$t"
    else
        select_arg="-DPORT=$t"
    fi

    if ! (cd "$d" && cmake "$select_arg" -DPICOCALC="$picocalc_flag" "$root" > cmake.log 2>&1 && make -j8 > make.log 2>&1); then
        printf 'FAIL (%s)\n' "$t"
        tail -30 "$d/make.log" || true
        fail=1
        break
    fi
    printf 'OK\n'
done

if [ "$fail" = 0 ]; then
    echo "All ${#TARGETS[@]} device variants built clean."

    # RAM-baseline gate — runs after a successful build so arm-none-eabi-size
    # can read the freshly-built ELFs. Skip with SKIP_RAM_BASELINE=1.
    if [ "${SKIP_RAM_BASELINE:-0}" != "1" ] && [ -x "$root/tools/check_ram_baseline.sh" ]; then
        printf '\n=== RAM baseline gate ===\n'
        if ! "$root/tools/check_ram_baseline.sh"; then
            fail=1
        fi
    fi
fi
exit "$fail"
