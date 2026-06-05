/*
 * ports/pico_sdk_common/hal_ws2812_pico.c — WS2812/SK6812 waveform over
 * the Pico SDK GPIO/SysTick timing path.
 *
 * This preserves the legacy bit timing from External.c while moving the
 * BASIC command parser/packer to cmd_ws2812_shared.c.
 */

#include <stddef.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/structs/systick.h"
#include "MMBasic_Includes.h"
#include "FileIO.h"
#include "hal/hal_pin.h"
#include "hal/hal_time.h"
#include "hal/hal_ws2812.h"

extern int ticks_per_second;

#define PICO_WS2812_SETUPTIME (12 - (Option.CPU_Speed - 250000) / 50000)
#define PICO_WS2812_SHORTPAUSE(a)                 \
    do {                                          \
        systick_hw->cvr = 0;                      \
        asm("NOP");                               \
        asm("NOP");                               \
        asm("NOP");                               \
        asm("NOP");                               \
        asm("NOP");                               \
        while (systick_hw->cvr > (uint32_t)(a)) { \
        }                                         \
    } while (0)

typedef struct {
    int t0h;
    int t0l;
    int t1h;
    int t1l;
    int reset_us;
} pico_ws2812_timing_t;

static int pico_ws2812_pause_ticks(MMFLOAT units_20000) {
    int ticks_per_millisecond = ticks_per_second / 1000;
    return 16777215 + PICO_WS2812_SETUPTIME -
           (int)((units_20000 * ticks_per_millisecond) / 20000.0);
}

static pico_ws2812_timing_t pico_ws2812_timing_for(hal_ws2812_type_t type) {
    switch (type) {
    case HAL_WS2812_ORIGINAL:
        return (pico_ws2812_timing_t){
            pico_ws2812_pause_ticks(7.0),
            pico_ws2812_pause_ticks(13.0),
            pico_ws2812_pause_ticks(14.0),
            pico_ws2812_pause_ticks(9.5),
            50};
    case HAL_WS2812_SK6812:
    case HAL_WS2812_SK6812W:
        return (pico_ws2812_timing_t){
            pico_ws2812_pause_ticks(6.0),
            pico_ws2812_pause_ticks(15.0),
            pico_ws2812_pause_ticks(12.0),
            pico_ws2812_pause_ticks(9.0),
            80};
    case HAL_WS2812_B:
    default:
        return (pico_ws2812_timing_t){
            pico_ws2812_pause_ticks(8.0),
            pico_ws2812_pause_ticks(14.0),
            pico_ws2812_pause_ticks(16.0),
            pico_ws2812_pause_ticks(6.5),
            280};
    }
}

static void __not_in_flash_func(pico_ws2812_emit)(uint32_t gpio,
                                                  int t1h, int t1l,
                                                  int t0h, int t0l,
                                                  size_t len,
                                                  const uint8_t * p) {
    for (size_t i = 0; i < len; i++) {
        for (int j = 0; j < 8; j++) {
            if (*p & (uint8_t)(0x80u >> j)) {
                hal_pin_write_fast(gpio, true);
                PICO_WS2812_SHORTPAUSE(t1h);
                hal_pin_write_fast(gpio, false);
                PICO_WS2812_SHORTPAUSE(t1l);
            } else {
                hal_pin_write_fast(gpio, true);
                PICO_WS2812_SHORTPAUSE(t0h);
                hal_pin_write_fast(gpio, false);
                PICO_WS2812_SHORTPAUSE(t0l);
            }
        }
        p++;
    }
}

int hal_ws2812_write(uint32_t gpio, hal_ws2812_type_t type,
                     const uint8_t * wire_bytes, size_t wire_len) {
    pico_ws2812_timing_t timing = pico_ws2812_timing_for(type);

    if (!wire_bytes || wire_len == 0)
        return -1;

    uint64_t endreset = hal_time_us_64() + (uint64_t)timing.reset_us;
    while (hal_time_us_64() < endreset) {
    }

    fileio_flash_write_begin();
    pico_ws2812_emit(gpio, timing.t1h, timing.t1l,
                     timing.t0h, timing.t0l,
                     wire_len, wire_bytes);
    fileio_flash_write_end();
    return 0;
}
