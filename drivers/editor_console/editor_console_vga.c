/*
 * drivers/editor_console/editor_console_vga.c — pure-VGA real impl
 * of the Editor.c console surface. Linked on the pure-VGA-family
 * ports (vga, vga_rp2350, vga_wifi_rp2350) where the tile fg/bg
 * arrays are 16-bit RGB121-packed (tilefcols/tilebcols). No HDMI _w
 * companion arrays exist on these ports.
 *
 * Also provides a no-op ScrollLCDSPISCR symbol for the editor's
 * MX470Scroll function-pointer comparison. spi_lcd.c (which defines
 * the real ScrollLCDSPISCR) is gated `#if !HAL_PORT_IS_VGA`, so the
 * symbol is otherwise missing on VGA family. SPIREAD is always false
 * on VGA so the comparison's branch never executes.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_editor_console.h"

void ScrollLCDSPISCR(int lines) { (void)lines; }

void hal_editor_tile_save(int xt, int yt, uint16_t *fc_out, uint16_t *bc_out) {
    if (fc_out) *fc_out = tilefcols[yt * X_TILE + xt];
    if (bc_out) *bc_out = tilebcols[yt * X_TILE + xt];
}

void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc) {
    for (int i = xt_start; i < xt_end; i++) {
        tilefcols[yt * X_TILE + i] = fc;
        tilebcols[yt * X_TILE + i] = bc;
    }
}

void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int i = xt_start; i < xt_end; i++) {
        tilefcols[yt * X_TILE + i] = fc;
        tilebcols[yt * X_TILE + i] = bc;
    }
}

void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int x = xt_start; x < X_TILE; x++) {
        tilefcols[yt * X_TILE + x] = fc;
        tilebcols[yt * X_TILE + x] = bc;
    }
}

void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb) {
    uint16_t fc = RGB121pack(fc_rgb);
    uint16_t bc = RGB121pack(bc_rgb);
    for (int y = yt_start; y < Y_TILE; y++) {
        for (int x = 0; x < X_TILE; x++) {
            tilefcols[y * X_TILE + x] = fc;
            tilebcols[y * X_TILE + x] = bc;
        }
    }
}

void hal_editor_tile_drawline(int yt, int fc_rgb) {
    /* Pure-VGA editor draws the divider line as RGB121-packed
     * MAGENTA (0x9999) — original used the literal value when
     * rendering. RGB121pack(MAGENTA) reduces to 0x9999. */
    uint16_t fc = RGB121pack(fc_rgb);
    int span = HRes / 8;
    for (int i = 0; i < span; i++) {
        tilefcols[yt * X_TILE + i] = fc;
    }
}

void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on) {
    /* Pure-VGA always uses RGB121pack on tilebcols. r_on=1 paints
     * the cursor cell BLUE; r_on=0 paints the default bg. The
     * original literal 0x1111 == RGB121pack(BLUE). */
    uint16_t bc = r_on ? RGB121pack(BLUE) : RGB121pack(bc_rgb);
    int span = gui_font_width / 8;
    for (int i = 0; i < span; i++) {
        tilebcols[yt * X_TILE + x_pixel / 8 + i] = bc;
    }
}
