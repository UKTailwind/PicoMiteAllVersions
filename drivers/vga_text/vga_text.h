/*
 * drivers/vga_text/vga_text.h — VGA text-mode console (80x25, 16 colors).
 *
 * The early boot console for the pc386 port. Writes directly to the
 * VGA text buffer at physical 0xB8000. Independent of the BIOS — we
 * never call INT 10h, so this works equally well after the bootloader
 * has handed off in protected mode.
 *
 * Driver state is private (a static struct in the .c file). All entry
 * points are reentrancy-safe in the trivial sense that the kernel is
 * single-threaded and interrupts are disabled in early boot.
 */

#ifndef DRIVERS_VGA_TEXT_H
#define DRIVERS_VGA_TEXT_H

#include <stdint.h>

/* Standard CGA/VGA text-mode attribute palette. Each cell is a 16-bit
 * value (low byte ASCII, high byte attribute). The attribute byte
 * splits as: [bg_3 bg_2 bg_1 fg_3 fg_2 fg_1 fg_0]; bit 7 is "blink"
 * on real CGA but doubles fg intensity on most VGA cards. */
enum vga_color {
    VGA_BLACK         = 0x0,
    VGA_BLUE          = 0x1,
    VGA_GREEN         = 0x2,
    VGA_CYAN          = 0x3,
    VGA_RED           = 0x4,
    VGA_MAGENTA       = 0x5,
    VGA_BROWN         = 0x6,
    VGA_LIGHT_GRAY    = 0x7,
    VGA_DARK_GRAY     = 0x8,
    VGA_LIGHT_BLUE    = 0x9,
    VGA_LIGHT_GREEN   = 0xA,
    VGA_LIGHT_CYAN    = 0xB,
    VGA_LIGHT_RED     = 0xC,
    VGA_LIGHT_MAGENTA = 0xD,
    VGA_YELLOW        = 0xE,
    VGA_WHITE         = 0xF,
};

void vga_text_init(void);
void vga_text_clear(void);
void vga_text_set_color(enum vga_color fg, enum vga_color bg);
void vga_text_putc(char c);
void vga_text_puts(const char *s);

#endif
