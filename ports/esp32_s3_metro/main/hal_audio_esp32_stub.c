/*
 * hal_audio_esp32_stub.c — Phase B stub for hal/hal_audio.h.
 * No-op bodies. Phase E+ wires I2S DAC.
 */

#include "hal/hal_audio.h"

void hal_audio_init(void) {}
void hal_audio_tone(double l, double r, int hd, long long d) { (void)l; (void)r; (void)hd; (void)d; }
void hal_audio_sound(int s, const char *c, const char *t, double f, int v) {
    (void)s; (void)c; (void)t; (void)f; (void)v;
}
void hal_audio_stop(void) {}
void hal_audio_volume(int l, int r) { (void)l; (void)r; }
void hal_audio_pause(void) {}
void hal_audio_resume(void) {}
