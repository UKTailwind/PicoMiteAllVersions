/*
 * Minimal FT6336U driver for the Freenove FNK0104B ILI9341 board.
 */

#include "esp32_ft6336u_touch.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "esp32_board_profile.h"
#include "esp32_freenove_i2c.h"

#define FT6336U_ADDR 0x38
#define FT_REG_DEVICE_MODE 0x00
#define FT_REG_TOUCHES 0x02
#define FT_REG_THRESHOLD 0x80
#define FT_REG_CTRL 0x86
#define FT_REG_TOUCHRATE_ACTIVE 0x88
#define FT_REG_CHIP_ID 0xA3
#define FT_REG_INTERRUPT_MODE 0xA4

#define FT_CHIP_FT6206 0x06
#define FT_CHIP_FT6236 0x36
#define FT_CHIP_FT6336 0x64

#define TOUCH_W 320
#define TOUCH_H 240

static const char * TAG = "ft6336u";
static int s_init_attempted;
static int s_ready;

static int gpio_valid(int gpio) {
    return gpio >= 0 && gpio < HAL_PORT_GPIO_COUNT;
}

static int touch_gpio_available(int gpio, int allow_shared_i2c_owner) {
    if (!gpio_valid(gpio)) return 0;
    int pin = codemap(gpio);
    if (pin <= 0 || pin > NBRPINS) return 0;
    if (ExtCurrentConfig[pin] == EXT_NOT_CONFIG) return 1;
    return allow_shared_i2c_owner &&
           esp32_board_profile_pin_owned_by_shared_i2c(pin);
}

static int touch_profile_pins_available(const esp32_board_profile_t * profile) {
    return touch_gpio_available(profile->touch.sda, 1) &&
           touch_gpio_available(profile->touch.scl, 1) &&
           touch_gpio_available(profile->touch.interrupt, 0) &&
           touch_gpio_available(profile->touch.reset, 0);
}

static void release_probe_i2c_if_unowned(const esp32_board_profile_t * profile) {
    int sda_pin = codemap(profile->touch.sda);
    int scl_pin = codemap(profile->touch.scl);
    if (!esp32_board_profile_pin_owned_by_shared_i2c(sda_pin) &&
        !esp32_board_profile_pin_owned_by_shared_i2c(scl_pin))
        esp32_freenove_i2c_deinit();
}

static void release_probe_gpios(const esp32_board_profile_t * profile) {
    if (gpio_valid(profile->touch.interrupt))
        (void)gpio_reset_pin((gpio_num_t)profile->touch.interrupt);
    if (gpio_valid(profile->touch.reset))
        (void)gpio_reset_pin((gpio_num_t)profile->touch.reset);
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int known_chip(uint8_t id) {
    return id == FT_CHIP_FT6206 || id == FT_CHIP_FT6236 || id == FT_CHIP_FT6336;
}

void esp32_ft6336u_touch_init(void) {
    if (s_init_attempted) return;
    s_init_attempted = 1;

    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (!profile->has_touch) return;
    if (!gpio_valid(profile->touch.sda) || !gpio_valid(profile->touch.scl)) {
        ESP_LOGW(TAG, "selected profile has incomplete touch I2C pins");
        return;
    }
    if (!touch_profile_pins_available(profile)) {
        ESP_LOGW(TAG, "selected profile touch pins are already in use");
        return;
    }

    esp_err_t err = esp32_freenove_i2c_init(profile->touch.sda, profile->touch.scl,
                                            400000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return;
    }

    if (gpio_valid(profile->touch.interrupt)) {
        gpio_config_t in = {
            .pin_bit_mask = 1ULL << (uint32_t)profile->touch.interrupt,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        (void)gpio_config(&in);
    }

    if (gpio_valid(profile->touch.reset)) {
        gpio_config_t out = {
            .pin_bit_mask = 1ULL << (uint32_t)profile->touch.reset,
            .mode = GPIO_MODE_OUTPUT,
        };
        (void)gpio_config(&out);
        gpio_set_level((gpio_num_t)profile->touch.reset, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)profile->touch.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    uint8_t id = 0;
    err = esp32_freenove_i2c_read_reg(FT6336U_ADDR, FT_REG_CHIP_ID, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch controller not found: %s", esp_err_to_name(err));
        release_probe_gpios(profile);
        release_probe_i2c_if_unowned(profile);
        return;
    }
    if (!known_chip(id)) ESP_LOGW(TAG, "unexpected FT6x36 chip id 0x%02x", id);

    (void)esp32_freenove_i2c_write_reg8(FT6336U_ADDR, FT_REG_DEVICE_MODE, 0x00);
    (void)esp32_freenove_i2c_write_reg8(FT6336U_ADDR, FT_REG_INTERRUPT_MODE, 0x00);
    (void)esp32_freenove_i2c_write_reg8(FT6336U_ADDR, FT_REG_CTRL, 0x00);
    (void)esp32_freenove_i2c_write_reg8(FT6336U_ADDR, FT_REG_THRESHOLD, 22);
    (void)esp32_freenove_i2c_write_reg8(FT6336U_ADDR, FT_REG_TOUCHRATE_ACTIVE, 0x01);

    s_ready = 1;
    esp32_board_profile_reserve_touch_pins();
    ESP_LOGI(TAG, "FT6x36 touch ready, chip id 0x%02x", id);
}

int esp32_ft6336u_touch_is_ready(void) {
    return s_ready;
}

int esp32_ft6336u_touch_read(int index, int * x, int * y) {
    if (!s_init_attempted) esp32_ft6336u_touch_init();
    if (!s_ready || index < 0 || index > 1) return 0;

    uint8_t buf[11] = {0};
    esp_err_t err = esp32_freenove_i2c_read_reg(FT6336U_ADDR, FT_REG_TOUCHES,
                                                buf, sizeof(buf));
    if (err != ESP_OK) return 0;

    int count = buf[0] & 0x0f;
    if (count <= index || count > 2) return 0;

    int off = index ? 7 : 1;
    int raw_x = ((int)(buf[off] & 0x0f) << 8) | buf[off + 1];
    int raw_y = ((int)(buf[off + 2] & 0x0f) << 8) | buf[off + 3];

    if (x) *x = clampi(raw_y, 0, TOUCH_W - 1);
    if (y) *y = clampi((TOUCH_H - 1) - raw_x, 0, TOUCH_H - 1);
    return 1;
}

int esp32_ft6336u_touch_down(void) {
    return esp32_ft6336u_touch_read(0, NULL, NULL);
}
