#!/usr/bin/env bash
# Build every device CMake COMPILE variant from clean dirs.
# Any failure stops the script — green across all targets is the gate.
#
# PICOCALC is only enabled for the default PICO target (the PicoCalc board).
# All other targets build as standard PicoMite variants.
set -euo pipefail

TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
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

    # PICO target is the PicoCalc; all others are standard PicoMite.
    picocalc_flag="false"
    if [ "$t" = "PICO" ]; then
        picocalc_flag="true"
    fi

    if ! (cd "$d" && cmake -DCOMPILE="$t" -DPICOCALC="$picocalc_flag" "$root" > cmake.log 2>&1 && make -j8 > make.log 2>&1); then
        printf 'FAIL (%s)\n' "$t"
        tail -30 "$d/make.log" || true
        fail=1
        break
    fi
    printf 'OK\n'
done

if [ "$fail" = 0 ]; then
    echo "All ${#TARGETS[@]} device variants built clean."
fi
exit "$fail"
