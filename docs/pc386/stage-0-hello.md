# Stage 0 — Hello kernel

The minimum bare-metal kernel that proves the cross-toolchain, multiboot entry, and console drivers work end-to-end. No interpreter, no input, no IRQs — just power-on → banner → halt.

## Goal

Boot in QEMU via `qemu-system-i386 -kernel build/mmbasic.elf`, see the banner over both serial COM1 (captured by `-serial stdio`) and the VGA text-mode window. Halt cleanly.

This stage validates the entire build chain in isolation: if Stage 0 doesn't boot, every subsequent stage is blocked.

## Deliverables

| File | Role |
|------|------|
| `ports/pc386/boot.S` | multiboot1 header + `_start` |
| `ports/pc386/linker.ld` | kernel link script |
| `ports/pc386/kmain.c` | entry from `_start`; init consoles, print banner, halt |
| `ports/pc386/io.h` | inb/outb/etc. inline asm |
| `drivers/vga_text/{vga_text.c,vga_text.h}` | 80x25 text-mode console at 0xB8000 |
| `drivers/serial_16550/{serial_16550.c,serial_16550.h}` | COM1 driver, 38400 8N1 |

## Why multiboot1 (not multiboot2) for the dev path

The original scaffold targeted multiboot2. Stage 0 surfaced a constraint not noted upfront: **QEMU's `-kernel` flag does not support multiboot2** — it accepts multiboot1, Linux bzImage, and PVH only. Multiboot2 requires an actual bootloader (Limine, GRUB).

For the dev inner loop (`qemu -kernel mmbasic.elf`) we therefore use multiboot1 — a 12-byte header (magic, flags, checksum) instead of multiboot2's tagged structure. Practical impact for Stage 0: zero, since we don't yet parse the info struct.

Stage 7 (real-hardware ISO path) adds a multiboot2 header *alongside* the multiboot1 one. Both can live in the same `.multiboot` section; bootloaders scan for their preferred magic and match to whichever they recognise.

## Verification

After `./build.sh`:

```sh
$ ./run_headless.sh --timeout 5
PicoMite PC386 - Stage 0
multiboot1 magic: 0x2badb002  [ok]
serial COM1: ok
VGA text 80x25: ok (you're reading this)

Stage 0 complete. Halting.
```

Visual check (`./run.sh`): same banner appears in QEMU's VGA window in light-green text on black.

DOSBox-X / 86Box runs are not required at this stage — QEMU `-kernel` doesn't exercise a real bootloader. Later stages validate the floppy bootloader and Limine-installed hard-disk paths.

## Deliberately NOT in this stage

- **Multiboot info parsing.** `info_addr` is received but ignored. Stage 1 parses the memory map.
- **Heap / allocator.** No `malloc` available. All output uses static buffers.
- **IDT / PIC / IRQs.** Interrupts stay disabled (`cli` from `_start`). Stage 3 wires them up alongside the keyboard driver.
- **Serial input.** `serial_getc` is not implemented. Comes with Stage 2 (line input over serial) or Stage 3 (full UART IRQ).
- **Stack guard / trap handlers.** No fault handlers. A bug here triple-faults; QEMU `-no-reboot` makes that the exit signal.
- **Any MMBasic core.** Stage 2 lifts the `mmbasic_stdio` HAL surface onto bare metal.

## Exit gate

1. `./build.sh` produces `build/mmbasic.elf` with no warnings (`-Wall -Wextra`).
2. `./run_headless.sh --timeout 5` prints the banner to stdout and exits 0 within the timeout.
3. `./run.sh` shows the banner in the VGA window (manual check).
4. `host/run_tests.sh` still 240/240 — the new port mustn't regress others.
5. `tools/check_hal_purity.sh` green — Stage 0 touches no core files, so this is automatic, but verify.

## Known issues / followups

- **Loopback self-test in `serial_init`** uses a busy-wait loop with magic constant `1000` for round-trip latency. Fine on QEMU; might be marginal on slow real hardware. Worth replacing with a PIT-based delay once Stage 5 lands the timer.
- **No-op `kputhex32` allocation pattern.** Banner formatting uses a stack `char buf[11]` with manual hex conversion — adequate for Stage 0, replaced by a real `printf` shim once Stage 2 brings in MMBasic's print path.
- **VGA hardware cursor is updated on every `putc`.** Two extra port writes per character. Negligible on real hardware but visible in QEMU's `-d in_asm` output. Batch the cursor update at end-of-line if it matters.

## Why "Stage 0" not "Phase 0"

The real-hal plan uses "Phase". This port uses "Stage" to keep the namespaces distinct — when both phase numbers and stage numbers appear in the same conversation or grep, the prefix tells you which plan a number belongs to.
