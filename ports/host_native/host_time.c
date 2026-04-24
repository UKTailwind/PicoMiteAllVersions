/*
 * host_time.c -- Monotonic time + msec-tick synthesizer for the host build.
 *
 * Moved out of host_stubs_legacy.c as part of the Host HAL refactor (Phase 1).
 * No behavior change — same functions, same semantics.
 *
 * The msec synthesizer (host_sync_msec_timer_value) updates MMBasic's
 * millisecond-granularity counters from the monotonic clock so that code
 * which polls mSecTimer / CursorTimer / the PAUSE timer sees forward
 * progress without a hardware 1ms IRQ. In --sim mode, a separate tick
 * thread in host_sim_server also bumps these counters every ms, so the
 * synthesizer is redundant but harmless there.
 */

#include <errno.h>
#include <time.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "host_time.h"

/* mSecTimer / CursorTimer are defined in host_stubs_legacy.c; referenced
 * as externs via Hardware_Includes.h. CURSOR_OFF / CURSOR_ON come from
 * the same chain. */

static void host_sync_msec_timer_value(uint64_t now_us) {
    mSecTimer = (long long)(now_us / 1000ULL);
    /* CursorTimer ticks at 1kHz on device via the timer IRQ in
     * PicoMite.c:884. On host there's no IRQ — synthesize it from the
     * monotonic clock so ShowCursor's blink math works. */
    CursorTimer = (int)((now_us / 1000ULL) % (CURSOR_OFF + CURSOR_ON));
}

uint64_t host_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void host_sync_msec_timer(void) {
    host_sync_msec_timer_value(host_now_us());
}

uint64_t host_time_us_64(void) {
    uint64_t now = host_now_us();
    host_sync_msec_timer_value(now);
    return now;
}

void host_sleep_us(uint64_t us) {
    if (us == 0) {
        host_sync_msec_timer();
        return;
    }
    /* nanosleep is a true blocking sleep on both targets:
     *   - Native host: libc nanosleep, kernel-scheduled.
     *   - WASM under -pthread: emscripten implements this via
     *     emscripten_futex_wait (Atomics.wait on a shared-memory
     *     cell). Parks just this pthread; the worker's JS event loop
     *     stays responsive for FS round-trips on the main thread. */
    struct timespec req;
    req.tv_sec = (time_t)(us / 1000000ULL);
    req.tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
    host_sync_msec_timer();
}
