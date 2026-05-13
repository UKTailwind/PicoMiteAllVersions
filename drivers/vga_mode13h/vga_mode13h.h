/*
 * drivers/vga_mode13h/vga_mode13h.h - PC386 linear framebuffer graphics.
 *
 * Bare-metal 386 path: expose MMBasic's legacy Draw.c function-pointer
 * surface over one boot-selected framebuffer.
 */

#ifndef DRIVERS_VGA_MODE13H_H
#define DRIVERS_VGA_MODE13H_H

#include <stdint.h>

void vga_mode13h_init(void);
void vga_mode13h_set_mode(int mode, int clear);
uint32_t vga_mode13h_get_pixel(int x, int y);
void vga_mode13h_fastgfx_create(void);
void vga_mode13h_fastgfx_close(void);
void vga_mode13h_fastgfx_reset(void);
void vga_mode13h_fastgfx_set_fps(int fps);
void vga_mode13h_fastgfx_swap(void);
void vga_mode13h_fastgfx_sync(void);

#endif /* DRIVERS_VGA_MODE13H_H */
