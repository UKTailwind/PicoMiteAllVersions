/*
 * shared/audio/audio_stream.h — portable file-decode / streaming engine.
 *
 * Decodes audio files (WAV/FLAC/MP3/MOD) to 16-bit
 * interleaved-stereo PCM and feeds the backend through the hal_audio
 * sample-streaming sink (hal_audio_sample_push). One decode path drives
 * every transport. Playback is background: open starts it, and
 * audio_stream_service() — called from each port's service loop via
 * checkWAVinput() — keeps the backend queue topped up.
 */

#ifndef SHARED_AUDIO_AUDIO_STREAM_H
#define SHARED_AUDIO_AUDIO_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start background playback of a WAV file. Appends ".wav" if no
 * extension. Returns 0 on success, negative on open/decode failure.
 * Sets CurrentlyPlaying = P_WAV. */
int  audio_stream_play_wav(char *fname);
int  audio_stream_play_mp3(char *fname);
int  audio_stream_play_flac(char *fname);
int  audio_stream_play_mod(char *fname);
int  audio_stream_play_mod_noloop(char *fname, int noloop);

/* Pump: decode and push as much as the backend queue will take; on
 * end-of-stream tear down and raise the completion interrupt. Safe to
 * call when nothing is streaming (returns immediately). */
void audio_stream_service(void);

/* Stop and release the current stream (file + decoder + sink). */
void audio_stream_stop(void);

/* Non-zero while a file stream is active. */
int  audio_stream_active(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_AUDIO_AUDIO_STREAM_H */
