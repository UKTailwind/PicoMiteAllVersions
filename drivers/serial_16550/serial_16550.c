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

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#define COM1_BASE       0x3F8
#define COM1_RBR        (COM1_BASE + 0)
#define COM1_THR        (COM1_BASE + 0)
#define COM1_DLL        (COM1_BASE + 0)  /* when DLAB=1 */
#define COM1_IER        (COM1_BASE + 1)
#define COM1_DLM        (COM1_BASE + 1)  /* when DLAB=1 */
#define COM1_FCR        (COM1_BASE + 2)
#define COM1_LCR        (COM1_BASE + 3)
#define COM1_MCR        (COM1_BASE + 4)
#define COM1_LSR        (COM1_BASE + 5)
#define COM1_SCRATCH    (COM1_BASE + 7)

#define LCR_DLAB        0x80
#define LCR_8N1         0x03

#define FCR_ENABLE      0x01
#define FCR_CLEAR_RX    0x02
#define FCR_CLEAR_TX    0x04
#define FCR_TRIGGER_14  0xC0

#define MCR_DTR         0x01
#define MCR_RTS         0x02
#define MCR_OUT2        0x08   /* must be set for IRQ delivery on PC */
#define MCR_LOOPBACK    0x10

#define LSR_DATA_READY  0x01
#define LSR_THR_EMPTY   0x20

/* 115200 / 3 = 38400 baud. */
#define BAUD_DIVISOR    3

bool serial_init(void) {
    /* Disable interrupts during setup. */
    outb(COM1_IER, 0x00);

    /* Program baud rate (DLAB=1, write divisor, restore DLAB=0). */
    outb(COM1_LCR, LCR_DLAB);
    outb(COM1_DLL, (uint8_t) (BAUD_DIVISOR & 0xFF));
    outb(COM1_DLM, (uint8_t) ((BAUD_DIVISOR >> 8) & 0xFF));

    /* 8 data bits, no parity, 1 stop bit. */
    outb(COM1_LCR, LCR_8N1);

    /* Enable + clear FIFO, trigger at 14 bytes. */
    outb(COM1_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIGGER_14);

    /* Loopback self-test: enable loopback, send a byte, read it back. */
    outb(COM1_MCR, MCR_DTR | MCR_RTS | MCR_OUT2 | MCR_LOOPBACK);
    outb(COM1_THR, 0xAE);
    /* Tiny spin to give the byte time to round-trip. */
    for (volatile int i = 0; i < 1000; i++) { (void) i; }
    if ((inb(COM1_LSR) & LSR_DATA_READY) == 0) {
        return false;
    }
    if (inb(COM1_RBR) != 0xAE) {
        return false;
    }

    /* Out of loopback; mark as ready for real I/O. */
    outb(COM1_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    return true;
}

void serial_putc(char c) {
    while ((inb(COM1_LSR) & LSR_THR_EMPTY) == 0) {
        /* spin */
    }
    outb(COM1_THR, (uint8_t) c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

int serial_getc_nonblock(void) {
    if ((inb(COM1_LSR) & LSR_DATA_READY) == 0) return -1;
    return (int) inb(COM1_RBR);
}

int serial_getc_blocking(void) {
    while ((inb(COM1_LSR) & LSR_DATA_READY) == 0) {
        /* spin */
    }
    return (int) inb(COM1_RBR);
}
