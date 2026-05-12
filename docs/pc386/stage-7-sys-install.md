# PC386 Stage 7 - `SYS C:` Install

Stage 7 makes the hard-drive image self-bootable and adds a pc386-local
`SYS C:` command.

What is implemented:

- `build_disks.sh` now creates `c.img` as a partitioned FAT16 IDE disk.
- Limine BIOS boot sectors are installed onto `c.img`.
- `/BOOT/MMBASIC.ELF`, `/BOOT/LIMINE.CONF`, and `/BOOT/LIMINE-BIOS.SYS`
  are present on C:.
- `SYS C:` refreshes those boot files from `A:/BOOT` to `C:/BOOT`.
- The ELF now carries both multiboot1 and minimal multiboot2 headers.
- Boot images carry the stripped boot ELF (`mmbasic-stripped.elf`), keeping
  the kernel file under 1 MB instead of copying DWARF debug sections.
- `build_disks.sh` also creates `pc386-floppy.img`, a true 1.44 MB FAT12
  superfloppy. The direct QEMU runner attaches it through the emulated FDC
  and the runtime mounts it as `A:`. It carries `/BOOT` as install media for
  `SYS C:` and, as of Stage 9, can BIOS-boot through the custom pc386
  floppy loader.
- `run_limine.sh` defaults to 32 MB for the BIOS/Limine path. The direct
  QEMU `-kernel` dev path still runs at 16 MB, but Limine needs more
  allocator headroom with the current kernel image.

Run examples:

```sh
ports/pc386/run_limine.sh
ports/pc386/run_limine.sh headless
PC386_LIMINE_BOOT=c ports/pc386/run_limine.sh
ports/pc386/run_floppy.sh
```

When booting `c.img` as the only IDE disk, the guest sees it as drive `C:`.
The legacy `a.img` helper is still buildable and can be selected with
`PC386_LIMINE_BOOT=a`, but normal PC-style operation is `A:` for floppy and
`C:` for the primary hard disk.

The floppy image is a real FDC-mounted data disk at runtime and a BIOS boot
floppy. The floppy path does not use Limine; the Stage 9 pc386 boot sector
and stage2 loader read `/BOOT/MMBASIC.ELF` directly.

Current boundary:

- Limine's MBR/VBR installer still runs host-side in `build_disks.sh`.
  The guest does not yet embed Limine's bootsector installer, so `SYS C:`
  updates boot files but does not rewrite raw boot sectors on arbitrary
  physical disks.
- Limine is still configured for `protocol: multiboot1` until `kmain`
  grows a multiboot2 information-structure parser.

Test coverage:

- `ports/pc386/tests/repl_expect.py sys_c` verifies the command and the
  resulting C:/BOOT files.
- A Limine boot from `c.img` as IDE primary master verifies the generated
  C: image can boot independently.
