/*
 * core/state/audio_state.c - small audio support state shared with BASIC.
 *
 * Runtime-visible playback state (CurrentlyPlaying, PlayingStr,
 * WAVInterrupt, WAVcomplete, tone phase state) lives in
 * shared/audio/audio_runtime.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* usertable points at a BASIC integer array loaded by PLAY LOAD SOUND;
 * read by the synth for the "U" waveform. NULL until loaded. */
unsigned short * usertable = NULL;
