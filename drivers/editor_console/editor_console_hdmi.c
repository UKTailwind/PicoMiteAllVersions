/*
 * drivers/editor_console/editor_console_hdmi.c — HDMI real impl of
 * the Editor.c console surface. Linked on hdmi_rp2350 and
 * dvi_wifi_rp2350. The HDMI tile family is dual-bank: when FullColour
 * is on, the editor walks the 16-bit tilefcols/tilebcols (RGB555);
 * otherwise it walks the 8-bit tilefcols_w/tilebcols_w (RGB332).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_editor_console.h"

/* spi_lcd.c is gated `#if !HAL_PORT_IS_VGA`, so ScrollLCDSPISCR is
 * not defined on VGA-family ports. The editor's MX470Scroll macro
 * compares ScrollLCD against ScrollLCDSPISCR for the redraw fallback;
 * on HDMI SPIREAD is always false so the branch never fires, but the
 * symbol still has to resolve at link time. */
void ScrollLCDSPISCR(int lines) { (void)lines; }

void hal_editor_tile_save(int xt, int yt, uint16_t *fc_out, uint16_t *bc_out) {
    if (FullColour) {
        if (fc_out) *fc_out = tilefcols[yt * X_TILE + xt];
        if (bc_out) *bc_out = tilebcols[yt * X_TILE + xt];
    } else {
        if (fc_out) *fc_out = tilefcols_w[yt * X_TILE + xt];
        if (bc_out) *bc_out = tilebcols_w[yt * X_TILE + xt];
    }
}

void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc) {
    if (FullColour) {
        for (int i = xt_start; i < xt_end; i++) {
            tilefcols[yt * X_TILE + i] = fc;
            tilebcols[yt * X_TILE + i] = bc;
        }
    } else {
        for (int i = xt_start; i < xt_end; i++) {
            tilefcols_w[yt * X_TILE + i] = (uint8_t)fc;
            tilebcols_w[yt * X_TILE + i] = (uint8_t)bc;
        }
    }
}

void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb) {
    if (FullColour) {
        uint16_t fc = RGB555(fc_rgb);
        uint16_t bc = RGB555(bc_rgb);
        for (int i = xt_start; i < xt_end; i++) {
            tilefcols[yt * X_TILE + i] = fc;
            tilebcols[yt * X_TILE + i] = bc;
        }
    } else {
        uint8_t fc = RGB332(fc_rgb);
        uint8_t bc = RGB332(bc_rgb);
        for (int i = xt_start; i < xt_end; i++) {
            tilefcols_w[yt * X_TILE + i] = fc;
            tilebcols_w[yt * X_TILE + i] = bc;
        }
    }
}

void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb) {
    if (FullColour) {
        uint16_t fc = RGB555(fc_rgb);
        uint16_t bc = RGB555(bc_rgb);
        for (int x = xt_start; x < X_TILE; x++) {
            tilefcols[yt * X_TILE + x] = fc;
            tilebcols[yt * X_TILE + x] = bc;
        }
    } else {
        uint8_t fc = RGB332(fc_rgb);
        uint8_t bc = RGB332(bc_rgb);
        for (int x = xt_start; x < X_TILE; x++) {
            tilefcols_w[yt * X_TILE + x] = fc;
            tilebcols_w[yt * X_TILE + x] = bc;
        }
    }
}

void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb) {
    if (FullColour) {
        uint16_t fc = RGB555(fc_rgb);
        uint16_t bc = RGB555(bc_rgb);
        for (int y = yt_start; y < Y_TILE; y++) {
            for (int x = 0; x < X_TILE; x++) {
                tilefcols[y * X_TILE + x] = fc;
                tilebcols[y * X_TILE + x] = bc;
            }
        }
    } else {
        uint8_t fc = RGB332(fc_rgb);
        uint8_t bc = RGB332(bc_rgb);
        for (int y = yt_start; y < Y_TILE; y++) {
            for (int x = 0; x < X_TILE; x++) {
                tilefcols_w[y * X_TILE + x] = fc;
                tilebcols_w[y * X_TILE + x] = bc;
            }
        }
    }
}

void hal_editor_tile_drawline(int yt, int fc_rgb) {
    int span = HRes / 8;
    if (FullColour) {
        uint16_t fc = RGB555(fc_rgb);
        for (int i = 0; i < span; i++) {
            tilefcols[yt * X_TILE + i] = fc;
        }
    } else {
        uint8_t fc = RGB332(fc_rgb);
        for (int i = 0; i < span; i++) {
            tilefcols_w[yt * X_TILE + i] = fc;
        }
    }
}

void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on) {
    int span = gui_font_width / 8;
    int base = yt * X_TILE + x_pixel / 8;
    if (FullColour) {
        uint16_t bc = r_on ? RGB555(BLUE) : RGB555(bc_rgb);
        for (int i = 0; i < span; i++) tilebcols[base + i] = bc;
    } else {
        uint8_t bc = r_on ? RGB332(BLUE) : RGB332(bc_rgb);
        for (int i = 0; i < span; i++) tilebcols_w[base + i] = bc;
    }
}
