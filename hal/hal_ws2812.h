/*
 * hal/hal_ws2812.h — timed one-wire LED strip output.
 *
 * The BASIC command layer owns parsing and RGB/RGBW packing. The HAL owns
 * generating the target-specific waveform on a raw GPIO.
 */

#ifndef HAL_WS2812_H
#define HAL_WS2812_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_WS2812_ORIGINAL = 0,
    HAL_WS2812_B,
    HAL_WS2812_SK6812,
    HAL_WS2812_SK6812W,
} hal_ws2812_type_t;

int hal_ws2812_write(uint32_t gpio, hal_ws2812_type_t type,
                     const uint8_t *wire_bytes, size_t wire_len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WS2812_H */

