# pc386 — bare-metal MMBasic on a 386 PC

This port boots MMBasic as the OS on a 386-class IBM PC compatible. No DOS, no kernel, no userspace — power on, BIOS POST, multiboot2 entry, MMBasic prompt. The interpreter *is* the OS.

Conceptually a peer of the RP2040/RP2350/ESP32-S3 device ports: another bare-metal 32-bit target satisfied by a port directory + driver implementations + per-stage `port_config.h` overrides. The HAL contract surface is unchanged from device targets; this port adds drivers for the IBM-PC peripheral set (VGA, PIT, 8042, ATA-PIO).

## Status

⏳ **Scaffolding only (2026-05-10).** Skeleton + plan in place. Real work pending — see staged delivery below.

## Plan

Canonical plan: [`docs/pc386-plan.md`](../../docs/pc386-plan.md). Per-stage detail under `docs/pc386/`.

Stages, in order:

| Stage | Deliverable |
|-------|-------------|
| 0 | Multiboot2 entry, GDT, serial console, VGA text. Banner over both. |
| 1 | Heap over multiboot memory map. |
| 2 | `mmbasic_stdio` HAL surface lifted onto bare metal. BASIC programs run via serial. |
| 3 | PS/2 keyboard. Interactive REPL. |
| 4 | VGA mode 13h video HAL. |
| 5 | PIT speaker audio HAL. |
| 6 | FAT16 + ATA-PIO, `LOAD "FOO.BAS"` from disk. |
| 7 | Real hardware (beige-box 486). |

## Toolchain

- `i686-elf-gcc` cross compiler (freestanding, `-nostdlib`).
- [Limine](https://limine-bootloader.org/) bootloader for the ISO path.
- QEMU (`qemu-system-i386`) as the primary emulator.
- DOSBox-X / 86Box as secondary "real-hardware-feel" backends.

Bootstrap the cross compiler:

```sh
../../toolchain/pc386/install_cross.sh
```

Setup detail and the full command-line cookbook live in [`docs/pc386/emulation-and-toolchain.md`](../../docs/pc386/emulation-and-toolchain.md).

## Build / run

```sh
./build.sh             # cross-compile → build/mmbasic.elf
./build.sh iso         # also produce build/mmbasic.iso (Limine-bootable)

./run.sh               # boot in QEMU with display
./run_headless.sh      # boot in QEMU headless, COM1 → stdio
./run_tests.sh         # iterate tests/*.bas, golden-compare
```

Iteration loop with QEMU `-kernel` is sub-second: edit C, `./build.sh`, `./run_headless.sh` — banner appears in your terminal.

## Exit gates

A stage closes when:

1. `./build.sh` is clean (`-Wall -Wextra -Werror`).
2. `./run_tests.sh` passes the stage's test corpus.
3. `tools/check_hal_purity.sh` stays green — no new core ifdefs.
4. `host/run_tests.sh` still 240/240. The port must not regress other targets.

Smoke-boot in DOSBox-X / 86Box is a desirable post-merge check, not a stage-close blocker.

## Why this port exists

Two reasons. First, it's a **forcing function for HAL purity** — if the contract is genuinely target-clean, a 32-bit bare-metal target with a wildly different peripheral set should drop in with no core changes. Any HAL leak this port surfaces is a real bug, fixed in core, not papered over here. (Same role `mmbasic_stdio` played for Phase 12.5.)

Second, it's the aesthetic endgame. PicoMite already runs as the OS on a $4 microcontroller. Running it as the OS on the PC architecture closes a loop — power on a 1986 beige-box and get the same BASIC prompt the kids of that era did, with a more capable interpreter underneath.
