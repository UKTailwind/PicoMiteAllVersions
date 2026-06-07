/*
 * Shared Freenove board I2C bus helpers.
 *
 * FT6336U touch and the future ES8311 codec both live on SDA=16/SCL=15.
 * Keep bus installation and locking here so those drivers do not fight over
 * I2C port ownership.
 */

#ifndef ESP32_FREENOVE_I2C_H
#define ESP32_FREENOVE_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t esp32_freenove_i2c_init(int sda_gpio, int scl_gpio, uint32_t hz);
void esp32_freenove_i2c_deinit(void);
esp_err_t esp32_freenove_i2c_read_reg(uint8_t addr, uint8_t reg,
                                      uint8_t * data, size_t len);
esp_err_t esp32_freenove_i2c_write_reg(uint8_t addr, uint8_t reg,
                                       const uint8_t * data, size_t len);
esp_err_t esp32_freenove_i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t value);

#endif /* ESP32_FREENOVE_I2C_H */
