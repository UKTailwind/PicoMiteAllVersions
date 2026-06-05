/*
 * VM syscall conversion rule:
 * - copy/adapt legacy implementation code as closely as possible
 * - copy/adapt dependent legacy helpers too when needed
 * - do not invent new algorithms when legacy code already exists
 * - do not call, wrap, or dispatch back into legacy handlers
 * Any deviation from legacy implementation shape must be explicit and justified.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vm_sys_time.h"
#include "vm_device_support.h"

static void vm_sys_time_set_mstring(uint8_t * out, const char * text) {
    size_t len = strlen(text);
    if (len > MAXSTRLEN) len = MAXSTRLEN;
    out[0] = (uint8_t)len;
    if (len) memcpy(out + 1, text, len);
}

/* Port hook — fill `out` with the current local-time `struct tm` and
 * return 1, or return 0 if the port couldn't read the clock. On device
 * this reads readusclock() + MMBasic's TimeOffsetToUptime; on host it
 * checks the MMBASIC_HOST_DATE / MMBASIC_HOST_TIME env-var overrides
 * (for deterministic tests) and falls back to localtime(). */
extern int port_vm_time_get_tm(struct tm * out);

void vm_sys_time_date(uint8_t * out) {
    struct tm tm;
    char text[32];
    if (!port_vm_time_get_tm(&tm)) {
        vm_sys_time_set_mstring(out, "00-00-0000");
        return;
    }
    snprintf(text, sizeof(text), "%02d-%02d-%04d",
             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    vm_sys_time_set_mstring(out, text);
}

void vm_sys_time_time(uint8_t * out) {
    struct tm tm;
    char text[16];
    if (!port_vm_time_get_tm(&tm)) {
        vm_sys_time_set_mstring(out, "00:00:00");
        return;
    }
    snprintf(text, sizeof(text), "%02d:%02d:%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    vm_sys_time_set_mstring(out, text);
}
