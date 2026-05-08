/*
 * hal/hal_display_pixel.h — VM-side PIXEL(x,y) read.
 *
 * Reading a pixel from the live display is a Display concern, not a
 * Framebuffer concern. The BASIC FRAMEBUFFER N/F/L commands maintain
 * off-screen buffers (see hal_vm_framebuffer.h); pixel readback works
 * on every port that has a real backing store, regardless of whether
 * FRAMEBUFFER itself is supported.
 *
 * Two implementations:
 *   drivers/display_pixel_readbuffer/  — devices, calls the legacy
 *                                        ReadBuffer fn-ptr (errors
 *                                        when ReadBuffer == DisplayNotSet,
 *                                        e.g. WEB before display init).
 *   drivers/display_pixel_host/        — host_native / host_wasm /
 *                                        mmbasic_stdio, calls the
 *                                        software-simulated
 *                                        host_runtime_get_pixel.
 */

#ifndef HAL_DISPLAY_PIXEL_H
#define HAL_DISPLAY_PIXEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PIXEL(x, y) — read the pixel at (x,y) from the live display, as
 * 24-bit RGB. Errors with "Invalid on this display" if the port has
 * no readback path wired up. */
int32_t hal_display_pixel_read(int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_PIXEL_H */
