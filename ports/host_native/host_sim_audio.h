/*
 * host_sim_audio.h -- Host-side audio event emitter for --sim mode.
 *
 * Every PLAY subcommand on the host is translated to a JSON message and
 * queued. In --sim builds, the Mongoose server thread drains the queue
 * on every poll and sends each message as a WebSocket TEXT frame.
 *
 * In non-sim host builds (mmbasic_test, CI) these all no-op so the same
 * cmd_play / vm_sys_audio sources link unchanged.
 */

#ifndef HOST_SIM_AUDIO_H
#define HOST_SIM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic tones / SFX used by PLAY TONE and PLAY STOP. `has_duration` = 0
 * means play forever (until the next STOP / TONE). duration_ms == 0 is
 * treated as a no-op by both the device and us. */
void host_sim_audio_tone(double left_hz, double right_hz,
                         int has_duration, long long duration_ms);
void host_sim_audio_stop(void);

/* PLAY SOUND slot, ch, type [, freq, vol].
 *   slot: 1..4
 *   ch:   "L" | "R" | "B"
 *   type: "S"|"Q"|"T"|"W"|"O"|"P"|"N"   (Sine/sQuare/Triangle/saWtooth/Off/Periodic-noise/white-Noise)
 *   freq: Hz, 1..20000
 *   vol:  0..25 (PicoMite per-slot cap)
 */
void host_sim_audio_sound(int slot, const char *ch, const char *type,
                          double freq_hz, int volume);

/* Master volume 0..100 per channel (PLAY VOLUME). */
void host_sim_audio_volume(int left, int right);

void host_sim_audio_pause(void);
void host_sim_audio_resume(void);

/*
 * Server-thread drain. Returns a heap buffer of concatenated JSON
 * messages separated by '\n'. Caller frees with free(). `*out_count`
 * receives the number of messages in the buffer. Each message is
 * null-terminated in place if the caller needs that — the drain does
 * not terminate, just separates with '\n'.
 *
 * Returns 0 when the queue is empty (and *out_buf == NULL).
 */
size_t host_sim_audio_drain(char ***out_msgs, int *out_count);

/* Release a message array previously returned by host_sim_audio_drain. */
void host_sim_audio_free_drain(char **msgs, int count);

#ifdef __cplusplus
}
#endif

#endif
