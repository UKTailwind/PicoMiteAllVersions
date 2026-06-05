/*
 * host_wasm_mode.c — BASIC `MODE N` for the WASM browser port.
 *
 * BASIC source can do `MODE 2` to switch to the resolution the user
 * assigned to mode 2 in the config dialog.  The mapping is pushed in
 * from JS at boot via wasm_set_mode_resolution(); see ports/host_wasm/web/app.mjs
 * and ports/host_wasm/web/worker.mjs.  Mid-program switches reallocate the
 * framebuffer in place (host_fb_resize) and bump host_fb_config_generation
 * so the JS canvas can resize itself on the next rAF tick.
 *
 * Modes 1-5 mirror the legacy VGA family.  Unconfigured slots raise
 * "Mode N not configured" — the dialog should populate every slot the
 * user expects to invoke from BASIC.
 */

#include "MMBasic.h"
#include "Commands.h"
#include "../host_native/host_fb.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#define HOST_WASM_MODE_MAX 5

static int mode_w[HOST_WASM_MODE_MAX + 1] = {0}; /* 1-indexed; [0] unused */
static int mode_h[HOST_WASM_MODE_MAX + 1] = {0};

/*
 * JS -> wasm: set the resolution assigned to mode `mode`.  Called once
 * per assigned mode at boot, before BASIC starts running.  Calling
 * again later replaces the mapping (no need to reboot).
 */
EMSCRIPTEN_KEEPALIVE
void wasm_set_mode_resolution(int mode, int w, int h) {
    if (mode < 1 || mode > HOST_WASM_MODE_MAX) return;
    if (w <= 0 || h <= 0) {
        mode_w[mode] = 0;
        mode_h[mode] = 0;
        return;
    }
    mode_w[mode] = w;
    mode_h[mode] = h;
}

/* Lookup helper used by cmd_mode below and by the runtime initialiser
 * that pre-resolves MODE 1 as the boot screen mode. */
int host_wasm_mode_lookup(int mode, int * w, int * h) {
    if (mode < 1 || mode > HOST_WASM_MODE_MAX) return 0;
    if (mode_w[mode] <= 0 || mode_h[mode] <= 0) return 0;
    if (w) *w = mode_w[mode];
    if (h) *h = mode_h[mode];
    return 1;
}

void cmd_mode(void) {
    int mode = getint(cmdline, 1, HOST_WASM_MODE_MAX);
    int w, h;
    if (!host_wasm_mode_lookup(mode, &w, &h))
        error("Mode % not configured", mode);
    host_fb_resize(w, h);
}
