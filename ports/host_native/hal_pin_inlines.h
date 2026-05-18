/*
 * ports/host_native/hal_pin_inlines.h — host-build stub for the Tier-B hal_pin
 * inline surface.
 *
 * There are no hot-path GPIO operations on the native / WASM host — no
 * XIP cache, no PIO bit-banger, no FASTGFX scanline. The fast variants
 * forward to the extern slow-path functions so callers can use them
 * uniformly.
 */

#ifndef HAL_PIN_INLINES_H
#define HAL_PIN_INLINES_H

#include <stdint.h>
#include <stdbool.h>

#include "hal/hal_pin.h"

static inline void hal_pin_write_fast(uint32_t gpio, bool high)
{
    hal_pin_write(gpio, high);
}

static inline bool hal_pin_read_fast(uint32_t gpio)
{
    return hal_pin_read(gpio);
}

static inline void hal_pin_toggle_fast(uint32_t gpio)
{
    hal_pin_toggle(gpio);
}

#endif  /* HAL_PIN_INLINES_H */
