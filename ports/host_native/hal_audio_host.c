#include <stdlib.h>
/*
 * ports/host_native/hal_audio_host.c — hal_audio on host-style ports.
 *
 * Forwards every HAL entry to the existing host_sim_audio_* family.
 * That family is already shared between the mmbasic_sim WebSocket
 * JSON emitter (host_sim_audio.c) and the WASM Web Audio bridge
 * (ports/host_wasm/host_wasm_audio.c) — picking between them is a
 * link-time choice in the port Makefile, so hal_audio_host.c compiles
 * identically against either and just forwards. No behaviour change:
 * Audio.c's host arm used to call these symbols directly;
 * it now routes through hal_audio and this file bridges back.
 */

#include "host_sim_audio.h"
#include "hal/hal_audio.h"

void hal_audio_init(void) {
    /* host backends initialise lazily on first call; nothing to do here. */
}

void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, int64_t duration_ms) {
    host_sim_audio_tone(left_hz, right_hz, has_duration, duration_ms);
}

void hal_audio_sound(int slot, const char *ch, const char *type,
                     double freq_hz, int volume) {
    host_sim_audio_sound(slot, ch, type, freq_hz, volume);
}

void hal_audio_stop(void) {
    host_sim_audio_stop();
}

void hal_audio_volume(int left_pct, int right_pct) {
    host_sim_audio_volume(left_pct, right_pct);
}

void hal_audio_pause(void) {
    host_sim_audio_pause();
}

void hal_audio_resume(void) {
    host_sim_audio_resume();
}

/* Streamed-sample sink: host has no audio output device, so the shared
 * decode engine runs but its PCM goes nowhere. Reporting "always room,
 * never queued" lets a stream decode straight through and finish. */
int  hal_audio_sample_begin(int sample_rate_hz) { (void)sample_rate_hz; return 0; }
void hal_audio_sample_end(void) {}
void hal_audio_sample_eof(void) {}
int  hal_audio_sample_space(void) { return 4096; }
int  hal_audio_sample_queued(void) { return 0; }
int  hal_audio_sample_push(const int16_t *frames, int n) { (void)frames; return n; }
int  hal_audio_sample_acquire(int16_t **frames, int *frame_capacity) {
    (void)frames; (void)frame_capacity; return 0;
}
void hal_audio_sample_commit(int frame_count) { (void)frame_count; }

void *hal_audio_workmem_alloc(unsigned long bytes) { return malloc((size_t)bytes); }
void *hal_audio_workmem_realloc(void *p, unsigned long bytes) { return realloc(p, (size_t)bytes); }
void  hal_audio_workmem_free(void *p) { free(p); }
