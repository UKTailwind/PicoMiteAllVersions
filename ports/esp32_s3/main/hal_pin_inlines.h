/*
 * ESP32-S3 Tier-B hal_pin inline surface.
 *
 * No ESP32 hot path currently requires direct GPIO register access here, so
 * the fast variants preserve behavior by forwarding to the port-owned HAL
 * implementation.
 */

#ifndef ESP32_S3_HAL_PIN_INLINES_H
#define ESP32_S3_HAL_PIN_INLINES_H

#include <stdint.h>
#include <stdbool.h>

#include "hal/hal_pin.h"

static inline void hal_pin_write_fast(uint32_t gpio, bool high) {
    hal_pin_write(gpio, high);
}

static inline bool hal_pin_read_fast(uint32_t gpio) {
    return hal_pin_read(gpio);
}

static inline void hal_pin_toggle_fast(uint32_t gpio) {
    hal_pin_toggle(gpio);
}

#endif /* ESP32_S3_HAL_PIN_INLINES_H */
