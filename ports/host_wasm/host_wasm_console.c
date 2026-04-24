/*
 * host_wasm_console.c -- WASM replacement for host_terminal.c.
 *
 * Input-only: a ring buffer of MMBasic key codes fed by JS via
 * wasm_push_key(). Drains through the same host_read_byte_* /
 * host_push_back_byte / host_terminal_get_size API that MMBasic's REPL
 * consumes (via host_runtime.c's MMInkey).
 *
 * No output path. Character output reaches the framebuffer through the
 * shared raster console (MMputchar -> putConsole -> DisplayPutC in
 * gfx_console_shared.c -> DrawBitmap -> host_fb_draw_bitmap), which is
 * unchanged from the native host.
 *
 * host_raw_mode_is_active() returns 1 unconditionally under WASM — there
 * is no cooked/line-buffered mode in a browser. That makes MMInkey take
 * the raw-mode branch in host_runtime.c, which is what we want.
 */

#include <stdint.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "host_terminal.h"

/* 2048 codes is plenty — a typist rarely outpaces the interpreter by more
 * than a few dozen keys. Power of two so the mask in push/pop is a nop. */
#define WASM_KEY_RING_SIZE 2048
static volatile int wasm_key_ring[WASM_KEY_RING_SIZE];
static volatile unsigned wasm_key_ring_head;  /* producer: wasm_push_key  */
static volatile unsigned wasm_key_ring_tail;  /* consumer: host_read_byte_nonblock */
static int wasm_pending_byte = -1;

/* ---------------------------------------------------------------- JS-facing */

/*
 * wasm_push_key — JS calls this from keydown handlers with an already-
 * decoded MMBasic key code. Printable ASCII passes through; arrows /
 * F-keys use the Hardware_Includes.h codes (UP=0x80, DOWN=0x81,
 * LEFT=0x82, RIGHT=0x83, F1=0x91, ...). ENTER arrives as 0x0D, BKSP as
 * 0x7F.
 *
 * Thread note: under plain ASYNCIFY we are single-threaded, so the
 * volatile + mask-based ring is overkill — but it costs nothing and
 * the interpreter may run on a dedicated worker in the future.
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void wasm_push_key(int code) {
    unsigned next = (wasm_key_ring_head + 1) & (WASM_KEY_RING_SIZE - 1);
    if (next == wasm_key_ring_tail) return;  /* ring full — drop */
    wasm_key_ring[wasm_key_ring_head] = code;
    wasm_key_ring_head = next;
}

/*
 * Address exports so a JS thread that does not run wasm (the shipping
 * app's main thread) can push keys directly into the ring via
 * Atomics.store on shared memory — no postMessage round-trip, no
 * per-keystroke worker wakeup. wasm_push_key is still exported for
 * tests that want a portable entry point.
 */
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
uintptr_t wasm_key_ring_ptr(void)      { return (uintptr_t)wasm_key_ring; }
EMSCRIPTEN_KEEPALIVE
uintptr_t wasm_key_ring_head_ptr(void) { return (uintptr_t)&wasm_key_ring_head; }
EMSCRIPTEN_KEEPALIVE
uintptr_t wasm_key_ring_tail_ptr(void) { return (uintptr_t)&wasm_key_ring_tail; }
EMSCRIPTEN_KEEPALIVE
int wasm_key_ring_size(void)           { return WASM_KEY_RING_SIZE; }
#endif

/* ---------------------------------------------------------------- host API */

void host_raw_mode_enter(void) {
    /* Browser has no cooked mode to switch out of. */
}

int host_raw_mode_is_active(void) {
    return 1;
}

int host_read_byte_nonblock(void) {
    if (wasm_pending_byte >= 0) {
        int c = wasm_pending_byte;
        wasm_pending_byte = -1;
        return c;
    }
    if (wasm_key_ring_head == wasm_key_ring_tail) return -1;
    int c = wasm_key_ring[wasm_key_ring_tail];
    wasm_key_ring_tail = (wasm_key_ring_tail + 1) & (WASM_KEY_RING_SIZE - 1);
    return c;
}

int host_read_byte_blocking_ms(int ms) {
    /* Poll the ring every 1 ms. nanosleep parks this pthread via
     * Atomics.wait under the wasm runtime, so we don't burn CPU
     * busy-waiting for input. */
    struct timespec req = { .tv_sec = 0, .tv_nsec = 1000000L };  /* 1 ms */
    for (int i = 0; i < ms; ++i) {
        int c = host_read_byte_nonblock();
        if (c >= 0) return c;
        nanosleep(&req, NULL);
    }
    return -1;
}

void host_push_back_byte(int c) {
    wasm_pending_byte = c;
}

/* Framebuffer size in glyphs — used by LIST / cmd_files pagination and
 * by Option.Width/Height. The native --sim comment in host_main.c notes
 * the framebuffer IS the console at 40x20 (8x12 glyphs on 320x240); we
 * do the same here. host_runtime_begin still enforces a floor of
 * SCREENHEIGHT (24) for pagination so programs that assume at least 24
 * rows don't hang. */
int host_terminal_get_size(int *rows, int *cols) {
    if (rows) *rows = 30;
    if (cols) *cols = 53;
    return 0;
}
