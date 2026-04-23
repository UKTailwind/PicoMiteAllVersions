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
