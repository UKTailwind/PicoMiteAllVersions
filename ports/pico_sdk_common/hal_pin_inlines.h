/*
 * ports/pico_sdk_common/hal_pin_inlines.h — Tier-B (hot-path) hal_pin
 * inline wrappers for pico-sdk targets.
 *
 * Pulled in by hal/hal_pin.h's trailing `#include "hal_pin_inlines.h"`.
 * The pico-sdk port directory is placed on the compiler's `-I` path so
 * the include resolves here; the host port supplies its own stub with
 * the same filename on its own include path.
 *
 * The fast variants exist so functions placed in SRAM with
 * __not_in_flash_func (WS2812e bit-banger, PIO bit-banging, FASTGFX
 * critical path) can write a pin without a cross-section call into the
 * flash-resident extern hal_pin_write.
 */

#ifndef HAL_PIN_INLINES_H
#define HAL_PIN_INLINES_H

#include <stdint.h>
#include <stdbool.h>

#include "hardware/gpio.h"

static inline void hal_pin_write_fast(uint32_t gpio, bool high)
{
    gpio_put(gpio, high);
}

static inline bool hal_pin_read_fast(uint32_t gpio)
{
    return gpio_get(gpio);
}

static inline void hal_pin_toggle_fast(uint32_t gpio)
{
    gpio_xor_mask64(1ULL << gpio);
}

#endif  /* HAL_PIN_INLINES_H */
