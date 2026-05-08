/*
 * drivers/display_pixel_host/display_pixel_host.c —
 * `hal_display_pixel_read` for the host-style ports (host_native,
 * host_wasm, mmbasic_stdio). Reads through the host framebuffer
 * simulation (host_runtime_get_pixel from host/host_fb.c, or the
 * stdio stub that returns 0).
 */

#include <stdint.h>

#include "hal/hal_display_pixel.h"

extern uint32_t host_runtime_get_pixel(int x, int y);

int32_t hal_display_pixel_read(int x, int y) {
    return (int32_t)(host_runtime_get_pixel(x, y) & 0xFFFFFFu);
}
