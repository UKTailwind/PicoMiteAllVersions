/*
 * hal/hal_audio_control.h - audio command/control HAL.
 *
 * Covers the top-level BASIC PLAY commands: TONE, SOUND slot, NOTE,
 * STOP, CLOSE, PAUSE, RESUME, and VOLUME. File/sample streaming uses
 * hal_audio_stream.h.
 */

#ifndef HAL_AUDIO_CONTROL_H
#define HAL_AUDIO_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time boot initialisation. Safe to call repeatedly; a backend
 * re-inits itself only if it has torn down (e.g. after PLAY CLOSE). */
void hal_audio_init(void);

/* PLAY TONE left_hz, right_hz [, duration_ms [, interrupt]].
 *   left_hz/right_hz: 0.0 .. ~22050 Hz. 0 means silence on that channel.
 *   has_duration: 0 = play forever (until next STOP/TONE/CLOSE).
 *                 1 = `duration_ms` is honoured.
 *   duration_ms:  only read when has_duration != 0. 0 returns immediately.
 * The BASIC-level `interrupt` argument (INTERRUPT clause) is not wired
 * through this HAL; callers handle it above the HAL. */
void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, int64_t duration_ms);

/* Non-zero when the backend publishes WAVcomplete for finite PLAY TONE
 * completion. Backends without this support must reject tone interrupts
 * at the shared command layer. */
int hal_audio_tone_interrupt_supported(void);

/* PLAY SOUND slot, ch, type [, freq [, volume]].
 *   slot:   1..4 (PicoMite has four hardware SOUND slots).
 *   ch:     "L" | "R" | "B" (left / right / both).
 *   type:   "S"|"Q"|"T"|"W"|"O"|"P"|"N"
 *           Sine / sQuare / Triangle / saWtooth / Off /
 *           Periodic-noise / white-Noise.
 *   freq:   1..20000 Hz (ignored for "O" = Off).
 *   volume: 0..25 (per-slot attenuation; per-channel master volume
 *           is a separate setting - see hal_audio_volume). */
void hal_audio_sound(int slot, const char * ch, const char * type,
                     double freq_hz, int volume);

/* PLAY STOP / PLAY CLOSE - tear down every voice, reset internal state. */
void hal_audio_stop(void);

/* PLAY VOLUME left, right - master per-channel volume, 0..100. */
void hal_audio_volume(int left_pct, int right_pct);

/* PLAY PAUSE / PLAY RESUME. Both are idempotent. */
void hal_audio_pause(void);
void hal_audio_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_CONTROL_H */
