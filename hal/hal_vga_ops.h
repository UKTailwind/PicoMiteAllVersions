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

/* SCREENMODE1 tile-color buffer init. HDMI ports call into the
 * scanout's settiles(); pure-VGA ports do an RGB121-tile-color loop
 * over X_TILE x Y_TILE tilefcols/tilebcols arrays. */
void hal_vga_init_screenmode1_tiles(void);

/* HDMI-specific HRes/VRes derivation from Option.CPU_Speed. Real
 * (HDMI ports) handles Freq720P / FreqXGA / FreqSVGA / FreqX; stub
 * is a no-op (pure-VGA CPU_Speed never matches those). */
void hal_vga_apply_hdmi_resolution(int display_type);

/* HDMI-only SCREENMODE4 / SCREENMODE5 framebuffer dispatch table.
 * Returns 1 if display_type was an HDMI-only mode and the
 * DrawRectangle / DrawBitmap / etc. function pointers were
 * assigned; returns 0 if it wasn't (caller continues with the
 * common SCREENMODE1/2/3 dispatch). */
int hal_vga_assign_hdmi_screenmode(int display_type);

/* Mode-1 tile-buffer init at the end of ResetDisplay. HDMI calls
 * settiles(); pure-VGA sets up tilefcols/tilebcols pointers from
 * the framebuffer + writes the initial tile colors. */
void hal_vga_init_screenmode_tiles(void);

/* Mode-1 tile init at the start of Display_SetMode (rp2350 only). HDMI
 * calls mapreset(); pure-VGA assigns tilefcols/tilebcols pointers. */
void hal_vga_setmode_mode1_pre_reset(void);

/* Setmode font selection: HDMI applies a medium-res alternate font
 * path when !FullColour && !MediumRes. Returns 1 if the hook set
 * the font; 0 if the caller should run the common font logic. */
int  hal_vga_setmode_select_alt_font(int display_type);

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

/* BLIT command COPY move for VGA — fast per-SCREENMODE path. The
 * function is monolithic on purpose: it owns byte-aligned memcpy
 * loops for SCREENMODE2/3, HDMI direct mem copy for SCREENMODE4/5, a
 * SCREENMODE1 tile-copy path, and a generic ReadBLITBuffer /
 * DrawBLITBuffer fallback (the latter is also what non-VGA targets
 * would use — but non-VGA cmd_blit doesn't take the COPY path; the
 * ReadBuffer==DisplayNotSet precondition check at the call site
 * already catches "can't blit on this display").
 *
 * Returns 1 if it handled the copy (caller is done). Returns 0 only
 * on the stub for non-VGA targets. Inputs have been clipped / flipped
 * by the caller before this is invoked. */
int hal_vga_ops_handle_blit_move(int x1, int y1, int x2, int y2, int w, int h);

/* ResetDisplay VGA body: applies the per-SCREENMODE/per-CPU_Speed
 * HRes/VRes/ScreenSize table, rewires DrawRectangle/DrawBitmap/… to
 * the right mode-specific function pointers, and repaints the tile
 * fg/bg arrays. No-op on non-VGA (the caller still runs
 * ResetGUI() under #ifdef GUICONTROLS, which is orthogonal). */
void hal_vga_ops_reset_display_vga(void);

/* Boot-time scanout pin recovery from soft reset (called from
 * InitReservedIO). Pure-VGA ports run VGArecovery(0) to re-claim the
 * RGB121 PIO pin block; HDMI + non-VGA ports no-op. */
void hal_vga_ops_reserved_io_recovery(void);

/* closeframebuffer VGA impl lives in drivers/vga_pio/vga_mode_ops.c
 * as a direct (non-hooked) definition. The non-VGA build has its
 * own closeframebuffer in Draw.c that uses the merge-pipeline hooks;
 * the two never coexist in a link. */

#ifdef __cplusplus
}
#endif

#endif /* HAL_VGA_OPS_H */
