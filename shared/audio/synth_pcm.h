/*
 * shared/audio/synth_pcm.h — portable MMBasic tone/sound synthesizer.
 *
 * The wavetables, per-slot SOUND state, master TONE state, and the
 * per-frame mixing kernel that turns that state into 16-bit-scale PCM
 * live here so every backend shares one synthesizer. The transport
 * (PWM/PIO DMA on RP2040/RP2350, I2S on ESP32) stays in each port:
 * it sets the state below, then pulls frames from the two mixers.
 *
 * Each mixer returns a stereo frame at full 32-bit scale, where the
 * top 16 bits are the audio sample (the value the RP2 PIO I2S program
 * shifts out). A 16-bit sink takes `frame >> 16`.
 *
 * Frequency drives the phase increment as `freq / sample_rate * 4096.0`
 * (the wavetables hold 4096 entries); the command layer computes that
 * and stores it in PhaseM. white-noise ("N") stores the dwell count in
 * PhaseM instead.
 */

#ifndef SHARED_AUDIO_SYNTH_PCM_H
#define SHARED_AUDIO_SYNTH_PCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- TONE voice state (set by PLAY TONE / PLAY VOLUME) --- */
extern volatile float PhaseAC_left, PhaseAC_right;   /* phase accumulators */
extern volatile float PhaseM_left, PhaseM_right;     /* phase increments   */
extern volatile int vol_left, vol_right;             /* master volume 0..100 */
extern volatile uint8_t mono;                        /* L==R fast path */

/* --- SOUND voice state (set by PLAY SOUND), one entry per slot --- */
extern volatile float sound_PhaseAC_left[], sound_PhaseAC_right[];
extern volatile float sound_PhaseM_left[], sound_PhaseM_right[];
extern volatile int sound_v_left[], sound_v_right[];           /* per-slot vol */
extern volatile unsigned short *sound_mode_left[], *sound_mode_right[]; /* table ptr */

/* --- wavetables / volume map (read-only) --- */
extern const unsigned short SineTable[4096];
extern const unsigned short triangletable[4096];
extern const unsigned short squaretable[1];   /* sentinel {99} */
extern const unsigned short nulltable[1];      /* sentinel {97} = silence */
extern const unsigned short sawtable[1];       /* sentinel {98} */
extern const unsigned short whitenoise[2];     /* sentinel = white noise */
extern const int mapping[101];                 /* volume -> gain lookup */
extern unsigned short *noisetable;             /* periodic-noise table ("P") */
extern unsigned short *usertable;              /* PLAY LOAD SOUND table ("U") */

/* Allocate the periodic-noise table on first use ("P" waveform). */
void setnoise(void);

/* One SOUND-slot sample for slot `i`. `mode` bit0 selects the right
 * channel; bit1 selects the raw (unmapped) path used by hardware-volume
 * backends. Shared by the per-frame mixer and the RP2 PWM/VS1053 ISR
 * paths, which scale the result differently. */
int getsound(int i, int mode);

/* Produce one stereo frame and advance the relevant phase accumulators.
 * Output is full 32-bit scale (sample == frame >> 16). */
void synth_pcm_sound_sample(int *left, int *right);
void synth_pcm_tone_frame(int32_t *left, int32_t *right);
void synth_pcm_sound_frame(int32_t *left, int32_t *right);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_AUDIO_SYNTH_PCM_H */
