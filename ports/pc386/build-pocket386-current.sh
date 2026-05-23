#!/usr/bin/env bash
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PORT_DIR/../.." && pwd)"
OUT_IMG="/Users/joshv/Documents/pocket386/mmbasic_c.img"

cd "$REPO_ROOT"

PC386_AUDIO=opl3 \
PC386_NO_FPU=1 \
PC386_NO_FDC=1 \
PC386_KBD_SCANCODE_SET=1 \
PC386_VIDEO=auto \
./ports/pc386/build.sh

PC386_REBUILD_C=1 ./ports/pc386/build_disks.sh

cp ports/pc386/test_disks/c-direct.img "$OUT_IMG"

ls -lh "$OUT_IMG"
shasum -a 256 "$OUT_IMG"
