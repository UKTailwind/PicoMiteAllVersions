/*
 * drivers/serial_16550/serial_16550.c — 16550-compatible UART (COM1).
 *
 * Register layout at base 0x3F8 (when DLAB=0):
 *   +0  RBR/THR — read buffer / transmit holding
 *   +1  IER     — interrupt enable
 *   +2  IIR/FCR — interrupt id / FIFO control
 *   +3  LCR     — line control (DLAB lives at bit 7)
 *   +4  MCR     — modem control
 *   +5  LSR     — line status (bit 5 = THR empty, bit 0 = data ready)
 *   +6  MSR     — modem status
 *   +7  scratch
 *
 * When DLAB=1, ports +0/+1 expose the divisor latch low/high bytes
 * for baud rate programming (115200 / divisor = baud).
 */

#include "serial_16550.h"

#include <stdint.h>

#include "../i8259_pic/i8259_pic.h"
#include "../../ports/pc386/idt.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#define COM1_BASE 0x3F8
#define COM1_RBR (COM1_BASE + 0)
#define COM1_THR (COM1_BASE + 0)
#define COM1_DLL (COM1_BASE + 0) /* when DLAB=1 */
#define COM1_IER (COM1_BASE + 1)
#define COM1_DLM (COM1_BASE + 1) /* when DLAB=1 */
#define COM1_FCR (COM1_BASE + 2)
#define COM1_LCR (COM1_BASE + 3)
#define COM1_MCR (COM1_BASE + 4)
#define COM1_LSR (COM1_BASE + 5)
#define COM1_SCRATCH (COM1_BASE + 7)

#define LCR_DLAB 0x80
#define LCR_8N1 0x03

#define FCR_ENABLE 0x01
#define FCR_CLEAR_RX 0x02
#define FCR_CLEAR_TX 0x04
#define FCR_TRIGGER_14 0xC0

#define MCR_DTR 0x01
#define MCR_RTS 0x02
#define MCR_OUT2 0x08 /* must be set for IRQ delivery on PC */
#define MCR_LOOPBACK 0x10

#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY 0x20

/* 115200 / 3 = 38400 baud. */
#define BAUD_DIVISOR 3

bool serial_init(void) {
    /* Enable the FIFO FIRST. The 16550 starts in 16450 single-byte
     * mode at hardware reset; any bytes arriving before FCR_ENABLE
     * overwrite RBR. With piped test input arriving in microseconds,
     * the difference between "FCR-enable second" and "FCR-enable
     * fifth" is the difference between dropping the first byte and
     * keeping it. */
    outb(COM1_FCR, FCR_ENABLE | FCR_TRIGGER_14);

    /* Disable interrupts during the rest of the setup. */
    outb(COM1_IER, 0x00);

    /* Program baud rate (DLAB=1, write divisor, restore DLAB=0). */
    outb(COM1_LCR, LCR_DLAB);
    outb(COM1_DLL, (uint8_t)(BAUD_DIVISOR & 0xFF));
    outb(COM1_DLM, (uint8_t)((BAUD_DIVISOR >> 8) & 0xFF));

    /* 8 data bits, no parity, 1 stop bit. Note that touching LCR
     * while DLAB=1 to write 8N1 also clears DLAB. */
    outb(COM1_LCR, LCR_8N1);

    /* DTR/RTS asserted, OUT2 set (required for IRQ delivery on PC). */
    outb(COM1_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    return true;
}

void serial_putc(char c) {
    while ((inb(COM1_LSR) & LSR_THR_EMPTY) == 0) {
        /* spin */
    }
    outb(COM1_THR, (uint8_t)c);
}

void serial_puts(const char * s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

/* IRQ-driven RX ring. Filled by serial_irq_handler from the ISR;
 * drained by serial_getc_nonblock. Pre-IRQ-init the ring is empty
 * so the function falls through to the LSR poll. */
#define RX_RING_SIZE 256
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

#define IER_RX_AVAIL 0x01

static void serial_irq_handler(idt_regs_t * r) {
    (void)r;
    /* Drain everything available — IRQ may coalesce multiple bytes. */
    while (inb(COM1_LSR) & LSR_DATA_READY) {
        uint8_t b = inb(COM1_RBR);
        uint16_t next = (rx_head + 1) % RX_RING_SIZE;
        if (next != rx_tail) {
            rx_ring[rx_head] = b;
            rx_head = next;
        }
    }
    pic_eoi(4);
}

void serial_irq_init(void) {
    /* Move whatever's already in the UART FIFO from the boot window
     * (test-harness piped input arrives before this code runs) into
     * our ring before enabling the IRQ. Discarding them — the obvious
     * "drain" pattern — would lose the first command from a piped run. */
    while (inb(COM1_LSR) & LSR_DATA_READY) {
        uint8_t b = inb(COM1_RBR);
        uint16_t next = (rx_head + 1) % RX_RING_SIZE;
        if (next != rx_tail) {
            rx_ring[rx_head] = b;
            rx_head = next;
        }
    }
    idt_register_handler(PIC_VECTOR(4), serial_irq_handler);
    pic_unmask(4);
    outb(COM1_IER, IER_RX_AVAIL);
}

int serial_getc_nonblock(void) {
    if (rx_head != rx_tail) {
        uint8_t b = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % RX_RING_SIZE;
        return (int)b;
    }
    /* No IRQs yet (or no buffered bytes) — fall back to LSR poll. */
    if ((inb(COM1_LSR) & LSR_DATA_READY) == 0) return -1;
    return (int)inb(COM1_RBR);
}

int serial_getc_blocking(void) {
    int c;
    while ((c = serial_getc_nonblock()) < 0) {
        /* spin */
    }
    return c;
}
