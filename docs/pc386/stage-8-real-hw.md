# PC386 Stage 8 - Real Hardware Bring-Up

Stage 8 is the remaining compatibility gate: boot the same pc386 kernel and
disk layout on a real 386/486-era IBM PC compatible.

## Current Target Shape

- Boot floppy: `ports/pc386/test_disks/pc386-floppy.img`, a 1.44 MB FAT12
  image with the custom pc386 stage1/stage2 loader and `/BOOT/MMBASIC.ELF`.
- Primary disk: `ports/pc386/test_disks/c.img`, a FAT16 hard-disk image with
  `/BOOT` plus persistent user files.
- Default display: VGA mode 13h, `320x200`.
- Optional higher modes: BIOS VBE if the card provides it.
- Keyboard: PS/2 controller, set-1 scancodes.
- Audio: SB16 if present/configured; PC speaker build remains available.
- LPT1: connector-level GPIO and Centronics printer output.

## Hardware To Try First

A realistic first target is a 486-class system or emulator-equivalent machine
with:

- 8-16 MB RAM.
- VGA/SVGA card with mode 13h and preferably VBE.
- IDE hard disk or CF/SD-to-IDE adapter.
- 1.44 MB floppy drive, Gotek, or another way to present the raw floppy image.
- PS/2 keyboard.
- Optional SB16-compatible sound card and LPT1 port.

The code is 386 protected-mode code, but a 486 is the pragmatic first hardware
gate because real firmware, DMA, VGA, and IDE behaviour vary more than the CPU
instruction set does.

## Validation Checklist

1. Boot from the pc386 floppy image and reach the VGA REPL.
2. Confirm COM1 mirror if a serial adapter is available.
3. Run `FILES` on `A:` and `C:`.
4. Run `EDIT "A:/FIZZBUZZ.BAS"` and exit cleanly.
5. Run `MODE` and test each advertised mode.
6. Run `RUN "C:/PICO_BLOCKS.BAS"` if present.
7. Test `PLAY TONE 440,400` on the configured audio backend.
8. Test LPT1 output with `OPEN "LPT1:" FOR OUTPUT AS #1`.
9. Run `SYS C:` and then verify the hard disk can boot independently where the
   firmware and disk geometry allow it.

## Known Risk Areas

- BIOS thunks must mask/restore the remapped PIC around real-mode calls.
- Some VGA cards may offer no VBE, leaving only mode 13h.
- Older IDE controllers may need CHS support beyond the current LBA-focused
  ATA path.
- Floppy and ISA DMA timing may differ from QEMU; the 82077 driver is
  intentionally simple and read-only today.
