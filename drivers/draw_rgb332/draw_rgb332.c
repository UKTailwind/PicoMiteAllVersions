/*
 * drivers/draw_rgb332/draw_rgb332.c — 8-bit RGB332 framebuffer draw
 * primitives (the `*256` family).
 *
 * Moved verbatim out of drivers/hdmi/hdmi_modes.c so the identical
 * primitives can back both the HDMI SCREENMODE5 path and the ESP32-S3
 * LCD_CAM VGA path without duplication. One byte per pixel, packed by
 * RGB332(); operates on WriteBuf at stride HRes over an HRes x VRes area.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_vga_ops.h"
#include "draw_rgb332.h"

/*
 * @cond
 * The following section will be excluded from the documentation.
 */
void DrawRectangle256(int x1, int y1, int x2, int y2, int c) {
    int y, t;
    uint8_t colour = RGB332(c);
    if (x1 < 0) x1 = 0;
    if (x1 >= HRes) x1 = HRes - 1;
    if (x2 < 0) x2 = 0;
    if (x2 >= HRes) x2 = HRes - 1;
    if (y1 < 0) y1 = 0;
    if (y1 >= VRes) y1 = VRes - 1;
    if (y2 < 0) y2 = 0;
    if (y2 >= VRes) y2 = VRes - 1;
    if (x2 <= x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++) {
        volatile uint8_t * p = WriteBuf + (y * HRes + x1);
        memset((void *)p, colour, x2 - x1 + 1);
    }
}
void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char * bitmap) {
    int i, j, k, m, x, y;
    //    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0) return;
    uint8_t fcolour = RGB332(fc);
    uint8_t bcolour = RGB332(bc);
    for (i = 0; i < height; i++) {            // step thru the font scan line by line
        for (j = 0; j < scale; j++) {         // repeat lines to scale the font
            for (k = 0; k < width; k++) {     // step through each bit in a scan line
                for (m = 0; m < scale; m++) { // repeat pixels to scale in the x axis
                    x = x1 + k * scale + m;
                    y = y1 + i * scale + j;
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes) { // if the coordinates are valid
                        uint8_t * p = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
                        if ((bitmap[((i * width) + k) / 8] >> (((height * width) - ((i * width) + k) - 1) % 8)) & 1) {
                            *p = fcolour;
                        } else {
                            if (bc >= 0) {
                                *p = bcolour;
                            }
                        }
                    }
                }
            }
        }
    }
}

void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char * p) {
    int x, y, t;
    union colourmap {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    uint8_t fcolour;
    uint8_t * pp;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0) x1 = 0;
    if (x1 >= HRes) x1 = HRes - 1;
    if (x2 < 0) x2 = 0;
    if (x2 >= HRes) x2 = HRes - 1;
    if (y1 < 0) y1 = 0;
    if (y1 >= VRes) y1 = VRes - 1;
    if (y2 < 0) y2 = 0;
    if (y2 >= VRes) y2 = VRes - 1;
    for (y = y1; y <= y2; y++) {
        for (x = x1; x <= x2; x++) {
            c.rgbbytes[0] = *p++; //this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            fcolour = RGB332(c.rgb);
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
            *pp = fcolour;
        }
    }
}
void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char * p) {
    int x, y, t;
    uint8_t c;
    uint8_t *pp, *qq = (uint8_t *)p;
    // make sure the coordinates are kept within the display area
    if (x2 <= x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    for (y = y1; y <= y2; y++) {
        for (x = x1; x <= x2; x++) {
            if (x >= 0 && x < HRes && y >= 0 && y < VRes) {
                pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
                c = *qq++;
                if (c != sprite_transparent || blank == -1) *pp = c;
            }
        }
    }
}
void DrawPixel256(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= HRes || y >= VRes) return;
    uint8_t colour = RGB332(c);
    uint8_t * p = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
    *p = colour;
}
void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char * c) {
    int x, y, t;
    uint8_t * pp;
    if (x2 <= x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0) xx1 = 0;
    if (x1 >= HRes) xx1 = HRes - 1;
    if (x2 < 0) xx2 = 0;
    if (x2 >= HRes) xx2 = HRes - 1;
    if (y1 < 0) yy1 = 0;
    if (y1 >= VRes) yy1 = VRes - 1;
    if (y2 < 0) yy2 = 0;
    if (y2 >= VRes) yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++) {
        for (x = xx1; x <= xx2; x++) {
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
            t = hal_vga_ops_layer_merge_rgb8(*pp, x, y);
            *c++ = ((t & 0x3) << 6);
            *c++ = (((t >> 2) & 0x7) << 5);
            *c++ = (((t >> 5) & 0x7) << 5);
        }
    }
}
void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char * c) {
    int x, y, t;
    uint8_t *pp, *qq = (uint8_t *)c;
    if (x2 <= x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 <= y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    int xx1 = x1, yy1 = y1, xx2 = x2, yy2 = y2;
    if (x1 < 0) xx1 = 0;
    if (x1 >= HRes) xx1 = HRes - 1;
    if (x2 < 0) xx2 = 0;
    if (x2 >= HRes) xx2 = HRes - 1;
    if (y1 < 0) yy1 = 0;
    if (y1 >= VRes) yy1 = VRes - 1;
    if (y2 < 0) yy2 = 0;
    if (y2 >= VRes) yy2 = VRes - 1;
    for (y = yy1; y <= yy2; y++) {
        for (x = xx1; x <= xx2; x++) {
            pp = (uint8_t *)((uint32_t)(WriteBuf + y * HRes + x));
            *qq++ = *pp;
        }
    }
}
void ScrollLCD256(int lines) {
    if (lines == 0) return;
    if (lines >= 0) {
        for (int i = 0; i < VRes - lines; i++) {
            int d = i * HRes, s = (i + lines) * HRes;
            for (int c = 0; c < (HRes); c++) WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, VRes - lines, HRes - 1, VRes - 1, PromptBC); // erase the lines to be scrolled off
    } else {
        lines = -lines;
        for (int i = VRes - 1; i >= lines; i--) {
            int d = i * HRes, s = (i - lines) * HRes;
            for (int c = 0; c < (HRes << 1); c++) WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
/*  @endcond */
