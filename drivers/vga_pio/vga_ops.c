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
#include "hal/hal_editor_console.h"

/* Declared in Draw.c unconditionally; only PICOMITEVGA ever toggles it
 * true (via BMP-save code paths). */
extern bool mergedread;

/* VGA palette-remap tables now live in the per-port real impl
 * files: drivers/vga_pio/vga_qvga_modes.c (pure-VGA — `remap[256]`)
 * and drivers/hdmi/hdmi_scanout.c (HDMI — remap555/remap332/
 * remap256). The shared vga_ops.c never references them directly. */

/* Size-of-screen-in-bytes scratch used by VGA mode dispatch. */
int ScreenSize = 0;

/* copyframetoscreen is the SPI-LCD path for pushing a scanline into the
 * physical LCD; on VGA there is no physical LCD to push to — the
 * framebuffer IS the display. Draw.c's docompressed / cmd_blitmemory
 * direct-to-screen fallbacks unconditionally reference this symbol
 * since the ifdef went away; the VGA stub below satisfies the link
 * and is never called (the dead branches are guarded by
 * `if(!WriteBuf)` / `if(s != NULL)`, both always false on VGA). */
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd) {
    (void)s; (void)xstart; (void)xend; (void)ystart; (void)yend; (void)odd;
}

/* cmd_blit MERGE on non-VGA uses blitmerge + setframebuffer in the
 * SPI-LCD FRAMEBUFFER subsystem (drivers/spi_lcd/spi_lcd_framebuffer.c).
 * On VGA those commands are rejected at runtime by the
 * hal_display_merge_has_pipeline() guard, but the linker still needs
 * the symbols — the branches are dead code on VGA. */
void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    (void)x0; (void)y0; (void)w; (void)h; (void)colour;
}
void setframebuffer(void) { }

int hal_vga_ops_handle_cls(int c) {
    if (!(DISPLAY_TYPE == SCREENMODE1 && WriteBuf == DisplayBuf)) return 0;
    DrawRectangle(0, 0, HRes - 1, VRes - 1, 0);
    memset((void *)WriteBuf, 0, ScreenSize);
    /* hal_editor_tile_paint_rgb hides the FullColour (HDMI) /
     * RGB121pack (pure VGA) tile-write dispatch. */
    for (int y = 0; y < Y_TILE; y++) {
        hal_editor_tile_paint_rgb(0, X_TILE, y, gui_fcolour, c);
    }
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
    /* SCREENMODE4 / SCREENMODE5 are HDMI-only screen modes — the
     * pure-VGA OPTION setter rejects them, so on pure-VGA ports
     * Option.DISPLAY_TYPE never matches and these blocks are dead
     * code. Keep them unconditional and let runtime guards do the
     * work. */
    if (WriteBuf == LayerBuf && DISPLAY_TYPE == SCREENMODE5 && LayerBuf != DisplayBuf) {
        memset((void *)WriteBuf, transparent, HRes * VRes);
        return 1;
    }
    if ((void *)WriteBuf == LayerBuf && DISPLAY_TYPE == SCREENMODE4 && LayerBuf != DisplayBuf) {
        uint16_t *p = (uint16_t *)WriteBuf;
        for (int i = 0; i < HRes * VRes; i++) *p++ = RGBtransparent;
        return 1;
    }
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
    int up = (lines > 0);
    if (!up) lines = -lines;
    if ((lines % ya) != 0) return;
    int offset = lines / ya;
    /* Per-tile read+write through the editor_console hooks hides the
     * FullColour (HDMI 16-bit / 8-bit) vs pure-VGA (RGB121-packed
     * 16-bit) array dispatch. */
    if (up) {
        for (int y = 0; y < Y_TILE - offset; y++) {
            for (int x = 0; x < X_TILE; x++) {
                uint16_t fc, bc;
                hal_editor_tile_save(x, y + offset, &fc, &bc);
                hal_editor_tile_paint_saved(x, x + 1, y, fc, bc);
            }
        }
    } else {
        for (int y = Y_TILE - 1; y >= offset; y--) {
            for (int x = 0; x < X_TILE; x++) {
                uint16_t fc, bc;
                hal_editor_tile_save(x, y - offset, &fc, &bc);
                hal_editor_tile_paint_saved(x, x + 1, y, fc, bc);
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
    /* hal_editor_tile_paint_rgb dispatches per port (RGB121pack on
     * pure VGA; FullColour-aware RGB555/RGB332 on HDMI). */
    for (int yy = yt; yy < yt + h; yy++) {
        hal_editor_tile_paint_rgb(xt, xt + w, yy, fc, bc);
    }
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

/* hal_vga_ops_wait_scanline_zero impl moved to per-port files:
 * vga_qvga_modes.c (pure-VGA — spins on QVgaScanLine) and
 * hdmi_scanout.c (HDMI — spins on v_scanline). */

void hal_vga_ops_retile_for_font(void) {
    if (!(gui_font_height >= 8 && (gui_font_width % 8) == 0)) return;
    ytileheight = gui_font_height;
    Y_TILE = (VRes + ytileheight - 1) / ytileheight;
    /* Replicate tile [0,0]'s fg/bg across the new grid. The editor
     * tile hooks dispatch the correct array (FullColour on HDMI vs
     * RGB121pack on pure VGA). */
    uint16_t fc, bc;
    hal_editor_tile_save(0, 0, &fc, &bc);
    for (int y = 0; y < Y_TILE; y++) {
        hal_editor_tile_paint_saved(0, X_TILE, y, fc, bc);
    }
}
