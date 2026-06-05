/*
 * ansi_mode.c — `MODE N` + `QUIT` BASIC commands for the ANSI port.
 *
 * MODE: switches the framebuffer to one of five preset resolutions.
 * Defaults below; can be overridden from the command line via
 *   --modes 1:320x200,2:640x480,...
 * Startup validates command-line mode overrides against the terminal;
 * built-in defaults may still be larger than the current window.
 *
 * QUIT: clean process exit. The atexit hooks restore the alt screen
 * and console mode before we go.
 */

#include <stdlib.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ansi_mode.h"
#include "host_fb.h"

#define ANSI_MODE_COUNT 5

static int ansi_modes_w[ANSI_MODE_COUNT + 1] = {0, 320, 640, 800, 320, 480};
static int ansi_modes_h[ANSI_MODE_COUNT + 1] = {0, 240, 480, 600, 200, 320};

int ansi_mode_max(void) {
    return ANSI_MODE_COUNT;
}

int ansi_mode_set(int n, int w, int h) {
    if (n < 1 || n > ANSI_MODE_COUNT) return -1;
    if (w <= 0 || h <= 0) return -1;
    ansi_modes_w[n] = w;
    ansi_modes_h[n] = h;
    return 0;
}

int ansi_mode_get(int n, int * w, int * h) {
    if (n < 1 || n > ANSI_MODE_COUNT) return -1;
    if (w) *w = ansi_modes_w[n];
    if (h) *h = ansi_modes_h[n];
    return 0;
}

void cmd_mode(void) {
    int mode = getint(cmdline, 1, ANSI_MODE_COUNT);
    if (ansi_modes_w[mode] <= 0 || ansi_modes_h[mode] <= 0)
        error("Mode % not configured", mode);
    host_fb_resize(ansi_modes_w[mode], ansi_modes_h[mode]);
}

void cmd_quit(void) {
    exit(0);
}
