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

if [[ -n "$TIMEOUT_SECS" ]]; then
    # Use the cross-platform timeout if available; fall back to gtimeout
    # (homebrew coreutils on macOS).
    if command -v timeout >/dev/null 2>&1; then
        exec timeout --foreground --kill-after=2 "$TIMEOUT_SECS" \
            qemu-system-i386 "${QEMU_ARGS[@]}"
    elif command -v gtimeout >/dev/null 2>&1; then
        exec gtimeout --foreground --kill-after=2 "$TIMEOUT_SECS" \
            qemu-system-i386 "${QEMU_ARGS[@]}"
    else
        echo "error: --timeout requires GNU coreutils 'timeout' (brew install coreutils)" >&2
        exit 1
    fi
else
    exec qemu-system-i386 "${QEMU_ARGS[@]}"
fi
