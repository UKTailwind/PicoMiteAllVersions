/*
 * drivers/spi_lcd/spi_lcd_nextgen_stub.c — no-op stubs for the NEXTGEN
 * family of buffered SPI-LCD display types (MEM332 framebuffer path).
 *
 * Real implementations live in drivers/spi_lcd/spi_lcd.c, inside the
 * `#if defined(PICOMITE) && defined(rp2350)` gates — only rp2350
 * PICOMITE has the RAM budget for a full 320×240×8-bit RGB332 shadow
 * buffer plus the wider OPTION DISPLAY enum values (NEXTGEN…ST7789C).
 *
 * This stub file is linked into every other target (rp2040 PICOMITE,
 * VGA/HDMI/WEB, host) so Draw.c's restorepanel() can assign these
 * function pointers unconditionally. The runtime check
 * `Option.DISPLAY_TYPE >= NEXTGEN` never matches on non-rp2350 targets
 * — the OPTION command errors if the user tries to set one — so the
 * stubs are compiled but never called.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

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
