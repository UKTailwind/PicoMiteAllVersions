/*
 * hal/hal_audio_stream.h - PCM stream sink and decoder workmem HAL.
 *
 * The shared decode engine produces 16-bit interleaved-stereo PCM and
 * feeds it to exactly one linked audio backend through this contract.
 */

#ifndef HAL_AUDIO_STREAM_H
#define HAL_AUDIO_STREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Begin / end a streamed-sample session: configure the output sample
 * rate and reset the queue. hal_audio_sample_begin returns 0 on success. */
int hal_audio_sample_begin(int sample_rate_hz);
void hal_audio_sample_end(void);

/* Non-blocking copy path. Returns the number of stereo frames accepted. */
int hal_audio_sample_push(const int16_t * frames, int frame_count);

/* Optional zero-copy path for backends with discrete writable buffers.
 * Returns non-zero when a buffer is acquired; the caller must finish with
 * hal_audio_sample_commit(). Backends can return 0 and rely on push(). */
int hal_audio_sample_acquire(int16_t ** frames, int * frame_capacity);
void hal_audio_sample_commit(int frame_count);

/* Queue state in stereo frames. */
int hal_audio_sample_space(void);
int hal_audio_sample_queued(void);

/* The decoder has reached EOF and all decoded frames have been queued. */
void hal_audio_sample_eof(void);

/* Working memory for file decoders and MOD file buffers. */
void * hal_audio_workmem_alloc(unsigned long bytes);
void * hal_audio_workmem_realloc(void * p, unsigned long bytes);
void hal_audio_workmem_free(void * p);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_STREAM_H */
