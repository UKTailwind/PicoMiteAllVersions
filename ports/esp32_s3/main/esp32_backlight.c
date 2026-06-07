/*
 * ESP32-S3 board-profile LCD backlight control.
 */

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "esp32_backlight.h"
#include "esp32_board_profile.h"

#define BACKLIGHT_DEFAULT_FREQUENCY 50000
#define BACKLIGHT_MIN_FREQUENCY 100
#define BACKLIGHT_MAX_FREQUENCY 100000
#define BACKLIGHT_DUTY_MAX 255

static const char * TAG = "backlight";
static int s_pin = ESP32_BOARD_PROFILE_NO_PIN;
static int s_frequency = BACKLIGHT_DEFAULT_FREQUENCY;
static int s_ready;

static int backlight_pin(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (!profile || !profile->has_lcd) return ESP32_BOARD_PROFILE_NO_PIN;
    return profile->lcd.backlight;
}

static void backlight_init_pwm(int pin, int frequency) {
    if (s_ready && s_pin == pin && s_frequency == frequency) return;

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = (uint32_t)frequency,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer config failed: %s", esp_err_to_name(err));
        return;
    }

    ledc_channel_config_t channel = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = BACKLIGHT_DUTY_MAX,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "channel config failed: %s", esp_err_to_name(err));
        return;
    }

    s_pin = pin;
    s_frequency = frequency;
    s_ready = 1;
}

void esp32_backlight_set(int level, int frequency) {
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (frequency <= 0) frequency = BACKLIGHT_DEFAULT_FREQUENCY;

    int pin = backlight_pin();
    if (pin < 0 || pin >= HAL_PORT_GPIO_COUNT) error("Backlight not available on this display");
    backlight_init_pwm(pin, frequency);
    if (!s_ready) error("Backlight not set up");

    uint32_t duty = (uint32_t)((level * BACKLIGHT_DUTY_MAX + 50) / 100);
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) {
        gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
        error("Backlight PWM failed");
    }
}

void esp32_backlight_init_default(void) {
    int level = Option.BackLightLevel ? Option.BackLightLevel : 100;
    esp32_backlight_set(level, BACKLIGHT_DEFAULT_FREQUENCY);
}

void cmd_backlight(void) {
    getargs(&cmdline, 3, (unsigned char *)",");
    if (argc < 1) error("Syntax");
    int level = getint(argv[0], 0, 100);
    int frequency = BACKLIGHT_DEFAULT_FREQUENCY;
    if (argc == 3) {
        if (checkstring(argv[2], (unsigned char *)"DEFAULT")) {
            Option.BackLightLevel = level;
            SaveOptions();
        } else {
            frequency = getint(argv[2], BACKLIGHT_MIN_FREQUENCY,
                               BACKLIGHT_MAX_FREQUENCY);
        }
    }
    esp32_backlight_set(level, frequency);
}
