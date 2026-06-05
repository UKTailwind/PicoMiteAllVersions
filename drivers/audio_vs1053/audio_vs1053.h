#ifndef DRIVERS_AUDIO_VS1053_AUDIO_VS1053_H
#define DRIVERS_AUDIO_VS1053_AUDIO_VS1053_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_vs1053_close_state(void);
void audio_vs1053_start_file(int mode);
void audio_vs1053_play_immediate(int play);
void audio_vs1053_play_wav(char * path);
void audio_vs1053_play_mp3(char * path, int position);
void audio_vs1053_play_flac(char * path);
void audio_vs1053_play_midi_file(char * path);
void audio_vs1053_play_note(unsigned char * args);
void audio_vs1053_play_stream(unsigned char * args);
void audio_vs1053_play_midi(unsigned char * args);
void audio_vs1053_play_midifile(unsigned char * args);
void audio_vs1053_play_halt(unsigned char * args);
void audio_vs1053_play_continue(unsigned char * args);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AUDIO_VS1053_AUDIO_VS1053_H */
