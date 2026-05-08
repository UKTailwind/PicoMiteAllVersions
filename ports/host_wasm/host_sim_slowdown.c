/*
 * host_sim_slowdown.c — WASM impl of the --slowdown throttle.
 *
 * On WASM, host_sleep_us floors to 1 ms (ASYNCIFY has to unwind to the
 * browser event loop, which ticks no faster than ~1 ms). Calling it
 * naively per statement would pay a full ms per statement — orders of
 * magnitude slower than the user wants for a 1 µs setting.
 *
 * Accumulate instead: add the requested µs to a carry and only sleep
 * when the carry crosses whole-millisecond boundaries. A 100 µs setting
 * translates to "sleep 1 ms every ~10 statements", giving true
 * sub-millisecond average pacing at the cost of burstier timing.
 */

#include <stdint.h>
#include "host_time.h"

int host_sim_slowdown_us = 0;

void host_sim_apply_slowdown(void) {
    if (host_sim_slowdown_us <= 0) return;
    static uint64_t accumulator_us = 0;
    accumulator_us += (uint64_t)host_sim_slowdown_us;
    if (accumulator_us >= 1000ULL) {
        uint64_t whole_ms = accumulator_us / 1000ULL;
        accumulator_us -= whole_ms * 1000ULL;
        host_sleep_us(whole_ms * 1000ULL);
    }
}
