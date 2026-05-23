/*
 * ansi_mode.c — `MODE N` + `QUIT` BASIC commands for the ANSI port.
 *
 * MODE: switches the framebuffer to one of five preset resolutions.
 *   1 →  320×240   (PicoMite VGA default)
 *   2 →  640×480
 *   3 →  800×600
 *   4 →  320×200
 *   5 →  480×320
 * Sizes that don't fit the terminal are letterboxed by the renderer.
 *
 * QUIT: clean process exit. The atexit hooks restore the alt screen
 * and console mode before we go.
 */

#include <stdlib.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "host_fb.h"

static const struct {
    int w, h;
} ansi_modes[] = {
    {   0,   0 },   /* index 0 — unused; modes are 1-indexed */
    { 320, 240 },
    { 640, 480 },
    { 800, 600 },
    { 320, 200 },
    { 480, 320 },
};
#define ANSI_MODE_COUNT ((int)(sizeof(ansi_modes) / sizeof(ansi_modes[0])) - 1)

void cmd_mode(void) {
    int mode = getint(cmdline, 1, ANSI_MODE_COUNT);
    host_fb_resize(ansi_modes[mode].w, ansi_modes[mode].h);
}

void cmd_quit(void) {
    exit(0);
}
