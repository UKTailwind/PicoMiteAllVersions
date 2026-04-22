/*
 * hal/hal_audio.h — audio backend HAL.
 *
 * Covers the top-level BASIC PLAY commands: TONE, SOUND slot, STOP,
 * CLOSE, PAUSE, RESUME, VOLUME. File-based playback (WAV / FLAC / MP3 /
 * MOD / MIDI) is NOT part of this initial surface — those codecs stream
 * 16-bit PCM frames into the same backend via a later
 * `hal_audio_sample_push()` addition.
 *
 * Signatures are designed to match the actual BASIC semantics
 * (independent L/R frequencies for TONE, 1..4 slot numbering with
 * "L"/"R"/"B" channel strings and "S"/"Q"/"T"/"W"/"O"/"P"/"N"
 * waveform strings for SOUND) rather than the idealised sketch that
 * lives in `hal/CONTRACT.md`. Passing waveform / channel as
 * `const char *` keeps the impl from having to re-parse and matches
 * the existing host_sim_audio function set that the host impl
 * already speaks.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md §"Global conventions"):
 *   Caller owns all buffers. HAL impl never calls MMBasic's error().
 *   Return int 0 = success, negative = errno-style (rarely used today;
 *   host impl never fails, device impl errors are diagnostic only).
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

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
 * through this HAL — callers handle it above the HAL. */
void hal_audio_tone(double left_hz, double right_hz,
                    int has_duration, long long duration_ms);

/* PLAY SOUND slot, ch, type [, freq [, volume]].
 *   slot:   1..4 (PicoMite has four hardware SOUND slots).
 *   ch:     "L" | "R" | "B" (left / right / both).
 *   type:   "S"|"Q"|"T"|"W"|"O"|"P"|"N"
 *           Sine / sQuare / Triangle / saWtooth / Off /
 *           Periodic-noise / white-Noise.
 *   freq:   1..20000 Hz (ignored for "O" = Off).
 *   volume: 0..25 (per-slot attenuation; per-channel master volume
 *           is a separate setting — see hal_audio_volume). */
void hal_audio_sound(int slot, const char *ch, const char *type,
                     double freq_hz, int volume);

/* PLAY STOP / PLAY CLOSE — tear down every voice, reset internal state.
 * Both commands map to this single entry point because neither backend
 * distinguishes STOP from CLOSE at the primitive level; the caller
 * sets `CurrentlyPlaying` appropriately. */
void hal_audio_stop(void);

/* PLAY VOLUME left, right — master per-channel volume, 0..100.
 * Each channel is attenuated independently; passing the same value
 * to both produces stereo-balanced output. */
void hal_audio_volume(int left_pct, int right_pct);

/* PLAY PAUSE / PLAY RESUME. Both are idempotent; a RESUME on a
 * backend that isn't paused is a no-op. WASM hosts use RESUME as the
 * gesture-armed AudioContext unlock — that semantic is implementation-
 * specific but the contract is the same. */
void hal_audio_pause(void);
void hal_audio_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */
