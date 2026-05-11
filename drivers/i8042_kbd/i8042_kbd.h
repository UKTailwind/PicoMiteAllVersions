/*
 * drivers/i8042_kbd/i8042_kbd.h — PS/2 (8042) keyboard front-end.
 *
 * IRQ1 fires on every scancode byte. The handler reads port 0x60 and
 * pushes the raw byte into a small ring; consumers (kbd_get_scancode
 * in 4c, the scancode→ASCII decoder in 4d) drain it.
 *
 * No FIFO drain inside the IRQ — keep the handler tiny so we can't
 * stall other IRQs (when more land in 4f).
 */
#ifndef DRIVERS_I8042_KBD_H
#define DRIVERS_I8042_KBD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialise: register IRQ1 handler with the IDT, unmask IRQ1 on the
 * PIC. Caller's responsible for the order — must be after pic_init
 * + idt_init. */
void kbd_init(void);

/* Pop the next raw scancode from the ring, or return -1 if empty. */
int  kbd_get_scancode(void);

/* True if the ring has bytes waiting. */
bool kbd_has_scancode(void);

/* Cooked input: drain scancodes and return one decoded key, or -1 if
 * nothing's ready. Returns:
 *   - ASCII code (0x20..0x7E) for printable keys (with shift / caps
 *     applied)
 *   - one of the special key codes (UP, DOWN, LEFT, RIGHT, HOME, END,
 *     PUP, PDOWN, INSERT, DEL, F1..F12, BKSP, ENTER, TAB, ESC) for
 *     non-printables. Codes match Hardware_Includes.h's defines so
 *     MMBasic Editor sees the same values it expects on every other
 *     port.
 * Modifier keys (Shift / Ctrl / Alt / Caps) are tracked internally
 * and never returned. */
int  kbd_get_key(void);

#endif
