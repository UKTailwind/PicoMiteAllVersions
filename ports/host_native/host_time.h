#ifndef HOST_TIME_H
#define HOST_TIME_H

#include <stdint.h>

/*
 * Host-side monotonic time + millisecond-tick synthesizer.
 *
 * On device the 1kHz timer IRQ in PicoMite.c bumps mSecTimer, CursorTimer,
 * PauseTimer, Timer1..5, etc. On host there is no IRQ — these are advanced
 * either by the --sim background tick thread or synthesized from the
 * monotonic clock on every call into host_time_us_64 / host_sleep_us.
 */

/* Monotonic microseconds since boot (clock_gettime CLOCK_MONOTONIC). Also
 * refreshes mSecTimer/CursorTimer so the interpreter's time-polling paths
 * see progress between MMInkey / routinechecks calls. */
uint64_t host_time_us_64(void);

/* Sleep for `us` microseconds, then refresh the msec counter. Passing 0
 * is explicitly allowed and just refreshes the counters (used by spin-poll
 * loops that don't want to actually sleep). */
void host_sleep_us(uint64_t us);

/* Raw monotonic-clock reading, without refreshing mSecTimer. Used by the
 * framebuffer merge scheduler which has its own `next_merge_us` deadline
 * and does not want to double-poke the msec synthesizer. */
uint64_t host_now_us(void);

/* Explicitly advance mSecTimer / CursorTimer from the current wall clock.
 * Called by host_runtime_check_timeout so even code that never sleeps sees
 * the cursor blink. */
void host_sync_msec_timer(void);

#endif /* HOST_TIME_H */
