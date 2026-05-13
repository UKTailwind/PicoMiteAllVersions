#!/usr/bin/env bash
# ports/pc386/run_dosbox.sh - boot the PC386 floppy image in DOSBox-X.
#
# This is a secondary emulator sanity check. QEMU remains the normal
# development/test target because it is scriptable and has better capture hooks.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
F_IMG="$PORT_DIR/test_disks/pc386-floppy.img"
CONF="${TMPDIR:-/tmp}/pc386-dosbox-x.conf"
DOSBOX_X="${DOSBOX_X:-}"

if [[ ! -f "$F_IMG" ]]; then
    echo "error: $F_IMG not found. Run ./ports/pc386/build.sh && ./ports/pc386/build_disks.sh first." >&2
    exit 1
fi

if [[ -z "$DOSBOX_X" ]]; then
    for candidate in \
        dosbox-x \
        /Applications/DOSBox-X.app/Contents/MacOS/dosbox-x \
        /Applications/dosbox-x.app/Contents/MacOS/dosbox-x \
        "$HOME/Downloads/dosbox-x-macosx-arm64-2026.05.02/dosbox-x-sdl2/dosbox-x.app/Contents/MacOS/dosbox-x" \
        "$HOME/Downloads/dosbox-x-macosx-arm64-2026.05.02/dosbox-x/dosbox-x.app/Contents/MacOS/dosbox-x"
    do
        if command -v "$candidate" >/dev/null 2>&1; then
            DOSBOX_X="$(command -v "$candidate")"
            break
        fi
        if [[ -x "$candidate" ]]; then
            DOSBOX_X="$candidate"
            break
        fi
    done
fi

if [[ -z "$DOSBOX_X" || ! -x "$DOSBOX_X" ]]; then
    cat >&2 <<EOF
error: DOSBox-X was not found.

Install it with Homebrew, move the .app into /Applications, or run with:

    DOSBOX_X=/path/to/dosbox-x ./ports/pc386/run_dosbox.sh
EOF
    exit 1
fi

cat > "$CONF" <<EOF
[sdl]
fullscreen=false
output=opengl

[dosbox]
machine=svga_s3
memsize=16

[cpu]
core=dynamic
cputype=auto
cycles=max

[render]
aspect=false
scaler=none

[autoexec]
imgmount A "$F_IMG" -t floppy -size 512,18,2,80
boot A:
EOF

exec "$DOSBOX_X" -conf "$CONF"
