/*
 * drivers/editor_console/editor_console_stub.c — non-VGA stub for
 * the Editor.c console surface. Linked on every port that does not
 * have SCREENMODE1 + tile fg/bg colour arrays (pico, pico_rp2350,
 * web, web_rp2350, host_native, host_wasm, mmbasic_stdio,
 * mmbasic_ansi). Every hook no-ops; saved colours read back as zero.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_editor_console.h"

void hal_editor_tile_save(int xt, int yt, uint16_t * fc_out, uint16_t * bc_out) {
    (void)xt;
    (void)yt;
    if (fc_out) *fc_out = 0;
    if (bc_out) *bc_out = 0;
}

void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc) {
    (void)xt_start;
    (void)xt_end;
    (void)yt;
    (void)fc;
    (void)bc;
}

void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb) {
    (void)xt_start;
    (void)xt_end;
    (void)yt;
    (void)fc_rgb;
    (void)bc_rgb;
}

void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb) {
    (void)xt_start;
    (void)yt;
    (void)fc_rgb;
    (void)bc_rgb;
}

void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb) {
    (void)yt_start;
    (void)fc_rgb;
    (void)bc_rgb;
}

void hal_editor_tile_drawline(int yt, int fc_rgb) {
    (void)yt;
    (void)fc_rgb;
}

void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on) {
    (void)x_pixel;
    (void)yt;
    (void)gui_font_width;
    (void)bc_rgb;
    (void)r_on;
}

void hal_editor_display_enter(hal_editor_display_state_t * st) {
    /* Non-VGA: snapshot Option.Refresh so the editor can suppress
     * panel-side autorefresh during full-screen redraws, then turn
     * Refresh off for the duration of the editor session. */
    st->modmode = 0;
    st->oldmode = 0;
    st->oldfont = 0;
    st->Y_TILE_save = 0;
    st->ytileheight_save = 0;
    st->RefreshSave = Option.Refresh;
    /* The pico_rp2350 NEXTGEN family runs in shadow-buffer mode which
     * doesn't need the autorefresh-suppression, so leave Refresh
     * untouched there. NEXTGEN is unconditionally defined; on ports
     * whose OPTION setter rejects NEXTGEN values DISPLAY_TYPE never
     * reaches that range and the suppression always fires. */
    if (!(Option.DISPLAY_TYPE >= NEXTGEN)) {
        Option.Refresh = 0;
    }
}

void hal_editor_display_exit(const hal_editor_display_state_t * st) {
    Option.Refresh = st->RefreshSave;
}

void hal_editor_modmode_font_select(void) {
    /* No SCREENMODE1 modmode on non-VGA — nothing to select. */
}

int hal_editor_mouse_main_pump(int fontinc, int * c_inout) {
    (void)fontinc;
    (void)c_inout;
    return 1;
}

int hal_editor_mouse_mark_pump(int fontinc, int * c_inout, unsigned char ** mark_io) {
    (void)fontinc;
    (void)c_inout;
    (void)mark_io;
    return 1;
}

void hal_editor_mouse_anchor_reset(void) {}
