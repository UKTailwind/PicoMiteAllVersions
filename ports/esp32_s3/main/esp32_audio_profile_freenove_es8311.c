#include "esp32_audio_profile.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_board_profile.h"
#include "esp32_freenove_i2c.h"

#define ES8311_I2C_HZ 400000u

static const char * TAG = "freenove_audio";

static void set_amp_level(const esp32_audio_profile_t * profile, int active) {
    if (!profile || profile->amp_enable < 0) return;
    gpio_set_level((gpio_num_t)profile->amp_enable,
                   active ? (profile->amp_active_level ? 1 : 0)
                          : (profile->amp_active_level ? 0 : 1));
}

static esp_err_t wr(uint8_t reg, uint8_t value) {
    return esp32_freenove_i2c_write_reg8(ESP32_BOARD_FREENOVE_ES8311_ADDR,
                                         reg, value);
}

static esp_err_t write_seq(const uint8_t (*seq)[2], size_t count) {
    for (size_t i = 0; i < count; i++) {
        esp_err_t err = wr(seq[i][0], seq[i][1]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t esp32_audio_profile_freenove_es8311_init(
    const esp32_audio_profile_t * profile) {
    if (!profile) return ESP_ERR_INVALID_ARG;
    if (profile->amp_enable >= 0) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << (uint32_t)profile->amp_enable,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) return err;
        set_amp_level(profile, 1);
    }

    esp_err_t err = esp32_freenove_i2c_init(ESP32_BOARD_FREENOVE_AUDIO_I2C_SDA,
                                            ESP32_BOARD_FREENOVE_AUDIO_I2C_SCL,
                                            ES8311_I2C_HZ);
    if (err != ESP_OK) {
        set_amp_level(profile, 0);
        return err;
    }

    uint8_t id = 0;
    err = esp32_freenove_i2c_read_reg(ESP32_BOARD_FREENOVE_ES8311_ADDR,
                                      0xfd, &id, 1);
    if (err != ESP_OK) {
        set_amp_level(profile, 0);
        return err;
    }

    static const uint8_t seq[][2] = {
        {0x00, 0x1f}, {0x00, 0x80}, {0x01, 0x3f}, {0x02, 0x00},
        {0x03, 0x10}, {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03},
        {0x07, 0x00}, {0x08, 0xff}, {0x09, 0x0c}, {0x0a, 0x0c},
        {0x0b, 0x00}, {0x0c, 0x1f}, {0x0d, 0x01}, {0x0e, 0x02},
        {0x0f, 0x44}, {0x10, 0x1f}, {0x11, 0x7f}, {0x12, 0x00},
        {0x13, 0x00}, {0x14, 0x10}, {0x15, 0x00}, {0x16, 0x24},
        {0x1b, 0x0a}, {0x1c, 0x6a}, {0x31, 0x00}, {0x32, 0xbf},
        {0x37, 0x08}, {0x44, 0x00},
    };
    vTaskDelay(pdMS_TO_TICKS(20));
    err = write_seq(seq, sizeof(seq) / sizeof(seq[0]));
    if (err == ESP_OK) ESP_LOGI(TAG, "ES8311 ready id=0x%02x", id);
    else set_amp_level(profile, 0);
    return err;
}

void esp32_audio_profile_freenove_es8311_deinit(
    const esp32_audio_profile_t * profile) {
    if (!profile) return;
    (void)wr(0x09, 0x4c); /* mute DAC serial input */
    if (profile->amp_enable >= 0) {
        set_amp_level(profile, 0);
        gpio_reset_pin((gpio_num_t)profile->amp_enable);
    }
}
