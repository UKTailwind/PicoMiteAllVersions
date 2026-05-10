#!/usr/bin/env bash
# ports/pc386/run_limine.sh — boot via Limine (no QEMU -kernel).
#
# This is the production boot path: BIOS -> A: MBR -> Limine VBR
# -> /boot/limine-bios.sys -> /boot/mmbasic.elf via multiboot1.
# Same kernel binary the dev-loop run.sh boots through QEMU's
# direct -kernel mechanism; the only difference is who loads it.
#
# Run ./build.sh && ./build_disks.sh first — the disk image must
# contain the Limine bootsector + stage 2 + kernel ELF.
#
# Usage:
#   ./run_limine.sh              # interactive boot, VGA window + serial
#   ./run_limine.sh headless     # headless, COM1 -> stdio (test harness)

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

A_IMG="$PORT_DIR/test_disks/a.img"
C_IMG="$PORT_DIR/test_disks/c.img"

if [[ ! -f "$A_IMG" ]]; then
    echo "error: $A_IMG not found. Run ./build_disks.sh first." >&2
    exit 1
fi

QEMU_ARGS=(
    -m 16M
    -drive file="$A_IMG",format=raw,if=ide,index=0
    -boot c                                # IDE primary master = bootable
    -no-reboot
    -no-shutdown
    -d guest_errors
)
if [[ -f "$C_IMG" ]]; then
    QEMU_ARGS+=(-drive file="$C_IMG",format=raw,if=ide,index=1)
fi

case "${1:-run}" in
    run)
        QEMU_ARGS+=(-serial mon:stdio)
        ;;
    headless)
        QEMU_ARGS+=(-display none -serial stdio)
        ;;
    debug)
        QEMU_ARGS+=(-serial mon:stdio -s -S)
        echo "QEMU paused at boot. In another terminal:"
        echo "    i686-elf-gdb $PORT_DIR/build/mmbasic.elf -ex 'target remote :1234'"
        ;;
    *)
        echo "usage: $0 [run|headless|debug]" >&2
        exit 2
        ;;
esac

exec qemu-system-i386 "${QEMU_ARGS[@]}"
