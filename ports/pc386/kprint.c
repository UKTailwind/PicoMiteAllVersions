/*
 * ports/pc386/kprint.c — kernel print helpers (see kprint.h).
 *
 * Mirrors every write to both VGA text mode and serial COM1. Until a
 * later stage adds console selection, that's the contract.
 */

#include "kprint.h"

#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"

void kputc(char c) {
    serial_putc(c);
    if (c == '\n') {
        vga_text_putc('\r');
    }
    vga_text_putc(c);
}

void kputs(const char * s) {
    while (*s) {
        kputc(*s++);
    }
}

void kputhex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 0xF];
    }
    buf[10] = '\0';
    kputs(buf);
}

void kputhex64(uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xF];
    }
    buf[18] = '\0';
    kputs(buf);
}

void kputu32(uint32_t v) {
    /* 4294967295 is 10 digits. */
    char buf[11];
    int n = 0;
    if (v == 0) {
        kputc('0');
        return;
    }
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        kputc(buf[--n]);
    }
}

void kputu64(uint64_t v) {
    /* 2^64 = 20 digits. */
    char buf[21];
    int n = 0;
    if (v == 0) {
        kputc('0');
        return;
    }
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        kputc(buf[--n]);
    }
}
