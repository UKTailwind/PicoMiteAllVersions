/*
 * runtime/runtime_terminal_hooks_noop.c — shared no-op defaults for the
 * Draw.c terminal hooks (port_terminal_handle_cls / port_terminal_emit_colour).
 *
 * Every port that has a real framebuffer (host_native, host_wasm,
 * mmbasic_stdio, mmbasic_ansi, every Pico variant, pc386) routes cmd_cls
 * and cmd_colour through Draw.c's framebuffer path; the terminal hooks
 * stay no-ops so that path runs. ESP32 supplies a strong override in
 * esp32_terminal.c that emits ANSI escapes instead — which is why
 * esp32_s3_metro deliberately does NOT link this TU.
 *
 * Previously duplicated as three byte-identical bodies
 * (ports/pico_sdk_common/terminal_hooks_noop.c,
 *  ports/host_native/host_terminal_hooks_noop.c,
 *  and a copy inline in ports/pc386/pc386_runtime.c). Consolidated per
 * docs/port-duplication-audit.md Finding 3.
 */
#include <stdbool.h>

bool port_terminal_handle_cls(void) {
    return false;
}
void port_terminal_emit_colour(int fg, int bg, int has_bg) {
    (void)fg;
    (void)bg;
    (void)has_bg;
}
