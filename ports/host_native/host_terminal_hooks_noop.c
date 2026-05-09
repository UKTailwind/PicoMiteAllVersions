/*
 * ports/host_native/host_terminal_hooks_noop.c — no-op defaults for the
 * Draw.c terminal hooks (port_terminal_handle_cls / port_terminal_emit_colour).
 *
 * Host-shape ports have a real framebuffer (mmbasic_test, mmbasic_sim,
 * mmbasic_ansi all run a backing display) so cmd_cls / cmd_colour go
 * through the framebuffer path in Draw.c. The hooks no-op here.
 *
 * ESP32 (no framebuffer) supplies its own strong impls in
 * esp32_terminal.c that emit ANSI clear + 24-bit colour escapes —
 * which is why this file is *not* part of the ESP32 link.
 */
#include <stdbool.h>

bool port_terminal_handle_cls(void) { return false; }
void port_terminal_emit_colour(int fg, int bg, int has_bg) {
    (void)fg; (void)bg; (void)has_bg;
}
