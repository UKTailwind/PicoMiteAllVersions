/*
 * drivers/vga_pio/vga_ops_stub.c — no-op stubs for the hal_vga_ops
 * surface on non-VGA targets (PICOMITE, WEB, host).
 *
 * Non-VGA ports don't have SCREENMODE1-5 tile framebuffers, so every
 * hook returns 0 and the caller falls through to its default path
 * (ClearScreen, DrawRectangle, …).
 */

#include <stddef.h>
#include "hal/hal_vga_ops.h"

int  hal_vga_ops_handle_cls(int c)         { (void)c; return 0; }
int  hal_vga_ops_handle_tile_cls(int c)    { (void)c; return 0; }
int  hal_vga_ops_handle_layer_clear(void)  { return 0; }
void hal_vga_ops_retile_for_font(void)     { }
void hal_vga_ops_wait_scanline_zero(void)  { }
uint8_t hal_vga_ops_layer_merge_byte(uint8_t primary, int x, int y) {
    (void)x; (void)y; return primary;
}
uint8_t hal_vga_ops_layer_merge_rgb8(uint8_t primary, int x, int y) {
    (void)x; (void)y; return primary;
}
volatile unsigned char *hal_vga_ops_fb_n_target(void) { return NULL; }
volatile unsigned char *hal_vga_ops_fb_t_target(void) { return NULL; }
int hal_vga_ops_fb_t_supported(void) { return 0; }
int  hal_vga_ops_fb2_tilematch(int x1, int y1, int w_px, int h_px) {
    (void)x1; (void)y1; (void)w_px; (void)h_px; return 0;
}
void hal_vga_ops_fb2_fill_tile_colours(int x1, int y1, int w_px, int h_px, int fc, int bc) {
    (void)x1; (void)y1; (void)w_px; (void)h_px; (void)fc; (void)bc;
}
void hal_vga_ops_scroll_tile_colours(int lines) { (void)lines; }
void hal_vga_ops_tile_colour(int x, int y, int *front, int *back) {
    (void)x; (void)y; *front = 0xFFFFFF; *back = 0;
}
