/*
 * drivers/spi_lcd/spi_lcd_mem332_stub.c — no-op stubs for the MEM332
 * buffered SPI-LCD family (ILI9488WBUFF, ST7796SPBUFF, ILI9341BUFF,
 * etc.). These display controllers run an 8-bit RGB332 shadow
 * framebuffer in RAM; only ports with the RAM budget for that
 * (currently rp2350 PicoMite) link the real impl in
 * spi_lcd_mem332.c. Every other target links this stub.
 *
 * The stub linker symbols satisfy the function-pointer assignments
 * Draw.c's restorepanel() makes unconditionally; runtime never
 * dispatches to them because the OPTION command rejects the MEM332
 * display-type names on non-MEM332 ports.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_spi_lcd_mem332.h"

void DrawRectangleMEM332(int x1, int y1, int x2, int y2, int c) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)c;
}

void DrawBitmapMEM332(int x1, int y1, int width, int height, int scale,
                      int fc, int bc, unsigned char *bitmap) {
    (void)x1; (void)y1; (void)width; (void)height; (void)scale;
    (void)fc; (void)bc; (void)bitmap;
}

void DrawBufferMEM332(int x1, int y1, int x2, int y2, unsigned char *p) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)p;
}

void ReadBufferMEM332(int x1, int y1, int x2, int y2, unsigned char *buff) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)buff;
}

void DrawBlitBufferMEM332(int x1, int y1, int x2, int y2, unsigned char *p) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)p;
}

void ReadBlitBufferMEM332(int x1, int y1, int x2, int y2, unsigned char *buff) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)buff;
}

void ScrollLCDMEM332(int lines) { (void)lines; }

/* Buffered-display init / option-setter stubs. */
int hal_spi_lcd_mem332_match_option(unsigned char *name) { (void)name; return 0; }
void hal_spi_lcd_mem332_init_display(int display_type) { (void)display_type; }
void hal_spi_lcd_mem332_init_luts(void) {}

extern BYTE (*xchg_byte)(BYTE data_out);

unsigned char hal_spi_lcd_read_response_byte(void) {
    /* Legacy path: 3 SPI swaps, capture the middle byte. */
    xchg_byte(0);
    unsigned char q = xchg_byte(0);
    xchg_byte(0);
    return q;
}

/* RGB332 LUT initializers — declared extern in SPI-LCD.h so they
 * always link. Real impls live in spi_lcd_mem332.c. */
void init_RGB332_to_RGB565_LUT(void) {}
void init_RGB332_to_RGB888_LUT(void) {}
