#ifndef ESP32_AUDIO_PROFILE_H
#define ESP32_AUDIO_PROFILE_H

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int mclk;
    int bclk;
    int ws;
    int dout;
    int din;
} esp32_audio_i2s_pins_t;

typedef struct esp32_audio_profile_s {
    uint8_t id;
    uint8_t board_profile_id;
    const char * option_name;
    esp32_audio_i2s_pins_t i2s;
    int amp_enable;
    int amp_active_level;
    esp_err_t (*init)(const struct esp32_audio_profile_s * profile);
    void (*deinit)(const struct esp32_audio_profile_s * profile);
} esp32_audio_profile_t;

const esp32_audio_profile_t * esp32_audio_profile_by_id(uint8_t id);
const esp32_audio_profile_t * esp32_audio_profile_by_name(unsigned char * p);
int esp32_audio_profile_available_for_current_board(const esp32_audio_profile_t * profile);
int esp32_audio_profile_pin_to_option(int gpio);

#endif /* ESP32_AUDIO_PROFILE_H */
