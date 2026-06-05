/*
 * drivers/i8259_pic/i8259_pic.h — 8259A programmable interrupt
 * controller pair (master + slave).
 *
 * Default mapping after BIOS leaves master IRQs at 0x08-0x0F and slave
 * at 0x70-0x77 — the master range collides with CPU exception vectors
 * (0x08 = double fault, 0x0F = reserved, etc.) in protected mode.
 * pic_remap moves them to the standard "remapped" positions:
 *   master 0x20-0x27  (IRQ0..IRQ7)
 *   slave  0x28-0x2F  (IRQ8..IRQ15)
 *
 * After remap, all IRQs are masked. Enable a specific IRQ with
 * pic_unmask(irq); send the End-Of-Interrupt with pic_eoi(irq) at the
 * end of every handler.
 */
#ifndef DRIVERS_I8259_PIC_H
#define DRIVERS_I8259_PIC_H

#include <stdint.h>
#include <stdbool.h>

/* Vector base after remap. */
#define PIC_REMAP_OFFSET 0x20
#define PIC_VECTOR(irq) (PIC_REMAP_OFFSET + (irq))

/* Initialise both PICs at the standard offsets, mask every line.
 * Safe to call once at boot. */
void pic_init(void);

void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

/* End-Of-Interrupt — call at the end of every IRQ handler. Sends the
 * EOI to the slave (if irq >= 8) and the master. */
void pic_eoi(uint8_t irq);

/* Spurious-IRQ filter: returns true if the firing IRQ is a glitch
 * (line 7 on master / line 15 on slave with no real ISR bit set).
 * Spurious IRQs need NO EOI on the master (skipping it would corrupt
 * the next real IRQ on the same line); the slave still needs a master
 * EOI on a slave-side spurious. Call from the IRQ7 / IRQ15 handlers
 * before doing real work. */
bool pic_is_spurious(uint8_t irq);

#endif
