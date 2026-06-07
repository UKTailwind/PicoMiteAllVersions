/*
 * Shared Freenove I2C bus for onboard touch/audio peripherals.
 */

#include "esp32_freenove_i2c.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FREENOVE_I2C_PORT I2C_NUM_0
#define FREENOVE_I2C_TIMEOUT_MS 100

static const char * TAG = "freenove_i2c";
static SemaphoreHandle_t s_lock;
static int s_installed;
static int s_sda = -1;
static int s_scl = -1;
static uint32_t s_hz;

static esp_err_t take_bus(void) {
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(FREENOVE_I2C_TIMEOUT_MS)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static void give_bus(void) {
    if (s_lock) xSemaphoreGive(s_lock);
}

esp_err_t esp32_freenove_i2c_init(int sda_gpio, int scl_gpio, uint32_t hz) {
    uint32_t requested_hz = hz ? hz : 400000;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }
    esp_err_t lock_err = take_bus();
    if (lock_err != ESP_OK) return lock_err;
    if (s_installed) {
        if (s_sda == sda_gpio && s_scl == scl_gpio) {
            if (s_hz == requested_hz) {
                give_bus();
                return ESP_OK;
            }
            i2c_config_t conf = {
                .mode = I2C_MODE_MASTER,
                .sda_io_num = sda_gpio,
                .scl_io_num = scl_gpio,
                .sda_pullup_en = GPIO_PULLUP_ENABLE,
                .scl_pullup_en = GPIO_PULLUP_ENABLE,
                .master.clk_speed = requested_hz,
                .clk_flags = 0,
            };
            esp_err_t err = i2c_param_config(FREENOVE_I2C_PORT, &conf);
            if (err == ESP_OK) {
                s_hz = requested_hz;
                ESP_LOGI(TAG, "I2C speed changed to %lu Hz", (unsigned long)s_hz);
            }
            give_bus();
            return err;
        }
        ESP_LOGE(TAG, "I2C bus already installed on GPIO%d/GPIO%d", s_sda, s_scl);
        give_bus();
        return ESP_ERR_INVALID_STATE;
    }
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = requested_hz,
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(FREENOVE_I2C_PORT, &conf);
    if (err != ESP_OK) {
        give_bus();
        return err;
    }
    err = i2c_driver_install(FREENOVE_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        give_bus();
        return err;
    }
    s_installed = 1;
    s_sda = sda_gpio;
    s_scl = scl_gpio;
    s_hz = conf.master.clk_speed;
    ESP_LOGI(TAG, "I2C ready on GPIO%d/GPIO%d at %lu Hz",
             s_sda, s_scl, (unsigned long)s_hz);
    give_bus();
    return ESP_OK;
}

void esp32_freenove_i2c_deinit(void) {
    if (!s_lock) return;
    esp_err_t lock_err = take_bus();
    if (lock_err != ESP_OK) return;
    if (s_installed) {
        int old_sda = s_sda;
        int old_scl = s_scl;
        (void)i2c_driver_delete(FREENOVE_I2C_PORT);
        s_installed = 0;
        s_sda = -1;
        s_scl = -1;
        s_hz = 0;
        if (old_sda >= 0) (void)gpio_reset_pin((gpio_num_t)old_sda);
        if (old_scl >= 0) (void)gpio_reset_pin((gpio_num_t)old_scl);
        ESP_LOGI(TAG, "I2C deinitialized");
    }
    give_bus();
}

esp_err_t esp32_freenove_i2c_read_reg(uint8_t addr, uint8_t reg,
                                      uint8_t * data, size_t len) {
    if (!s_installed || !data || len == 0) return ESP_ERR_INVALID_STATE;
    esp_err_t err = take_bus();
    if (err != ESP_OK) return err;
    err = i2c_master_write_read_device(FREENOVE_I2C_PORT, addr, &reg, 1,
                                       data, len,
                                       pdMS_TO_TICKS(FREENOVE_I2C_TIMEOUT_MS));
    give_bus();
    return err;
}

esp_err_t esp32_freenove_i2c_write_reg(uint8_t addr, uint8_t reg,
                                       const uint8_t * data, size_t len) {
    if (!s_installed) return ESP_ERR_INVALID_STATE;
    if (len && !data) return ESP_ERR_INVALID_ARG;
    if (len > 15) return ESP_ERR_INVALID_SIZE;
    uint8_t buf[16];
    buf[0] = reg;
    if (len && data) memcpy(&buf[1], data, len);
    esp_err_t err = take_bus();
    if (err != ESP_OK) return err;
    err = i2c_master_write_to_device(FREENOVE_I2C_PORT, addr, buf, len + 1,
                                     pdMS_TO_TICKS(FREENOVE_I2C_TIMEOUT_MS));
    give_bus();
    return err;
}

esp_err_t esp32_freenove_i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value) {
    return esp32_freenove_i2c_write_reg(addr, reg, &value, 1);
}
