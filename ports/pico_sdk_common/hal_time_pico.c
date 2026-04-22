/*
 * ports/pico_sdk_common/hal_time_pico.c — hal_time over pico/time.h.
 *
 * Thin wrapper. sleep_us on the Pico SDK is a busy-wait under the hood
 * (it resolves into a timer comparator + WFE loop); that's the correct
 * semantics for MMBasic's PAUSE / SOUND generator / device-timed I2C, so
 * we inherit the behaviour by calling it directly.
 */

#include "pico/stdlib.h"
#include "hal/hal_time.h"

uint64_t hal_time_us_64(void)
{
    return time_us_64();
}

void hal_time_sleep_us(uint32_t us)
{
    sleep_us(us);
}

uint32_t hal_time_ms_tick(void)
{
    return (uint32_t)(time_us_64() / 1000ULL);
}
