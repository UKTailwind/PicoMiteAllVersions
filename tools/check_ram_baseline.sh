#!/usr/bin/env bash
#
# tools/check_ram_baseline.sh
#
# Compares per-target RAM usage (text + data + bss from arm-none-eabi-size)
# against committed baselines in tools/ram_baseline_<TARGET>.txt.
#
# Usage:
#   tools/check_ram_baseline.sh                # check all targets in build_all/
#   tools/check_ram_baseline.sh --capture      # capture current sizes as new baselines
#   tools/check_ram_baseline.sh --diff         # show delta from baseline (no fail)
#
# Each baseline file contains one line: "text data bss" (decimal).
# The gate: .bss must not grow more than 64 bytes without an explicit
# baseline update (--capture). This catches accidental BSS bloat from
# new globals while allowing minor fluctuations from compiler/SDK changes.
#
# Requires: arm-none-eabi-size on PATH, build_all/ populated by buildall.sh.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASELINE_DIR="$REPO_ROOT/tools"
BUILD_ALL="$REPO_ROOT/build_all"
BSS_THRESHOLD=64  # bytes of BSS growth before failing

TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
    VGAWIFIRP2350 DVIWIFIRP2350
)

mode="check"
if [[ "${1:-}" == "--capture" ]]; then
    mode="capture"
elif [[ "${1:-}" == "--diff" ]]; then
    mode="diff"
fi

fail=0

for t in "${TARGETS[@]}"; do
    elf="$BUILD_ALL/$t/PicoMite.elf"
    baseline="$BASELINE_DIR/ram_baseline_${t}.txt"

    if [[ ! -f "$elf" ]]; then
        if [[ "$mode" == "check" ]]; then
            printf '%-20s SKIP (no ELF — run buildall.sh first)\n' "$t"
        fi
        continue
    fi

    # Parse arm-none-eabi-size output: "   text	   data	    bss	    dec	    hex	filename"
    read -r cur_text cur_data cur_bss _ _ _ < <(arm-none-eabi-size "$elf" | tail -1)

    if [[ "$mode" == "capture" ]]; then
        echo "$cur_text $cur_data $cur_bss" > "$baseline"
        printf '%-20s captured: text=%s data=%s bss=%s\n' "$t" "$cur_text" "$cur_data" "$cur_bss"
        continue
    fi

    if [[ ! -f "$baseline" ]]; then
        printf '%-20s NO BASELINE (run --capture to create)\n' "$t"
        if [[ "$mode" == "check" ]]; then
            fail=1
        fi
        continue
    fi

    read -r base_text base_data base_bss < "$baseline"
    delta_text=$(( cur_text - base_text ))
    delta_data=$(( cur_data - base_data ))
    delta_bss=$(( cur_bss - base_bss ))

    if [[ "$mode" == "diff" ]]; then
        printf '%-20s text=%+d  data=%+d  bss=%+d\n' "$t" "$delta_text" "$delta_data" "$delta_bss"
        continue
    fi

    # check mode
    if (( delta_bss > BSS_THRESHOLD )); then
        printf '%-20s FAIL: bss grew by %+d bytes (threshold %d)\n' "$t" "$delta_bss" "$BSS_THRESHOLD"
        printf '    baseline: text=%s data=%s bss=%s\n' "$base_text" "$base_data" "$base_bss"
        printf '    current:  text=%s data=%s bss=%s\n' "$cur_text" "$cur_data" "$cur_bss"
        fail=1
    else
        printf '%-20s OK (bss %+d)\n' "$t" "$delta_bss"
    fi
done

if [[ "$mode" == "check" ]]; then
    echo
    if [[ $fail -ne 0 ]]; then
        echo "RAM baseline check FAILED. Run 'tools/check_ram_baseline.sh --capture' to update."
        exit 1
    fi
    echo "RAM baseline check passed."
fi
exit 0
