/*
 * drivers/display_pixel_readbuffer/display_pixel_readbuffer.c —
 * Shared `hal_display_pixel_read` for every device port that uses the
 * legacy `ReadBuffer` fn-ptr (PICOMITE SPI-LCD + every scanout port:
 * VGA / HDMI / WEB). Identical to what the interp's fun_pixel does.
 *
 * Ports whose ReadBuffer is wired up (PICOMITE / VGA / HDMI) read
 * back the live pixel; ports that leave it pinned at DisplayNotSet
 * (WEB) error with "Invalid on this display" — same shape as the
 * interp.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_pixel.h"

int32_t hal_display_pixel_read(int x, int y) {
    int p = 0;
    if ((void *)ReadBuffer == (void *)DisplayNotSet)
        error("Invalid on this display");
    ReadBuffer(x, y, x, y, (unsigned char *)&p);
    return (int32_t)(p & 0xFFFFFF);
}
