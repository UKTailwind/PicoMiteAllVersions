/***********************************************************************************************************************
PicoMite MMBasic

audio.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/
/**
* @file Audio.c
* @author Geoff Graham, Peter Mather
* @brief Source for Audio MMBasic command
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */
#include <stdio.h>
#include <stdbool.h>                                // Pascal
#include <stdint.h>                                 // Pascal
#include "ffconf.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "hardware/regs/addressmap.h"     /* XIP_BASE */


/* Audio.c: host-side cmd_play / CloseAudio / StopAudio / checkWAVinput. */
/* Device body lives in drivers/pwm_synth/pwm_synth.c, linked per-target */
/* by CMakeLists.txt — Audio.c compiles only on host builds. */


/* Host body: minimal PLAY for --sim (WS-emitted tones / SFX). File-based
 * playback (WAV/FLAC/MP3/MODFILE/MIDI etc.) is rejected at parse time.
 * vm_sys_audio.c's host branch drives the same hal_audio backend, so
 * PLAY TONE via interp and FRUN emit identical events. */

#include "hal/hal_audio.h"

/* CurrentlyPlaying, WAVInterrupt, WAVcomplete are defined in
 * core/state/audio_state.c. */
volatile int vol_left = 100, vol_right = 100;
int WAV_fnbr = 0;
int PWM_FREQ = 0;
volatile uint64_t SoundPlay = 0;
volatile float PhaseM_left = 0.0f, PhaseM_right = 0.0f;
volatile float PhaseAC_left = 0.0f, PhaseAC_right = 0.0f;
volatile uint8_t mono = 0;

static int host_play_parse_channel(unsigned char *arg, int *left, int *right) {
    *left = 0; *right = 0;
    if (checkstring(arg, (unsigned char *)"L")) { *left = 1; return 1; }
    if (checkstring(arg, (unsigned char *)"R")) { *right = 1; return 1; }
    if (checkstring(arg, (unsigned char *)"B")) { *left = *right = 1; return 1; }
    char *p = (char *)getCstring(arg);
    if (!strcasecmp(p, "L")) { *left = 1; return 1; }
    if (!strcasecmp(p, "R")) { *right = 1; return 1; }
    if (!strcasecmp(p, "B") || !strcasecmp(p, "M")) { *left = *right = 1; return 1; }
    return 0;
}

static const char *host_play_parse_type(unsigned char *arg) {
    if (checkstring(arg, (unsigned char *)"O")) return "O";
    if (checkstring(arg, (unsigned char *)"Q")) return "Q";
    if (checkstring(arg, (unsigned char *)"T")) return "T";
    if (checkstring(arg, (unsigned char *)"W")) return "W";
    if (checkstring(arg, (unsigned char *)"S")) return "S";
    if (checkstring(arg, (unsigned char *)"P")) return "P";
    if (checkstring(arg, (unsigned char *)"N")) return "N";
    if (checkstring(arg, (unsigned char *)"U")) return "U";
    char *p = (char *)getCstring(arg);
    if (!strcasecmp(p, "O")) return "O";
    if (!strcasecmp(p, "Q")) return "Q";
    if (!strcasecmp(p, "T")) return "T";
    if (!strcasecmp(p, "W")) return "W";
    if (!strcasecmp(p, "S")) return "S";
    if (!strcasecmp(p, "P")) return "P";
    if (!strcasecmp(p, "N")) return "N";
    if (!strcasecmp(p, "U")) return "U";
    return NULL;
}

void MIPS16 cmd_play(void) {
    unsigned char *tp;

    if (checkstring(cmdline, (unsigned char *)"STOP")) {
        hal_audio_stop();
        CurrentlyPlaying = P_NOTHING;
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"PAUSE")) {
        hal_audio_pause();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"RESUME")) {
        hal_audio_resume();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"CLOSE")) {
        hal_audio_stop();
        CurrentlyPlaying = P_NOTHING;
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"VOLUME"))) {
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        int vl = 100, vr = 100;
        if (*argv[0]) vl = getint(argv[0], 0, 100);
        vr = vl;
        if (argc == 3) vr = getint(argv[2], 0, 100);
        hal_audio_volume(vl, vr);
        vol_left = vl;
        vol_right = vr;
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TONE"))) {
        getargs(&tp, 7, (unsigned char *)",");
        if (!(argc == 3 || argc == 5 || argc == 7)) error("Argument count");
        MMFLOAT f_left = getnumber(argv[0]);
        MMFLOAT f_right = getnumber(argv[2]);
        if (f_left < 0.0 || f_left > 22050.0) error("Valid is 0Hz to 20KHz");
        if (f_right < 0.0 || f_right > 22050.0) error("Valid is 0Hz to 20KHz");
        int has_dur = 0;
        long long dur_ms = 0;
        if (argc > 4) {
            dur_ms = getint(argv[4], 0, INT_MAX);
            has_dur = 1;
            if (dur_ms == 0) return;
        }
        /* Interrupt arg (argv[6]) is ignored on host — WAV interrupts
         * aren't plumbed through --sim. */
        hal_audio_tone((double)f_left, (double)f_right, has_dur, dur_ms);
        CurrentlyPlaying = P_TONE;
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"SOUND"))) {
        getargs(&tp, 9, (unsigned char *)",");
        if (!(argc == 5 || argc == 7 || argc == 9)) error("Argument count");
        int slot = getint(argv[0], 1, 4);
        int left = 0, right = 0;
        if (!host_play_parse_channel(argv[2], &left, &right))
            error("Position must be L, R, or B");
        const char *type = host_play_parse_type(argv[4]);
        if (!type) error("Invalid type");
        if (!left && !right) error("Position must be L, R, or B");
        if (argc == 5 && strcmp(type, "O") != 0) error("Argument count");
        MMFLOAT f_in = 10.0;
        if (argc >= 7) f_in = getnumber(argv[6]);
        if (f_in < 1.0 || f_in > 20000.0) error("Valid is 1Hz to 20KHz");
        int vol = 25;
        if (argc == 9) vol = getint(argv[8], 0, 25);
        const char *ch = (left && right) ? "B" : (left ? "L" : "R");
        hal_audio_sound(slot, ch, type, (double)f_in, vol);
        CurrentlyPlaying = P_SOUND;
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"NEXT") ||
        checkstring(cmdline, (unsigned char *)"PREVIOUS") ||
        checkstring(cmdline, (unsigned char *)"LOAD SOUND")) {
        /* No MOD/FLAC/MP3 player state to step through on host. */
        return;
    }
    error("Unsupported on host: PLAY WAV/FLAC/MP3/MODFILE etc.");
}

void CloseAudio(int all) {
    (void)all;
    hal_audio_stop();
    CurrentlyPlaying = P_NOTHING;
    WAV_fnbr = 0;
}

void StopAudio(void) {
    hal_audio_stop();
    CurrentlyPlaying = P_NOTHING;
}

void checkWAVinput(void) { /* no streaming input on host */ }
void audio_checks(void) { /* no callback-driven playback on host */ }
