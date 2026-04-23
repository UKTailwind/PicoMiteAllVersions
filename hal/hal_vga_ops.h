#ifndef HAL_VGA_OPS_H
#define HAL_VGA_OPS_H

#include <stdint.h>

/* hal_vga_ops — small helpers wrapping VGA-specific branches that used
 * to live behind `#ifdef PICOMITEVGA` in Draw.c.
 *
 * Every hook has a PICOMITEVGA real impl in drivers/vga_pio/ and a
 * non-VGA no-op stub. Callers invoke unconditionally; the runtime
 * HAL_PORT_IS_VGA check inside the real impl gates the VGA-specific
 * logic. */

#ifdef __cplusplus
extern "C" {
#endif

/* CLS handler for SCREENMODE1 tile-colour framebuffer:
 *  - PICOMITEVGA, SCREENMODE1 on DisplayBuf: fill framebuffer + reset
 *    the per-tile fg/bg colour tables; returns 1 (caller skips its
 *    own DrawRectangle).
 *  - Otherwise: returns 0 (caller does its own DrawRectangle).
 *  - Non-VGA stub: returns 0 always.
 *
 * `c` is the background colour the user asked for. */
int hal_vga_ops_handle_cls(int c);

/* gfx_cls helper for tile-mode ClearScreen when `use_default` is 0:
 * SCREENMODE2/SCREENMODE3 on VGA memset the writebuffer to a packed
 * 4-bit foreground pattern and return 1; other modes return 0 and let
 * the caller fall back to ClearScreen(). Non-VGA stub: returns 0. */
int hal_vga_ops_handle_tile_cls(int colour);

/* WriteBuf==Layer / SecondLayer fast path for SCREENMODE2/3/4/5 +
 * HDMI layer-transparent memset: returns 1 if it handled the clear,
 * 0 otherwise. Non-VGA stub: returns 0. */
int hal_vga_ops_handle_layer_clear(void);

/* cmd_font post-hook: when the console font changes on SCREENMODE1,
 * retile the per-tile fg/bg colour arrays so the entire screen picks
 * up the new tile height. No-op on non-VGA. */
void hal_vga_ops_retile_for_font(void);

/* VGA/HDMI scanline-zero wait: used by rendering benchmarks and demos
 * to align to the top-of-frame so drawing doesn't tear visibly. Spins
 * on QVgaScanLine (QVGA non-HDMI) or v_scanline (HDMI). No-op on
 * non-VGA. */
void hal_vga_ops_wait_scanline_zero(void);

/* ReadBuffer16 / ReadBuffer16Fast / Read sprite-blit helpers: VGA can
 * have a LayerBuf sitting above DisplayBuf, and when `mergedread` is
 * set (BMP-save code paths), reads at layer-pixel positions should
 * return the *layer* nibble if it is non-transparent, otherwise the
 * primary framebuffer nibble. The helper takes the primary byte read
 * from WriteBuf at byte-offset (y, x>>1) and returns the effective
 * byte after per-nibble layer substitution. Non-VGA stub returns the
 * input unchanged. */
uint8_t hal_vga_ops_layer_merge_byte(uint8_t primary, int x, int y);

/* Sprite-blit 24-bit RGB read (ReadBuffer pixel format uses 3 bytes
 * per pixel — RGB332-packed layer + BLIT buffer). Same idea as
 * _layer_merge_byte but for the 8-bit RGB332 sprite-blit path, where
 * the sprite read also checks the layer's mergedread substitution. */
uint8_t hal_vga_ops_layer_merge_rgb8(uint8_t primary, int x, int y);

/* BLIT FRAMEBUFFER "N" / "T" source/dest resolution. Used by
 * cmd_blit_framebuffer to turn user-typed letters into
 * framebuffer pointers:
 *  - VGA:       N → DisplayBuf
 *  - non-VGA:   N → NULL (direct-to-LCD read/write path)
 *  - rp2350 VGA/HDMI: T → SecondLayer
 *  - others:    T not accepted (caller should error "Syntax") */
volatile unsigned char *hal_vga_ops_fb_n_target(void);
volatile unsigned char *hal_vga_ops_fb_t_target(void);
int hal_vga_ops_fb_t_supported(void);

/* DrawBitmap2 tile-alignment fast path for SCREENMODE1:
 *  - tilematch(x1,y1,w_px,h_px) returns 1 when the bitmap aligns to
 *    the 8 × ytileheight tile grid, 0 otherwise. Non-VGA stub: 0.
 *  - fill_tile_colours(x1,y1,w_px,h_px,fc,bc) bulk-writes the tile fg
 *    and bg colour arrays for every tile covered by the bitmap.
 *    Caller only invokes when tilematch()==1. No-op on non-VGA. */
int  hal_vga_ops_fb2_tilematch(int x1, int y1, int w_px, int h_px);
void hal_vga_ops_fb2_fill_tile_colours(int x1, int y1, int w_px, int h_px, int fc, int bc);

/* ScrollLCD2 tile-colour scroll helper: when the pixel scroll lines
 * is a whole number of tile rows, shift the tile fg/bg colour arrays
 * to match. `lines` is the vertical pixel delta (positive scrolls
 * content up, negative scrolls it down). No-op on non-VGA. */
void hal_vga_ops_scroll_tile_colours(int lines);

/* ReadBuffer2 per-pixel tile-colour lookup: the bit-mode framebuffer
 * reads need to translate 1/0 bits into the active tile's fg/bg
 * colour, which on VGA means consulting tilefcols[]/tilebcols[]. On
 * non-VGA the caller uses fixed white/black. */
void hal_vga_ops_tile_colour(int x, int y, int *front, int *back);

#ifdef __cplusplus
}
#endif

#endif /* HAL_VGA_OPS_H */
