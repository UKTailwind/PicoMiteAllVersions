/*
 * host_fastgfx_resync.c — WASM resync hook + JS-facing vsync counter.
 *
 * Two WASM-only concerns folded together because both relate to the
 * browser's frame-pacing reality:
 *
 *  1. wasm_vsync_counter is written from JS every requestAnimationFrame.
 *     bc_fastgfx_sync no longer spins on it, but it stays exported so
 *     smoke tests can observe rAF cadence from wasm's vantage point.
 *
 *  2. host_fastgfx_resync_after_sleep handles the case where the
 *     fastgfx deadline drifts more than two frames behind real time —
 *     a GC pause, tab visibility flip, or main-thread block. Without
 *     the resync, the rest of the session spins trying to catch up.
 */

#include <stdint.h>
#include "host_time.h"

volatile uint32_t wasm_vsync_counter = 0;

uint64_t host_fastgfx_resync_after_sleep(uint64_t next_sync_us, uint64_t frame_us) {
    uint64_t now = host_time_us_64();
    if (next_sync_us + 2 * frame_us < now) {
        return now + frame_us;
    }
    return next_sync_us;
}
