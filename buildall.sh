#!/usr/bin/env bash
# Build every device CMake COMPILE variant from clean dirs.
# Any failure stops the script — green across all targets is the gate.
set -euo pipefail

TARGETS=(
    PICO PICOUSB VGA VGAUSB WEB
    PICORP2350 PICOUSBRP2350 VGARP2350 VGAUSBRP2350
    HDMI HDMIUSB WEBRP2350
)

root="$(cd "$(dirname "$0")" && pwd)"
fail=0

for t in "${TARGETS[@]}"; do
    d="$root/build_all/$t"
    rm -rf "$d" && mkdir -p "$d"
    printf '=== %s ===\n' "$t"
    if ! (cd "$d" && cmake -DCOMPILE="$t" "$root" > cmake.log 2>&1 && make -j8 > make.log 2>&1); then
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
