/*
 * shared/audio/audio_runtime.c - shared audio runtime ownership.
 *
 * This module owns the BASIC-visible audio mode/completion state and the
 * public stop/close/service/interrupt entry points. Audio command/backend
 * bodies provide the backend hooks below.
 */

#include <stdbool.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "audio_runtime.h"

const char * const PlayingStr[] = {"PAUSED TONE", "PAUSED FLAC", "PAUSED MP3", "PAUSED SOUND", "PAUSED MOD", "PAUSED ARRAY", "PAUSED WAV", "OFF",
                                   "OFF", "TONE", "SOUND", "WAV", "FLAC", "MP3",
                                   "MIDI", "", "MOD", "STREAM", "ARRAY", ""};

volatile e_CurrentlyPlaying CurrentlyPlaying = P_NOTHING;
char * WAVInterrupt = NULL;
volatile bool WAVcomplete = 0;

volatile uint64_t SoundPlay = 0;
volatile float PhaseM_left = 0.0f, PhaseM_right = 0.0f;
volatile float PhaseAC_left = 0.0f, PhaseAC_right = 0.0f;
volatile uint8_t mono = 0;

int __attribute__((weak)) audio_runtime_backend_close(int all, e_CurrentlyPlaying was_playing) {
    (void)all;
    (void)was_playing;
    return 0;
}

void __attribute__((weak)) audio_runtime_backend_stop(e_CurrentlyPlaying was_playing) {
    (void)was_playing;
}

void __attribute__((weak)) audio_runtime_backend_service(void) {
}

int audio_runtime_mode_is_file(e_CurrentlyPlaying mode) {
    return mode == P_WAV || mode == P_FLAC || mode == P_MP3 ||
           mode == P_MIDI || mode == P_MOD || mode == P_ARRAY ||
           mode == P_STREAM;
}

void CloseAudio(int all) {
    e_CurrentlyPlaying was_playing = CurrentlyPlaying;
    int complete_interrupt = audio_runtime_mode_is_file(was_playing) &&
                             WAVInterrupt != NULL;
    int backend_complete = audio_runtime_backend_close(all, was_playing);
    CurrentlyPlaying = P_NOTHING;
    WAVcomplete = complete_interrupt || backend_complete;
    FSerror = 0;
}

void StopAudio(void) {
    e_CurrentlyPlaying was_playing = CurrentlyPlaying;
    audio_runtime_backend_stop(was_playing);
    CurrentlyPlaying = P_NOTHING;
    WAVcomplete = 0;
}

void audio_runtime_service(void) {
    audio_runtime_backend_service();
}

void checkWAVinput(void) {
    audio_runtime_service();
}

int audio_interrupt_pending(unsigned char ** target) {
    if (WAVInterrupt == NULL || !WAVcomplete) return 0;
    if (target) *target = (unsigned char *)WAVInterrupt;
    WAVcomplete = false;
    return 1;
}
