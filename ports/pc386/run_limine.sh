#!/usr/bin/env bash
# ports/pc386/run_limine.sh — boot C: via Limine (no QEMU -kernel).
#
# This is the production hard-disk boot path: BIOS -> C: MBR -> Limine VBR
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
#   PC386_AUDIO=opl3 ./run_limine.sh
#   PC386_AUDIO=sb16 ./run_limine.sh
#   PC386_LIMINE_BOOT=a ./run_limine.sh
#                                # legacy helper image as IDE primary

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

A_IMG="$PORT_DIR/test_disks/a.img"
C_IMG="$PORT_DIR/test_disks/c.img"
BOOT_DISK="${PC386_LIMINE_BOOT:-c}"
MEMORY="${PC386_LIMINE_MEM:-32M}"
AUDIO_BACKEND="${PC386_AUDIO:-opl3}"
SB_BASE="${PC386_SB_BASE:-0x220}"
SB_IRQ="${PC386_SB_IRQ:-5}"
SB_DMA="${PC386_SB_DMA:-1}"
SB_DMA16="${PC386_SB_DMA16:-5}"

QEMU_ARGS=(
    -m "$MEMORY"
    -boot c                                # IDE primary master = bootable
    -vga std
    -no-reboot
    -no-shutdown
    -d guest_errors
)

if [[ "${1:-run}" == "headless" ]]; then
    QEMU_AUDIO_DRIVER="none"
elif qemu-system-i386 -audiodev help 2>&1 | grep -qx "coreaudio"; then
    QEMU_AUDIO_DRIVER="coreaudio"
else
    QEMU_AUDIO_DRIVER="none"
fi

case "$AUDIO_BACKEND" in
    auto)
        QEMU_ARGS+=(
            -machine pc,pcspk-audiodev=pcaudio
            -audiodev "$QEMU_AUDIO_DRIVER,id=pcaudio"
            -device "adlib,audiodev=pcaudio"
            -device "sb16,audiodev=pcaudio,iobase=$SB_BASE,irq=$SB_IRQ,dma=$SB_DMA,dma16=$SB_DMA16"
        )
        ;;
    opl3)
        QEMU_ARGS+=(
            -machine pc
            -audiodev "$QEMU_AUDIO_DRIVER,id=opl3"
            -device "adlib,audiodev=opl3"
        )
        ;;
    pcspk)
        QEMU_ARGS+=(
            -machine pc,pcspk-audiodev=pcspk
            -audiodev "$QEMU_AUDIO_DRIVER,id=pcspk"
        )
        ;;
    sb16)
        QEMU_ARGS+=(
            -machine pc
            -audiodev "$QEMU_AUDIO_DRIVER,id=sb16"
            -device "sb16,audiodev=sb16,iobase=$SB_BASE,irq=$SB_IRQ,dma=$SB_DMA,dma16=$SB_DMA16"
        )
        ;;
    *)
        echo "error: PC386_AUDIO must be one of: auto opl3 pcspk sb16" >&2
        exit 2
        ;;
esac

case "$BOOT_DISK" in
    a|A)
        if [[ ! -f "$A_IMG" ]]; then
            echo "error: $A_IMG not found. Run ./build_disks.sh first." >&2
            exit 1
        fi
        QEMU_ARGS+=(-drive file="$A_IMG",format=raw,if=ide,index=0)
        ;;
    c|C)
        if [[ ! -f "$C_IMG" ]]; then
            echo "error: $C_IMG not found. Run ./build_disks.sh first." >&2
            exit 1
        fi
        QEMU_ARGS+=(-drive file="$C_IMG",format=raw,if=ide,index=0)
        ;;
    *)
        echo "error: PC386_LIMINE_BOOT must be a or c" >&2
        exit 2
        ;;
esac

case "${1:-run}" in
    run)
        QEMU_ARGS+=(-display cocoa,zoom-to-fit=on -serial mon:stdio)
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
