/*
 * drivers/hdmi/hdmi_modes.c — HDMI-specific SCREENMODE4/5 drawing
 * primitives and HDMI-mode cmd_tile / cmd_map bodies.
 *
 * Extracted from drivers/vga_pio/vga_mode_ops.c's `#ifdef HDMI`
 * branch. Linked only on HDMI / HDMIUSB targets; VGA-without-HDMI
 * ports link drivers/vga_pio/vga_qvga_modes.c instead (the other
 * side of the `#ifndef HDMI / #else / #endif` split that used to
 * be in vga_mode_ops.c).
 *
 * Contents:
 *   DrawRectangle555 / DrawBitmap555 / DrawBuffer555 / DrawBuffer555Fast
 *   ReadBuffer555 / ReadBuffer555Fast / DrawPixel555 / ScrollLCD555
 *     16-bit RGB555 framebuffer primitives (SCREENMODE4)
 *   DrawRectangle256 / DrawBitmap256 / DrawBuffer256 / ReadBuffer256
 *   DrawPixel256 / ScrollLCD256
 *     8-bit RGB332 framebuffer primitives (SCREENMODE5)
 *   cmd_tile / cmd_map
 *     HDMI-mode BASIC command bodies (multi-mode, delegate on screen mode)
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_vga_ops.h"

/* VGA palette / mode-state globals defined in Draw.c + drivers/vga_pio/. */
extern uint32_t remap555[256];
extern uint32_t remap332[256];
extern uint16_t remap256[256];
extern uint32_t map16quads[16];
extern uint32_t map16pairs[16];
extern const int CMM1map[16];

/*
 * @cond
 * The following section will be excluded from the documentation.
 */
void DrawRectangle555(int x1, int y1, int x2, int y2, int c) {
    int x, y, t;
    uint16_t col = ((c & 0xf8) >> 3) | ((c & 0xf800) >> 6) | ((c & 0xf80000) >> 9);
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
        uint16_t * p = (uint16_t *)((uint8_t *)(WriteBuf + ((y * HRes + x1) * 2)));
        for (x = x1; x <= x2; x++) {
            *p++ = col;
        }
    }
}
void DrawBitmap555(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char * bitmap) {
    int i, j, k, m, x, y;
    //    unsigned char mask;
    if (x1 >= HRes || y1 >= VRes || x1 + width * scale < 0 || y1 + height * scale < 0) return;
    uint16_t fcolour = RGB555(fc);
    uint16_t bcolour = RGB555(bc);
    for (i = 0; i < height; i++) {            // step thru the font scan line by line
        for (j = 0; j < scale; j++) {         // repeat lines to scale the font
            for (k = 0; k < width; k++) {     // step through each bit in a scan line
                for (m = 0; m < scale; m++) { // repeat pixels to scale in the x axis
                    x = x1 + k * scale + m;
                    y = y1 + i * scale + j;
                    if (x >= 0 && x < HRes && y >= 0 && y < VRes) { // if the coordinates are valid
                        uint16_t * p = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
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

void DrawBuffer555(int x1, int y1, int x2, int y2, unsigned char * p) {
    int x, y, t;
    union colourmap {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    uint16_t fcolour;
    uint16_t * pp;
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
            fcolour = RGB555(c.rgb);
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            *pp = fcolour;
        }
    }
}
void DrawBuffer555Fast(int x1, int y1, int x2, int y2, int blank, unsigned char * p) {
    int x, y, t;
    uint16_t c;
    uint16_t *pp, *qq = (uint16_t *)p;
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
                pp = (uint16_t *)(WriteBuf + (y * (HRes << 1)) + (x << 1));
                c = *qq++;
                if (c != sprite_transparent || blank == -1) *pp = c;
            }
        }
    }
}
void DrawPixel555(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= HRes || y >= VRes) return;
    uint16_t colour = RGB555(c);
    uint16_t * p = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
    *p = colour;
}
void ReadBuffer555(int x1, int y1, int x2, int y2, unsigned char * c) {
    int x, y, t;
    uint16_t * pp;
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
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            t = *pp;
            *c++ = ((t & 0x1F) << 3);
            *c++ = (((t >> 5) & 0x1F) << 3);
            *c++ = (((t >> 10) & 0x1F) << 3);
        }
    }
}
void ReadBuffer555Fast(int x1, int y1, int x2, int y2, unsigned char * c) {
    int x, y, t;
    uint16_t *pp, *qq = (uint16_t *)c;
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
            pp = (uint16_t *)(((uint32_t)WriteBuf) + (y * (HRes << 1)) + (x << 1));
            *qq++ = *pp;
        }
    }
}
void ScrollLCD555(int lines) {
    if (lines == 0) return;
    if (lines >= 0) {
        for (int i = 0; i < VRes - lines; i++) {
            int d = i * (HRes << 1), s = (i + lines) * (HRes << 1);
            for (int c = 0; c < (HRes << 1); c++) WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, VRes - lines, HRes - 1, VRes - 1, PromptBC); // erase the lines to be scrolled off
    } else {
        lines = -lines;
        for (int i = VRes - 1; i >= lines; i--) {
            int d = i * (HRes << 1), s = (i - lines) * (HRes << 1);
            for (int c = 0; c < (HRes << 1); c++) WriteBuf[d + c] = WriteBuf[s + c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
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

void cmd_tile(void) {
    unsigned char * tp;
    uint32_t bcolour = 0xFFFFFFFF, fcolour = 0xFFFFFFFF;
    int xlen = 1, ylen = 1;
    if (DISPLAY_TYPE != SCREENMODE1) error("Invalid for this screen mode");
    if (checkstring(cmdline, (unsigned char *)"RESET")) {
        fcolour = (FullColour) ? RGB555(Option.DefaultFC) : RGB332(Option.DefaultFC);
        bcolour = (FullColour) ? RGB555(Option.DefaultBC) : RGB332(Option.DefaultBC);
        for (int x = 0; x < X_TILE; x++) {
            for (int y = 0; y < Y_TILE; y++) {
                if (FullColour) {
                    if (fcolour != 0xFFFFFFFF) tilefcols[y * X_TILE + x] = fcolour;
                    if (bcolour != 0xFFFFFFFF) tilebcols[y * X_TILE + x] = bcolour;
                } else {
                    if (fcolour != 0xFFFFFFFF) tilefcols_w[y * X_TILE + x] = fcolour;
                    if (bcolour != 0xFFFFFFFF) tilebcols_w[y * X_TILE + x] = bcolour;
                }
            }
        }
    } else if ((tp = checkstring(cmdline, (unsigned char *)"HEIGHT"))) {
        ytileheight = getint(tp, 8, VRes);
        Y_TILE = VRes / ytileheight;
        if (VRes % ytileheight) Y_TILE++;
        ClearScreen(Option.DefaultBC);
    } else {
        getargs(&cmdline, 11, (unsigned char *)",");
        if (!(DISPLAY_TYPE == SCREENMODE1)) return;
        if (argc < 5) error("Syntax");
        int x = getint(argv[0], 0, X_TILE - 1);
        int y = getint(argv[2], 0, Y_TILE - 1);
        int tilebcolour, tilefcolour;
        if (*argv[4]) {
            tilefcolour = getColour((char *)argv[4], 0);
            fcolour = (FullColour) ? RGB555(tilefcolour) : RGB332(tilefcolour);
        }
        if (argc >= 7 && *argv[6]) {
            tilebcolour = getColour((char *)argv[6], 0);
            bcolour = (FullColour) ? RGB555(tilebcolour) : RGB332(tilebcolour);
        }
        if (argc >= 9 && *argv[8]) {
            xlen = getint(argv[8], 1, X_TILE - x);
        }
        if (argc >= 11 && *argv[10]) {
            ylen = getint(argv[10], 1, Y_TILE - y);
        }
        for (int xp = x; xp < x + xlen; xp++) {
            for (int yp = y; yp < y + ylen; yp++) {
                if (FullColour) {
                    if (fcolour != 0xFFFFFFFF) tilefcols[yp * X_TILE + xp] = (uint16_t)fcolour;
                    if (bcolour != 0xFFFFFFFF) tilebcols[yp * X_TILE + xp] = (uint16_t)bcolour;
                } else {
                    if (fcolour != 0xFFFFFFFF) tilefcols_w[yp * X_TILE + xp] = (uint8_t)fcolour;
                    if (bcolour != 0xFFFFFFFF) tilebcols_w[yp * X_TILE + xp] = (uint8_t)bcolour;
                }
            }
        }
    }
}
void cmd_map(void) {
    unsigned char * p;
    if (!(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3 || DISPLAY_TYPE == SCREENMODE5)) error("Invalid for this screen mode");
    if ((p = checkstring(cmdline, (unsigned char *)"RESET"))) {
        mapreset();
    } else if (checkstring(cmdline, (unsigned char *)"GRAYSCALE") || checkstring(cmdline, (unsigned char *)"GREYSCALE")) {
        while (v_scanline != 0) {
        }
        for (int i = 1; i <= 32; i++) {
            int j = i * 8 - (8 - i / 4 + 1);
            if (j < 0) j = 0;
            map256[i - 1] = remap256[i - 1] = RGB555(j * 65536 + j * 256 + j);
            map256[i + 32 - 1] = remap256[i + 32 - 1] = RGB555(j);
            map256[i + 64 - 1] = remap256[i + 64 - 1] = RGB555(j * 256);
            map256[i + 96 - 1] = remap256[i + 96 - 1] = RGB555(j * 256 + j);
            map256[i + 128 - 1] = remap256[i + 128 - 1] = RGB555(j * 65536);
            map256[i + 160 - 1] = remap256[i + 160 - 1] = RGB555(j * 65536 + j);
            map256[i + 192 - 1] = remap256[i + 192 - 1] = RGB555(j * 65536 + j * 256);
            map256[i + 224 - 1] = remap256[i + 224 - 1] = RGB555(j * 65536 + j * 256 + j);
        }
        for (int i = 1; i <= 16; i++) {
            int j = i * 16 - (16 - i + 1);
            map16quads[i - 1] = remap332[i - 1] = RGB332(j * 65536 + j * 256 + j) | (RGB332(j * 65536 + j * 256 + j) << 8) | (RGB332(j * 65536 + j * 256 + j) << 16) | (RGB332(j * 65536 + j * 256 + j) << 24);
            map16pairs[i - 1] = remap555[i - 1] = (RGB555(j * 65536 + j * 256 + j) | (RGB555(j * 65536 + j * 256 + j) << 16));
        }
    } else if ((p = checkstring(cmdline, (unsigned char *)"MAXIMITE"))) {
        while (v_scanline != 0) {
        }
        for (int i = 0; i < 16; i++) map256[i] = remap256[i] = RGB555(CMM1map[i]);
        for (int i = 0; i < 16; i++) {
            map16quads[i] = remap332[i] = RGB332(CMM1map[i]) | (RGB332(CMM1map[i]) << 8) | (RGB332(CMM1map[i]) << 16) | (RGB332(CMM1map[i]) << 24);
            map16pairs[i] = remap555[i] = RGB555(CMM1map[i]) | (RGB555(CMM1map[i]) << 8);
        }
    } else if ((p = checkstring(cmdline, (unsigned char *)"SET"))) {
        while (v_scanline != 0) {
        }
        for (int i = 0; i < 256; i++) map256[i] = remap256[i];
        for (int i = 0; i < 16; i++) {
            map16pairs[i] = remap555[i];
            map16quads[i] = remap332[i];
        }
    } else {
        int cl = getint(cmdline, 0, 255);
        if (DISPLAY_TYPE != SCREENMODE5 && cl > 15) error("Mode supports 16 colours (0-15)");
        while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
        if (!*cmdline) error("Invalid syntax");
        ++cmdline;
        if (!*cmdline) error("Invalid syntax");
        int col = getColour((char *)cmdline, 0);
        remap256[cl] = RGB555(col);
        remap555[cl] = RGB555(col) | (RGB555(col) << 16);
        remap332[cl] = RGB332(col) | (RGB332(col) << 8) | (RGB332(col) << 16) | (RGB332(col) << 24);
    }
}
