/*
 * drivers/i8042_kbd/i8042_kbd.c — IRQ1 scancode collector.
 */

#include <stdint.h>

#include "i8042_kbd.h"
#include "../i8259_pic/i8259_pic.h"
#include "../../ports/pc386/idt.h"

#define KBD_DATA_PORT  0x60

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#define RING_SIZE 128
static volatile uint8_t  ring[RING_SIZE];
static volatile uint16_t ring_head;     /* next write index (filled by ISR) */
static volatile uint16_t ring_tail;     /* next read index  (consumed by mainline) */

static void kbd_irq_handler(idt_regs_t *r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);
    uint16_t next = (ring_head + 1) % RING_SIZE;
    if (next != ring_tail) {     /* drop on overflow rather than overwrite */
        ring[ring_head] = sc;
        ring_head = next;
    }
    pic_eoi(1);
}

void kbd_init(void) {
    /* Drain anything sitting in the data port from BIOS (some firmware
     * leaves a key code there; if we don't read it, IRQ1 never fires). */
    while (1) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        (void)inb(KBD_DATA_PORT);
    }
    idt_register_handler(PIC_VECTOR(1), kbd_irq_handler);
    pic_unmask(1);
}

int kbd_get_scancode(void) {
    if (ring_head == ring_tail) return -1;
    uint8_t sc = ring[ring_tail];
    ring_tail = (ring_tail + 1) % RING_SIZE;
    return sc;
}

bool kbd_has_scancode(void) {
    return ring_head != ring_tail;
}
