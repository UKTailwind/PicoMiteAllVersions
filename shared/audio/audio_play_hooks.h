/*
 * shared/audio/audio_play_hooks.h - extension hooks for non-common PLAY forms.
 */

#ifndef SHARED_AUDIO_AUDIO_PLAY_HOOKS_H
#define SHARED_AUDIO_AUDIO_PLAY_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_play_hooks {
    void (*play_load_sound)(unsigned char * args);
    void (*play_note)(unsigned char * args);
    void (*play_file)(const char * verb, unsigned char * args);
    void (*play_midifile)(unsigned char * args);
    void (*play_midi)(unsigned char * args);
    void (*play_stream)(unsigned char * args);
    void (*play_array)(unsigned char * args);
    void (*play_halt)(unsigned char * args);
    void (*play_continue)(unsigned char * args);
    void (*play_modsample)(unsigned char * args);
    void (*play_playlist_next)(void);
    void (*play_playlist_previous)(void);
} audio_play_hooks_t;

const audio_play_hooks_t * audio_play_hooks_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_AUDIO_AUDIO_PLAY_HOOKS_H */
