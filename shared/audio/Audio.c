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
#include <stdbool.h> // Pascal
#include <stdint.h>  // Pascal
#include <math.h>
#include "ffconf.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_flash.h"
#include "hal/hal_time.h"
#include "audio_stream.h"
#include "audio_play_hooks.h"
#include "audio_play_common.h"
#include "audio_runtime.h"

#include "hal/hal_audio_control.h"

volatile int __attribute__((weak)) vol_left = 100;
volatile int __attribute__((weak)) vol_right = 100;
int __attribute__((weak)) WAV_fnbr = 0;
int __attribute__((weak)) PWM_FREQ = 0;
extern unsigned short * usertable; /* PLAY LOAD SOUND target (audio_state.c) */
static uint8_t s_sound_slot_mask[4];

static void sound_clear_slots(void) {
    for (int i = 0; i < 4; i++) s_sound_slot_mask[i] = 0;
}

static int sound_all_off(void) {
    for (int i = 0; i < 4; i++)
        if (s_sound_slot_mask[i]) return 0;
    return 1;
}

static void sound_mark_slot(int slot, int left, int right, const char * type) {
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

static void sound_stop_if_all_off(void) {
    if (CurrentlyPlaying != P_SOUND && CurrentlyPlaying != P_PAUSE_SOUND) return;
    if (!sound_all_off()) return;
    hal_audio_stop();
    sound_clear_slots();
    CurrentlyPlaying = P_NOTHING;
}

static void audio_prepare_tone(void) {
    if (CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_STOP) return;
    StopAudio();
}

static void audio_prepare_sound(void) {
    if (CurrentlyPlaying == P_NOTHING || CurrentlyPlaying == P_SOUND ||
        CurrentlyPlaying == P_PAUSE_SOUND || CurrentlyPlaying == P_STOP) return;
    StopAudio();
}

static int sound_owns_backend(void) {
    return CurrentlyPlaying == P_SOUND || CurrentlyPlaying == P_PAUSE_SOUND;
}

int __attribute__((weak)) audio_cmd_play_common_can_handle(const char * verb) {
    (void)verb;
    return 1;
}

int __attribute__((weak)) audio_cmd_play_file_can_use_common(const char * verb, char * fname) {
    (void)verb;
    (void)fname;
    return 1;
}

void __attribute__((weak)) audio_cmd_play_require_available(void) {
}

void __attribute__((weak)) audio_cmd_play_prepare_tone(void) {
    audio_prepare_tone();
}

void __attribute__((weak)) audio_cmd_play_prepare_sound(void) {
    audio_prepare_sound();
}

void __attribute__((weak)) audio_cmd_play_prepare_file(const char * verb) {
    (void)verb;
    StopAudio();
}

const audio_play_hooks_t * __attribute__((weak)) audio_play_hooks_get(void) {
    return NULL;
}

int __attribute__((weak)) hal_audio_tone_interrupt_supported(void) {
    return 0;
}

static const audio_play_hooks_t * audio_play_hooks(void) {
    return audio_play_hooks_get();
}

static void play_unsupported(const char * feature) {
    error("PLAY $ not supported", feature);
}

static void play_hook_args(void (*hook)(unsigned char *), unsigned char * args,
                           const char * feature) {
    if (hook) {
        hook(args);
        return;
    }
    play_unsupported(feature);
}

static void play_hook_file(const char * verb, unsigned char * args) {
    const audio_play_hooks_t * hooks = audio_play_hooks();
    if (hooks && hooks->play_file) {
        hooks->play_file(verb, args);
        return;
    }
    play_unsupported(verb);
}

static void play_hook_noargs(void (*hook)(void), const char * feature) {
    if (hook) {
        hook();
        return;
    }
    play_unsupported(feature);
}

static int play_parse_channel(unsigned char * arg, int * left, int * right) {
    *left = 0;
    *right = 0;
    if (checkstring(arg, (unsigned char *)"L")) {
        *left = 1;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"R")) {
        *right = 1;
        return 1;
    }
    if (checkstring(arg, (unsigned char *)"B")) {
        *left = *right = 1;
        return 1;
    }
    char * p = (char *)getCstring(arg);
    if (!strcasecmp(p, "L")) {
        *left = 1;
        return 1;
    }
    if (!strcasecmp(p, "R")) {
        *right = 1;
        return 1;
    }
    if (!strcasecmp(p, "B") || !strcasecmp(p, "M")) {
        *left = *right = 1;
        return 1;
    }
    return 0;
}

static const char * play_parse_type(unsigned char * arg) {
    if (checkstring(arg, (unsigned char *)"O")) return "O";
    if (checkstring(arg, (unsigned char *)"Q")) return "Q";
    if (checkstring(arg, (unsigned char *)"T")) return "T";
    if (checkstring(arg, (unsigned char *)"W")) return "W";
    if (checkstring(arg, (unsigned char *)"S")) return "S";
    if (checkstring(arg, (unsigned char *)"P")) return "P";
    if (checkstring(arg, (unsigned char *)"N")) return "N";
    if (checkstring(arg, (unsigned char *)"U")) return "U";
    char * p = (char *)getCstring(arg);
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

static void play_pause(void) {
    if (CurrentlyPlaying < P_STOP) return; /* already paused */
    if (CurrentlyPlaying == P_TONE)
        CurrentlyPlaying = P_PAUSE_TONE;
    else if (CurrentlyPlaying == P_SOUND)
        CurrentlyPlaying = P_PAUSE_SOUND;
    else if (CurrentlyPlaying == P_WAV)
        CurrentlyPlaying = P_PAUSE_WAV;
    else if (CurrentlyPlaying == P_FLAC)
        CurrentlyPlaying = P_PAUSE_FLAC;
    else if (CurrentlyPlaying == P_MP3)
        CurrentlyPlaying = P_PAUSE_MP3;
    else if (CurrentlyPlaying == P_MOD)
        CurrentlyPlaying = P_PAUSE_MOD;
    else if (CurrentlyPlaying == P_ARRAY)
        CurrentlyPlaying = P_PAUSE_ARRAY;
    else
        error("Nothing playing");
    hal_audio_pause();
}

static void play_resume(void) {
    if (CurrentlyPlaying == P_PAUSE_TONE)
        CurrentlyPlaying = P_TONE;
    else if (CurrentlyPlaying == P_PAUSE_SOUND)
        CurrentlyPlaying = P_SOUND;
    else if (CurrentlyPlaying == P_PAUSE_WAV)
        CurrentlyPlaying = P_WAV;
    else if (CurrentlyPlaying == P_PAUSE_FLAC)
        CurrentlyPlaying = P_FLAC;
    else if (CurrentlyPlaying == P_PAUSE_MP3)
        CurrentlyPlaying = P_MP3;
    else if (CurrentlyPlaying == P_PAUSE_MOD)
        CurrentlyPlaying = P_MOD;
    else if (CurrentlyPlaying == P_PAUSE_ARRAY)
        CurrentlyPlaying = P_ARRAY;
    else
        error("Nothing to resume");
    hal_audio_resume();
}

int MIPS16 audio_cmd_play_common(void) {
    unsigned char * tp;

    if (checkstring(cmdline, (unsigned char *)"STOP")) {
        if (!audio_cmd_play_common_can_handle("STOP")) return 0;
        if (CurrentlyPlaying == P_NOTHING) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
            return 1;
        }
        int complete_file_interrupt = audio_runtime_mode_is_file(CurrentlyPlaying) &&
                                      WAVInterrupt != NULL;
        CloseAudio(1);
        if (!complete_file_interrupt) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
        }
        return 1;
    }
    audio_cmd_play_require_available();
    if (checkstring(cmdline, (unsigned char *)"PAUSE")) {
        if (!audio_cmd_play_common_can_handle("PAUSE")) return 0;
        play_pause();
        return 1;
    }
    if (checkstring(cmdline, (unsigned char *)"RESUME")) {
        if (!audio_cmd_play_common_can_handle("RESUME")) return 0;
        play_resume();
        return 1;
    }
    if (checkstring(cmdline, (unsigned char *)"CLOSE")) {
        if (!audio_cmd_play_common_can_handle("CLOSE")) return 0;
        int complete_file_interrupt = audio_runtime_mode_is_file(CurrentlyPlaying) &&
                                      WAVInterrupt != NULL;
        CloseAudio(1);
        if (!complete_file_interrupt) {
            WAVInterrupt = NULL;
            WAVcomplete = 0;
        }
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"VOLUME"))) {
        if (!audio_cmd_play_common_can_handle("VOLUME")) return 0;
        getargs(&tp, 3, (unsigned char *)",");
        if (argc < 1) error("Argument count");
        int vl = 100, vr = 100;
        if (*argv[0]) vl = getint(argv[0], 0, 100);
        vr = vl;
        if (argc == 3) vr = getint(argv[2], 0, 100);
        hal_audio_volume(vl, vr);
        vol_left = vl;
        vol_right = vr;
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"TONE"))) {
        if (!audio_cmd_play_common_can_handle("TONE")) return 0;
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
        if (dur_ms == 0 && has_dur) return 1;
        char * tone_interrupt = NULL;
        if (argc == 7) {
            if (!CurrentLinePtr) error("No program running");
            if (!hal_audio_tone_interrupt_supported())
                error("PLAY TONE interrupt not supported");
            tone_interrupt = (char *)GetIntAddress(argv[6]);
        }
        audio_cmd_play_prepare_tone();
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
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"SOUND"))) {
        if (!audio_cmd_play_common_can_handle("SOUND")) return 0;
        getargs(&tp, 9, (unsigned char *)",");
        if (!(argc == 5 || argc == 7 || argc == 9)) error("Argument count");
        int slot = getint(argv[0], 1, 4);
        int left = 0, right = 0;
        if (!play_parse_channel(argv[2], &left, &right))
            error("Position must be L, R, or B");
        const char * type = play_parse_type(argv[4]);
        if (!type) error("Invalid type");
        if (!left && !right) error("Position must be L, R, or B");
        if (strcasecmp(type, "U") == 0 && usertable == NULL) error("Not loaded");
        if (argc == 5 && strcmp(type, "O") != 0) error("Argument count");
        MMFLOAT f_in = 10.0;
        if (argc >= 7) f_in = getnumber(argv[6]);
        if (f_in < 1.0 || f_in > 20000.0) error("Valid is 1Hz to 20KHz");
        int vol = 25;
        if (argc == 9) vol = getint(argv[8], 0, 25);
        const char * ch = (left && right) ? "B" : (left ? "L" : "R");
        audio_cmd_play_prepare_sound();
        if (strcasecmp(type, "O") == 0) {
            if (!sound_owns_backend()) return 1;
            hal_audio_sound(slot, ch, type, (double)f_in, vol);
            sound_mark_slot(slot, left, right, type);
            sound_stop_if_all_off();
            return 1;
        }
        hal_audio_sound(slot, ch, type, (double)f_in, vol);
        sound_mark_slot(slot, left, right, type);
        CurrentlyPlaying = P_SOUND;
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"LOAD SOUND"))) {
        if (!audio_cmd_play_common_can_handle("LOAD SOUND")) {
            const audio_play_hooks_t * hooks = audio_play_hooks();
            play_hook_args(hooks ? hooks->play_load_sound : NULL, tp, "LOAD SOUND");
            return 1;
        }
        if (usertable != NULL) error("Already loaded");
        int64_t * aint;
        skipspace(tp);
        int size = parseintegerarray(tp, &aint, 1, 1, NULL, false);
        if (size != 1024) error("Array size");
        usertable = (unsigned short *)aint;
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"NOTE"))) {
        if (!audio_cmd_play_common_can_handle("NOTE")) {
            const audio_play_hooks_t * hooks = audio_play_hooks();
            play_hook_args(hooks ? hooks->play_note : NULL, tp, "NOTE");
            return 1;
        }
        unsigned char * xp;
        if ((xp = checkstring(tp, (unsigned char *)"ON"))) {
            getargs(&xp, 5, (unsigned char *)",");
            if (argc != 5) error("Syntax");
            int channel = getint(argv[0], 0, 3);
            int note = getint(argv[2], 0, 127);
            int velocity = getint(argv[4], 0, 127);
            int slot = channel + 1;
            audio_cmd_play_prepare_sound();
            if (velocity == 0) {
                if (!sound_owns_backend()) return 1;
                hal_audio_sound(slot, "B", "O", 10.0, 0);
                sound_mark_slot(slot, 1, 1, "O");
                sound_stop_if_all_off();
                return 1;
            } else {
                hal_audio_sound(slot, "B", "S",
                                audio_play_midi_note_frequency(note),
                                audio_play_note_velocity_volume(velocity));
                sound_mark_slot(slot, 1, 1, "S");
            }
            CurrentlyPlaying = P_SOUND;
            return 1;
        }
        if ((xp = checkstring(tp, (unsigned char *)"OFF"))) {
            getargs(&xp, 5, (unsigned char *)",");
            if (!(argc == 3 || argc == 5)) error("Syntax");
            int channel = getint(argv[0], 0, 3);
            (void)getint(argv[2], 0, 127);
            if (argc == 5) (void)getint(argv[4], 0, 127);
            audio_cmd_play_prepare_sound();
            if (!sound_owns_backend()) return 1;
            hal_audio_sound(channel + 1, "B", "O", 10.0, 0);
            sound_mark_slot(channel + 1, 1, 1, "O");
            sound_stop_if_all_off();
            return 1;
        }
        error("Syntax");
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"WAV"))) {
        if (!audio_cmd_play_common_can_handle("WAV")) {
            play_hook_file("WAV", tp);
            return 1;
        }
        unsigned char * hook_args = tp;
        getargs(&tp, 3, (unsigned char *)",");
        if (!(argc == 1 || argc == 3)) error("Argument count");
        char * fname = (char *)getFstring(argv[0]);
        if (!audio_cmd_play_file_can_use_common("WAV", fname)) {
            play_hook_file("WAV", hook_args);
            return 1;
        }
        audio_cmd_play_prepare_file("WAV");
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_wav(fname) != 0) error("Cannot play file");
        if (argc == 3) {
            if (!CurrentLinePtr) error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);
            WAVcomplete = false;
            InterruptUsed = true;
        }
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MP3"))) {
        if (!audio_cmd_play_common_can_handle("MP3")) {
            play_hook_file("MP3", tp);
            return 1;
        }
        unsigned char * hook_args = tp;
        getargs(&tp, 3, (unsigned char *)",");
        if (!(argc == 1 || argc == 3)) error("Argument count");
        char * fname = (char *)getFstring(argv[0]);
        if (!audio_cmd_play_file_can_use_common("MP3", fname)) {
            play_hook_file("MP3", hook_args);
            return 1;
        }
        audio_cmd_play_prepare_file("MP3");
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_mp3(fname) != 0) error("Cannot play file");
        if (argc == 3) {
            if (!CurrentLinePtr) error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);
            WAVcomplete = false;
            InterruptUsed = true;
        }
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"FLAC"))) {
        if (!audio_cmd_play_common_can_handle("FLAC")) {
            play_hook_file("FLAC", tp);
            return 1;
        }
        unsigned char * hook_args = tp;
        getargs(&tp, 3, (unsigned char *)",");
        if (!(argc == 1 || argc == 3)) error("Argument count");
        char * fname = (char *)getFstring(argv[0]);
        if (!audio_cmd_play_file_can_use_common("FLAC", fname)) {
            play_hook_file("FLAC", hook_args);
            return 1;
        }
        audio_cmd_play_prepare_file("FLAC");
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_flac(fname) != 0) error("Cannot play file");
        if (argc == 3) {
            if (!CurrentLinePtr) error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);
            WAVcomplete = false;
            InterruptUsed = true;
        }
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MODFILE"))) {
        if (!audio_cmd_play_common_can_handle("MODFILE")) {
            play_hook_file("MODFILE", tp);
            return 1;
        }
        unsigned char * hook_args = tp;
        getargs(&tp, 3, (unsigned char *)",");
        if (!(argc == 1 || argc == 3)) error("Argument count");
        char * fname = (char *)getFstring(argv[0]);
        if (!audio_cmd_play_file_can_use_common("MODFILE", fname)) {
            play_hook_file("MODFILE", hook_args);
            return 1;
        }
        audio_cmd_play_prepare_file("MODFILE");
        WAVInterrupt = NULL;
        WAVcomplete = 0;
        if (audio_stream_play_mod_noloop(fname, argc == 3) != 0) error("Cannot play file");
        if (argc == 3) {
            if (!CurrentLinePtr) error("No program running");
            WAVInterrupt = (char *)GetIntAddress(argv[2]);
            WAVcomplete = false;
            InterruptUsed = true;
        }
        return 1;
    }
    if (checkstring(cmdline, (unsigned char *)"NEXT")) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_noargs(hooks ? hooks->play_playlist_next : NULL, "NEXT");
        return 1;
    }
    if (checkstring(cmdline, (unsigned char *)"PREVIOUS")) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_noargs(hooks ? hooks->play_playlist_previous : NULL, "PREVIOUS");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MIDIFILE"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_midifile : NULL, tp, "MIDIFILE");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MIDI"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_midi : NULL, tp, "MIDI");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"STREAM"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_stream : NULL, tp, "STREAM");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"ARRAY"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_array : NULL, tp, "ARRAY");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"HALT"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_halt : NULL, tp, "HALT");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"CONTINUE"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_continue : NULL, tp, "CONTINUE");
        return 1;
    }
    if ((tp = checkstring(cmdline, (unsigned char *)"MODSAMPLE"))) {
        const audio_play_hooks_t * hooks = audio_play_hooks();
        play_hook_args(hooks ? hooks->play_modsample : NULL, tp, "MODSAMPLE");
        return 1;
    }
    return 0;
}

void MIPS16 cmd_play(void) {
    if (audio_cmd_play_common()) return;
    error("Unknown command");
}

#ifndef AUDIO_RUNTIME_BACKEND_EXTERNAL
int audio_runtime_backend_close(int all, e_CurrentlyPlaying was_playing) {
    (void)all;
    (void)was_playing;
    audio_stream_stop();
    hal_audio_stop();
    sound_clear_slots();
    WAV_fnbr = 0;
    usertable = NULL;
    FSerror = 0;
    return 0;
}

void audio_runtime_backend_stop(e_CurrentlyPlaying was_playing) {
    (void)was_playing;
    audio_stream_stop();
    hal_audio_stop();
    sound_clear_slots();
}

void audio_runtime_backend_service(void) {
    audio_stream_service();
}
void audio_checks(void) { /* no callback-driven playback on host */ }
#endif
