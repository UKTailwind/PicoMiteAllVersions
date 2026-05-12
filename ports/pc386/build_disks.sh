#!/usr/bin/env bash
# ports/pc386/build_disks.sh — produce test_disks/{a,c,pc386-floppy}.img.
#
# a.img = 8 MB FAT16 partitioned IDE image, retained as a Limine
#      boot/helper image for compatibility with earlier pc386 scripts.
#      Limine writes the MBR + VBR; partition 1 starts at sector 63 and
#      holds /boot/{mmbasic.elf,limine.conf,limine-bios.sys} plus the
#      sample programs. It is not attached by default; C: is the normal
#      primary hard disk now.
# C: = 32 MB FAT16 partitioned IDE image, the "hard drive" target for
#      SYS C:. It is also Limine-installed so it can boot by itself.
# pc386-floppy.img = 1.44 MB FAT12 superfloppy. Runtime FDC support
#      mounts it as A:. B: is reserved for a second floppy if attached.
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
F_IMG="$DISK_DIR/pc386-floppy.img"
DEMOS_DIR="$REPO_ROOT/ports/esp32_s3_metro/main/demos"
KERNEL="$PORT_DIR/build/mmbasic.elf"
KERNEL_BOOT="$PORT_DIR/build/mmbasic-stripped.elf"
FLOPPY_STAGE1="$PORT_DIR/build/bootloader/floppy_stage1.bin"
FLOPPY_STAGE2="$PORT_DIR/build/bootloader/floppy_stage2.bin"

if [[ ! -f "$KERNEL" ]]; then
    echo "error: $KERNEL not found. Run ./build.sh first." >&2
    exit 1
fi
if [[ ! -f "$KERNEL_BOOT" ]]; then
    echo "error: $KERNEL_BOOT not found. Run 'make -C ports/pc386' first." >&2
    exit 1
fi
if [[ ! -f "$FLOPPY_STAGE1" || ! -f "$FLOPPY_STAGE2" ]]; then
    echo "error: floppy bootloader not found. Run 'make -C ports/pc386' first." >&2
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
# to work; per-image config maps Z: -> a.img and Y: -> c.img using
# partition 1. X: maps the real floppy image as a whole-disk FAT12
# superfloppy. We never write outside this script's scope so
# the temporary file lives next to the disk images.
MTOOLSRC_FILE="$DISK_DIR/.mtoolsrc"
cat > "$MTOOLSRC_FILE" <<EOF
drive x: file="$F_IMG" mformat_only
drive z: file="$A_IMG" partition=1 mformat_only
drive y: file="$C_IMG" partition=1 mformat_only
EOF
export MTOOLSRC="$MTOOLSRC_FILE"

# --- Legacy 8 MB FAT16 partitioned IDE helper image. ------------------------
truncate -s 8M "$A_IMG"
mpartition -I z:
mpartition -c -b 63 z:
mpartition -a z:
mformat -v "PCM_BOOT" z:
mmd     z:/BOOT
mcopy "$KERNEL_BOOT"          z:/BOOT/MMBASIC.ELF
mcopy "$PORT_DIR/limine.conf" z:/BOOT/LIMINE.CONF
mcopy "$LIMINE_BIOS_SYS"      z:/BOOT/LIMINE-BIOS.SYS

# Hello + FizzBuzz ride on the boot disk — they're tiny and form the
# first thing the user runs after the prompt comes up.
mcopy "$DEMOS_DIR/hello.bas"     z:/HELLO.BAS
mcopy "$DEMOS_DIR/fizzbuzz.bas"  z:/FIZZBUZZ.BAS

cat > "$DISK_DIR/README.TXT" <<'EOF'
PicoMite PC386 legacy Limine helper image.

Boot:               Limine -> mmbasic.elf via multiboot1
Sample programs:    RUN "HELLO.BAS"
                    RUN "FIZZBUZZ.BAS"

Larger demos and persistent storage live on drive C:.
EOF
mcopy "$DISK_DIR/README.TXT" z:/README.TXT

# Install Limine's MBR + VBR. Must come AFTER the FAT is populated so
# Limine can index its stage-2 file by cluster offset.
limine bios-install "$A_IMG"

# --- C: 32 MB FAT16 partitioned IDE image. ----------------------------------
truncate -s 32M "$C_IMG"
mpartition -I y:
mpartition -c -b 63 y:
mpartition -a y:
mformat -h 16 -s 63 -t 65 -v "PCM_DATA" y:
mmd   y:/BOOT
mmd   y:/PROGRAMS
mcopy "$KERNEL_BOOT"          y:/BOOT/MMBASIC.ELF
mcopy "$PORT_DIR/limine.conf" y:/BOOT/LIMINE.CONF
mcopy "$LIMINE_BIOS_SYS"      y:/BOOT/LIMINE-BIOS.SYS
mcopy "$DEMOS_DIR/mand.bas"   y:/PROGRAMS/MAND.BAS
mcopy "$DEMOS_DIR/sieve.bas"  y:/PROGRAMS/SIEVE.BAS
if [[ -f "$REPO_ROOT/pico_blocks.bas" ]]; then
    mcopy "$REPO_ROOT/pico_blocks.bas" y:/PICO_BLOCKS.BAS
fi
if [[ -f "$REPO_ROOT/demo_sound_sfx.bas" ]]; then
    mcopy "$REPO_ROOT/demo_sound_sfx.bas" y:/SFX_DEMO.BAS
fi
if [[ -f "$PORT_DIR/demos/pcl_demo.bas" ]]; then
    mcopy "$PORT_DIR/demos/pcl_demo.bas" y:/PCL_DEMO.BAS
fi
mcopy "$DISK_DIR/README.TXT"  y:/README.TXT
limine bios-install "$C_IMG"

# --- 1.44 MB FAT12 bootable superfloppy. ------------------------------------
cat > "$DISK_DIR/FLOPPY.TXT" <<'EOF'
PicoMite PC386 1.44 MB floppy image.

Attach as a raw floppy/FDC image. The pc386 runtime mounts this as A:.
It also carries /BOOT as install media for SYS C:.
EOF
python3 "$PORT_DIR/tools/make_boot_floppy.py" \
    --boot-sector "$FLOPPY_STAGE1" \
    --stage2 "$FLOPPY_STAGE2" \
    --kernel "$KERNEL_BOOT" \
    --limine-conf "$PORT_DIR/limine.conf" \
    --limine-sys "$LIMINE_BIOS_SYS" \
    --hello "$DEMOS_DIR/hello.bas" \
    --fizzbuzz "$DEMOS_DIR/fizzbuzz.bas" \
    --readme "$DISK_DIR/FLOPPY.TXT" \
    "$F_IMG"

ls -la "$DISK_DIR"
echo
echo "Legacy helper image contents:"
mdir -/ z:
echo "C: contents:"
mdir -/ y:
echo "A: floppy image contents:"
mdir -/ x:
