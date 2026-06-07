/*
 * esp32_board_profile.h - ESP32-S3 board profile table.
 *
 * The firmware is one ESP32-S3 image. Board-specific wiring is selected by
 * the persisted profile id in Option.extensions[].
 */

#ifndef ESP32_BOARD_PROFILE_H
#define ESP32_BOARD_PROFILE_H

#include <stdint.h>

#define ESP32_BOARD_PROFILE_ID_GENERIC 1
#define ESP32_BOARD_PROFILE_ID_METRO 2
#define ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341 3

#define ESP32_BOARD_GENERIC_NAME "GENERIC"
#define ESP32_BOARD_GENERIC_DEVICE_NAME "MMBasic ESP32-S3"

#define ESP32_BOARD_METRO_NAME "METRO"
#define ESP32_BOARD_METRO_DEVICE_NAME "MMBasic ESP32-S3 Metro"
#define ESP32_BOARD_METRO_SD_SCLK 39
#define ESP32_BOARD_METRO_SD_MOSI 42
#define ESP32_BOARD_METRO_SD_MISO 21
#define ESP32_BOARD_METRO_SD_CS 45
#define ESP32_BOARD_METRO_AUDIO_BCLK 5
#define ESP32_BOARD_METRO_AUDIO_WS 6
#define ESP32_BOARD_METRO_AUDIO_DOUT 7
#define ESP32_BOARD_METRO_WS2812_PIN 13

#define ESP32_BOARD_FREENOVE_ILI9341_NAME "FREENOVE ILI9341"
#define ESP32_BOARD_FREENOVE_ILI9341_DEVICE_NAME "MMBasic ESP32-S3 Freenove ILI9341"
#define ESP32_BOARD_FREENOVE_SD_SCLK 38
#define ESP32_BOARD_FREENOVE_SD_MOSI 40
#define ESP32_BOARD_FREENOVE_SD_MISO 39
#define ESP32_BOARD_FREENOVE_SD_CS 47
#define ESP32_BOARD_FREENOVE_SD_D1 41
#define ESP32_BOARD_FREENOVE_SD_D2 48
#define ESP32_BOARD_FREENOVE_LCD_SCLK 12
#define ESP32_BOARD_FREENOVE_LCD_MOSI 11
#define ESP32_BOARD_FREENOVE_LCD_MISO 13
#define ESP32_BOARD_FREENOVE_LCD_CS 10
#define ESP32_BOARD_FREENOVE_LCD_DC 46
#define ESP32_BOARD_FREENOVE_LCD_RST -1
#define ESP32_BOARD_FREENOVE_LCD_BL 45
#define ESP32_BOARD_FREENOVE_LCD_SPI_HZ 40000000
#define ESP32_BOARD_FREENOVE_TOUCH_SDA 16
#define ESP32_BOARD_FREENOVE_TOUCH_SCL 15
#define ESP32_BOARD_FREENOVE_TOUCH_INT 17
#define ESP32_BOARD_FREENOVE_TOUCH_RST 18
#define ESP32_BOARD_FREENOVE_AUDIO_MCLK 4
#define ESP32_BOARD_FREENOVE_AUDIO_BCLK 5
#define ESP32_BOARD_FREENOVE_AUDIO_DIN 6
#define ESP32_BOARD_FREENOVE_AUDIO_DOUT 8
#define ESP32_BOARD_FREENOVE_AUDIO_WS 7
#define ESP32_BOARD_FREENOVE_AUDIO_AMP_EN 1
#define ESP32_BOARD_FREENOVE_AUDIO_AMP_ACTIVE_LEVEL 0
#define ESP32_BOARD_FREENOVE_AUDIO_I2C_SDA 16
#define ESP32_BOARD_FREENOVE_AUDIO_I2C_SCL 15
#define ESP32_BOARD_FREENOVE_ES8311_ADDR 0x18

#define ESP32_BOARD_PROFILE_NO_PIN -1
#define ESP32_BOARD_PROFILE_SD_SPI_FREQ_KHZ 10000
#define ESP32_BOARD_PROFILE_SD_SECTOR_SIZE 512

typedef enum {
    ESP32_AUDIO_SINK_NONE = 0,
    ESP32_AUDIO_SINK_I2S_DAC,
    ESP32_AUDIO_SINK_ES8311
} esp32_board_audio_sink_t;

typedef struct {
    int sclk;
    int mosi;
    int miso;
    int cs;
    int d1;
    int d2;
    int spi_freq_khz;
} esp32_board_sd_pins_t;

typedef struct {
    int sclk;
    int mosi;
    int miso;
    int cs;
    int dc;
    int rst;
    int backlight;
    int spi_hz;
} esp32_board_lcd_pins_t;

typedef struct {
    int sda;
    int scl;
    int interrupt;
    int reset;
} esp32_board_touch_pins_t;

typedef struct {
    esp32_board_audio_sink_t sink;
    int mclk;
    int bclk;
    int ws;
    int dout;
    int din;
    int amp_enable;
    int amp_active_level;
    int i2c_sda;
    int i2c_scl;
    int i2c_addr;
} esp32_board_audio_pins_t;

typedef struct {
    uint8_t id;
    const char * configure_name;
    const char * device_name;
    const char * platform_name;
    uint8_t has_sd;
    uint8_t has_lcd;
    uint8_t has_touch;
    uint8_t has_audio;
    uint8_t has_ws2812;
    esp32_board_sd_pins_t sd;
    esp32_board_lcd_pins_t lcd;
    esp32_board_touch_pins_t touch;
    esp32_board_audio_pins_t audio;
    int ws2812_pin;
} esp32_board_profile_t;

const esp32_board_profile_t * esp32_board_profile_current(void);
const esp32_board_profile_t * esp32_board_profile_by_id(uint8_t id);
const esp32_board_profile_t * esp32_board_profile_by_name(unsigned char * p);
const char * esp32_board_profile_device_name(void);
uint8_t esp32_board_profile_current_id(void);
void esp32_board_profile_set(uint8_t id);
int esp32_board_profile_option_setter(unsigned char * cmdline);
void esp32_board_profile_apply_defaults(const esp32_board_profile_t * profile);
void esp32_board_profile_reserve_pins(void);
void esp32_board_profile_reserve_lcd_pins(void);
void esp32_board_profile_reserve_touch_pins(void);
void esp32_board_profile_update_shared_i2c_pins(void);
int esp32_board_profile_pin_owned_by_shared_i2c(int pin);
void esp32_board_profile_print_option(void);

#endif /* ESP32_BOARD_PROFILE_H */
