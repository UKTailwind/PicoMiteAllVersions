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
# ports/esp32_s3/main/demos/ — same hello/fizzbuzz/mand/sieve
# corpus as the ESP32 port.
#
# Re-running refreshes the boot/helper images from scratch, but preserves an
# existing C: data image by default. Set PC386_REBUILD_C=1 to recreate C:.

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
DIRECT_C_IMG="$DISK_DIR/c-direct.img"
F_IMG="$DISK_DIR/pc386-floppy.img"
DEMOS_DIR="$REPO_ROOT/ports/esp32_s3/main/demos"
MAND_MENU_SRC="$REPO_ROOT/demos/bench/mand.bas"
PICO_BLOCKS_PC386_SRC="$PORT_DIR/demos/pico_blocks_20fps.bas"
PICO_VADERS_SRC="$REPO_ROOT/ports/host_wasm/demos/Picovaders.bas"
PICO_VADERS_FASTGFX_SRC="$REPO_ROOT/ports/host_wasm/demos/Picovaders_fastgfx.bas"
KERNEL="$PORT_DIR/build/mmbasic.elf"
KERNEL_BOOT="$PORT_DIR/build/mmbasic-stripped.elf"
FLOPPY_STAGE1="$PORT_DIR/build/bootloader/floppy_stage1.bin"
FLOPPY_STAGE2="$PORT_DIR/build/bootloader/floppy_stage2.bin"
HDD_STAGE1="$PORT_DIR/build/bootloader/hdd_stage1.bin"
HDD_STAGE2="$PORT_DIR/build/bootloader/hdd_stage2.bin"
HDD_LOAD_PLAN="$PORT_DIR/build/bootloader/hdd_load_plan.inc"

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

mkdir -p "$DISK_DIR"
rm -f "$A_IMG" "$F_IMG" "$DISK_DIR/.mtoolsrc" "$DISK_DIR/README.TXT" "$DISK_DIR/FLOPPY.TXT"

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
drive w: file="$DIRECT_C_IMG" partition=1 mformat_only
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
if [[ ! -f "$C_IMG" || "${PC386_REBUILD_C:-0}" == "1" ]]; then
    rm -f "$C_IMG"
    truncate -s 32M "$C_IMG"
    mpartition -I y:
    mpartition -c -b 63 y:
    mpartition -a y:
    mformat -h 16 -s 63 -t 65 -v "PCM_DATA" y:
    mmd   y:/BOOT
    mmd   y:/PROGRAMS
    mcopy "$MAND_MENU_SRC"        y:/MAND.BAS
    mcopy "$MAND_MENU_SRC"        y:/PROGRAMS/MAND.BAS
    mcopy "$DEMOS_DIR/sieve.bas"  y:/PROGRAMS/SIEVE.BAS
    if [[ -f "$PICO_BLOCKS_PC386_SRC" ]]; then
        mcopy "$PICO_BLOCKS_PC386_SRC" y:/PBLOCK20.BAS
    fi
    if [[ -f "$PICO_VADERS_SRC" ]]; then
        mcopy "$PICO_VADERS_SRC" y:/VADERS.BAS
    fi
    if [[ -f "$PICO_VADERS_FASTGFX_SRC" ]]; then
        mcopy "$PICO_VADERS_FASTGFX_SRC" y:/VADERSFG.BAS
    fi
    if [[ -f "$REPO_ROOT/demos/sound/demo_sound_sfx.bas" ]]; then
        mcopy "$REPO_ROOT/demos/sound/demo_sound_sfx.bas" y:/SFX_DEMO.BAS
    fi
    if [[ -f "$PORT_DIR/demos/pcl_demo.bas" ]]; then
        mcopy "$PORT_DIR/demos/pcl_demo.bas" y:/PCL_DEMO.BAS
    fi
    mcopy "$DISK_DIR/README.TXT"  y:/README.TXT
else
    echo "preserving existing C: image $C_IMG (set PC386_REBUILD_C=1 to recreate)"
    mmd y:/BOOT 2>/dev/null || true
fi
mcopy -o "$KERNEL_BOOT"          y:/BOOT/MMBASIC.ELF
mcopy -o "$PORT_DIR/limine.conf" y:/BOOT/LIMINE.CONF
mcopy -o "$LIMINE_BIOS_SYS"      y:/BOOT/LIMINE-BIOS.SYS
limine bios-install "$C_IMG"

# --- C: 32 MB FAT16 direct-boot IDE image, no Limine. -----------------------
#
# This image is intended for old BIOSes that can boot CHS hard disks but do
# not reliably support INT 13h Extensions. The MBR loads a tiny stage2 from
# sectors 1..16, and stage2 loads /BOOT/MMBASIC.ELF from a fixed contiguous
# LBA plan.
rm -f "$DIRECT_C_IMG"
truncate -s 32M "$DIRECT_C_IMG"
mpartition -I w:
mpartition -c -b 63 w:
mpartition -a w:
mformat -h 16 -s 63 -t 65 -v "PCM_DIRECT" w:
mmd   w:/BOOT
mcopy "$KERNEL_BOOT"          w:/BOOT/MMBASIC.ELF
mcopy "$PORT_DIR/limine.conf" w:/BOOT/LIMINE.CONF
mcopy "$LIMINE_BIOS_SYS"      w:/BOOT/LIMINE-BIOS.SYS
mmd   w:/PROGRAMS
mcopy "$MAND_MENU_SRC"        w:/MAND.BAS
mcopy "$MAND_MENU_SRC"        w:/PROGRAMS/MAND.BAS
mcopy "$DEMOS_DIR/sieve.bas"  w:/PROGRAMS/SIEVE.BAS
if [[ -f "$PICO_BLOCKS_PC386_SRC" ]]; then
    mcopy "$PICO_BLOCKS_PC386_SRC" w:/PBLOCK20.BAS
fi
if [[ -f "$PICO_VADERS_SRC" ]]; then
    mcopy "$PICO_VADERS_SRC" w:/VADERS.BAS
fi
if [[ -f "$PICO_VADERS_FASTGFX_SRC" ]]; then
    mcopy "$PICO_VADERS_FASTGFX_SRC" w:/VADERSFG.BAS
fi
if [[ -f "$REPO_ROOT/demos/sound/demo_sound_sfx.bas" ]]; then
    mcopy "$REPO_ROOT/demos/sound/demo_sound_sfx.bas" w:/SFX_DEMO.BAS
fi
if [[ -f "$PORT_DIR/demos/pcl_demo.bas" ]]; then
    mcopy "$PORT_DIR/demos/pcl_demo.bas" w:/PCL_DEMO.BAS
fi
mcopy "$DISK_DIR/README.TXT"  w:/README.TXT

kernel_fat_ranges="$(mshowfat w:/BOOT/MMBASIC.ELF)"
kernel_range_count="$(printf '%s\n' "$kernel_fat_ranges" | grep -o '<[^>]*>' | wc -l | tr -d ' ')"
if [[ "$kernel_range_count" != "1" ]]; then
    echo "error: direct-boot MMBASIC.ELF is not contiguous: $kernel_fat_ranges" >&2
    exit 1
fi
kernel_first_cluster="$(printf '%s\n' "$kernel_fat_ranges" | sed -E 's/.*<([0-9]+)(-[0-9]+)?>.*/\1/')"
# Geometry is fixed by the mpartition/mformat commands above:
# partition start 63, reserved 1, FATs 2, sectors/FAT 254, root dir 32.
kernel_lba=$((63 + 1 + 2 * 254 + 32 + kernel_first_cluster - 2))
python3 "$PORT_DIR/tools/gen_floppy_load_plan.py" \
    --kernel-lba "$kernel_lba" \
    "$KERNEL_BOOT" \
    "$HDD_LOAD_PLAN"
make -C "$PORT_DIR" hdd_bootloader

hdd_stage2_sectors=$((($(stat -f '%z' "$HDD_STAGE2") + 511) / 512))
if (( hdd_stage2_sectors > 16 )); then
    echo "error: HDD stage2 is $hdd_stage2_sectors sectors, max is 16" >&2
    exit 1
fi
dd if="$HDD_STAGE1" of="$DIRECT_C_IMG" bs=1 count=446 conv=notrunc status=none
dd if="$HDD_STAGE2" of="$DIRECT_C_IMG" bs=512 seek=1 conv=notrunc status=none

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
echo "Direct-boot C: contents:"
mdir -/ w:
echo "Direct-boot kernel FAT allocation:"
printf '%s\n' "$kernel_fat_ranges"
echo "Direct-boot kernel LBA: $kernel_lba"
echo "A: floppy image contents:"
mdir -/ x:
