/*
 * drivers/vga_pio/vga_ops.c — real VGA-specific branches lifted out of
 * Draw.c's ClearScreen / gfx_cls helpers.
 *
 * Linked only into PICOMITEVGA variants (VGA + HDMI).  HDMI vs
 * non-HDMI mode dispatch stays inside this file using local #ifdef
 * HDMI — port files are permitted local target-macro ifdefs.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_vga_ops.h"

/* Declared in Draw.c unconditionally; only PICOMITEVGA ever toggles it
 * true (via BMP-save code paths). */
extern bool mergedread;

int hal_vga_ops_handle_cls(int c) {
    if (!(DISPLAY_TYPE == SCREENMODE1 && WriteBuf == DisplayBuf)) return 0;
    DrawRectangle(0, 0, HRes - 1, VRes - 1, 0);
#ifdef HDMI
    memset((void *)WriteBuf, 0, ScreenSize);
    if (FullColour) {
        uint16_t bcolour = RGB555(c);
        for (int x = 0; x < X_TILE; x++) {
            for (int y = 0; y < Y_TILE; y++) {
                tilefcols[y * X_TILE + x] = RGB555(gui_fcolour);
                tilebcols[y * X_TILE + x] = bcolour;
            }
        }
    } else {
        uint8_t bcolour = RGB332(c);
        for (int x = 0; x < X_TILE; x++) {
            for (int y = 0; y < Y_TILE; y++) {
                tilefcols_w[y * X_TILE + x] = RGB332(gui_fcolour);
                tilebcols_w[y * X_TILE + x] = bcolour;
            }
        }
    }
    CurrentX = CurrentY = 0;
#else
    memset((void *)WriteBuf, 0, ScreenSize);
    for (int x = 0; x < X_TILE; x++) {
        for (int y = 0; y < Y_TILE; y++) {
            tilefcols[y * X_TILE + x] = RGB121pack(gui_fcolour);
            tilebcols[y * X_TILE + x] = RGB121pack(c);
        }
    }
#endif
    return 1;
}

int hal_vga_ops_handle_tile_cls(int colour) {
    if (!(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3)) return 0;
    int fc = colour;
    unsigned char fcolour = RGB121(fc);
    fcolour |= (fcolour << 4);
    memset((void *)WriteBuf, fcolour, ScreenSize);
    return 1;
}

int hal_vga_ops_handle_layer_clear(void) {
    if ((WriteBuf == LayerBuf && (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3) && LayerBuf != DisplayBuf)
     || (WriteBuf == SecondLayer && (DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3) && SecondLayer != DisplayBuf)) {
        uint8_t colourv = (WriteBuf == LayerBuf
                             ? transparent | (transparent << 4)
                             : transparents | (transparents << 4));
        memset((void *)WriteBuf, colourv, HRes * VRes / 2);
        return 1;
    }
#ifdef HDMI
    if (WriteBuf == LayerBuf && DISPLAY_TYPE == SCREENMODE5 && LayerBuf != DisplayBuf) {
        memset((void *)WriteBuf, transparent, HRes * VRes);
        return 1;
    }
    if ((void *)WriteBuf == LayerBuf && DISPLAY_TYPE == SCREENMODE4 && LayerBuf != DisplayBuf) {
        uint16_t *p = (uint16_t *)WriteBuf;
        for (int i = 0; i < HRes * VRes; i++) *p++ = RGBtransparent;
        return 1;
    }
#endif
    return 0;
}

uint8_t hal_vga_ops_layer_merge_byte(uint8_t primary, int x, int y) {
    if (!(WriteBuf == DisplayBuf && LayerBuf != DisplayBuf && LayerBuf != NULL)) return primary;
    uint8_t layer = *(uint8_t *)(((uintptr_t)LayerBuf) + (y * (HRes >> 1)) + (x >> 1));
    if (!mergedread) return primary;
    uint8_t hi = (primary >> 4) & 0xF;
    uint8_t lo = primary & 0xF;
    uint8_t lhi = (layer >> 4) & 0xF;
    uint8_t llo = layer & 0xF;
    if (lhi != transparent) hi = lhi;
    if (llo != transparent) lo = llo;
    return (uint8_t)((hi << 4) | lo);
}

void hal_vga_ops_tile_colour(int x, int y, int *front, int *back) {
    int tile = (x / 8) + (y / ytileheight) * X_TILE;
    *back  = RGB121map[tilebcols[tile] & 0xF];
    *front = RGB121map[tilefcols[tile] & 0xF];
}

void hal_vga_ops_scroll_tile_colours(int lines) {
    int ya = ytileheight;
    if (lines == 0) return;
    if (lines > 0) {
        if ((lines % ya) != 0) return;
        int offset = lines / ya;
        for (int y = 0; y < Y_TILE - offset; y++) {
            int d = y * X_TILE, s = (y + offset) * X_TILE;
            for (int x = 0; x < X_TILE; x++) {
#ifdef HDMI
                if (FullColour) {
#endif
                    tilefcols[d + x] = tilefcols[s + x];
                    tilebcols[d + x] = tilebcols[s + x];
#ifdef HDMI
                } else {
                    tilefcols_w[d + x] = tilefcols_w[s + x];
                    tilebcols_w[d + x] = tilebcols_w[s + x];
                }
#endif
            }
        }
    } else {
        lines = -lines;
        if ((lines % ya) != 0) return;
        int offset = lines / ya;
        for (int y = Y_TILE - 1; y >= offset; y--) {
            int d = y * X_TILE, s = (y - offset) * X_TILE;
            for (int x = 0; x < X_TILE; x++) {
#ifdef HDMI
                if (FullColour) {
#endif
                    tilefcols[d + x] = tilefcols[s + x];
                    tilebcols[d + x] = tilebcols[s + x];
#ifdef HDMI
                } else {
                    tilefcols_w[d + x] = tilefcols_w[s + x];
                    tilebcols_w[d + x] = tilebcols_w[s + x];
                }
#endif
            }
        }
    }
}

int hal_vga_ops_fb2_tilematch(int x1, int y1, int w_px, int h_px) {
    const int xa = 8;
    const int ya = ytileheight;
    return (x1 % xa == 0 && y1 % ya == 0 && w_px % xa == 0 && h_px % ya == 0);
}

void hal_vga_ops_fb2_fill_tile_colours(int x1, int y1, int w_px, int h_px, int fc, int bc) {
    const int xa = 8;
    const int ya = ytileheight;
    int xt = x1 / xa, yt = y1 / ya;
    int w = w_px / xa, h = h_px / ya;
    int fcolour, bcolour;
#ifdef HDMI
    fcolour = FullColour ? RGB555(fc) : RGB332(fc);
    bcolour = FullColour ? RGB555(bc) : RGB332(bc);
    if (FullColour) {
        for (int yy = yt; yy < yt + h; yy++)
            for (int xx = xt; xx < xt + w; xx++) {
                tilefcols[yy * X_TILE + xx] = (uint16_t)fcolour;
                tilebcols[yy * X_TILE + xx] = (uint16_t)bcolour;
            }
    } else {
        for (int yy = yt; yy < yt + h; yy++)
            for (int xx = xt; xx < xt + w; xx++) {
                tilefcols_w[yy * X_TILE + xx] = (uint8_t)fcolour;
                tilebcols_w[yy * X_TILE + xx] = (uint8_t)bcolour;
            }
    }
#else
    fcolour = RGB121pack(fc);
    bcolour = RGB121pack(bc);
    for (int yy = yt; yy < yt + h; yy++)
        for (int xx = xt; xx < xt + w; xx++) {
            tilefcols[yy * X_TILE + xx] = (uint16_t)fcolour;
            tilebcols[yy * X_TILE + xx] = (uint16_t)bcolour;
        }
#endif
}

volatile unsigned char *hal_vga_ops_fb_n_target(void) {
    return (volatile unsigned char *)DisplayBuf;
}

volatile unsigned char *hal_vga_ops_fb_t_target(void) {
#ifdef rp2350
    return (volatile unsigned char *)SecondLayer;
#else
    return NULL;
#endif
}

int hal_vga_ops_fb_t_supported(void) {
#ifdef rp2350
    return 1;
#else
    return 0;
#endif
}

uint8_t hal_vga_ops_layer_merge_rgb8(uint8_t primary, int x, int y) {
    if (!(WriteBuf == DisplayBuf && LayerBuf != DisplayBuf && LayerBuf != NULL)) return primary;
    uint8_t layer = *(uint8_t *)(((uintptr_t)LayerBuf) + (y * HRes) + x);
    if (!mergedread) return primary;
    if (layer == transparent) return primary;
    return layer;
}

/* Display_Refresh on VGA: the scanline-DMA runs continuously so there
 * is no flush step. SPI-LCD ports implement the real thing in
 * drivers/spi_lcd/spi_lcd.c. */
void Display_Refresh(void) { }

void hal_vga_ops_wait_scanline_zero(void) {
#ifdef HDMI
    extern volatile int32_t v_scanline;
    while (v_scanline != 0) { }
#else
    extern volatile int QVgaScanLine;
    while (QVgaScanLine != 0) { }
#endif
}

void hal_vga_ops_retile_for_font(void) {
    if (!(gui_font_height >= 8 && (gui_font_width % 8) == 0)) return;
    ytileheight = gui_font_height;
    Y_TILE = (VRes + ytileheight - 1) / ytileheight;
    for (int i = 0; i < X_TILE * Y_TILE; i++) {
#if defined(rp2350) && defined(HDMI)
        if (FullColour) {
            tilefcols[i] = tilefcols[0];
            tilebcols[i] = tilebcols[0];
        } else {
            tilefcols_w[i] = tilefcols_w[0];
            tilebcols_w[i] = tilebcols_w[0];
        }
#else
        tilefcols[i] = tilefcols[0];
        tilebcols[i] = tilebcols[0];
#endif
    }
}
