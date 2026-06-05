/*
 * drivers/draw_rgb332/draw_rgb332.h — 8-bit RGB332 framebuffer draw
 * primitives (the `*256` family).
 *
 * One byte per pixel, packed by RGB332(): bits 7..5 red, 4..2 green,
 * 1..0 blue. Operates on the global WriteBuf at stride HRes over an
 * HRes x VRes surface, so the same primitives serve any RGB332 scanout
 * backend — HDMI SCREENMODE5 (drivers/hdmi) and the ESP32-S3 LCD_CAM
 * VGA path (drivers/vga_lcdcam_s3).
 */

#ifndef DRAW_RGB332_H
#define DRAW_RGB332_H

void DrawRectangle256(int x1, int y1, int x2, int y2, int c);
void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char * bitmap);
void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char * p);
void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char * p);
void DrawPixel256(int x, int y, int c);
void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char * c);
void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char * c);
void ScrollLCD256(int lines);

#endif /* DRAW_RGB332_H */
