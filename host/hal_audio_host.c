/*
 * host/hal_audio_host.c — hal_audio on the native host / WASM host.
 *
 * Forwards every HAL entry to the existing host_sim_audio_* family.
 * That family is already shared between the mmbasic_sim WebSocket
 * JSON emitter (host/host_sim_audio.c) and the WASM Web Audio bridge
 * (host/host_wasm_audio.c) — picking between them is a link-time
 * choice in host/Makefile vs host/Makefile.wasm, so hal_audio_host.c
 * compiles identically against either and just forwards. No behaviour
 * change: Audio.c's host arm used to call these symbols directly;
 * it now routes through hal_audio and this file bridges back.
 */

#include "host_sim_audio.h"
#include "hal/hal_audio.h"

void hal_audio_init(void) {
    /* host backends initialise lazily on first call; nothing to do here. */
}

void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, long long duration_ms) {
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
