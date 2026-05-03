/*
 * drivers/editor_console/editor_console_stub.c — non-VGA stub for
 * the Editor.c console surface. Linked on every port that does not
 * have SCREENMODE1 + tile fg/bg colour arrays (pico, pico_rp2350,
 * web, web_rp2350, host_native, host_wasm, mmbasic_stdio,
 * mmbasic_ansi). Every hook no-ops; saved colours read back as zero.
 */

#include "hal/hal_editor_console.h"

void hal_editor_tile_save(int xt, int yt, uint16_t *fc_out, uint16_t *bc_out) {
    (void)xt; (void)yt;
    if (fc_out) *fc_out = 0;
    if (bc_out) *bc_out = 0;
}

void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc) {
    (void)xt_start; (void)xt_end; (void)yt; (void)fc; (void)bc;
}

void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb) {
    (void)xt_start; (void)xt_end; (void)yt; (void)fc_rgb; (void)bc_rgb;
}

void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb) {
    (void)xt_start; (void)yt; (void)fc_rgb; (void)bc_rgb;
}

void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb) {
    (void)yt_start; (void)fc_rgb; (void)bc_rgb;
}

void hal_editor_tile_drawline(int yt, int fc_rgb) {
    (void)yt; (void)fc_rgb;
}

void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on) {
    (void)x_pixel; (void)yt; (void)gui_font_width; (void)bc_rgb; (void)r_on;
}
