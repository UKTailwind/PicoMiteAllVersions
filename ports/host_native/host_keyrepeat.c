/*
 * host_keyrepeat.c — opt-in keystroke rate limiter for the host ports.
 *
 * Called from host_terminal{,_win32}.c's host_read_byte_nonblock so
 * the rate cap applies regardless of platform. See host_keyrepeat.h.
 */

#include <stdint.h>

#include "host_keyrepeat.h"
#include "host_time.h"

static int host_kr_enabled = 0;
static int host_kr_start_ms = 600;
static int host_kr_rate_ms = 200;
static int host_kr_last_byte = -1;
static uint64_t host_kr_first_us = 0;
static uint64_t host_kr_last_seen_us = 0;
static uint64_t host_kr_last_pass_us = 0;
static int host_kr_seen_count = 0;
static int host_kr_held = 0;

/* A terminal only gives us bytes, not key-down/key-up events. Preserve
 * fast double-typing by allowing the first repeated byte through, then
 * treat subsequent identical bytes closer than this as OS auto-repeat. */
#define HOST_KR_REPEAT_CADENCE_US 80000ULL

void host_keyrepeat_configure(int start_ms, int rate_ms) {
    if (start_ms <= 0 || rate_ms <= 0) {
        host_kr_enabled = 0;
        return;
    }
    host_kr_start_ms = start_ms;
    host_kr_rate_ms = rate_ms;
    host_kr_enabled = 1;
    host_kr_last_byte = -1;
    host_kr_first_us = 0;
    host_kr_last_seen_us = 0;
    host_kr_last_pass_us = 0;
    host_kr_seen_count = 0;
    host_kr_held = 0;
}

int host_keyrepeat_filter(int byte) {
    if (!host_kr_enabled || byte < 0) return byte;
    uint64_t now = host_time_us_64();
    if (byte != host_kr_last_byte) {
        host_kr_last_byte = byte;
        host_kr_first_us = now;
        host_kr_last_seen_us = now;
        host_kr_last_pass_us = now;
        host_kr_seen_count = 1;
        host_kr_held = 0;
        return byte;
    }

    uint64_t since_first_us = now - host_kr_first_us;
    uint64_t since_seen_us = now - host_kr_last_seen_us;
    uint64_t since_pass_us = now - host_kr_last_pass_us;
    host_kr_last_seen_us = now;

    if (host_kr_held) {
        uint64_t start_us = (uint64_t)host_kr_start_ms * 1000ULL;
        uint64_t rate_us = (uint64_t)host_kr_rate_ms * 1000ULL;
        if (since_first_us >= start_us && since_pass_us >= rate_us) {
            host_kr_last_pass_us = now;
            return byte;
        }
        return -1;
    }

    if (since_first_us >= (uint64_t)host_kr_start_ms * 1000ULL) {
        host_kr_last_pass_us = now;
        host_kr_held = 1;
        return byte;
    }

    if (host_kr_seen_count >= 2 && since_seen_us <= HOST_KR_REPEAT_CADENCE_US) {
        host_kr_held = 1;
        return -1;
    }

    host_kr_seen_count++;
    if (host_kr_seen_count == 2) {
        host_kr_last_pass_us = now;
        return byte;
    }

    host_kr_last_pass_us = now;
    return byte;
}
