# Stage 4 — PS/2 keyboard + IDT + PIC remap

**Goal:** Interrupt-driven keyboard input. PS/2 (8042) generates IRQ1 on every scancode; an ISR decodes to ASCII and pushes into `ConsoleRxBuf`; MMBasic's existing `MMInkey` drain logic reads from there. Same path the device builds (PicoMite, esp32) take. Side benefit: enabling UART RX IRQ (IRQ4) on COM1 closes the test-harness boot-race that drops the first piped char.

## Non-negotiables

1. **No HAL purity regression.** New code is in `drivers/i8259_pic/`, `drivers/i8042_kbd/`, `ports/pc386/idt.{c,S}`, `ports/pc386/irq.{c,S}`. Zero new `#if PORT_PC386` in MMBasic core.
2. **Test harness stays green.** Stage 3's `repl_expect.py` 12 tests must continue to pass; once 4f lands, the boot-race char-drop documented in the 3e commit is gone (test harness can drop the 2-second `sleep` it currently injects before the first command).
3. **`host/run_tests.sh` 243/243.** Same constraint as every prior stage.

## Sub-stages

- **4a — IDT skeleton.** A 256-entry `idt[256]` of gate descriptors in BSS, an `idtr` 6-byte register-image, `lidt` issued before the first `sti`. Vectors 0..31 are CPU exceptions (Divide Error, Page Fault, GP, etc.) — wire them all to a single `exc_unhandled` handler that prints "EXCEPTION %d at EIP=..." over serial and halts. Exposes the bare-metal scaffolding without yet enabling external IRQs.

- **4b — PIC remap + sti.** 8259A master at 0x20, slave at 0xA0. Reprogram so master IRQs land on vectors 0x20..0x27 and slave on 0x28..0x2F (defaults conflict with CPU exception vectors). Mask all IRQs initially, install per-vector stub asm (`isr_irq0` … `isr_irq15`) that pushes vector + jumps to a common `irq_dispatch_c`, then `sti`. The dispatcher fans out by vector to a registered C handler. Default handler EOIs the PIC and returns.

- **4c — IRQ1 PS/2 raw scancodes.** `drivers/i8042_kbd/` — read scancode bytes from port 0x60 in the IRQ1 handler, push into a small ring buffer (`uint8_t kbd_scancodes[64]`). Verified by running a "key dump" debug build that prints scancodes over serial as they arrive (manual smoke test under QEMU; press a few keys, confirm scancodes flow).

- **4d — Scancode → ASCII (set 1, US layout).** Translation table covering plain + shifted + caps-lock printables, plus the named-key codes MMBasic's editor wants (UP/DOWN/LEFT/RIGHT, HOME/END, PgUp/PgDown, F1..F12, BKSP, ENTER, TAB, ESC). Make/break codes tracked for the modifier keys (LSHIFT/RSHIFT/LCTRL/LALT/CAPS). Keep the table in `drivers/i8042_kbd/scan_to_ascii.c` so a future BR/FR/I2C-keyboard variant can swap layouts at link time.

- **4e — `MMInkey` from `ConsoleRxBuf`.** Replace the COM1-polling `MMInkey`/`MMgetchar` in `pc386_runtime.c` with the same ConsoleRxBuf-drain path the device builds use. PS/2 IRQ1 → scancode decode → `ConsoleRxBuf[ConsoleRxBufHead++]`. `MMInkey` reads `ConsoleRxBuf[ConsoleRxBufTail]` non-blocking; `MMgetchar` spins on it. The `hal_keyboard_service` hook (currently a no-op) becomes the routinechecks-tier drain that runs the same path host_native uses for its key script.

- **4f — IRQ4 UART RX.** COM1 RX produces IRQ4 instead of polling. Adds `serial_16550_irq` driver. RX bytes go into a UART RX FIFO that mirrors the keyboard ring; both feed `ConsoleRxBuf` so the REPL can be driven from either PS/2 (real keyboard) or `qemu -serial stdio` (the test harness). The 2-second `sleep` before the first command in `repl_expect.py` should become a no-op — confirm by removing it and re-running the suite 5x.

## Validation

A stage-4 close requires:

1. `make -C ports/pc386` clean.
2. `python3 ports/pc386/tests/repl_expect.py` — 12/12 tests pass × 5 sequential runs (same baseline as stage 3 close).
3. `host/run_tests.sh` — 243/243.
4. Manual smoke test in QEMU: keyboard input via `qemu -display gtk` (real PS/2 emulation) works for typing BASIC commands. Backspace, Enter, arrow keys (history), Tab in editor.
5. The `repl_expect.py` `sleep(2)` workaround for the boot-race char drop is removed and the suite still passes.

## Open questions

- **Slave PIC handling.** PS/2 mouse + RTC live behind the slave PIC (IRQ12 + IRQ8). We don't need either for stage 4 — the keyboard is on IRQ1 (master). Slave PIC bring-up happens in 4b but stays masked until needed.
- **Spurious IRQ7 / IRQ15.** The 8259 generates these on a glitch on the highest-priority unmasked input. Standard handling: read ISR, if bit isn't set, skip the EOI (else the next real IRQ on that vector hangs). 4b's dispatcher honors this.
- **APIC vs PIC.** Modern (post-2005ish) hardware emulates the 8259 well enough for this to work. Not worth the APIC bring-up complexity for a stage-3-grade pc386 port. Revisit if Stage 8's real-hardware tests find a board where the legacy PIC path is broken.
