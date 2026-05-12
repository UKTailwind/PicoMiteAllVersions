# PC386 Stage 9 - FDC + Boot Floppy

Goal: make `A:` a real PC floppy drive instead of an IDE helper image, while
keeping `C:` as the primary hard disk.

Current state:

- `drivers/fdc_82077/` implements a polling, read-only NEC 765/Intel 82077
  path for 1.44 MB FAT12 media using DMA channel 2.
- FatFs maps physical drive 0 to `A:` over FDC, physical drive 1 to optional
  `B:` over FDC, and physical drive 2 to `C:` over ATA primary master.
- `ports/pc386/run.sh`, `run_headless.sh`, `screen_probe.py`, and
  `repl_expect.py` attach `pc386-floppy.img` with `if=floppy,index=0` and
  attach `c.img` as IDE primary master.
- `C:/OPTIONS.INI` is the persistent options store. The floppy is read-only
  from MMBasic for now.
- `pc386-floppy.img` carries `/BOOT` so `SYS C:` can copy installer files
  from `A:/BOOT` to `C:/BOOT`.
- `ports/pc386/bootloader/` contains a custom two-stage floppy bootloader:
  stage1 is the FAT12 boot sector, stage2 loads the contiguous
  `/BOOT/MMBASIC.ELF` PT_LOAD segments and enters the kernel through the
  existing multiboot1 contract.
- `ports/pc386/tools/make_boot_floppy.py` builds a bootable 1.44 MB FAT12
  floppy image without Limine or partition-table assumptions.
- `ports/pc386/run_floppy.sh` boots the image through BIOS/FDC in QEMU.

Validated:

- BIOS boots `pc386-floppy.img` through stage1/stage2 into the existing
  pc386 kernel.
- `A:` lists `HELLO.BAS`, `FIZZBUZZ.BAS`, and `README.TXT` from the QEMU FDC.
- `LOAD "A:\HELLO.BAS"` and `RUN` work.
- `C:` is writable, supports relative `CHDIR`, and remains the default drive.
- `SYS C:` refreshes hard-drive boot files from `A:/BOOT`.
- `PC386_BOOT=floppy ports/pc386/tests/repl_expect.py arith files load_run sys_c`
  covers the floppy boot path.

Current boundary:

- The floppy runtime driver is read-only from MMBasic for now.
- The bootloader depends on the generated floppy image layout: stage2 lives
  in reserved sectors, and `MMBASIC.ELF` is allocated contiguously at the
  LBA emitted by `gen_floppy_load_plan.py`.
- The handoff memory map is intentionally minimal and sized for the 16 MB
  QEMU/default 386 target.
