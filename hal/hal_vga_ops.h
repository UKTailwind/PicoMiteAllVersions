#ifndef HAL_VGA_OPS_H
#define HAL_VGA_OPS_H

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

#ifdef __cplusplus
}
#endif

#endif /* HAL_VGA_OPS_H */
