/*
 * host_keys.c -- Test-harness scripted-key injection.
 *
 * Moved out of host_stubs_legacy.c as part of the Host HAL refactor
 * (Phase 1). No behavior change — same env-var names, same escape
 * syntax, same semantics for host_keydown.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_keys.h"
#include "host_time.h"

/* Decoded script + cursor. host_key_script_pos indexes the next char to
 * deliver; when pos == len the script is exhausted. */
static char   host_key_script[512] = {0};
static size_t host_key_script_len  = 0;
static size_t host_key_script_pos  = 0;

/* Latched from host_runtime_configure_keys (CLI). If _set is 0 we fall
 * back to the MMBASIC_HOST_KEYS / MMBASIC_HOST_KEYS_AFTER_MS env vars. */
static char host_config_key_script[512] = {0};
static int  host_config_key_delay_ms    = 0;
static int  host_config_key_delay_set   = 0;

/* Wall-clock deadline before the first scripted key may be delivered.
 * 0 means "ready immediately". */
static uint64_t host_key_ready_us = 0;

static int host_parse_escaped_char(const char **src) {
    const char *p = *src;
    if (*p != '\\') {
        int ch = (unsigned char)*p;
        if (*p) p++;
        *src = p;
        return ch;
    }

    p++;
    switch (*p) {
        case 'n': *src = p + 1; return '\n';
        case 'r': *src = p + 1; return '\r';
        case 't': *src = p + 1; return '\t';
        case '\\': *src = p + 1; return '\\';
        case 'x':
            if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                char hex[3] = {p[1], p[2], 0};
                *src = p + 3;
                return (int)strtol(hex, NULL, 16);
            }
            break;
        default:
            break;
    }

    *src = p;
    return '\\';
}

void host_runtime_configure_keys(const char *keys, int delay_ms) {
    host_config_key_script[0] = '\0';
    host_config_key_delay_set = 0;
    if (keys && *keys) {
        snprintf(host_config_key_script, sizeof(host_config_key_script), "%s", keys);
        host_config_key_delay_set = 1;
    }
    host_config_key_delay_ms = delay_ms < 0 ? 0 : delay_ms;
}

void host_runtime_keys_load(void) {
    const char *env = host_config_key_script[0] ? host_config_key_script
                                                : getenv("MMBASIC_HOST_KEYS");
    const char *delay_env = getenv("MMBASIC_HOST_KEYS_AFTER_MS");
    host_key_script_len = 0;
    host_key_script_pos = 0;
    host_key_ready_us = 0;
    if (!env || !*env) return;

    const char *p = env;
    while (*p && host_key_script_len + 1 < sizeof(host_key_script)) {
        host_key_script[host_key_script_len++] = (char)host_parse_escaped_char(&p);
    }
    host_key_script[host_key_script_len] = '\0';

    int delay_ms = 0;
    if (host_config_key_delay_set) delay_ms = host_config_key_delay_ms;
    else if (delay_env && *delay_env) delay_ms = atoi(delay_env);
    if (delay_ms < 0) delay_ms = 0;
    host_key_ready_us = host_time_us_64() + (uint64_t)delay_ms * 1000ULL;
}

int host_runtime_keys_ready(void) {
    if (host_key_script_pos >= host_key_script_len) return 0;
    if (host_key_ready_us && host_time_us_64() < host_key_ready_us) return 0;
    return 1;
}

unsigned char host_runtime_keys_peek_char(void) {
    if (!host_runtime_keys_ready()) return 0;
    return (unsigned char)host_key_script[host_key_script_pos];
}

int host_runtime_keys_consume(void) {
    if (host_key_script_pos >= host_key_script_len) return -2;
    if (host_key_ready_us && host_time_us_64() < host_key_ready_us) return -1;
    return (unsigned char)host_key_script[host_key_script_pos++];
}

/* host_keydown backs MMBasic's KEYDOWN function — n=0 asks "is a key
 * ready?", n=1..6 returns the scripted key (no consume). Kept as a
 * separate entry point because it's part of the MMBasic runtime API
 * and is referenced by host_stubs_legacy.c via its extern. */
int host_keydown(int n) {
    if (n == 0) return host_runtime_keys_ready() ? 1 : 0;
    if (n >= 1 && n <= 6) return host_runtime_keys_peek_char();
    return 0;
}
