/*
 * runtime/runtime_console_adapter.c — shared storage for the
 * mm_runtime_console_adapter pointer.
 *
 * Lives in its own TU so device ports (Pico, ESP32, pc386) that don't
 * link runtime_console.c can still pull in the adapter accessor — the
 * shared runtime_console_printstring.c reads the slot to choose
 * between adapter-routed flush + telnet drain or fflush(stdout) as a
 * fallback. host_native plugs the slot during boot; the other ports
 * leave it NULL and get the fallback behaviour.
 */

#include "runtime.h"

static const mm_runtime_console_adapter * console_adapter;

void mmbasic_runtime_console_set_adapter(const mm_runtime_console_adapter * adapter) {
    console_adapter = adapter;
}

const mm_runtime_console_adapter * mmbasic_runtime_console_get_adapter(void) {
    return console_adapter;
}
