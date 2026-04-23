/*
 * hal/hal_time.h — monotonic clock + blocking sleep HAL.
 *
 * Surface core code uses for timing. All functions are "wall-clock monotonic" —
 * they do not rewind when the system RTC is set or the machine resumes from
 * sleep.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md):
 *   caller owns all buffers; HAL impl never calls MMBasic's error().
 */

#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Microseconds since boot. Monotonic, never wraps in practice (2^64 µs ≈
 * 584 500 years). Cheap to call on every iteration of a tight loop on
 * device; slightly more expensive on host (clock_gettime syscall). */
uint64_t hal_time_us_64(void);

/* Block for at least `us` microseconds.
 *
 * On cooperative ports (emscripten/WASM) this yields to the host scheduler
 * so the browser event loop can service DOM / audio / file events. A call
 * of `us = 0` on those ports is still a valid "yield" request.
 *
 * On native / device ports the implementation is a true blocking wait
 * with 1 µs granularity (or the best the target provides).
 */
void hal_time_sleep_us(uint32_t us);

/* Current time in milliseconds since boot, modulo 2^32 (≈ 49 days).
 * Convenience wrapper for the common `time_us_64() / 1000` pattern — kept
 * so callers that only need 32-bit ms precision stay branch-free. */
uint32_t hal_time_ms_tick(void);

/* Throttle hook called on every VM backward branch (bc_vm.c's
 * bc_vm_poll_interrupts) and on the interpreter's per-statement loop
 * if the host emulator is running with `--slowdown`. Real body is
 * host-only (host_runtime.c's host_sim_apply_slowdown); device impl
 * is a no-op. Declared here so core code can call it without a
 * target gate. */
void hal_time_slowdown_tick(void);

#ifdef __cplusplus
}
#endif

#endif  /* HAL_TIME_H */
