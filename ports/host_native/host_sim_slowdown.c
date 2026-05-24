/*
 * host_sim_slowdown.c — native impl of the --slowdown throttle.
 *
 * host_sim_apply_slowdown is called from hal_time_host.c and from
 * bc_vm_poll_interrupts on every backward branch. Sub-millisecond
 * sleeps are unreliable across platforms — Linux nanosleep happily
 * sleeps 10 µs, but Windows mingw-w64's nanosleep rounds up to the
 * system timer tick (1 ms with timeBeginPeriod(1), 15.6 ms without)
 * which silently no-ops small requests. Accumulate µs into ms
 * boundaries so a --slowdown of 100 µs still applies 1 ms of sleep
 * every ~10 backward branches on every platform.
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
