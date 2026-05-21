# Pocket386 / PC386 Port State

Date: 2026-05-20

Branch:

```sh
pocket386-port
```

Deploy image:

```sh
/Users/joshv/Documents/pocket386/mmbasic_c.img
```

Current image SHA256 at the time this note was written:

```text
f5449a36dfd13dc7b984e7b74c66e28dc1fa90a32cac7e84757ccf8f3e5fff00
```

## Hardware Facts

- Target machine is a Pocket386-class 386 PC.
- Video chip observed on the board: Cirrus Logic GD5420.
- Runtime `MODE DEBUG` reports Cirrus backend, GD5420, 512KB video RAM.
- Sound path is OPL3/AdLib compatible. `PLAY TONE` works.
- Build is configured for 386-only instructions and no hardware FPU.
- FDC probing is disabled for this target.
- Boot image used for deployment is the direct-boot C: image, not the Limine image.

## Current Port Status

Working or previously confirmed:

- Kernel boots on the Pocket386 from the CF/IDE image.
- BASIC prompt works.
- Keyboard input works.
- `FILES` pagination crash was fixed.
- Soft-float build is used.
- Heap/high memory setup gives roughly 2MB available rather than the old low-memory-only layout.
- OPL3 tone output is present.
- C: direct boot image includes `MAND.BAS`, `PBLOCK20.BAS`, `VADERS.BAS`, `VADERSFG.BAS`, `SFX_DEMO.BAS`, and `PCL_DEMO.BAS`.

Known active issue:

- Cirrus extended graphics modes are still under test.
- Hardware is GD5420 with 512KB VRAM, so 1024x768 must be 16-color planar only.
- `MODE 2` used to be the most stable extended mode, but recent GD5420 table experiments caused duplicated/offset prompt rendering. The latest build restored the old 640x480 fallback table, but this still needs confirmation on hardware.
- `MODE 3`, `MODE 4`, and `MODE 6` are not considered stable yet.

## Build

From the repo root:

```sh
./ports/pc386/build-pocket386-current.sh
```

That script:

1. Builds the pc386 kernel with the current Pocket386 flags.
2. Rebuilds the direct-boot C: image.
3. Copies `ports/pc386/test_disks/c-direct.img` to `/Users/joshv/Documents/pocket386/mmbasic_c.img`.
4. Prints the new image SHA256.

Equivalent manual commands:

```sh
PC386_AUDIO=opl3 \
PC386_NO_FPU=1 \
PC386_NO_FDC=1 \
PC386_KBD_SCANCODE_SET=1 \
PC386_VIDEO=auto \
./ports/pc386/build.sh

PC386_REBUILD_C=1 ./ports/pc386/build_disks.sh

cp ports/pc386/test_disks/c-direct.img /Users/joshv/Documents/pocket386/mmbasic_c.img

shasum -a 256 /Users/joshv/Documents/pocket386/mmbasic_c.img
```

## Flashing

Replace `diskN` with the actual CF device from `diskutil list`.

```sh
diskutil unmountDisk /dev/diskN
sudo dd if=/Users/joshv/Documents/pocket386/mmbasic_c.img of=/dev/rdiskN bs=4m status=progress
sync
diskutil eject /dev/diskN
```

Use the raw disk device for the CF card, not a partition like `diskNs1`.
