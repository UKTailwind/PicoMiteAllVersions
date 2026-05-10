# Stage 2 — Disk + filesystem + bootable Limine

Close the "no flash" gap. Where MCU ports use a flash partition for `LOAD`/`SAVE`/`FILES`, this port uses an actual block device through `hal_storage`, with FatFs (`hal_filesystem`) layered on top. Boot the kernel via Limine off a small "floppy" IDE image (drive A:) and have a larger HDD-style image (drive C:) for everyday persistence.

## Why one driver path covers A: + C:

QEMU lets you attach the same disk image as either a floppy (`-fda`, uses the 765 FDC) or as IDE (`-drive`, uses ATA). On real hardware, USB sticks and CompactFlash cards present as IDE/SATA. So we model **both A: and C: as IDE drives, just different sizes** — `A:` is a 1.44 MB FAT12 image, `C:` is a 32 MB+ FAT16 image. One ATA-PIO driver (~150 LOC) covers both, the BASIC user sees `A:\` and `C:\` in the way they expect, and the real 765 FDC chip becomes a Stage 9 nice-to-have for booting from an actual physical floppy on real hardware.

## Goal

After Stage 2:
- `hal_storage` is implemented by an ATA-PIO driver in `drivers/ata_pio/`.
- `hal_filesystem` is implemented by a FatFs port in `drivers/fatfs/` (the same FatFs that the device + host ports already use; the adapter is the new code).
- A: and C: are both mountable, with FAT12 and FAT16 respectively.
- Limine boots the kernel from A:.
- Kernel prints a directory listing of A: and C: on boot, proving the chain.

The interpreter doesn't run yet (that's Stage 3). Stage 2 ends with the *ability* to read/write files; Stage 3 connects MMBasic to it.

## Deliverables

| File | Role |
|------|------|
| `drivers/ata_pio/{ata_pio.c,ata_pio.h}` | IDE PIO driver. `ata_read_sectors`, `ata_write_sectors`. LBA28 first; LBA48 if the user disk needs it. |
| `drivers/fatfs/ff_glue.c` | FatFs disk_io adapter binding `disk_read`/`disk_write` to the ATA-PIO driver. |
| `drivers/fatfs/ffconf.h` | FatFs config — FAT12 + FAT16 + FAT32, no LFN initially (saves heap). |
| (vendored) `drivers/fatfs/ff.c`, `ff.h`, `ffunicode.c`, `ffsystem.c` | Stock FatFs sources, same vendored copy other ports use. |
| `ports/pc386/hal_storage_ata.c` | `hal_storage` impl: routes block reads/writes through `drivers/ata_pio`. |
| `ports/pc386/hal_filesystem_fatfs.c` | `hal_filesystem` impl: routes file ops through FatFs. |
| `ports/pc386/limine.cfg` | Limine boot config: kernel = `/boot/mmbasic.elf`, multiboot1 protocol. |
| `ports/pc386/build_disks.sh` | Builds `a.img` (1.44 MB FAT12 + Limine bootsector + kernel) and `c.img` (32 MB FAT16, blank). |

## Boot flow

1. QEMU starts with `-drive file=a.img,if=ide,index=0 -drive file=c.img,if=ide,index=1 -boot a`.
2. BIOS reads sector 0 of A: → Limine MBR.
3. Limine reads `/boot/limine.cfg` from A:'s FAT12, finds `KERNEL_PATH=boot:///boot/mmbasic.elf`.
4. Limine loads `mmbasic.elf`, jumps to `_start` per multiboot1 protocol.
5. Kernel runs Stage 1's mmap parse, then Stage 2's disk init: probes both IDE drives, mounts FAT on each.
6. Kernel prints something like:

```
A: FAT12, 1474560 bytes total, 1389056 free, 4 entries
C: FAT16, 33554432 bytes total, 33548288 free, 0 entries
```

7. Halts.

## Test harness

`build_disks.sh` builds the images using `mtools` (no Linux mount, no sudo):

```sh
# A:
mformat -i a.img -f 1440 ::
mmd     -i a.img ::/boot
mcopy   -i a.img build/mmbasic.elf ::/boot/
mcopy   -i a.img limine.cfg        ::/boot/
mcopy   -i a.img README.TXT        ::/
limine bios-install a.img          # writes Limine to A:'s MBR

# C: blank for now; Stage 3 will populate with .bas test programs.
truncate -s 32M c.img
mformat -i c.img ::
```

`run_tests.sh` will (in Stage 3+) cycle:
1. `mtools`-build a per-test C: image with the BASIC program in it.
2. `qemu-system-i386 -drive a.img -drive c.img -boot a -serial stdio -display none`.
3. Capture COM1 output, diff against `.ok` golden.

## Deliberately NOT in this stage

- **MMBasic on top.** Stage 3 lifts the stdio HAL surface and wires `LOAD`/`SAVE` to FatFs. Stage 2 just proves the FS works.
- **Write-back caching.** All FatFs writes go straight to disk. Easy to add later if write performance bites.
- **CD-ROM (ATAPI).** Not needed; we boot off A: as IDE. Stage 8 may revisit if we want a CD-ROM-only install media.
- **Real 765 FDC.** Optional Stage 9. Pure aesthetics.
- **NVMe / AHCI.** No. ATA-PIO is enough. We are not optimizing IOPS on a 386 emulator.

## Exit gate

1. `./build.sh && ./build_disks.sh` produces `a.img` (1.44 MB) and `c.img` (32 MB).
2. `qemu-system-i386 -drive a.img,if=ide -drive c.img,if=ide -boot a -serial stdio -display none` boots through Limine and prints the FS listing within ~3 seconds.
3. `tools/check_hal_purity.sh` green — Stage 2 only adds drivers + port glue, no core changes.
4. `host/run_tests.sh` still green.

## Why not just multiboot modules

The expedient option (use Limine modules to pass `.bas` files alongside the kernel as a read-only RAM "FS") was considered and rejected. Rationale: the user-visible model would diverge from the device ports (where `LOAD`/`SAVE`/`FILES` work over real persistent storage), the test harness for module-based input is uglier than `mtools`-built FAT images, and the work would have to be undone at Stage 6 anyway. Worth biting the disk-driver bullet upfront.
