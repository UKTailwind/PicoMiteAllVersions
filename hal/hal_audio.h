/*
 * hal/hal_audio.h — audio backend HAL.
 *
 * Covers the top-level BASIC PLAY commands: TONE, SOUND slot, NOTE,
 * STOP, CLOSE, PAUSE, RESUME, VOLUME, plus the sample sink used by
 * shared WAV / FLAC / MP3 / MOD decoding. MIDI remains outside the
 * shared audio path.
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
 * through this HAL — callers handle it above the HAL. */
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
 *           is a separate setting — see hal_audio_volume). */
void hal_audio_sound(int slot, const char *ch, const char *type,
                     double freq_hz, int volume);

/* PLAY NOTE is implemented above the HAL as a small MIDI-note-number to
 * SOUND-voice adapter. Non-MIDI backends expose four note channels
 * (0..3), mapped onto the existing four SOUND slots. Full 0..15 MIDI
 * channel semantics remain specific to hardware MIDI/VS1053 paths. */

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

/* --- File / sample streaming (PLAY WAV/FLAC/MP3/MOD/ARRAY) ---
 *
 * The shared decode engine (shared/audio/audio_stream.c) decodes to
 * 16-bit interleaved-stereo PCM and feeds it to the backend here, so a
 * single decoder path drives every transport (RP2 PWM/I2S, ESP32 I2S).
 *
 * hal_audio_sample_push() is non-blocking: it copies as many of the
 * `frame_count` stereo frames as the backend's queue can take and
 * returns that count (0 if full). The decode loop retries the remainder
 * on the next service tick, which paces decoding to playback. */
int  hal_audio_sample_push(const int16_t *frames, int frame_count);

/* Optional zero-copy path for backends with discrete writable buffers.
 * Returns non-zero when a buffer is acquired; `*frames` receives writable
 * interleaved-stereo storage and `*frame_capacity` receives its capacity in
 * stereo frames. The caller must finish with hal_audio_sample_commit().
 * Backends that use rings/DMA queues can return 0 and rely on push(). */
int  hal_audio_sample_acquire(int16_t **frames, int *frame_capacity);
void hal_audio_sample_commit(int frame_count);

/* Free space in the backend queue, in stereo frames — the decode loop
 * uses it to size each read. */
int  hal_audio_sample_space(void);

/* Queued stereo frames not yet played. End-of-stream is reached when the
 * decoder is exhausted AND this returns 0. */
int  hal_audio_sample_queued(void);

/* The decoder has reached EOF and all decoded frames have been queued.
 * Backends that use "no next buffer yet" underrun recovery should switch
 * to drain-only mode here. */
void hal_audio_sample_eof(void);

/* Begin / end a streamed-sample session: configure the output sample
 * rate and reset the queue. hal_audio_sample_begin returns 0 on success. */
int  hal_audio_sample_begin(int sample_rate_hz);
void hal_audio_sample_end(void);

/* Working memory for the file decoders + MOD file buffer. On targets with
 * a small main heap (ESP32) this comes from PSRAM; elsewhere it is plain
 * malloc. NULL on failure. */
void *hal_audio_workmem_alloc(unsigned long bytes);
void *hal_audio_workmem_realloc(void *p, unsigned long bytes);
void  hal_audio_workmem_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */
