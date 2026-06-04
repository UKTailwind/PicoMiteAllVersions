#ifndef ESP32_AUDIO_OPTIONS_H
#define ESP32_AUDIO_OPTIONS_H

void esp32_audio_print_options(void);
int esp32_audio_option_setter(unsigned char *line);
int esp32_audio_mminfo(unsigned char *ep, unsigned char *out_sret, int *out_targ);

#endif
