/*
 * host_keyrepeat.c — opt-in keystroke rate limiter for the host ports.
 *
 * Called from host_terminal{,_win32}.c's host_read_byte_nonblock so
 * the rate cap applies regardless of platform. See host_keyrepeat.h.
 */

#include <stdint.h>

#include "host_keyrepeat.h"
#include "host_time.h"

static int      host_kr_enabled      = 0;
static int      host_kr_start_ms     = 600;
static int      host_kr_rate_ms      = 200;
static int      host_kr_last_byte    = -1;
static uint64_t host_kr_last_pass_us = 0;
static int      host_kr_held         = 0;

void host_keyrepeat_configure(int start_ms, int rate_ms) {
    if (start_ms <= 0 || rate_ms <= 0) {
        host_kr_enabled = 0;
        return;
    }
    host_kr_start_ms  = start_ms;
    host_kr_rate_ms   = rate_ms;
    host_kr_enabled   = 1;
    host_kr_last_byte = -1;
    host_kr_held      = 0;
}

int host_keyrepeat_filter(int byte) {
    if (!host_kr_enabled || byte < 0) return byte;
    uint64_t now = host_time_us_64();
    if (byte != host_kr_last_byte) {
        host_kr_last_byte    = byte;
        host_kr_last_pass_us = now;
        host_kr_held         = 0;
        return byte;
    }
    uint64_t delta_us     = now - host_kr_last_pass_us;
    uint64_t threshold_us = (uint64_t)(host_kr_held ? host_kr_rate_ms
                                                     : host_kr_start_ms) * 1000ULL;
    if (delta_us >= threshold_us) {
        host_kr_last_pass_us = now;
        host_kr_held         = 1;
        return byte;
    }
    return -1;
}
