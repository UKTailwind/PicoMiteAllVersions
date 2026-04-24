#ifndef HOST_KEYS_H
#define HOST_KEYS_H

/*
 * Test-harness scripted-key injection.
 *
 * The host harness can drive a .bas file end-to-end by feeding MMInkey a
 * fixed stream of keystrokes: a --keys argument, or the env var
 * MMBASIC_HOST_KEYS, both optionally paired with --keys-after-ms /
 * MMBASIC_HOST_KEYS_AFTER_MS to delay the first key. That is how the
 * Editor and interactive REPL paths get exercised in run_tests.sh
 * without requiring a live TTY.
 *
 * The flow:
 *   host_runtime_configure_keys(keys, delay_ms)  -- stash the CLI args
 *   host_runtime_keys_load()                     -- called from
 *     host_runtime_begin at the start of each .bas run; parses the
 *     script (decoding \n, \t, \xNN, …) and arms the delay timer
 *   MMInkey() calls host_runtime_keys_consume()  -- per keystroke
 *   host_keydown(n) peeks the script            -- for KEYDOWN(n)
 */

/* Latch the CLI/env key script + delay. Safe to call zero-or-more times;
 * the last call wins. Called from host_main.c while parsing argv. */
void host_runtime_configure_keys(const char *keys, int delay_ms);

/* Decode the latched script into the internal buffer and arm the
 * "not ready until T+delay" timer. Called from host_runtime_begin at
 * the start of each interpreter/VM run. */
void host_runtime_keys_load(void);

/*
 * MMInkey integration. Three states:
 *   -2  no script queued — caller falls through to stdin/WS/... paths
 *   -1  queued but delay has not yet elapsed — caller returns -1
 *    0..255  a consumed character from the script
 *
 * The delay-elapsed check uses host_time_us_64, so timers advance even
 * when the caller's polling loop never sleeps.
 */
int host_runtime_keys_consume(void);

/* Peek: 1 if script has queued chars AND delay elapsed, else 0. Used by
 * host_keydown(0) to answer "is there a key ready?" */
int host_runtime_keys_ready(void);

/* Peek-char: next scripted char if ready, else 0. Used by host_keydown(n)
 * for n in 1..6. */
unsigned char host_runtime_keys_peek_char(void);

/* MMBasic-facing KEYDOWN() backing — same contract as the device driver:
 *   n == 0     → 1 if a key is queued and ready, else 0
 *   n in 1..6  → next scripted char (no consume), or 0 if none/not ready */
int host_keydown(int n);

#endif
