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
#include <math.h>
#include "ffconf.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "audio_stream.h"
#include "audio_play_common.h"


#include "hal/hal_audio.h"

/* CurrentlyPlaying, WAVInterrupt, WAVcomplete are defined in
 * core/state/audio_state.c. */
volatile int vol_left = 100, vol_right = 100;
int WAV_fnbr = 0;
int PWM_FREQ = 0;
extern unsigned short *usertable;   /* PLAY LOAD SOUND target (audio_state.c) */
volatile uint64_t SoundPlay = 0;
volatile float PhaseM_left = 0.0f, PhaseM_right = 0.0f;
volatile float PhaseAC_left = 0.0f, PhaseAC_right = 0.0f;
volatile uint8_t mono = 0;
static uint8_t s_sound_slot_mask[4];

static void shared_sound_clear_slots(void) {
    for (int i = 0; i < 4; i++) s_sound_slot_mask[i] = 0;
}

static int shared_audio_file_mode(int mode) {
    return mode == P_WAV || mode == P_FLAC || mode == P_MP3 ||
           mode == P_MOD || mode == P_ARRAY || mode == P_STREAM;
}

static int shared_sound_all_off(void) {
    for (int i = 0; i < 4; i++) if (s_sound_slot_mask[i]) return 0;
    return 1;
}

static void shared_sound_mark(int slot, int left, int right, const char *type) {
    uint8_t mask = s_sound_slot_mask[slot - 1];
    if (strcasecmp(type, "O") == 0) {
        if (left) mask &= (uint8_t)~1u;
        if (right) mask &= (uint8_t)~2u;
    } else {
        if (left) mask |= 1u;
        if (right) mask |= 2u;
    }
    s_sound_slot_mask[slot - 1] = mask;
}

static void shared_sound_stop_if_all_off(void) {
    if (CurrentlyPlaying != P_SOUND && CurrentlyPlaying != P_PAUSE_SOUND) return;
    if (!shared_sound_all_off()) return;
    hal_audio_stop();
    shared_sound_clear_slots();
    CurrentlyPlaying = P_NOTHING;
}

static void shared_audio_prepare_tone(void) {
    if (CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_STOP) return;
    StopAudio();
}

static void shared_audio_prepare_sound(void) {
    if (CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_SOUND ||
        CurrentlyPlaying == P_PAUSE_SOUND || CurrentlyPlaying == P_STOP) return;
    StopAudio();
}

static int shared_audio_sound_owns_backend(void) {
    return CurrentlyPlaying == P_SOUND || CurrentlyPlaying == P_PAUSE_SOUND;
}

int __attribute__((weak)) hal_audio_tone_interrupt_supported(void) {
    return 0;
}

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

static void shared_play_pause(void) {
    if (CurrentlyPlaying < P_STOP) return; /* already paused */
    if (CurrentlyPlaying == P_TONE) CurrentlyPlaying = P_PAUSE_TONE;
    else if (CurrentlyPlaying == P_SOUND) CurrentlyPlaying = P_PAUSE_SOUND;
    else if (CurrentlyPlaying == P_WAV) CurrentlyPlaying = P_PAUSE_WAV;
    else if (CurrentlyPlaying == P_FLAC) CurrentlyPlaying = P_PAUSE_FLAC;
    else if (CurrentlyPlaying == P_MP3) CurrentlyPlaying = P_PAUSE_MP3;
    else if (CurrentlyPlaying == P_MOD) CurrentlyPlaying = P_PAUSE_MOD;
    else if (CurrentlyPlaying == P_ARRAY) CurrentlyPlaying = P_PAUSE_ARRAY;
    else error("Nothing playing");
    hal_audio_pause();
}

static void shared_play_resume(void) {
    if (CurrentlyPlaying == P_PAUSE_TONE) CurrentlyPlaying = P_TONE;
    else if (CurrentlyPlaying == P_PAUSE_SOUND) CurrentlyPlaying = P_SOUND;
    else if (CurrentlyPlaying == P_PAUSE_WAV) CurrentlyPlaying = P_WAV;
    else if (CurrentlyPlaying == P_PAUSE_FLAC) CurrentlyPlaying = P_FLAC;
    else if (CurrentlyPlaying == P_PAUSE_MP3) CurrentlyPlaying = P_MP3;
    else if (CurrentlyPlaying == P_PAUSE_MOD) CurrentlyPlaying = P_MOD;
    else if (CurrentlyPlaying == P_PAUSE_ARRAY) CurrentlyPlaying = P_ARRAY;
    else error("Nothing to resume");
    hal_audio_resume();
}

void MIPS16 cmd_play(void) {
    unsigned char *tp;

    if (checkstring(cmdline, (unsigned char *)"STOP")) {
        if (CurrentlyPlaying == P_NOTHING) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
            return;
        }
        int complete_file_interrupt = shared_audio_file_mode(CurrentlyPlaying) &&
                                      WAVInterrupt != NULL;
        CloseAudio(1);
        if (!complete_file_interrupt) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
        }
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"PAUSE")) {
        shared_play_pause();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"RESUME")) {
        shared_play_resume();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"CLOSE")) {
        int complete_file_interrupt = shared_audio_file_mode(CurrentlyPlaying) &&
                                      WAVInterrupt != NULL;
        CloseAudio(1);
        if (!complete_file_interrupt) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
        }
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
        int64_t dur_ms = 0;
        if (argc > 4) {
            dur_ms = getint(argv[4], 0, INT_MAX);
            has_dur = 1;
        }
        if (dur_ms == 0 && has_dur) return;
        char *tone_interrupt = NULL;
        if (argc == 7) {
            if (!CurrentLinePtr) error("No program running");
            if (!hal_audio_tone_interrupt_supported())
                error("PLAY TONE interrupt not supported");
            tone_interrupt = (char *)GetIntAddress(argv[6]);
        }
        shared_audio_prepare_tone();
        hal_audio_tone((double)f_left, (double)f_right, has_dur, dur_ms);
        if (tone_interrupt) {
            WAVInterrupt = tone_interrupt;
            WAVcomplete = false;
            InterruptUsed = true;
        } else {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
        }
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
        if (strcasecmp(type, "U") == 0 && usertable == NULL) error("Not loaded");
        if (argc == 5 && strcmp(type, "O") != 0) error("Argument count");
        MMFLOAT f_in = 10.0;
        if (argc >= 7) f_in = getnumber(argv[6]);
        if (f_in < 1.0 || f_in > 20000.0) error("Valid is 1Hz to 20KHz");
        int vol = 25;
        if (argc == 9) vol = getint(argv[8], 0, 25);
        const char *ch = (left && right) ? "B" : (left ? "L" : "R");
        if (strcasecmp(type, "O") == 0) {
            if (!shared_audio_sound_owns_backend()) return;
            hal_audio_sound(slot, ch, type, (double)f_in, vol);
            shared_sound_mark(slot, left, right, type);
            shared_sound_stop_if_all_off();
            return;
        }
        shared_audio_prepare_sound();
        hal_audio_sound(slot, ch, type, (double)f_in, vol);
        shared_sound_mark(slot, left, right, type);
        CurrentlyPlaying = P_SOUND;
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"LOAD SOUND"))) {
        int64_t *aint;
        skipspace(tp);
        int size = parseintegerarray(tp, &aint, 1, 1, NULL, false);
        if (size != 1024) error("Array size");
        usertable = (unsigned short *)aint;
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"NOTE"))) {
        unsigned char *xp;
        if ((xp = checkstring(tp, (unsigned char *)"ON"))) {
            getargs(&xp, 5, (unsigned char *)",");
            if (argc != 5) error("Syntax");
            int channel = getint(argv[0], 0, 3);
            int note = getint(argv[2], 0, 127);
            int velocity = getint(argv[4], 0, 127);
            int slot = channel + 1;
            if (velocity == 0) {
                if (!shared_audio_sound_owns_backend()) return;
                hal_audio_sound(slot, "B", "O", 10.0, 0);
                shared_sound_mark(slot, 1, 1, "O");
                shared_sound_stop_if_all_off();
                return;
            } else {
                shared_audio_prepare_sound();
                hal_audio_sound(slot, "B", "S",
                                audio_play_midi_note_frequency(note),
                                audio_play_note_velocity_volume(velocity));
                shared_sound_mark(slot, 1, 1, "S");
            }
            CurrentlyPlaying = P_SOUND;
            return;
        }
        if ((xp = checkstring(tp, (unsigned char *)"OFF"))) {
            getargs(&xp, 5, (unsigned char *)",");
            if (!(argc == 3 || argc == 5)) error("Syntax");
            int channel = getint(argv[0], 0, 3);
            (void)getint(argv[2], 0, 127);
            if (argc == 5) (void)getint(argv[4], 0, 127);
            if (!shared_audio_sound_owns_backend()) return;
            hal_audio_sound(channel + 1, "B", "O", 10.0, 0);
            shared_sound_mark(channel + 1, 1, 1, "O");
            shared_sound_stop_if_all_off();
            return;
        }
        error("Syntax");
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"WAV"))) {
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        char *fname = (char *)getFstring(argv[0]);
        StopAudio();
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_wav(fname) != 0) error("Cannot play file");
        if (argc == 3) {
            if (!CurrentLinePtr) error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);
            WAVcomplete = false;
            InterruptUsed = true;
        }
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MP3"))) {
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        char *fname = (char *)getFstring(argv[0]);
        StopAudio();
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_mp3(fname) != 0) error("Cannot play file");
        if (argc == 3) { WAVInterrupt = (char *)GetIntAddress(argv[2]); WAVcomplete = false; InterruptUsed = true; }
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"FLAC"))) {
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        char *fname = (char *)getFstring(argv[0]);
        StopAudio();
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_flac(fname) != 0) error("Cannot play file");
        if (argc == 3) { WAVInterrupt = (char *)GetIntAddress(argv[2]); WAVcomplete = false; InterruptUsed = true; }
        return;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MODFILE"))) {
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        char *fname = (char *)getFstring(argv[0]);
        StopAudio();
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_mod(fname) != 0) error("Cannot play file");
        if (argc == 3) { WAVInterrupt = (char *)GetIntAddress(argv[2]); WAVcomplete = false; InterruptUsed = true; }
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"NEXT") ||
        checkstring(cmdline, (unsigned char *)"PREVIOUS")) {
        /* No playlist stepping in the shared path yet. */
        return;
    }
    error("Unsupported in shared audio: PLAY MIDI/ARRAY/STREAM etc.");
}

void CloseAudio(int all) {
    (void)all;
    int complete_file_interrupt = shared_audio_file_mode(CurrentlyPlaying) &&
                                  WAVInterrupt != NULL;
    audio_stream_stop();
    hal_audio_stop();
    CurrentlyPlaying = P_NOTHING;
    shared_sound_clear_slots();
    WAV_fnbr = 0;
    usertable = NULL;
    WAVcomplete = complete_file_interrupt;
    FSerror = 0;
}

void StopAudio(void) {
    audio_stream_stop();
    hal_audio_stop();
    CurrentlyPlaying = P_NOTHING;
    shared_sound_clear_slots();
    WAVInterrupt = NULL;
    WAVcomplete = 0;
}

void checkWAVinput(void) { audio_stream_service(); }
void audio_checks(void) { /* no callback-driven playback on host */ }
