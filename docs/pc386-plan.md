# PC386 Bare-Metal Port — Plan

Boot MMBasic as the OS on a 386-class IBM PC compatible. No DOS, no kernel underneath — power on, BIOS POST, MMBasic prompt. The HAL refactor (`real-hal-plan.md`) makes this conceptually a peer of the RP2040/RP2350/ESP32-S3 device ports: another bare-metal 32-bit target, satisfied by a port directory plus a list of HAL driver implementations.

This file is the index. Per-stage detail lives under `pc386/`. Read this page plus the one stage doc you're touching.

- **Branch:** `pc386-port` (off `esp32-port`).
- **Predecessor plans:** `real-hal-plan.md` (HAL contract surface), `real-hal/esp32-s3-port.md` (template for adding a new bare-metal target). Locked invariants from those plans are not revisited here.

## Goals

1. **Boot to BASIC.** Power on → multiboot2 entry → MMBasic banner → REPL prompt. No DOS, no underlying kernel, no userspace. The interpreter *is* the OS.
2. **HAL purity unchanged.** Adding the port introduces zero new target-macro `#ifdef`s in core or VM. New target shape is expressed as a port directory + driver implementations + per-stage `port_config.h` overrides.
3. **Automated test loop.** A `run_tests.sh` that mirrors `host/run_tests.sh` UX: cross-compile, boot kernel headless in QEMU with `-kernel mmbasic.elf -serial stdio -display none`, capture serial output, golden-compare. CI-friendly.
4. **Real hardware reachable.** The same artifact (`mmbasic.iso`, Limine-bootable) runs in QEMU, DOSBox-X, 86Box, and on a beige-box 486 from a USB stick. No build-flag divergence between dev and target.
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
- **Bootloader:** [Limine](https://limine-bootloader.org/). Multiboot2-compatible, single-binary, modern. `limine.cfg` points at the kernel ELF.
- **Build host:** macOS or Linux. No Docker required.
- **Primary emulator:** QEMU (`qemu-system-i386`). Direct multiboot2 ELF boot via `-kernel` for fast iteration; ISO boot via `-cdrom` for bootloader-path testing.
- **Secondary emulators:** DOSBox-X (BIOS-accurate sanity check), 86Box (period-authentic, pre-real-HW gate).

See [`pc386/emulation-and-toolchain.md`](pc386/emulation-and-toolchain.md) for setup detail and command-line cookbook.

## Stage status

| Stage | Status | One-line state |
|-------|--------|----------------|
| [0 — hello kernel](pc386/stage-0-hello.md) | ⏳ | Multiboot2 entry, GDT, serial COM1 + VGA text. Prints banner. QEMU `-kernel` boots and stdio captures it. |
| [1 — heap](pc386/stage-1-heap.md) | ⏳ | Parse multiboot memory map; init `TryGetMemory`/`FreeMemory` over conventional + extended RAM. |
| [2 — stdio port](pc386/stage-2-stdio.md) | ⏳ | Lift `ports/mmbasic_stdio` HAL surface onto bare metal. BASIC programs run; output via serial. First `run_tests.sh` parity goal. |
| [3 — keyboard](pc386/stage-3-keyboard.md) | ⏳ | PS/2 (8042) driver, IDT + PIC remap. Interactive REPL works in QEMU. |
| [4 — VGA mode 13h](pc386/stage-4-video.md) | ⏳ | `hal_video.h` driver against linear framebuffer at `0xA0000`. `PIXEL`, `LINE`, `CIRCLE`, `BOX` work. |
| [5 — PIT speaker](pc386/stage-5-audio.md) | ⏳ | `hal_audio.h` driver against PIT channel 2. `TONE`, `PLAY` work as square waves. |
| [6 — FAT16 filesystem](pc386/stage-6-fs.md) | ⏳ | FatFs port against `hal_storage`. ATA-PIO driver for IDE. `LOAD "FOO.BAS"` from disk image. |
| [7 — real hardware](pc386/stage-7-real-hw.md) | ⏳ | Flash to USB / write to floppy. Boot beige-box 486. The aesthetic payoff. |

## Validation model

A stage closes when:
1. `ports/pc386/build.sh` produces `mmbasic.elf` cleanly with no warnings (`-Wall -Wextra -Werror`).
2. `ports/pc386/run_tests.sh` is green for that stage's test corpus.
3. `tools/check_hal_purity.sh` stays green — no new core ifdefs introduced by integration.
4. `host/run_tests.sh` still 240/240 (or whatever the current number is — never lower). The pc386 port must not regress other targets.
5. The interpreter's BASIC test corpus passes the subset the stage's HAL surface supports. Full 192/192 parity is the long-term goal; reached when Stage 6 lands.

Smoke-boot in DOSBox-X and 86Box is a desirable post-merge verification, not a stage-close blocker. Real-hardware boot (Stage 7) is its own gate.

## Test harness contract

Each stage's tests follow the same pattern as `ports/mmbasic_stdio/tests/`:
- `tests/<stage>/*.bas` — BASIC programs.
- `tests/<stage>/*.ok` — expected serial output.
- `run_tests.sh` runs each `.bas` through `qemu-system-i386 -kernel mmbasic.elf -serial stdio -display none -no-reboot`, captures stdout, diffs against `.ok`.
- Tests signal completion by writing `PASS\n` or `FAIL: <reason>\n` to COM1 and triple-faulting (which makes QEMU exit cleanly under `-no-reboot`).

## Directory layout

```
ports/pc386/
  README.md
  port_config.h              # inherits host_native, overrides for bare-metal x86
  multiboot2.S               # entry + multiboot2 header
  linker.ld                  # kernel link script (load at 1 MB)
  Makefile                   # cross-compile, output mmbasic.elf + mmbasic.iso
  build.sh                   # wrapper: ./build.sh [debug|release]
  run.sh                     # QEMU with display
  run_headless.sh            # QEMU -display none -serial stdio
  run_tests.sh               # iterate tests/*.bas, golden-compare
  run_dosbox.sh              # boot iso in DOSBox-X (sanity)
  run_86box.sh               # boot iso in 86Box (period-authentic)
  limine.cfg                 # bootloader config for ISO path
  tests/<stage>/*.bas        # per-stage test corpus
drivers/
  vga_text/                  # boot console (text mode 80x25)
  serial_16550/              # COM1 (test output channel)
  pit_timer/                 # system tick + PC speaker
  ps2_kbd/                   # 8042 keyboard
  vga_mode13h/               # video HAL (320x200x256)
  fat16_atapi/               # filesystem on IDE
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

- **Heap size default.** RP2040 = 128 KB, ESP32-S3 = 104 KB. A 386 with 4 MB has more RAM than either. Decide before Stage 1 lands; probably 1–2 MB to give BASIC programs real room without fragmenting the multiboot memory map.
- **Serial vs VGA as primary console.** Test runs use serial. Interactive use prefers VGA text mode. Stage 0 lights up both; later stages may need a `console_select()` switch.
- **Real-hardware floor.** Targeting "any 386+" is forward-looking but real-mode-only segments of legacy hardware (DMA, ISA buses) may force a 486 floor. Resolve at Stage 7.
