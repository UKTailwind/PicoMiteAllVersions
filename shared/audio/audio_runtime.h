#ifndef SHARED_AUDIO_AUDIO_RUNTIME_H
#define SHARED_AUDIO_AUDIO_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

void audio_runtime_service(void);
int audio_interrupt_pending(unsigned char **target);

#ifdef __cplusplus
}
#endif

#endif /* SHARED_AUDIO_AUDIO_RUNTIME_H */
