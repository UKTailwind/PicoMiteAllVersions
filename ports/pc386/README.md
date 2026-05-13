# pc386 - bare-metal MMBasic on a 386 PC

This port boots MMBasic as the operating system on a 386-class IBM PC
compatible. There is no DOS underneath: BIOS POST hands control to the pc386
boot path, the kernel brings up PC hardware, and the user lands at the MMBasic
prompt.

The port is intentionally shaped like the MCU targets. Core MMBasic is linked
with a pc386 port directory, HAL implementations, and PC-specific drivers for
VGA/VBE graphics, PS/2 keyboard, COM1, ATA-PIO, 82077-compatible floppy,
Sound Blaster 16 or PC speaker audio, and LPT1 GPIO/Centronics output.

## Status

The QEMU development path is usable:

- VGA REPL by default, with COM1 mirrored for logs and automated tests.
- `A:` is a real 1.44 MB FAT12 boot floppy image.
- `C:` is the primary FAT16 hard-disk image and persistent data drive.
- `MODE 1` boots to 320x200 VGA mode 13h; `MODE 2..6` use VBE when present.
- `FASTGFX`, file I/O, editor open/save, `SYS C:`, SB16 audio, PC speaker
  fallback, LPT1 GPIO, and `OPEN "LPT1:" FOR OUTPUT` are implemented.
- DOSBox-X can boot the floppy image for sanity checks. QEMU remains the
  primary development and test target; Bochs/86Box are better real-PC gates.

Real hardware validation is still the main open stage.

## Build

Install the cross toolchain first:

```sh
./toolchain/pc386/install_cross.sh
```

Build the default SB16 kernel and disk images:

```sh
./ports/pc386/build.sh
./ports/pc386/build_disks.sh
```

`build_disks.sh` refreshes the boot floppy and boot files, but preserves an
existing `C:` image by default. To intentionally recreate `C:`:

```sh
PC386_REBUILD_C=1 ./ports/pc386/build_disks.sh
```

## Run

Interactive QEMU, BIOS/floppy boot, VGA window, COM1 mirror:

```sh
./ports/pc386/run.sh
```

Useful variants:

```sh
./ports/pc386/run.sh unscaled
./ports/pc386/run.sh fullscreen
./ports/pc386/run.sh debug
./ports/pc386/run.sh kernel
./ports/pc386/run_headless.sh
./ports/pc386/run_dosbox.sh
```

The boot floppy image is:

```text
ports/pc386/test_disks/pc386-floppy.img
```

The primary hard-disk image is:

```text
ports/pc386/test_disks/c.img
```

## Test

Focused smoke/regression checks:

```sh
python3 ports/pc386/tests/repl_expect.py files editor_floppy_after_mode graphics_vbe
python3 ports/pc386/tests/screen_probe.py --mode-stress --out /tmp/pc386-mode-stress.ppm
```

Broader QEMU test harness:

```sh
./ports/pc386/run_tests.sh
```

See [docs/pc386/emulation-and-toolchain.md](../../docs/pc386/emulation-and-toolchain.md)
for emulator setup and command-line details.

## Notes For Maintainers

- BIOS thunks are delicate. After the kernel remaps the 8259 PIC to
  `0x20..0x2F`, real-mode BIOS calls must mask both PICs and restore the old
  masks after returning to protected mode. Otherwise IRQ1 can arrive as
  `int 21h` while BIOS/DOSBox-X is running real-mode code.
- The native 82077 FDC path is the validation target for QEMU, Bochs, and real
  hardware. DOSBox-X has an A:-only BIOS `int 13h` fallback because it can boot
  an image through BIOS services without fully modelling the guest-visible FDC
  and DMA path.
- `C:` is intentionally preserved by default during disk rebuilds. Avoid wiping
  user programs unless `PC386_REBUILD_C=1` was explicitly requested.

Canonical plan and stage notes live at [docs/pc386-plan.md](../../docs/pc386-plan.md).
