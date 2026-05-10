#!/usr/bin/env bash
# ports/pc386/run.sh — boot the kernel in QEMU with display.
#
# Usage:
#   ./run.sh               # interactive boot, VGA window + serial mirrored to terminal
#   ./run.sh debug         # halt at boot, expose GDB stub on :1234
#
# For headless test runs see run_headless.sh.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL="$PORT_DIR/build/mmbasic.elf"

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

QEMU_ARGS=(
    -kernel "$KERNEL"
    -m 16M                                # 16 MB — realistic 486-era RAM
    -serial mon:stdio                     # COM1 mirrored to terminal, with monitor escape (Ctrl-A + c)
    -no-reboot
    -no-shutdown
    -d guest_errors
)

# Attach test disks if they exist. Build them via ./build_disks.sh.
A_IMG="$PORT_DIR/test_disks/a.img"
C_IMG="$PORT_DIR/test_disks/c.img"
if [[ -f "$A_IMG" ]]; then
    QEMU_ARGS+=(-drive file="$A_IMG",format=raw,if=ide,index=0)
fi
if [[ -f "$C_IMG" ]]; then
    QEMU_ARGS+=(-drive file="$C_IMG",format=raw,if=ide,index=1)
fi

case "${1:-run}" in
    run)
        ;;
    debug)
        QEMU_ARGS+=(-s -S)                # GDB stub on :1234, halt at boot
        echo "QEMU paused at boot. In another terminal:"
        echo "    i686-elf-gdb $KERNEL -ex 'target remote :1234'"
        ;;
    *)
        echo "usage: $0 [run|debug]" >&2
        exit 2
        ;;
esac

exec qemu-system-i386 "${QEMU_ARGS[@]}"
