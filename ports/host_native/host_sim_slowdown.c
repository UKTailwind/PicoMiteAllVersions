/*
 * host_sim_slowdown.c — native impl of the --slowdown throttle.
 *
 * host_sim_apply_slowdown is called from hal_time_host.c and from
 * bc_vm_poll_interrupts on every backward branch. Native uses
 * host_sleep_us directly (1 µs resolution); the WASM port has its
 * own copy that accumulates to ms-boundaries because ASYNCIFY
 * floors host_sleep_us at 1 ms.
 */

#include <stdint.h>
#include "host_time.h"

int host_sim_slowdown_us = 0;

void host_sim_apply_slowdown(void) {
    if (host_sim_slowdown_us > 0) host_sleep_us((uint64_t)host_sim_slowdown_us);
}
