/*
 * ports/host_native/hal_time_host.c — hal_time over host_time.c.
 *
 * host_time_us_64 uses clock_gettime(CLOCK_MONOTONIC) on native and on
 * WASM (emscripten maps it to performance.now()). host_sleep_us uses
 * nanosleep, which on WASM under -pthread resolves to emscripten_futex_wait
 * — parks the pthread without blocking the main thread, so browser events
 * keep flowing.
 */

#include <stdint.h>

#include "host_time.h"
#include "hal/hal_time.h"

uint64_t hal_time_us_64(void) {
    return host_time_us_64();
}

void hal_time_sleep_us(uint32_t us) {
    host_sleep_us((uint64_t)us);
}

uint32_t hal_time_ms_tick(void) {
    return (uint32_t)(host_time_us_64() / 1000ULL);
}

extern void host_sim_apply_slowdown(void);
void hal_time_slowdown_tick(void) {
    host_sim_apply_slowdown();
}
