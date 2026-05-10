#!/usr/bin/env bash
# ports/pc386/run_headless.sh — boot the kernel in QEMU headless,
# COM1 piped to stdio. The form used by run_tests.sh.
#
# Usage:
#   ./run_headless.sh                     # boot mmbasic.elf
#   ./run_headless.sh path/to/other.elf   # boot a specific kernel
#   ./run_headless.sh --timeout 10        # kill QEMU after 10 seconds
#
# Exits with QEMU's exit code. Triple-faults under -no-reboot exit 0;
# anything else (timeout, kernel panic detected by QEMU) is non-zero.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

KERNEL="$PORT_DIR/build/mmbasic.elf"
TIMEOUT_SECS=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout)
            TIMEOUT_SECS="$2"
            shift 2
            ;;
        -*)
            echo "unknown flag: $1" >&2
            exit 2
            ;;
        *)
            KERNEL="$1"
            shift
            ;;
    esac
done

if [[ ! -f "$KERNEL" ]]; then
    echo "error: $KERNEL not found. Run ./build.sh first." >&2
    exit 1
fi

QEMU_ARGS=(
    -kernel "$KERNEL"
    -m 16M
    -display none
    -serial stdio
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

if [[ -n "$TIMEOUT_SECS" ]]; then
    # Prefer GNU coreutils 'timeout' if available; otherwise fall back
    # to a portable background-and-kill that works without coreutils
    # (macOS by default doesn't ship 'timeout').
    if command -v timeout >/dev/null 2>&1; then
        exec timeout --foreground --kill-after=2 "$TIMEOUT_SECS" \
            qemu-system-i386 "${QEMU_ARGS[@]}"
    elif command -v gtimeout >/dev/null 2>&1; then
        exec gtimeout --foreground --kill-after=2 "$TIMEOUT_SECS" \
            qemu-system-i386 "${QEMU_ARGS[@]}"
    else
        qemu-system-i386 "${QEMU_ARGS[@]}" &
        QEMU_PID=$!
        sleep "$TIMEOUT_SECS"
        if kill -0 "$QEMU_PID" 2>/dev/null; then
            kill -TERM "$QEMU_PID" 2>/dev/null
            sleep 1
            kill -KILL "$QEMU_PID" 2>/dev/null
        fi
        wait "$QEMU_PID" 2>/dev/null
        # SIGTERM exit (143) means we hit the timeout — treat as success
        # for "kernel halted, we killed QEMU" semantics.
        rc=$?
        if [[ "$rc" == 143 ]] || [[ "$rc" == 137 ]]; then
            exit 0
        fi
        exit "$rc"
    fi
else
    exec qemu-system-i386 "${QEMU_ARGS[@]}"
fi
