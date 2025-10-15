/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

audio.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

#ifndef AUDIO_HEADER
#define AUDIO_HEADER

/* ============================================================================
 * Function declarations
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

void CloseAudio(int all);
void StopAudio(void);
void audioInterrupt(void);
void CheckAudio(void);
void checkWAVinput(void);

/* ============================================================================
 * Constants
 * ============================================================================ */
#define WAV_BUFFER_SIZE 8192

/* ============================================================================
 * Type definitions - Audio playback state enumeration
 * ============================================================================ */
typedef enum
{
    P_PAUSE_TONE,
    P_PAUSE_FLAC,
    P_PAUSE_MP3,
    P_PAUSE_SOUND,
    P_PAUSE_MOD,
    P_PAUSE_ARRAY,
    P_PAUSE_WAV,
    P_STOP,
    P_NOTHING,
    P_TONE,
    P_SOUND,
    P_WAV,
    P_FLAC,
    P_MP3,
    P_MIDI,
    P_SYNC,
    P_MOD,
    P_STREAM,
    P_ARRAY,
    P_WAVOPEN
} e_CurrentlyPlaying;

/* ============================================================================
 * Type definitions - File list structure
 * ============================================================================ */
typedef struct sa_flist
{
    char fn[FF_MAX_LFN];
} a_flist;

/* ============================================================================
 * External variables - Playback state
 * ============================================================================ */
extern const char *const PlayingStr[];
extern volatile e_CurrentlyPlaying CurrentlyPlaying;

/* ============================================================================
 * External variables - WAV playback
 * ============================================================================ */
extern char *WAVInterrupt;
extern bool WAVcomplete;
extern int WAV_fnbr;
extern volatile int wav_filesize;
extern volatile int ppos; // Playing position for PLAY WAV
extern char WAVfilename[FF_MAX_LFN];
extern volatile int swingbuf, nextbuf, playreadcomplete;

/* ============================================================================
 * External variables - Audio configuration
 * ============================================================================ */
extern int PWM_FREQ;
extern volatile uint8_t mono;
extern volatile uint8_t audiorepeat;
extern uint8_t trackplaying, trackstoplay;

/* ============================================================================
 * External variables - Volume control
 * ============================================================================ */
extern volatile int vol_left, vol_right;

/* ============================================================================
 * External variables - Buffers
 * ============================================================================ */
extern char *sbuff1, *sbuff2, *modbuff;
extern int16_t *g_buff1, *g_buff2;
extern uint16_t *playbuff;
extern int16_t *uplaybuff;
extern volatile uint32_t bcount[3];

/* ============================================================================
 * External variables - Audio output
 * ============================================================================ */
extern uint16_t left, right;
extern void (*AudioOutput)(uint16_t left, uint16_t right);

/* ============================================================================
 * External variables - Sound synthesis (per-sound channel)
 * ============================================================================ */
extern volatile int sound_v_left[MAXSOUNDS];
extern volatile int sound_v_right[MAXSOUNDS];
extern volatile float sound_PhaseAC_left[MAXSOUNDS];
extern volatile float sound_PhaseAC_right[MAXSOUNDS];
extern volatile float sound_PhaseM_left[MAXSOUNDS];
extern volatile float sound_PhaseM_right[MAXSOUNDS];
extern volatile unsigned short *sound_mode_left[MAXSOUNDS];
extern volatile unsigned short *sound_mode_right[MAXSOUNDS];

/* ============================================================================
 * External variables - Phase accumulators
 * ============================================================================ */
extern volatile float PhaseM_left, PhaseM_right;
extern volatile float PhaseAC_left, PhaseAC_right;
extern volatile unsigned char PWM_count;
extern volatile uint64_t SoundPlay;

/* ============================================================================
 * External variables - Waveform tables
 * ============================================================================ */
extern const unsigned short SineTable[4096];
extern const unsigned short nulltable[];
extern const unsigned short squaretable[];

/* ============================================================================
 * External variables - Hardware SPI configuration
 * ============================================================================ */
extern uint16_t AUDIO_SPI;
extern uint16_t AUDIO_CLK_PIN, AUDIO_MOSI_PIN, AUDIO_MISO_PIN;
extern uint16_t AUDIO_CS_PIN, AUDIO_RESET_PIN, AUDIO_DREQ_PIN;
extern uint16_t AUDIO_DCS_PIN, AUDIO_LDAC_PIN;

/* ============================================================================
 * External variables - Audio streaming
 * ============================================================================ */
extern int streamsize;
extern volatile int *streamwritepointer;
extern volatile int *streamreadpointer;
extern char *streambuffer;

/* ============================================================================
 * External variables - Playlist
 * ============================================================================ */
extern a_flist *alist;

#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

#endif /* AUDIO_HEADER */

/*  @endcond */