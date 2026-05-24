#ifndef HOST_KEYREPEAT_H
#define HOST_KEYREPEAT_H

/*
 * Opt-in per-byte rate limiter sitting in front of host_read_byte_nonblock.
 *
 * Configure with the same (initial-delay, sustained-rate) shape as
 * Option.RepeatStart / Option.RepeatRate. While disabled the filter
 * is a no-op (terminal OS controls repeat). While enabled, identical
 * bytes arriving from stdin are dropped until either the initial
 * delay (first repeat) or the sustained rate (subsequent repeats)
 * has elapsed. A different byte always passes through and resets the
 * held-key state.
 *
 * Multi-byte escape sequences (arrow keys etc) are not filtered —
 * their byte triples don't repeat at the byte level. The filter is
 * intended for game inputs that read single-byte INKEY$.
 */

void host_keyrepeat_configure(int start_ms, int rate_ms);
int  host_keyrepeat_filter(int byte);

#endif
