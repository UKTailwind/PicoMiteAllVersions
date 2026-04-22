/*
 * host/hal_keyboard_host.c — hal_keyboard on the native host build.
 *
 * The host port does not use MMBasic's ConsoleRxBuf pump model. Its
 * MMInkey() (host_runtime.c) reads directly from either the scripted-key
 * queue (host_runtime_keys_consume), the --sim WebSocket key queue
 * (host_sim_pop_key), or raw stdin. There is no hardware to drive
 * forward on a 1 kHz tick, so both HAL entry points are no-ops.
 *
 * host_runtime_keys_load() is still called from host_runtime.c's own
 * init path; it's a CLI/env-var concern that lives outside the HAL.
 */

#include "hal/hal_keyboard.h"

void hal_keyboard_service(void) {
    /* No-op: no hardware to pump. */
}

void hal_keyboard_clear_repeat_state(void) {
    /* No-op: host has no auto-repeat; scripted keys are consumed one
     * at a time by host_runtime_keys_consume(). */
}
