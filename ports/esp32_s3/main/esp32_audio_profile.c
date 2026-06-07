#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "esp32_audio_profile.h"
#include "esp32_board_profile.h"
#include "esp32_option_ext.h"

esp_err_t esp32_audio_profile_freenove_es8311_init(
    const esp32_audio_profile_t * profile);
void esp32_audio_profile_freenove_es8311_deinit(
    const esp32_audio_profile_t * profile);

static const esp32_audio_profile_t s_profiles[] = {
    {
        .id = ESP32_AUDIO_PROFILE_FREENOVE,
        .board_profile_id = ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341,
        .option_name = "FREENOVE",
        .i2s = {
            .mclk = ESP32_BOARD_FREENOVE_AUDIO_MCLK,
            .bclk = ESP32_BOARD_FREENOVE_AUDIO_BCLK,
            .ws = ESP32_BOARD_FREENOVE_AUDIO_WS,
            .dout = ESP32_BOARD_FREENOVE_AUDIO_DOUT,
            .din = ESP32_BOARD_PROFILE_NO_PIN,
        },
        .amp_enable = ESP32_BOARD_FREENOVE_AUDIO_AMP_EN,
        .amp_active_level = ESP32_BOARD_FREENOVE_AUDIO_AMP_ACTIVE_LEVEL,
        .init = esp32_audio_profile_freenove_es8311_init,
        .deinit = esp32_audio_profile_freenove_es8311_deinit,
    },
};

static const size_t s_profile_count = sizeof(s_profiles) / sizeof(s_profiles[0]);

const esp32_audio_profile_t * esp32_audio_profile_by_id(uint8_t id) {
    for (size_t i = 0; i < s_profile_count; i++)
        if (s_profiles[i].id == id) return &s_profiles[i];
    return NULL;
}

const esp32_audio_profile_t * esp32_audio_profile_by_name(unsigned char * p) {
    for (size_t i = 0; i < s_profile_count; i++)
        if (checkstring(p, (unsigned char *)s_profiles[i].option_name))
            return &s_profiles[i];
    return NULL;
}

int esp32_audio_profile_available_for_current_board(
    const esp32_audio_profile_t * profile) {
    return profile &&
           profile->board_profile_id == esp32_board_profile_current_id();
}

int esp32_audio_profile_pin_to_option(int gpio) {
    if (gpio < 0) return 0;
    int pin = codemap(gpio);
    if (pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED)) return 0;
    return pin;
}
