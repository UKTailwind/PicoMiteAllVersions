#!/usr/bin/env bash
# ports/pc386/build_disks.sh — produce test_disks/{a,c}.img.
#
# A: = 8 MB FAT16 partitioned IDE image, intended as the boot disk.
#      Limine writes the MBR + VBR; partition 1 starts at sector 63 and
#      holds /boot/{mmbasic.elf,limine.conf,limine-bios.sys} plus the
#      sample programs. (Stage 9 will reinstate a literal 1.44 MB FAT12
#      floppy via a real 765 FDC driver if anyone wants that aesthetic
#      on real hardware; QEMU and modern boxes never need it.)
# C: = 32 MB FAT16 image, the "hard drive" target for SYS C: install
#      (Stage 7).
#
# Both populated from the shared MMBasic demo set vendored under
# ports/esp32_s3_metro/main/demos/ — same hello/fizzbuzz/mand/sieve
# corpus as the ESP32 port.
#
# Idempotent: re-running rebuilds the images from scratch.

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PORT_DIR/../.." && pwd)"
cd "$PORT_DIR"

for tool in mformat mpartition limine; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        cat >&2 <<EOF
error: $tool not found.

Install:
    brew install mtools limine    # macOS
    sudo apt install mtools       # Debian/Ubuntu (Limine: build from source)
EOF
        exit 1
    fi
done

DISK_DIR="$PORT_DIR/test_disks"
A_IMG="$DISK_DIR/a.img"
C_IMG="$DISK_DIR/c.img"
DEMOS_DIR="$REPO_ROOT/ports/esp32_s3_metro/main/demos"
KERNEL="$PORT_DIR/build/mmbasic.elf"

if [[ ! -f "$KERNEL" ]]; then
    echo "error: $KERNEL not found. Run ./build.sh first." >&2
    exit 1
fi

# Locate Limine's bootsector data file. Homebrew installs it under
# /opt/homebrew/share/limine/ on Apple Silicon and /usr/local/share/limine
# on Intel. Distros typically use /usr/share/limine/.
LIMINE_BIOS_SYS=""
for cand in \
    /opt/homebrew/share/limine/limine-bios.sys \
    /usr/local/share/limine/limine-bios.sys \
    /usr/share/limine/limine-bios.sys
do
    if [[ -f "$cand" ]]; then
        LIMINE_BIOS_SYS="$cand"
        break
    fi
done
if [[ -z "$LIMINE_BIOS_SYS" ]]; then
    echo "error: limine-bios.sys not found under any known prefix" >&2
    exit 1
fi

rm -rf "$DISK_DIR"
mkdir -p "$DISK_DIR"

# mtools needs a drive-letter configuration in MTOOLSRC for mpartition
# to work; per-image config maps Z: -> a.img (partition 1) and Y: ->
# c.img (whole image). We never write outside this script's scope so
# the temporary file lives next to the disk images.
MTOOLSRC_FILE="$DISK_DIR/.mtoolsrc"
cat > "$MTOOLSRC_FILE" <<EOF
drive z: file="$A_IMG" partition=1 mformat_only
drive y: file="$C_IMG" mformat_only
EOF
export MTOOLSRC="$MTOOLSRC_FILE"

# --- A: 8 MB FAT16 partitioned IDE image. -----------------------------------
truncate -s 8M "$A_IMG"
mpartition -I z:
mpartition -c -b 63 z:
mformat -v "PCM_BOOT" z:
mmd     z:/BOOT
mcopy "$KERNEL"               z:/BOOT/MMBASIC.ELF
mcopy "$PORT_DIR/limine.conf" z:/BOOT/LIMINE.CONF
mcopy "$LIMINE_BIOS_SYS"      z:/BOOT/LIMINE-BIOS.SYS

# Hello + FizzBuzz ride on the boot disk — they're tiny and form the
# first thing the user runs after the prompt comes up.
mcopy "$DEMOS_DIR/hello.bas"     z:/HELLO.BAS
mcopy "$DEMOS_DIR/fizzbuzz.bas"  z:/FIZZBUZZ.BAS

cat > "$DISK_DIR/README.TXT" <<'EOF'
PicoMite PC386 boot disk (A:).

Boot:               Limine -> mmbasic.elf via multiboot1
Sample programs:    RUN "HELLO.BAS"
                    RUN "FIZZBUZZ.BAS"

Larger demos and persistent storage live on drive C:.
EOF
mcopy "$DISK_DIR/README.TXT" z:/README.TXT

# Install Limine's MBR + VBR. Must come AFTER the FAT is populated so
# Limine can index its stage-2 file by cluster offset.
limine bios-install "$A_IMG"

# --- C: 32 MB FAT16 (hard drive target). ------------------------------------
# Unpartitioned — mformat on a "superfloppy" (whole-disk FAT) is fine
# here because Limine never touches C:; the kernel's ATA-PIO driver
# handles it directly. Stage 7's SYS C: install will need to add a
# partition + Limine here too if/when we want C: to be self-bootable.
truncate -s 32M "$C_IMG"
mformat -h 16 -s 63 -t 65 -v "PCM_DATA" y:
mmd   y:/PROGRAMS
mcopy "$DEMOS_DIR/mand.bas"   y:/PROGRAMS/MAND.BAS
mcopy "$DEMOS_DIR/sieve.bas"  y:/PROGRAMS/SIEVE.BAS
mcopy "$DISK_DIR/README.TXT"  y:/README.TXT

ls -la "$DISK_DIR"
echo
echo "A: contents:"
mdir -/ z:
echo "C: contents:"
mdir -/ y:
