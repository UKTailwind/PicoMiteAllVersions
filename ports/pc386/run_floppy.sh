#!/usr/bin/env bash
# ports/pc386/run_floppy.sh - boot the 1.44 MB PC386 floppy image in QEMU.
#
# This exercises the real BIOS floppy boot path and starts in VGA mode 13h:
#   BIOS -> A: boot sector -> PC386 stage2 -> /BOOT/MMBASIC.ELF
#
# Usage:
#   ./run_floppy.sh
#   ./run_floppy.sh headless
#   PC386_AUDIO=opl3 ./run_floppy.sh
#   PC386_AUDIO=pcspk ./run_floppy.sh

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

F_IMG="$PORT_DIR/test_disks/pc386-floppy.img"
C_IMG="$PORT_DIR/test_disks/c.img"
KERNEL_BOOT="$PORT_DIR/build/mmbasic-stripped.elf"
CONFIG_STAMP="$PORT_DIR/build/.pc386_audio_config"
FLOPPY_STAGE1="$PORT_DIR/build/bootloader/floppy_stage1.bin"
FLOPPY_STAGE2="$PORT_DIR/build/bootloader/floppy_stage2.bin"
MEMORY="${PC386_FLOPPY_MEM:-16M}"
AUDIO_BACKEND="${PC386_AUDIO:-opl3}"
SB_BASE="${PC386_SB_BASE:-0x220}"
SB_IRQ="${PC386_SB_IRQ:-5}"
SB_DMA="${PC386_SB_DMA:-1}"
SB_DMA16="${PC386_SB_DMA16:-5}"
RUN_MODE="${1:-run}"

if [[ ! -f "$F_IMG" ]]; then
    echo "error: $F_IMG not found. Run ./build.sh && ./build_disks.sh first." >&2
    exit 1
fi
if [[ ! -f "$C_IMG" ]]; then
    echo "error: $C_IMG not found. Run ./build_disks.sh first." >&2
    exit 1
fi
if [[ "${PC386_ALLOW_STALE_DISKS:-0}" != "1" ]]; then
    if [[ "$AUDIO_BACKEND" == "sb16" && -f "$CONFIG_STAMP" && "$(cat "$CONFIG_STAMP")" != "PC386_AUDIO=sb16" ]]; then
        cat >&2 <<EOF
error: the current kernel build is not an SB16 build.

PC386_AUDIO=sb16 on run.sh adds QEMU's Sound Blaster device, but the boot
floppy still needs an MMBASIC.ELF built with the SB16 HAL:

    ./ports/pc386/build.sh
    ./ports/pc386/build_disks.sh
    PC386_AUDIO=sb16 ./ports/pc386/run.sh
EOF
        exit 1
    fi
    for dep in "$KERNEL_BOOT" "$FLOPPY_STAGE1" "$FLOPPY_STAGE2"; do
        if [[ ! -f "$dep" ]]; then
            echo "error: $dep not found. Run ./build.sh first." >&2
            exit 1
        fi
        if [[ "$dep" -nt "$F_IMG" ]]; then
            cat >&2 <<EOF
error: $F_IMG is older than $(basename "$dep").

The floppy image contains an older MMBASIC.ELF, so QEMU would boot stale code.
Rebuild the image after stopping any QEMU process that has it open:

    ./ports/pc386/build.sh
    ./ports/pc386/build_disks.sh

Set PC386_ALLOW_STALE_DISKS=1 only when you intentionally want the old image.
EOF
            exit 1
        fi
    done
fi

QEMU_ARGS=(
    -m "$MEMORY"
    -boot a
    -vga std
    -drive file="$F_IMG",format=raw,if=floppy,index=0
    -drive file="$C_IMG",format=raw,if=ide,index=0
    -no-reboot
    -no-shutdown
    -d guest_errors
)

if [[ "$RUN_MODE" == "headless" ]]; then
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

case "$RUN_MODE" in
    run)
        QEMU_ARGS+=(-display cocoa,zoom-to-fit=on -serial mon:stdio)
        ;;
    headless)
        QEMU_ARGS+=(-display none -serial stdio)
        ;;
    unscaled)
        QEMU_ARGS+=(-serial mon:stdio)
        ;;
    fullscreen)
        QEMU_ARGS+=(-display cocoa,zoom-to-fit=on -serial mon:stdio -full-screen)
        ;;
    debug)
        QEMU_ARGS+=(-display cocoa,zoom-to-fit=on -serial mon:stdio -s -S)
        echo "QEMU paused at boot. In another terminal:"
        echo "    i686-elf-gdb $PORT_DIR/build/mmbasic.elf -ex 'target remote :1234'"
        ;;
    *)
        echo "usage: $0 [run|headless|unscaled|fullscreen|debug]" >&2
        exit 2
        ;;
esac

exec qemu-system-i386 "${QEMU_ARGS[@]}"
