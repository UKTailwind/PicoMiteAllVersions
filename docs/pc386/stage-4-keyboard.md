# Stage 4 — PS/2 keyboard + IDT + PIC remap

**Goal:** Interrupt-driven keyboard input. PS/2 (8042) generates IRQ1 on every scancode; an ISR decodes to ASCII and pushes into `ConsoleRxBuf`; MMBasic's existing `MMInkey` drain logic reads from there. Same path the device builds (PicoMite, esp32) take. Side benefit: enabling UART RX IRQ (IRQ4) on COM1 closes the test-harness boot-race that drops the first piped char.

## Non-negotiables

1. **No HAL purity regression.** New code is in `drivers/i8259_pic/`, `drivers/i8042_kbd/`, `ports/pc386/idt.{c,S}`, `ports/pc386/irq.{c,S}`. Zero new `#if PORT_PC386` in MMBasic core.
2. **Test harness stays green.** Stage 3's `repl_expect.py` 12 tests must continue to pass; once 4f lands, the boot-race char-drop documented in the 3e commit is gone (test harness can drop the 2-second `sleep` it currently injects before the first command).
3. **`ports/host_native/run_tests.sh` 243/243.** Same constraint as every prior stage.

## Sub-stages

- **4a ✅ — IDT skeleton.** `ports/pc386/idt.{h,c}` + `idt_asm.S`. Own GDT in `boot.S` (selectors 0x08 KCODE / 0x10 KDATA), then `lidt` a 256-entry table. CPU exception vectors 0..31 wire to `exc_unhandled` (prints vector + CS:EIP + err code + eflags then panics) instead of triple-faulting. `idt_register_handler(vec, fn)` lets later sub-stages plug in real IRQ handlers. ASM common stub does `pusha` + segs + reload to KDATA + `idt_dispatch(regs)` + restore + `iretl`; per-vector stubs generated via `.altmacro` + `.rept`.

- **4b ✅ — PIC remap + sti.** `drivers/i8259_pic/` — remap 8259A pair to vectors 0x20..0x2F (avoids overlap with CPU exception vectors in protected mode), all lines start masked except the cascade IRQ2. `pic_unmask`/`pic_mask`/`pic_eoi` for handler use; `pic_is_spurious` for the IRQ7/IRQ15 glitch path. kmain `sti`'s once both IDT and PIC are up.

- **4c ✅ — IRQ1 PS/2 raw scancodes.** `drivers/i8042_kbd/` — IRQ1 handler reads port 0x60, pushes byte into a 128-entry ring. `kbd_init` drains BIOS leftovers + registers handler + unmasks IRQ1. `kbd_get_scancode` / `kbd_has_scancode` for consumers.

- **4d ✅ — Scancode → ASCII (set 1, US layout).** `drivers/i8042_kbd/scan_to_ascii.c` — modifier state machine (Shift/Ctrl/Alt/Caps), `0xE0` extended prefix decoder for arrow/HOME/END/PgUp/PgDown/INSERT/DEL, F1..F12, plus all named-key codes matching `Hardware_Includes.h` (UP, DOWN, LEFT, RIGHT, ESC, BKSP, ENTER, TAB). `kbd_get_key()` returns the MMBasic-shaped key code or -1.

- **4e ✅ — `MMInkey` over PS/2 + COM1.** `MMInkey` drains `kbd_get_key()` first (real keyboard input from PS/2), falls through to `serial_getc_nonblock` (test harness piped input). `MMgetchar` does the same but spins until something's ready, with a `hlt` between checks so an idle CPU doesn't burn.

- **4f ✅ — IRQ4 UART RX.** `serial_16550` adds an IRQ4 handler that drains the UART FIFO into a 256-byte ring; `serial_getc_nonblock` reads the ring first, falls back to LSR poll if pre-IRQ-init. `serial_init` now enables the FIFO FIRST (pre-FCR-enable bytes are lost in 16450 single-byte mode — moving the FCR write earlier keeps everything that arrives during the rest of init), and the loopback self-test is gone (it was eating piped input + the test was overdefensive theatre on a guaranteed-present 0x3F8 UART). `serial_irq_init` moves any boot-buffered bytes into the ring rather than discarding them. Bytes arriving BEFORE the kernel's first instruction in kmain are still lost (bootloader doesn't FIFO-enable) — but that's only reachable from `printf | qemu` test invocations that bypass the test harness; `repl_expect.py` writes after detecting the boot prompt and is unaffected.

- **4g ✅ — VGA-text console as primary, serial as fallback.** `SerialConsolePutC` (the byte-out callback all MMBasic output funnels through) forks every byte to both `vga_text_putc` and `serial_putc`. Standard IBM-PC convention: VGA + PS/2 is the user-facing console; serial is the remote/debug path. When QEMU runs with its default GUI window, the banner + prompt + REPL output appear in the VGA window and PS/2 IRQ1 drives the prompt. Adding `-serial stdio` simultaneously gives a terminal-side console that mirrors the same bytes — useful for capture / debugging. `repl_expect.py` runs against the serial path and stays 12/12 unchanged.

## Validation

A stage-4 close requires:

1. `make -C ports/pc386` clean.
2. `python3 ports/pc386/tests/repl_expect.py` — 12/12 tests pass × 5 sequential runs (same baseline as stage 3 close).
3. `ports/host_native/run_tests.sh` — 243/243.
4. Manual smoke test in QEMU: keyboard input via `qemu -display gtk` (real PS/2 emulation) works for typing BASIC commands. Backspace, Enter, arrow keys (history), Tab in editor.
5. The `repl_expect.py` `sleep(2)` workaround for the boot-race char drop is removed and the suite still passes.

## Open questions

- **Slave PIC handling.** PS/2 mouse + RTC live behind the slave PIC (IRQ12 + IRQ8). We don't need either for stage 4 — the keyboard is on IRQ1 (master). Slave PIC bring-up happens in 4b but stays masked until needed.
- **Spurious IRQ7 / IRQ15.** The 8259 generates these on a glitch on the highest-priority unmasked input. Standard handling: read ISR, if bit isn't set, skip the EOI (else the next real IRQ on that vector hangs). 4b's dispatcher honors this.
- **APIC vs PIC.** Modern (post-2005ish) hardware emulates the 8259 well enough for this to work. Not worth the APIC bring-up complexity for a stage-3-grade pc386 port. Revisit if Stage 8's real-hardware tests find a board where the legacy PIC path is broken.
