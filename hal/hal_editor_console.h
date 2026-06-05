/*
 * hal/hal_editor_console.h — Editor.c console surface hooks.
 *
 * Editor.c writes tile foreground / background colours in several
 * places (CLEAR_TO_EOL, CLEAR_TO_EOS, DRAW_LINE, DisplayPutClever's
 * reverse-video patch, and the mouse-cursor highlight in
 * FullScreenEditor / MarkMode). The encoding differs across screen
 * modes:
 *
 *   - HDMI ports (hdmi_rp2350, dvi_wifi_rp2350): FullColour=1 walks
 *     the 16-bit tilefcols/tilebcols arrays packed RGB555;
 *     FullColour=0 walks the 8-bit tilefcols_w/tilebcols_w arrays
 *     packed RGB332.
 *   - Pure-VGA ports (vga, vga_rp2350, vga_wifi_rp2350): only the
 *     16-bit tilefcols/tilebcols arrays exist, packed RGB121.
 *   - Non-VGA ports (pico, web, host): no tile colours — stub.
 *
 * Real impls in drivers/editor_console/editor_console_{vga,hdmi}.c;
 * non-VGA ports link editor_console_stub.c.
 *
 * The "tile rect" helpers operate on a horizontal tile-strip
 * (xt_start..xt_end-1, yt). The save / restore pair lets the editor
 * snapshot the current tile colour and put it back; the captured
 * value's encoding is opaque to the caller (uint16_t holds a packed
 * RGB121 or RGB555 directly, or a zero-extended RGB332).
 */

#ifndef HAL_EDITOR_CONSOLE_H
#define HAL_EDITOR_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot the (foreground, background) tile colour at (xt, yt). The
 * returned uint16_t is opaque — pass it back to
 * hal_editor_tile_paint_saved() to restore. Stub returns 0/0. */
void hal_editor_tile_save(int xt, int yt, uint16_t * fc_out, uint16_t * bc_out);

/* Write a previously saved (fc, bc) pair across (xt_start..xt_end-1,
 * yt). Stub no-ops. */
void hal_editor_tile_paint_saved(int xt_start, int xt_end, int yt,
                                 uint16_t fc, uint16_t bc);

/* Paint (xt_start..xt_end-1, yt) with the RGB888 source colours
 * (fc_rgb, bc_rgb), packed appropriately for the active mode. Stub
 * no-ops. */
void hal_editor_tile_paint_rgb(int xt_start, int xt_end, int yt,
                               int fc_rgb, int bc_rgb);

/* MX470Display CLEAR_TO_EOL: from CurrentX/gui_font_height tile down
 * to end-of-row, paint (fc_rgb, bc_rgb). Stub no-ops.
 * Caller has already tested the
 * `DISPLAY_TYPE==SCREENMODE1 && Option.ColourCode &&
 *  ytileheight==gui_font_height` precondition. */
void hal_editor_tile_clear_eol(int xt_start, int yt, int fc_rgb, int bc_rgb);

/* MX470Display CLEAR_TO_EOS: from yt_start tile down to bottom, paint
 * (fc_rgb, bc_rgb). Same precondition as _clear_eol. Stub no-ops. */
void hal_editor_tile_clear_eos(int yt_start, int fc_rgb, int bc_rgb);

/* MX470Display DRAW_LINE foreground stripe at yt — `Option.Height-2`
 * row's foreground fills the screen-width MAGENTA. Caller has tested
 * the same precondition. Stub no-ops. */
void hal_editor_tile_drawline(int yt, int fc_rgb);

/* DisplayPutClever's per-character reverse-video tile-bg patch.
 * `r_on` is the editor's reverse-video toggle; when 1 the bg is
 * BLUE, when 0 the bg is bc_rgb. Stub no-ops. */
void hal_editor_tile_putchar_bg(int x_pixel, int yt,
                                int gui_font_width, int bc_rgb, bool r_on);

/* -----------------------------------------------------------------
 * Editor enter/exit display-mode setup. VGA-family real impls swap
 * into SCREENMODE1 when HRes<512 (modmode), reset the framebuffer +
 * tile colours, and snapshot Y_TILE/ytileheight/oldmode/oldfont for
 * later restore. Non-VGA stubs leave everything zeroed.
 * ----------------------------------------------------------------- */
typedef struct {
    int modmode;
    int oldmode;
    int oldfont;
    int Y_TILE_save;
    int ytileheight_save;
    char RefreshSave;
} hal_editor_display_state_t;

void hal_editor_display_enter(hal_editor_display_state_t * st);
void hal_editor_display_exit(const hal_editor_display_state_t * st);

/* Modmode font selection — HDMI dispatches between SetFont(1) and
 * SetFont((2<<4)|1) based on FullColour/MediumRes; pure-VGA always
 * uses SetFont(1). Stub no-ops. */
void hal_editor_modmode_font_select(void);

/* -----------------------------------------------------------------
 * Mouse-cursor pump — runs only on ports that bring the editor mouse
 * into play (vga, vga_rp2350, hdmi_rp2350). Other ports link the
 * mouse-stub which returns 1 (caller proceeds with MMInkey).
 *
 * Both pumps return 1 iff the caller should still call MMInkey()
 * (i.e. no mouse click was intercepted). On a right click the pump
 * sets *c_inout = F4; on middle, F5; MarkMode left click is 9999.
 * ----------------------------------------------------------------- */
int hal_editor_mouse_main_pump(int fontinc, int * c_inout);
int hal_editor_mouse_mark_pump(int fontinc, int * c_inout, unsigned char ** mark_io);

/* Re-anchor the mouse to (curx, cury) after keyboard-driven cursor
 * moves (called from MarkMode's clipboard-yank and DEL paths). Stub
 * no-ops. */
void hal_editor_mouse_anchor_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_EDITOR_CONSOLE_H */
