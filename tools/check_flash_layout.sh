#!/usr/bin/env bash
#
# tools/check_flash_layout.sh
#
# Verifies each firmware ELF's flash-resident LOAD segments end before the
# port's option/program flash region. This catches images that would erase
# part of themselves during first-boot option reset.
#
# Usage:
#   tools/check_flash_layout.sh
#       Check all targets in build_all/.
#   tools/check_flash_layout.sh --target picocalc_rp2040 --build-dir build
#       Check one target from a standalone build directory.
#
# Requires: arm-none-eabi-readelf on PATH.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ALL="$REPO_ROOT/build_all"
XIP_BASE=$((0x10000000))
FLASH_LAYOUT_MARGIN=${FLASH_LAYOUT_MARGIN:-32768}

TARGET_ROWS=(
    "PICO:pico:HAL_PORT_FLASH_TARGET_OFFSET"
    "PICOUSB:pico:HAL_PORT_FLASH_TARGET_OFFSET_USB"
    "VGA:vga:HAL_PORT_FLASH_TARGET_OFFSET"
    "VGAUSB:vga:HAL_PORT_FLASH_TARGET_OFFSET_USB"
    "WEB:web:HAL_PORT_FLASH_TARGET_OFFSET"
    "PICORP2350:pico_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "PICOUSBRP2350:pico_rp2350:HAL_PORT_FLASH_TARGET_OFFSET_USB"
    "VGARP2350:vga_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "VGAUSBRP2350:vga_rp2350:HAL_PORT_FLASH_TARGET_OFFSET_USB"
    "HDMI:hdmi_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "HDMIUSB:hdmi_rp2350:HAL_PORT_FLASH_TARGET_OFFSET_USB"
    "WEBRP2350:web_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "VGAWIFIRP2350:vga_wifi_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "DVIWIFIRP2350:dvi_wifi_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "picocalc_rp2040:picocalc_rp2040:HAL_PORT_FLASH_TARGET_OFFSET"
    "picocalc_wifi_rp2040:picocalc_wifi_rp2040:HAL_PORT_FLASH_TARGET_OFFSET"
    "picocalc_rp2350:picocalc_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
    "picocalc_wifi_rp2350:picocalc_wifi_rp2350:HAL_PORT_FLASH_TARGET_OFFSET"
)

usage() {
    sed -n '3,18p' "$0" >&2
}

target_filter=""
build_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            target_filter="${2:?missing value for --target}"
            shift 2
            ;;
        --build-dir)
            build_dir="${2:?missing value for --build-dir}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'error: unknown argument: %s\n' "$1" >&2
            usage
            exit 2
            ;;
    esac
done

hex_to_dec() {
    local h="${1#0x}"
    printf '%d' "$((16#$h))"
}

find_target_row() {
    local target="$1"
    local row row_target
    for row in "${TARGET_ROWS[@]}"; do
        IFS=: read -r row_target _ _ <<< "$row"
        if [[ "$row_target" == "$target" ]]; then
            printf '%s\n' "$row"
            return 0
        fi
    done
    return 1
}

read_macro_expr() {
    local port="$1"
    local macro="$2"
    local config="$REPO_ROOT/ports/$port/port_config.h"
    sed -nE "s|^[[:space:]]*#define[[:space:]]+$macro[[:space:]]+(.+)$|\\1|p" "$config" \
        | head -1 \
        | sed -E 's|//.*$||; s|/\*.*\*/||; s|[[:space:]]+$||'
}

flash_end_from_elf() {
    local elf="$1"
    local max_end=0
    local type off virt phys filesz memsz rest phys_dec filesz_dec end

    while read -r type off virt phys filesz memsz rest; do
        [[ "$type" == "LOAD" ]] || continue
        phys_dec="$(hex_to_dec "$phys")"
        filesz_dec="$(hex_to_dec "$filesz")"
        if (( filesz_dec > 0 && phys_dec >= XIP_BASE && phys_dec < 0x20000000 )); then
            end=$(( phys_dec + filesz_dec - XIP_BASE ))
            (( end > max_end )) && max_end="$end"
        fi
    done < <(arm-none-eabi-readelf -lW "$elf")

    if (( max_end == 0 )); then
        printf 'error: no flash-resident LOAD segments found in %s\n' "$elf" >&2
        return 1
    fi
    printf '%d\n' "$max_end"
}

check_one() {
    local target="$1"
    local dir="$2"
    local row port macro expr offset flash_end margin

    row="$(find_target_row "$target")" || {
        printf '%-22s FAIL: no target-to-port mapping\n' "$target"
        return 1
    }
    IFS=: read -r _ port macro <<< "$row"

    local elf="$dir/PicoMite.elf"
    if [[ ! -f "$elf" ]]; then
        printf '%-22s SKIP (no ELF at %s)\n' "$target" "$elf"
        return 0
    fi

    expr="$(read_macro_expr "$port" "$macro")"
    if [[ -z "$expr" ]]; then
        printf '%-22s FAIL: %s missing in ports/%s/port_config.h\n' "$target" "$macro" "$port"
        return 1
    fi

    offset=$((expr))
    flash_end="$(flash_end_from_elf "$elf")"
    margin=$(( offset - flash_end ))

    if (( margin < FLASH_LAYOUT_MARGIN )); then
        printf '%-22s FAIL: flash_end=%d offset=%d margin=%d min=%d (%s/%s)\n' \
            "$target" "$flash_end" "$offset" "$margin" "$FLASH_LAYOUT_MARGIN" "$port" "$macro"
        return 1
    fi

    printf '%-22s OK flash_end=%d offset=%d margin=%d\n' \
        "$target" "$flash_end" "$offset" "$margin"
    return 0
}

if ! command -v arm-none-eabi-readelf >/dev/null 2>&1; then
    echo "error: arm-none-eabi-readelf not found on PATH" >&2
    exit 127
fi

fail=0

if [[ -n "$target_filter" ]]; then
    if [[ -z "$build_dir" ]]; then
        build_dir="$BUILD_ALL/$target_filter"
    elif [[ "$build_dir" != /* ]]; then
        build_dir="$REPO_ROOT/$build_dir"
    fi
    check_one "$target_filter" "$build_dir" || fail=1
else
    for row in "${TARGET_ROWS[@]}"; do
        IFS=: read -r target _ _ <<< "$row"
        check_one "$target" "$BUILD_ALL/$target" || fail=1
    done
fi

if (( fail )); then
    echo
    echo "Flash layout check FAILED."
    exit 1
fi

echo
echo "Flash layout check passed."
