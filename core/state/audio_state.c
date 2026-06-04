/*
 * core/state/audio_state.c — audio globals read from multiple TUs.
 *
 * These three globals are the audio state BASIC-visible commands poll:
 * CurrentlyPlaying reports the current playback mode (used by MM.INFO,
 * OPTION AUDIO, Touch.c), and WAVInterrupt / WAVcomplete are the BASIC
 * interrupt hook and done-flag read by External.c's interrupt dispatch.
 *
 * Keeping storage here consolidates what previously lived in both the
 * device (#ifndef MMBASIC_HOST) and host (#else) branches of Audio.c.
 *
 * Extern declarations live in Audio.h / Hardware_Includes.h.
 *
 * Audio.c still owns its internal state: per-voice SOUND arrays
 * (sound_v_left/right, sound_PhaseAC_*, sound_PhaseM_*), DMA buffer
 * bookkeeping (bcount, swingbuf, nextbuf, playreadcomplete, ppos), and
 * codec objects (mywav, myflac, mymp3). None of those leave Audio.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

volatile e_CurrentlyPlaying CurrentlyPlaying = P_NOTHING;
char *WAVInterrupt = NULL;
bool WAVcomplete = 0;

/* usertable points at a BASIC integer array loaded by PLAY LOAD SOUND;
 * read by the synth for the "U" waveform. NULL until loaded. */
unsigned short *usertable = NULL;
