/*
 * drivers/vga_pio/vga_mode_stub.c — non-VGA display-mode stub.
 *
 * Ports that compile vga_ops_stub.c but provide their own display-mode
 * switcher should omit this file from their source list.
 */

#include <stdbool.h>

/* setmode is a VGA-only entry for switching SCREENMODE (1-5) live.
 * Draw.h extern-declares it unconditionally; Commands.c calls it from
 * cmd_new / cmd_end. Keep this stub strong on host builds so BSD libc's
 * setmode(const char *) cannot satisfy MMBasic's display-mode call. */
void setmode(int mode, bool clear) {
    (void)mode;
    (void)clear;
}
