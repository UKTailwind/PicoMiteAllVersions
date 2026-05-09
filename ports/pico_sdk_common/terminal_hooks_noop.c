/*
 * ports/pico_sdk_common/terminal_hooks_noop.c — no-op defaults for the
 * Draw.c terminal hooks on pico-shape device variants.
 *
 * Pico ports drive a real LCD/VGA framebuffer; cmd_cls / cmd_colour
 * route through Draw.c's framebuffer path. The hooks no-op here so
 * that path runs.
 *
 * Linked into every pico variant via the PICOMITE_SOURCES list in
 * the top-level CMakeLists.txt.
 */
#include <stdbool.h>

bool port_terminal_handle_cls(void) { return false; }
void port_terminal_emit_colour(int fg, int bg, int has_bg) {
    (void)fg; (void)bg; (void)has_bg;
}
