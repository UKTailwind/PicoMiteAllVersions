#!/usr/bin/env bash
# ports/pc386/build_disks.sh — produce test_disks/{a,c}.img.
#
# A: = 1.44 MB FAT12 image, intended as the boot floppy (Stage 2d adds
#      Limine to its MBR).
# C: = 32 MB FAT16 image, the "hard drive" target for SYS C: install
#      (Stage 7).
#
# Both are populated from the shared MMBasic demo set vendored under
# ports/esp32_s3_metro/main/demos/ — the same hello / fizzbuzz / mand
# / sieve programs the ESP32-S3 port ships, so the file-system test
# corpus matches across ports. mtools is used to format and copy
# without root or loopback devices; brew install mtools provides it
# on macOS.
#
# Idempotent: re-running rebuilds the images from scratch.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PORT_DIR/../.." && pwd)"
cd "$PORT_DIR"

if ! command -v mformat >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: mformat (mtools) not found.

Install:
    brew install mtools           # macOS
    sudo apt install mtools       # Debian/Ubuntu
EOF
    exit 1
fi

DISK_DIR="$PORT_DIR/test_disks"
A_IMG="$DISK_DIR/a.img"
C_IMG="$DISK_DIR/c.img"
DEMOS_DIR="$REPO_ROOT/ports/esp32_s3_metro/main/demos"

rm -rf "$DISK_DIR"
mkdir -p "$DISK_DIR"

if [[ ! -d "$DEMOS_DIR" ]]; then
    echo "error: demos dir $DEMOS_DIR not found" >&2
    exit 1
fi

# --- A: 1.44 MB FAT12 (boot floppy). ----------------------------------------
# mtools picks FAT12 automatically for a -f 1440 floppy image; no flags
# needed beyond that.
truncate -s 1474560 "$A_IMG"
mformat -i "$A_IMG" -f 1440 -v "PCM_BOOT" ::

# Hello + FizzBuzz ride on the boot floppy — they're tiny and form
# the first thing the user runs after `RUN "HELLO.BAS"`.
mcopy -i "$A_IMG" "$DEMOS_DIR/hello.bas"     ::/HELLO.BAS
mcopy -i "$A_IMG" "$DEMOS_DIR/fizzbuzz.bas"  ::/FIZZBUZZ.BAS

# A small README so the listing isn't bare.
cat > "$DISK_DIR/README.TXT" <<'EOF'
PicoMite PC386 boot floppy (A:).

Sample programs:
  RUN "HELLO.BAS"
  RUN "FIZZBUZZ.BAS"

Larger programs are on drive C:.
EOF
mcopy -i "$A_IMG" "$DISK_DIR/README.TXT" ::/README.TXT

# --- C: 32 MB FAT16 (hard drive target). ------------------------------------
# 32 MB / 512 = 65536 sectors. With CHS 16 heads x 63 sectors x 65 cyl
# = 65520 sectors, mtools rounds to fit. Drop the -F flag (which would
# force FAT32) — at 32 MB mformat picks FAT16 by default.
truncate -s 33554432 "$C_IMG"
mformat -i "$C_IMG" -h 16 -s 63 -t 65 -v "PCM_DATA" ::

mmd   -i "$C_IMG" ::/PROGRAMS
mcopy -i "$C_IMG" "$DEMOS_DIR/mand.bas"   ::/PROGRAMS/MAND.BAS
mcopy -i "$C_IMG" "$DEMOS_DIR/sieve.bas"  ::/PROGRAMS/SIEVE.BAS
mcopy -i "$C_IMG" "$DISK_DIR/README.TXT"  ::/README.TXT

ls -la "$DISK_DIR"
echo
echo "A: contents:"
mdir -i "$A_IMG" -/ ::
echo "C: contents:"
mdir -i "$C_IMG" -/ ::
