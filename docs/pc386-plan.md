# PC386 Bare-Metal Port — Plan

Boot MMBasic as the OS on a 386-class IBM PC compatible. No DOS, no kernel underneath — power on, BIOS POST, MMBasic prompt. The HAL refactor (`real-hal-plan.md`) makes this conceptually a peer of the RP2040/RP2350/ESP32-S3 device ports: another bare-metal 32-bit target, satisfied by a port directory plus a list of HAL driver implementations.

This file is the index. Per-stage detail lives under `pc386/`. Read this page plus the one stage doc you're touching.

- **Branch:** `pc386-port` (off `esp32-port`).
- **Predecessor plans:** `real-hal-plan.md` (HAL contract surface), `real-hal/esp32-s3-port.md` (template for adding a new bare-metal target). Locked invariants from those plans are not revisited here.

## Goals

1. **Boot to BASIC.** Power on → BIOS → pc386 bootloader or multiboot loader → MMBasic banner → REPL prompt. No DOS, no underlying kernel, no userspace. The interpreter *is* the OS.
2. **HAL purity unchanged.** Adding the port introduces zero new target-macro `#ifdef`s in core or VM. New target shape is expressed as a port directory + driver implementations + per-stage `port_config.h` overrides.
3. **Automated test loop.** QEMU-backed Python harnesses boot the port, drive the REPL over COM1, and inspect both serial output and screenshots. CI-friendly tests use QEMU only.
4. **Real hardware reachable.** The same kernel and disk layout boots in QEMU and is structured for DOSBox-X/Bochs/86Box sanity checks and later beige-box validation. The normal user artifact is the bootable FAT12 floppy plus the C: hard-disk image; the Limine-installed C: path remains available for hard-disk boot validation.
5. **No interpreter changes.** Any HAL leak surfaced by this port gets fixed in core, not papered over here. Same rule as `mmbasic_stdio` (Phase 12.5 of real-hal).

## Non-goals

- DOS executable / DPMI port. That's a separate, simpler project (DJGPP build of `mmbasic_stdio`); not this plan.
- Multi-core, SMP, AP bring-up. Single CPU is fine.
- Paging / virtual memory. Flat 4 GB GDT, identity-mapped, no `CR0.PG`.
- 16-bit real-mode targets (8088 / 286). Out of scope per the address-space-model discussion.
- Networking. The HAL has `hal_net.h` but this port leaves it stubbed.
- USB. PS/2 keyboard is enough; USB host is a tarpit not worth opening on a 386 port.

## Toolchain

- **Cross compiler:** `i686-elf-gcc` + `i686-elf-binutils` (freestanding ELF target, no host libc). Bootstrap via `toolchain/pc386/install_cross.sh`.
- **Boot protocol(s):** multiboot1 in `.multiboot` for the dev and custom floppy paths. A minimal multiboot2 header also exists for future bootloader compatibility, but the kernel still consumes multiboot1 info.
- **Bootloader:** custom pc386 floppy stage1/stage2 for the normal boot floppy; [Limine](https://limine-bootloader.org/) remains installed on C: for hard-disk boot validation.
- **Build host:** macOS or Linux. No Docker required.
- **Primary emulator:** QEMU (`qemu-system-i386`). Floppy boot is the normal interactive path; direct multiboot1 ELF boot via `-kernel` remains available for isolation.
- **Secondary emulators:** DOSBox-X (BIOS-boot sanity check), Bochs/86Box (period-authentic, pre-real-HW gates).

See [`pc386/emulation-and-toolchain.md`](pc386/emulation-and-toolchain.md) for setup detail and command-line cookbook.

## Stage status

| Stage | Status | One-line state |
|-------|--------|----------------|
| [0 — hello kernel](pc386/stage-0-hello.md) | ✅ | Multiboot1 entry, serial COM1 + VGA text drivers, banner over both. QEMU `-kernel` boots and stdio captures it. (Multiboot2 deferred to Stage 7 — QEMU `-kernel` only speaks multiboot1.) |
| [1 — heap](pc386/stage-1-heap.md) | ✅ | Multiboot1 header requests MEMINFO; kmain walks the mmap and reports each region; static 1 MB MMBasic heap reserved in BSS at `~0x108000` ready for Stage 3 to hand to the interpreter. |
| [2 — disk + FS + Limine boot](pc386/stage-2-disk.md) | ✅ | ATA-PIO + FatFs over IDE brought up the first persistent disks. Current layout has `C:` as the primary IDE hard disk; the old `a.img` Limine helper remains only for compatibility. |
| [3 — stdio port](pc386/stage-3-stdio.md) | ✅ | MMBasic interpreter runs over COM1: full REPL with banner / line editing, FILES / LOAD / LIST / RUN over FatFs. Current tests load HELLO.BAS + FIZZBUZZ.BAS from real floppy `A:` and write to hard disk `C:`. |
| [4 — keyboard](pc386/stage-4-keyboard.md) | ✅ | IDT + own GDT, 8259A PIC remap, PS/2 IRQ1 + scancode→ASCII (US set 1, full modifier + extended-key support), 16550 IRQ4 RX. MMInkey drains both PS/2 (real keyboard input) and COM1 (test harness). 12-test repl_expect.py 12/12 × 5 runs; host_native still 243/243. |
| [5 — VGA mode 13h](pc386/stage-5-video.md) | ✅ | BIOS/FDC boot defaults to real VGA mode 13h (`320x200`). Cold `MODE 1` is VGA 13h; after entering VBE, `MODE 1` returns to a `320x200` logical screen on the known-good VBE `640x480` surface because QEMU/SeaBIOS does not reliably re-expose legacy A0000 scanout after LFB modes. `MODE 2`/`3`/`4` switch to VBE `640x480`/`800x600`/`1024x768`; `MODE 5` is `480x480` letterboxed inside VBE `640x480`; `MODE 6` is `320x320` pixel-doubled and letterboxed inside VBE `1024x768`. Real hardware uses BIOS VBE; QEMU/Bochs can use the documented DISPI interface when detected. There is no VGA planar mode-12h drawing path. `PIXEL`, `LINE`, `CIRCLE`, `BOX`, `CLS`, `TEXT`, `PIXEL(x,y)` readback, and `FASTGFX CREATE/FPS/SWAP/SYNC/CLOSE` work over the VGA/VBE scanout path. FatFs file commands, C: directory navigation, EDIT file open/save regressions, VGA/VBE graphics REPL tests, and default VGA screenshot probes are covered. |
| [6 — PC audio](pc386/stage-6-audio.md) | ✅ | The default `PC386_AUDIO=sb16` build uses a QEMU/ISA Sound Blaster 16 backend with 8-bit stereo DMA; `PC386_AUDIO=pcspk` remains available for PIT channel 2 + port `0x61`. Persistent `OPTION SB16 base[,irq[,dma[,dma16]]]` is stored as sparse `C:/OPTIONS.INI` key/value overrides. `PLAY TONE`, `PLAY SOUND` voices/noise, `PLAY STOP`, `PLAY PAUSE`, and `PLAY RESUME` work; the QEMU harness covers the SB16 build. File/stream audio remains unsupported. |
| [6.5 — LPT1 GPIO](pc386/stage-6_5-lpt1-gpio.md) | ✅ | `hal_pin.h`, `SETPIN`, `PIN()`, `PIN()=`, and VM pin syscalls map to LPT1 at `0x378`. BASIC pin numbers mirror DB-25: data outputs 2..9, control outputs 1/14/16/17, status inputs 10/11/12/13/15. PC-side inversions are hidden so BASIC sees connector-level logic. `repl_expect.py` covers output latch readback and read-only status-pin errors. |
| [6.6 — LPT1 / Centronics](pc386/stage-6_6-lpt1-centronics.md) | ✅ | `drivers/lpt_centronics/` owns the Strobe/Busy/Ack handshake; existing BASIC file commands (`OPEN "LPT1:" FOR OUTPUT AS #n`, `PRINT #n`, `CLOSE #n`) ship bytes through QEMU's parallel-port backend or a real LPT printer. `LPRINT` is deliberately left out unless/until we choose to add command-table sugar. Harness attaches `-parallel file:...` and byte-compares printer output. |
| [7 — `SYS C:\` install](pc386/stage-7-sys-install.md) | ✅ | `C:` is the partitioned, Limine-installed FAT16 primary hard-drive image. `SYS C:` refreshes `/BOOT` kernel + Limine files onto C: from the boot floppy's `/BOOT`. ELF carries multiboot1 + minimal multiboot2 headers; Limine remains configured for multiboot1 until the kernel parses multiboot2 info. |
| [8 — real hardware](pc386/stage-8-real-hw.md) | ⏳ | Beige-box bring-up. Boot from a USB stick (presents as IDE), `SYS C:\` to install onto the HDD, run native. The aesthetic payoff. |
| [9 — FDC + boot floppy](pc386/stage-9-fdc.md) | ✅ | Runtime 765/82077 FDC read support is in place: QEMU attaches `pc386-floppy.img` as a true floppy and MMBasic mounts it as `A:`; `B:` is reserved for a second floppy. The same 1.44 MB FAT12 image now BIOS-boots through a custom stage1/stage2 loader into the existing multiboot1 kernel ABI. |

## Validation model

A stage closes when:
1. `ports/pc386/build.sh` produces `mmbasic.elf` cleanly with no warnings (`-Wall -Wextra -Werror`).
2. `ports/pc386/run_tests.sh` is green for that stage's test corpus.
3. `tools/check_hal_purity.sh` stays green — no new core ifdefs introduced by integration.
4. `host/run_tests.sh` still 240/240 (or whatever the current number is — never lower). The pc386 port must not regress other targets.
5. The interpreter's BASIC test corpus passes the subset the stage's HAL surface supports. Full 192/192 parity is the long-term goal; reached when Stage 6 lands.

Smoke-boot in DOSBox-X and Bochs/86Box is desirable post-merge verification, not a stage-close blocker. Real-hardware boot (Stage 8) is its own gate.

## Test harness contract

The current pc386 harness is Python-driven rather than `.bas/.ok` files:

- `ports/pc386/tests/repl_expect.py` boots QEMU, drives the MMBasic REPL over
  COM1, and checks command output substrings.
- `ports/pc386/tests/screen_probe.py` drives BASIC over COM1, asks QEMU for a
  `screendump`, parses the PPM, and samples actual displayed pixels.
- Both harnesses copy disk images into temporary directories before booting, so
  tests do not lock or mutate the working `test_disks/*.img` files.
- QEMU is the CI-capable target. DOSBox-X/Bochs/86Box are manual compatibility
  probes, not deterministic golden-output runners.

## Directory layout

```
ports/pc386/
  README.md
  port_config.h              # inherits host_native, overrides for bare-metal x86
  boot.S                     # multiboot1 entry + minimal multiboot2 header
  linker.ld                  # kernel link script (load at 1 MB)
  Makefile                   # cross-compile, output mmbasic.elf + bootloader
  build.sh                   # wrapper: ./build.sh [build|iso|clean]
  build_disks.sh             # build/refresh a.img, c.img, pc386-floppy.img
  run.sh                     # QEMU with display; defaults to floppy boot
  run_floppy.sh              # QEMU BIOS/FDC boot path
  run_limine.sh              # QEMU C: Limine boot path
  run_headless.sh            # QEMU -display none -serial stdio
  run_dosbox.sh              # boot floppy in DOSBox-X (sanity)
  bootloader/                # floppy stage1/stage2
  tools/                     # disk/bootloader image builders
  tests/repl_expect.py       # serial REPL harness
  tests/screen_probe.py      # QEMU screenshot/pixel harness
drivers/
  vga_text/                  # boot console (text mode 80x25)
  serial_16550/              # COM1 (test output channel)
  ata_pio/                   # IDE / ATA-PIO block device
  fatfs/                     # FatFs over hal_storage (FAT12/16/32)
  fdc_82077/                 # 765/82077-compatible floppy controller
  i8259_pic/                 # PIC remap/mask/eoi helpers
  i8042_kbd/                 # PS/2 keyboard
  lpt_centronics/            # LPT1 GPIO/Centronics
  vga_mode13h/               # VGA/VBE graphics HAL
toolchain/pc386/
  install_cross.sh           # bootstrap i686-elf-gcc if missing
docs/
  pc386-plan.md              # this doc
  pc386/
    emulation-and-toolchain.md
    stage-0-hello.md
    stage-1-heap.md
    ... (per stage)
```

Driver directories appear as their stages land. Empty dirs are not committed.

## Open questions

- **Heap size default.** The port currently reserves a fixed 1 MB MMBasic heap
  from normal extended RAM. A future option could scale this from the memory
  map or expose a persistent heap-size option.
- **Real-hardware floor.** The software targets 386-class protected mode, but
  practical validation may require a 486 with ISA/VLB/PCI VGA, IDE, PS/2, and
  optionally SB16/LPT1. Resolve with Stage 8 hardware tests.
- **Fixed-disk geometry.** Modern QEMU and most post-1996 hardware speak LBA.
  Legacy 386-era IDE may need CHS fallback in the ATA path.
