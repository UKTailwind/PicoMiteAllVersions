/*
 * ports/host_native/hal_keyboard_host.c — hal_keyboard on the host build.
 *
 * The host port does not use MMBasic's ConsoleRxBuf pump model. Its
 * MMInkey() (host_runtime.c) reads directly from either the scripted-key
 * queue (host_runtime_keys_consume), the --sim WebSocket key queue
 * (host_sim_pop_key), or raw stdin. There is no hardware to drive
 * forward on a 1 kHz tick, so service() and init() are no-ops.
 *
 * fun_keydown backs through host_keydown (host_keys.c), which
 * inspects the scripted-key queue. Lock state is not modelled on host —
 * host_keydown returns 0 for n=8 today; we preserve that.
 *
 * OPTION KEYBOARD on the host build rejects every layout: the host
 * build has no PS/2 or USB hardware and doesn't persist Option.KeyboardConfig
 * through a reboot loop. The interpreter's OPTION KEYBOARD command is
 * hardware-gated on host anyway (see host_peripheral_stubs.c).
 */

#include "hal/hal_keyboard.h"

extern int host_keydown(int n);

void hal_keyboard_service(void) {
    /* No-op: no hardware to pump. */
}

void hal_keyboard_clear_repeat_state(void) {
    /* No-op: host has no auto-repeat; scripted keys are consumed one
     * at a time by host_runtime_keys_consume(). */
}

void hal_keyboard_init(void) {
    /* No-op: no hardware; scripted-key queue is configured by
     * host_runtime_keys_load() at init from env/argv. */
}

int hal_keyboard_keydown_count(void) {
    return host_keydown(0);
}

int hal_keyboard_keydown_slot(int slot) {
    if (slot < 1 || slot > 6) return 0;
    return host_keydown(slot);
}

uint32_t hal_keyboard_lock_state(void) {
    return (uint32_t)host_keydown(8);
}

int hal_keyboard_set_layout(int layout) {
    (void)layout;
    return -1;
}

int hal_keyboard_external_mouse_active(void) {
    /* No PS/2 or USB pointer device on host. */
    return 0;
}
