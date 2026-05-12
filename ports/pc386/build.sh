#!/usr/bin/env bash
# ports/pc386/build.sh — cross-compile the bare-metal kernel.
#
# Usage:
#   ./build.sh             # build the default SB16 release kernel
#   PC386_AUDIO=pcspk ./build.sh
#   ./build.sh clean       # remove build/
#   ./build.sh iso         # also produce build/mmbasic.iso (Stage 7+)

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PORT_DIR"

# Sanity: cross compiler must be on PATH.
if ! command -v i686-elf-gcc >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: i686-elf-gcc not found on PATH.

Bootstrap the cross toolchain first:

    ../../toolchain/pc386/install_cross.sh

EOF
    exit 1
fi

case "${1:-build}" in
    build)
        make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
        ;;
    iso)
        make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" iso
        ;;
    clean)
        make clean
        ;;
    *)
        echo "usage: $0 [build|iso|clean]" >&2
        exit 2
        ;;
esac
