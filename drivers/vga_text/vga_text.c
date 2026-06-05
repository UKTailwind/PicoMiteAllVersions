/*
 * drivers/vga_text/vga_text.c — VGA text-mode console (80x25, 16 colors).
 *
 * Buffer layout (physical 0xB8000):
 *   row 0:  cell[0] cell[1] ... cell[79]    (80 cells × 2 bytes = 160 bytes)
 *   row 1:  cell[0] cell[1] ... cell[79]
 *   ...
 *   row 24: cell[0] cell[1] ... cell[79]
 *
 * Each cell is a 16-bit value: low byte ASCII, high byte attribute.
 *
 * No BIOS calls — we own the framebuffer directly. The hardware cursor
 * register is updated via I/O ports 0x3D4/0x3D5 (CRTC), so a real VGA
 * card draws a blinking caret at the right place. This is purely
 * cosmetic for QEMU but matters on real hardware.
 */

#include "vga_text.h"

#include <stddef.h>

/* Forward decl — implemented inline in ports/pc386/io.h. To avoid a
 * cross-port include from a driver TU, we just redeclare the wrapper
 * we need. Same prototype as ports/pc386/io.h. */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5

static struct {
    uint8_t row;
    uint8_t col;
    uint8_t attr;
} vga = {
    .row = 0,
    .col = 0,
    .attr = (VGA_BLACK << 4) | VGA_LIGHT_GRAY,
};

static inline uint16_t cell(char c) {
    return ((uint16_t)vga.attr << 8) | (uint8_t)c;
}

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)vga.row * VGA_COLS + vga.col;
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll_up_one(void) {
    /* Move rows 1..24 up by one row. */
    for (size_t r = 1; r < VGA_ROWS; r++) {
        for (size_t c = 0; c < VGA_COLS; c++) {
            VGA_BUFFER[(r - 1) * VGA_COLS + c] = VGA_BUFFER[r * VGA_COLS + c];
        }
    }
    /* Clear the new bottom row. */
    for (size_t c = 0; c < VGA_COLS; c++) {
        VGA_BUFFER[(VGA_ROWS - 1) * VGA_COLS + c] = cell(' ');
    }
    vga.row = VGA_ROWS - 1;
}

void vga_text_init(void) {
    vga.row = 0;
    vga.col = 0;
    vga.attr = (VGA_BLACK << 4) | VGA_LIGHT_GRAY;
    vga_text_clear();
}

void vga_text_clear(void) {
    for (size_t i = 0; i < (size_t)VGA_COLS * VGA_ROWS; i++) {
        VGA_BUFFER[i] = cell(' ');
    }
    vga.row = 0;
    vga.col = 0;
    update_hw_cursor();
}

void vga_text_set_color(enum vga_color fg, enum vga_color bg) {
    vga.attr = (uint8_t)((bg & 0xF) << 4) | (uint8_t)(fg & 0xF);
}

void vga_text_putc(char c) {
    switch (c) {
    case '\n':
        vga.col = 0;
        vga.row++;
        break;
    case '\r':
        vga.col = 0;
        break;
    case '\b':
        if (vga.col > 0) {
            vga.col--;
            VGA_BUFFER[(size_t)vga.row * VGA_COLS + vga.col] = cell(' ');
        }
        break;
    case '\t':
        do {
            vga_text_putc(' ');
        } while (vga.col % 8 != 0 && vga.col < VGA_COLS);
        return;
    default:
        VGA_BUFFER[(size_t)vga.row * VGA_COLS + vga.col] = cell(c);
        vga.col++;
        break;
    }

    if (vga.col >= VGA_COLS) {
        vga.col = 0;
        vga.row++;
    }
    if (vga.row >= VGA_ROWS) {
        scroll_up_one();
    }
    update_hw_cursor();
}

void vga_text_puts(const char * s) {
    while (*s) {
        vga_text_putc(*s++);
    }
}
