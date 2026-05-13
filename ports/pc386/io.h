/*
 * ports/pc386/io.h — x86 port I/O helpers.
 *
 * Inline asm wrappers for IN/OUT instructions. Used by drivers that
 * talk to legacy PC peripherals at fixed I/O ports (PIT, 8259, 8042,
 * VGA CRTC, ATA, etc.).
 *
 * Header-only, port-private. Drivers under drivers/<x>/ that need
 * these declare local copies (see drivers/serial_16550/serial_16550.c)
 * to avoid having a per-port include path leak into a generic driver.
 */

#ifndef PORTS_PC386_IO_H
#define PORTS_PC386_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* I/O delay: writing to unused port 0x80 wastes a bus cycle and is the
 * canonical way to add a ~1 µs delay after legacy port writes that
 * need settling time (e.g. PIC remap, PIT programming). */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif
