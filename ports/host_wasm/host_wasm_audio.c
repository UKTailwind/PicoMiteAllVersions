/*
 * host_wasm_audio.c -- Web Audio bridge for the WASM host port.
 *
 * Provides the same host_sim_audio_* symbol set that Audio.c's host body
 * calls into, but instead of queuing JSON for a WebSocket drain (the
 * native --sim path in host_sim_audio.c), each call drops straight into
 * JS via EM_ASM. The JS side (host/web/ui/audio.js) owns the actual
 * WebAudio graph.
 *
 * This file is only linked into the WASM build. The native host
 * (mmbasic_test) and --sim host (mmbasic_sim) keep using
 * host_sim_audio.c unchanged.
 *
 * The drain API (host_sim_audio_drain / _free_drain) is retained as a
 * stub because other non-sim host code paths link against the symbols;
 * the WASM build has no server thread to drain into, so these return 0.
 */

#include "host_sim_audio.h"

#include <stddef.h>

#ifdef MMBASIC_WASM

#include <emscripten.h>

/* Under pthreads wasm_boot spawns a dedicated runtime thread; BASIC
 * PLAY primitives therefore run from a pthread, not the main worker.
 * MAIN_THREAD_ASYNC_EM_ASM proxies the EM_ASM back to the main
 * runtime thread (the Worker that owns the WebAssembly.Memory and
 * the message channel to the page). From there we postMessage to
 * the page, which relays to window.picomiteAudio.
 *
 * The `typeof window !== 'undefined'` guard covers the non-pthread
 * case (e.g. running on the page main thread directly); the
 * postMessage fallback covers the worker case. */
#ifndef MAIN_THREAD_ASYNC_EM_ASM
/* Headless / non-pthread fallback so the TU still builds. */
#define MAIN_THREAD_ASYNC_EM_ASM EM_ASM
#endif

void host_sim_audio_tone(double left_hz, double right_hz,
                         int has_duration, long long duration_ms) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.tone($0, $1, $2 ? $3 : -1);
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'tone', args: [$0, $1, $2 ? $3 : -1] });
        }
    }, left_hz, right_hz, has_duration, (double)duration_ms);
}

void host_sim_audio_stop(void) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.stop();
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'stop', args: [] });
        }
    });
}

void host_sim_audio_sound(int slot, const char *ch, const char *type,
                          double freq_hz, int volume) {
    MAIN_THREAD_ASYNC_EM_ASM({
        var chStr = UTF8ToString($1);
        var tyStr = UTF8ToString($2);
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.sound($0, chStr, tyStr, $3, $4);
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'sound', args: [$0, chStr, tyStr, $3, $4] });
        }
    }, slot, ch ? ch : "B", type ? type : "O", freq_hz, volume);
}

void host_sim_audio_volume(int left, int right) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.volume($0, $1);
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'volume', args: [$0, $1] });
        }
    }, left, right);
}

void host_sim_audio_pause(void) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.pause();
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'pause', args: [] });
        }
    });
}

void host_sim_audio_resume(void) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (typeof window !== 'undefined' && window.picomiteAudio) {
            window.picomiteAudio.resume();
        } else if (typeof postMessage === 'function') {
            postMessage({ type: 'audio', op: 'resume', args: [] });
        }
    });
}

size_t host_sim_audio_drain(char ***out_msgs, int *out_count) {
    if (out_msgs) *out_msgs = NULL;
    if (out_count) *out_count = 0;
    return 0;
}

void host_sim_audio_free_drain(char **msgs, int count) {
    (void)msgs; (void)count;
}

#else  /* !MMBASIC_WASM — shouldn't be linked, but stub so the TU still builds. */

void host_sim_audio_tone(double l, double r, int h, long long m) {
    (void)l; (void)r; (void)h; (void)m;
}
void host_sim_audio_stop(void) {}
void host_sim_audio_sound(int s, const char *c, const char *t, double f, int v) {
    (void)s; (void)c; (void)t; (void)f; (void)v;
}
void host_sim_audio_volume(int l, int r) { (void)l; (void)r; }
void host_sim_audio_pause(void) {}
void host_sim_audio_resume(void) {}
size_t host_sim_audio_drain(char ***m, int *c) {
    if (m) *m = NULL;
    if (c) *c = 0;
    return 0;
}
void host_sim_audio_free_drain(char **m, int c) { (void)m; (void)c; }

#endif
