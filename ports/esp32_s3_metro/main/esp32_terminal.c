/*
 * esp32_terminal.c — VT100-emitting overrides for screen-aware BASIC
 * commands on a port without a framebuffer.
 *
 * Strong overrides for the weak hooks declared in Draw.c:
 *   port_terminal_handle_cls()   — emit ANSI clear-screen + cursor home,
 *                                  return true so cmd_cls() returns early
 *                                  before the DISPLAY_TYPE check fires.
 *   port_terminal_emit_colour()  — emit ANSI 24-bit fg/bg sequences after
 *                                  cmd_colour() updates gui_fcolour /
 *                                  gui_bcolour. The user's terminal does
 *                                  the rendering; further draws use the
 *                                  same colour state for any text printed
 *                                  via PRINT.
 */

#include <stdbool.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern char SerialConsolePutC(char c, int flush);

static void emit(const char *s) {
    while (*s) SerialConsolePutC(*s++, 0);
}

bool port_terminal_handle_cls(void) {
    emit("\033[2J\033[H");
    return true;
}

void port_terminal_emit_colour(int fg, int bg, int has_bg) {
    char buf[64];
    snprintf(buf, sizeof buf, "\033[38;2;%d;%d;%dm",
             (fg >> 16) & 0xff, (fg >> 8) & 0xff, fg & 0xff);
    emit(buf);
    if (has_bg) {
        snprintf(buf, sizeof buf, "\033[48;2;%d;%d;%dm",
                 (bg >> 16) & 0xff, (bg >> 8) & 0xff, bg & 0xff);
        emit(buf);
    }
}

void port_apply_default_console_colors(int default_fc, int default_bc) {
    port_terminal_emit_colour(default_fc, default_bc, 1);
}
