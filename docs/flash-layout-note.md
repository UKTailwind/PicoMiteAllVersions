# Critical: FLASH_TARGET_OFFSET vs. binary size

## Symptom
After flashing any RP2350 variant, the board immediately returns to BOOTSEL
instead of running. `picotool info` on the running device reports:

```
ERROR: Block loop is not valid - no block found at 100fXXXX
```

Flashing verifies OK (`picotool load -v` reports 100%), drag-and-drop via the
bootrom's own MSD gives the same result, and full-flash erase before flashing
does not help. HDMI screen stays blank, no USB-CDC appears.

## Root cause
MMBasic reserves a flash region for saved OPTIONs / program slots starting at
`FLASH_TARGET_OFFSET` (defined per-variant in `configuration.h`). On first
boot the firmware erases/writes that sector. If `FLASH_TARGET_OFFSET` is
**smaller than the firmware's `binary_end`**, the first write destroys bytes
inside the firmware image itself — specifically the IMAGE_DEF Block 2
terminator that the RP2350 bootrom uses to validate the image.

After that first write, every subsequent boot fails bootrom validation and
drops straight to BOOTSEL. The image flashed fine; it corrupts itself on
first run.

The bug is silent and hard to diagnose because:
- `picotool load -v` verifies right after flashing, *before* the firmware
  has run, so it passes.
- The failure mode (BOOTSEL on boot) looks identical to a bad flash or
  unsupported board.
- Swapping `PICO_BOARD`, disabling PSRAM, erasing flash — none of it helps,
  because the firmware overwrites itself every time.

## How to detect
Compare `picotool info <firmware.uf2>` output against the `FLASH_TARGET_OFFSET`
for that variant in `configuration.h`:

- `binary end:  0x100fXXXX`  →  firmware occupies flash up to `0xXXXX`
- `FLASH_TARGET_OFFSET (N * 1024)`  →  MMBasic saves starting at `N*0x400`

If `binary_end - 0x10000000 >= FLASH_TARGET_OFFSET`, the variant is broken.

A 4 KB guard band is wise because `FLASH_ERASE_SIZE` is 4 KB and the save
region is sector-aligned — an overlap anywhere in the sector wipes the tail.

## Variants I've seen bite
With SDK 2.1.1 + GCC 14.2.1 + current tree, the `HDMI` (no-USB-host) build
produced `binary_end = 0x100f4888` (≈977 KB) while its
`FLASH_TARGET_OFFSET` was `976 * 1024 = 0xF4000`. Bump to `1024 * 1024`
resolved it. Other `PICOMITEVGA` / `PICOMITE` offsets are similarly tight
(800–1136 KB range) and will eventually hit the same wall as features are
added or a newer SDK inflates the build.

## Fix
In `configuration.h`, raise `FLASH_TARGET_OFFSET` for the affected variant
so there's at least one 4 KB sector of headroom above `binary_end`:

```c
#ifdef HDMI
#ifdef USBKEYBOARD
#define FLASH_TARGET_OFFSET (1056 * 1024)   // was 1008
#else
#define FLASH_TARGET_OFFSET (1024 * 1024)   // was 976
#endif
```

Rebuild, reflash, confirm `picotool info <uf2>` shows
`binary_end < FLASH_TARGET_OFFSET`, then boot. A running `/dev/cu.usbmodem*`
with PID `0x0009` is proof the fix took.

## Defensive idea for the portable build
Add a CMake-time assertion so builds fail loudly when the image outgrows
its save-region offset, e.g. post-link check the ELF's `_end` (or the
padded binary size) against `FLASH_TARGET_OFFSET` and `message(FATAL_ERROR ...)`
if they collide. Cheaper than re-diagnosing this from scratch.
