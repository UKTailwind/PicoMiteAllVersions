/*
 * shared/audio/audio_play_common.h — small PLAY-command helpers shared by
 * the command frontends and audio backends.
 */

#ifndef SHARED_AUDIO_PLAY_COMMON_H
#define SHARED_AUDIO_PLAY_COMMON_H

#include <math.h>

static inline double audio_play_midi_note_frequency(int note)
{
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

static inline int audio_play_note_velocity_volume(int velocity)
{
    if (velocity <= 0) return 0;
    return (velocity * 25 + 126) / 127;
}

static inline int audio_play_volume_to_synth(int volume)
{
    return volume * 41 / 25;
}

#endif /* SHARED_AUDIO_PLAY_COMMON_H */
