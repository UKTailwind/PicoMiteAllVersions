/*
 * drivers/i8259_pic/i8259_pic.c — 8259A pair driver.
 */

#include "i8259_pic.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Tiny stall — some old chipsets need a beat between consecutive
 * outb's during init. Writing to an unused port (0x80) is the
 * traditional trick. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01

#define PIC_EOI    0x20
#define PIC_READ_ISR 0x0B

void pic_init(void) {
    /* ICW1: start init sequence in cascade mode (ICW1_ICW4 says ICW4
     * needed). */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: vector base. */
    outb(PIC1_DATA, PIC_REMAP_OFFSET);     io_wait();
    outb(PIC2_DATA, PIC_REMAP_OFFSET + 8); io_wait();

    /* ICW3: cascading wiring — master tells slave it's on IRQ2; slave
     * tells master its cascade ID is 2. */
    outb(PIC1_DATA, 1 << 2);  io_wait();
    outb(PIC2_DATA, 2);       io_wait();

    /* ICW4: 8086/88 mode (vs 8080). */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Mask everything until handlers are registered. Cascade line
     * (IRQ2) MUST stay enabled on the master so slave-side IRQs
     * propagate; we'll let pic_unmask(2) be implicit by leaving it
     * unmasked here. */
    outb(PIC1_DATA, 0xFB);   /* all masked except IRQ2 cascade */
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1u << irq));
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) | (1u << irq));
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

bool pic_is_spurious(uint8_t irq) {
    uint16_t cmd_port = (irq < 8) ? PIC1_CMD : PIC2_CMD;
    outb(cmd_port, PIC_READ_ISR);
    uint8_t isr = inb(cmd_port);
    uint8_t bit = (irq < 8) ? irq : (irq - 8);
    if ((isr & (1 << bit)) == 0) {
        /* Spurious. If on slave side, master still needs an EOI for
         * the cascade IRQ2. */
        if (irq >= 8) outb(PIC1_CMD, PIC_EOI);
        return true;
    }
    return false;
}
