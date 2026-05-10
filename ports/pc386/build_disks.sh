#!/usr/bin/env bash
# ports/pc386/build_disks.sh — produce test_disks/{a,c}.img.
#
# Stage 2a (current): raw images with known signature bytes in sector
# 0 — enough to exercise the ATA-PIO read path. mtools is not yet
# required at this sub-stage.
#
# Stage 2b (next): rebuilds these as real FAT12 / FAT16 images via
# mtools, with kernel + limine.cfg copied into A:/boot/.
#
# Idempotent: re-running rebuilds the images from scratch.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PORT_DIR"

DISK_DIR="$PORT_DIR/test_disks"
A_IMG="$DISK_DIR/a.img"
C_IMG="$DISK_DIR/c.img"

mkdir -p "$DISK_DIR"

# A: (1.44 MB, "floppy"-sized but attached as IDE).
printf 'A:PicoMite-pc386\xaa\x55' > "$A_IMG"
truncate -s 1474560 "$A_IMG"

# C: (32 MB).
printf 'C:HardDrive-PicoMite\xde\xad\xbe\xef' > "$C_IMG"
truncate -s 33554432 "$C_IMG"

ls -la "$DISK_DIR"
