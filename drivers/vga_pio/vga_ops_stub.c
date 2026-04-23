/*
 * drivers/vga_pio/vga_ops_stub.c — no-op stubs for the hal_vga_ops
 * surface on non-VGA targets (PICOMITE, WEB, host).
 *
 * Non-VGA ports don't have SCREENMODE1-5 tile framebuffers, so every
 * hook returns 0 and the caller falls through to its default path
 * (ClearScreen, DrawRectangle, …).
 */

#include "hal/hal_vga_ops.h"

int  hal_vga_ops_handle_cls(int c)         { (void)c; return 0; }
int  hal_vga_ops_handle_tile_cls(int c)    { (void)c; return 0; }
int  hal_vga_ops_handle_layer_clear(void)  { return 0; }
void hal_vga_ops_retile_for_font(void)     { }
void hal_vga_ops_wait_scanline_zero(void)  { }
