#ifndef SHARED_AUDIO_AUDIO_RUNTIME_H
#define SHARED_AUDIO_AUDIO_RUNTIME_H

#include "ffconf.h"
#include "Audio.h"

#ifdef __cplusplus
extern "C" {
#endif

void audio_runtime_service(void);
int audio_interrupt_pending(unsigned char ** target);
int audio_runtime_mode_is_file(e_CurrentlyPlaying mode);

int audio_runtime_backend_close(int all, e_CurrentlyPlaying was_playing);
void audio_runtime_backend_stop(e_CurrentlyPlaying was_playing);
void audio_runtime_backend_service(void);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_AUDIO_AUDIO_RUNTIME_H */
