#!/usr/bin/env bash
# ports/pc386/run.sh — boot PC386 in QEMU with display.
#
# Usage:
#   ./run.sh               # BIOS/FDC boot, VGA window + serial mirror
#   PC386_AUDIO=opl3 ./run.sh
#   PC386_AUDIO=pcspk ./run.sh
#   PC386_AUDIO=sb16 PC386_SB_BASE=0x240 ./run.sh
#   ./run.sh unscaled      # raw guest-pixel window for pixel-level debugging
#   ./run.sh fullscreen    # start QEMU full-screen
#   ./run.sh debug         # halt at boot, expose GDB stub on :1234
#   ./run.sh kernel        # direct QEMU -kernel path, serial/audio isolation
#
# For headless test runs see run_headless.sh.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL="$PORT_DIR/build/mmbasic.elf"
AUDIO_BACKEND="${PC386_AUDIO:-opl3}"
SB_BASE="${PC386_SB_BASE:-0x220}"
SB_IRQ="${PC386_SB_IRQ:-5}"
SB_DMA="${PC386_SB_DMA:-1}"
SB_DMA16="${PC386_SB_DMA16:-5}"

if [[ ! -f "$KERNEL" ]]; then
    echo "error: $KERNEL not found. Run ./build.sh first." >&2
    exit 1
fi

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: qemu-system-i386 not found.

Install QEMU:
    brew install qemu                     # macOS
    sudo apt install qemu-system-x86      # Debian/Ubuntu

EOF
    exit 1
fi

RUN_MODE="${1:-run}"
case "$RUN_MODE" in
    run|unscaled|fullscreen|debug|headless)
        exec "$PORT_DIR/run_floppy.sh" "$RUN_MODE"
        ;;
    kernel)
        RUN_MODE="run"
        ;;
    kernel-unscaled)
        RUN_MODE="unscaled"
        ;;
    kernel-fullscreen)
        RUN_MODE="fullscreen"
        ;;
    kernel-debug)
        RUN_MODE="debug"
        ;;
    kernel-headless)
        RUN_MODE="headless"
        ;;
esac

QEMU_ARGS=(
    -kernel "$KERNEL"
    -m 16M                                # 16 MB — realistic 486-era RAM
    -vga std                              # standard VGA adapter; VBE is optional for higher modes
    -display cocoa,zoom-to-fit=on         # Usable default; use "unscaled" for raw 320x200 pixels
    -serial mon:stdio                     # COM1 mirrored to terminal, with monitor escape (Ctrl-A + c)
    -no-reboot
    -no-shutdown
    -d guest_errors
)

if qemu-system-i386 -audiodev help 2>&1 | grep -qx "coreaudio"; then
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

# Attach test disks if they exist. Build them via ./build_disks.sh.
F_IMG="$PORT_DIR/test_disks/pc386-floppy.img"
C_IMG="$PORT_DIR/test_disks/c.img"
if [[ -f "$F_IMG" ]]; then
    QEMU_ARGS+=(-drive file="$F_IMG",format=raw,if=floppy,index=0)
fi
if [[ -f "$C_IMG" ]]; then
    QEMU_ARGS+=(-drive file="$C_IMG",format=raw,if=ide,index=0)
fi

case "$RUN_MODE" in
    run)
        ;;
    headless)
        for i in "${!QEMU_ARGS[@]}"; do
            if [[ "${QEMU_ARGS[$i]}" == "-display" && "${QEMU_ARGS[$((i + 1))]:-}" == "cocoa,zoom-to-fit=on" ]]; then
                unset 'QEMU_ARGS[i]' 'QEMU_ARGS[i + 1]'
                QEMU_ARGS=("${QEMU_ARGS[@]}")
                break
            fi
        done
        for i in "${!QEMU_ARGS[@]}"; do
            if [[ "${QEMU_ARGS[$i]}" == "-serial" && "${QEMU_ARGS[$((i + 1))]:-}" == "mon:stdio" ]]; then
                QEMU_ARGS[$((i + 1))]="stdio"
                break
            fi
        done
        QEMU_ARGS+=(-display none)
        ;;
    unscaled)
        for i in "${!QEMU_ARGS[@]}"; do
            if [[ "${QEMU_ARGS[$i]}" == "-display" && "${QEMU_ARGS[$((i + 1))]:-}" == "cocoa,zoom-to-fit=on" ]]; then
                unset 'QEMU_ARGS[i]' 'QEMU_ARGS[i + 1]'
                QEMU_ARGS=("${QEMU_ARGS[@]}")
                break
            fi
        done
        ;;
    fullscreen)
        QEMU_ARGS+=(-full-screen)
        ;;
    debug)
        QEMU_ARGS+=(-s -S)                # GDB stub on :1234, halt at boot
        echo "QEMU paused at boot. In another terminal:"
        echo "    i686-elf-gdb $KERNEL -ex 'target remote :1234'"
        ;;
    *)
        echo "usage: $0 [run|unscaled|fullscreen|debug|headless|kernel|kernel-headless|kernel-unscaled|kernel-fullscreen|kernel-debug]" >&2
        exit 2
        ;;
esac

exec qemu-system-i386 "${QEMU_ARGS[@]}"
