/*
 * esp32_board_profile.c - ESP32-S3 board profile selection.
 */

#include <stddef.h>
#include <string.h>

#include "esp_system.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "esp32_board_profile.h"
#include "esp32_option_ext.h"

extern int esp32_ft6336u_touch_is_ready(void);

static int s_sd_pins_reserved;
static int s_lcd_pins_reserved;
static int s_touch_pins_reserved;
static int s_ws2812_pins_reserved;

static const esp32_board_profile_t s_profiles[] = {
    {
        .id = ESP32_BOARD_PROFILE_ID_GENERIC,
        .configure_name = ESP32_BOARD_GENERIC_NAME,
        .device_name = ESP32_BOARD_GENERIC_DEVICE_NAME,
        .platform_name = ESP32_BOARD_GENERIC_NAME,
        .has_sd = 0,
        .has_lcd = 0,
        .has_touch = 0,
        .has_audio = 0,
        .has_ws2812 = 0,
        .sd = {ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
               ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
               ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN, 0},
        .lcd = {ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, 0},
        .touch = {ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN},
        .audio = {ESP32_AUDIO_SINK_NONE, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, 0, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, 0},
        .ws2812_pin = ESP32_BOARD_PROFILE_NO_PIN,
    },
    {
        .id = ESP32_BOARD_PROFILE_ID_METRO,
        .configure_name = ESP32_BOARD_METRO_NAME,
        .device_name = ESP32_BOARD_METRO_DEVICE_NAME,
        .platform_name = ESP32_BOARD_METRO_NAME,
        .has_sd = 1,
        .has_lcd = 0,
        .has_touch = 0,
        .has_audio = 1,
        .has_ws2812 = 1,
        .sd = {ESP32_BOARD_METRO_SD_SCLK, ESP32_BOARD_METRO_SD_MOSI,
               ESP32_BOARD_METRO_SD_MISO, ESP32_BOARD_METRO_SD_CS,
               ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
               ESP32_BOARD_PROFILE_SD_SPI_FREQ_KHZ},
        .lcd = {ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                ESP32_BOARD_PROFILE_NO_PIN, 0},
        .touch = {ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, ESP32_BOARD_PROFILE_NO_PIN},
        .audio = {ESP32_AUDIO_SINK_I2S_DAC, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_METRO_AUDIO_BCLK, ESP32_BOARD_METRO_AUDIO_WS,
                  ESP32_BOARD_METRO_AUDIO_DOUT, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, 0, ESP32_BOARD_PROFILE_NO_PIN,
                  ESP32_BOARD_PROFILE_NO_PIN, 0},
        .ws2812_pin = ESP32_BOARD_METRO_WS2812_PIN,
    },
    {
        .id = ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341,
        .configure_name = ESP32_BOARD_FREENOVE_ILI9341_NAME,
        .device_name = ESP32_BOARD_FREENOVE_ILI9341_DEVICE_NAME,
        .platform_name = ESP32_BOARD_FREENOVE_ILI9341_NAME,
        .has_sd = 1,
        .has_lcd = 1,
        .has_touch = 1,
        .has_audio = 1,
        .has_ws2812 = 0,
        .sd = {ESP32_BOARD_FREENOVE_SD_SCLK, ESP32_BOARD_FREENOVE_SD_MOSI,
               ESP32_BOARD_FREENOVE_SD_MISO, ESP32_BOARD_FREENOVE_SD_CS,
               ESP32_BOARD_FREENOVE_SD_D1, ESP32_BOARD_FREENOVE_SD_D2,
               ESP32_BOARD_PROFILE_SD_SPI_FREQ_KHZ},
        .lcd = {ESP32_BOARD_FREENOVE_LCD_SCLK, ESP32_BOARD_FREENOVE_LCD_MOSI,
                ESP32_BOARD_FREENOVE_LCD_MISO, ESP32_BOARD_FREENOVE_LCD_CS,
                ESP32_BOARD_FREENOVE_LCD_DC, ESP32_BOARD_FREENOVE_LCD_RST,
                ESP32_BOARD_FREENOVE_LCD_BL, ESP32_BOARD_FREENOVE_LCD_SPI_HZ},
        .touch = {ESP32_BOARD_FREENOVE_TOUCH_SDA, ESP32_BOARD_FREENOVE_TOUCH_SCL,
                  ESP32_BOARD_FREENOVE_TOUCH_INT, ESP32_BOARD_FREENOVE_TOUCH_RST},
        .audio = {ESP32_AUDIO_SINK_ES8311, ESP32_BOARD_FREENOVE_AUDIO_MCLK,
                  ESP32_BOARD_FREENOVE_AUDIO_BCLK, ESP32_BOARD_FREENOVE_AUDIO_WS,
                  ESP32_BOARD_FREENOVE_AUDIO_DOUT, ESP32_BOARD_FREENOVE_AUDIO_DIN,
                  ESP32_BOARD_FREENOVE_AUDIO_AMP_EN,
                  ESP32_BOARD_FREENOVE_AUDIO_AMP_ACTIVE_LEVEL,
                  ESP32_BOARD_FREENOVE_AUDIO_I2C_SDA,
                  ESP32_BOARD_FREENOVE_AUDIO_I2C_SCL,
                  ESP32_BOARD_FREENOVE_ES8311_ADDR},
        .ws2812_pin = ESP32_BOARD_PROFILE_NO_PIN,
    },
};

static const size_t s_profile_count = sizeof(s_profiles) / sizeof(s_profiles[0]);

const esp32_board_profile_t * esp32_board_profile_by_id(uint8_t id) {
    for (size_t i = 0; i < s_profile_count; i++)
        if (s_profiles[i].id == id) return &s_profiles[i];
    return NULL;
}

const esp32_board_profile_t * esp32_board_profile_by_name(unsigned char * p) {
    for (size_t i = 0; i < s_profile_count; i++)
        if (checkstring(p, (unsigned char *)s_profiles[i].configure_name))
            return &s_profiles[i];
    if (checkstring(p, (unsigned char *)"FREENOVE"))
        return esp32_board_profile_by_id(ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341);
    return NULL;
}

const esp32_board_profile_t * esp32_board_profile_current(void) {
    const esp32_board_profile_t * profile =
        esp32_board_profile_by_id(ESP32_OPTION_BOARD_PROFILE);
    return profile ? profile : &s_profiles[0];
}

const char * esp32_board_profile_device_name(void) {
    return esp32_board_profile_current()->device_name;
}

uint8_t esp32_board_profile_current_id(void) {
    return esp32_board_profile_current()->id;
}

void esp32_board_profile_set(uint8_t id) {
    if (!esp32_board_profile_by_id(id)) id = ESP32_BOARD_PROFILE_ID_GENERIC;
    ESP32_OPTION_BOARD_PROFILE = id;
}

static int profile_pin(int gpio) {
    return gpio >= 0 ? codemap(gpio) : 0;
}

static int profile_pin_invalid(int pin) {
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

static void reserve_profile_gpio(int gpio) {
    int pin = profile_pin(gpio);
    if (!profile_pin_invalid(pin)) ExtCurrentConfig[pin] = EXT_BOOT_RESERVED;
}

static void release_profile_gpio(int gpio) {
    int pin = profile_pin(gpio);
    if (!profile_pin_invalid(pin) && ExtCurrentConfig[pin] == EXT_BOOT_RESERVED) {
        ExtCurrentConfig[pin] = EXT_NOT_CONFIG;
    }
}

static int profile_pin_matches_gpio(int pin, int gpio) {
    return !profile_pin_invalid(pin) && pin == profile_pin(gpio);
}

static int current_audio_profile_uses_shared_i2c(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    return profile->id == ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341 &&
           ESP32_OPTION_AUDIO_KIND == ESP32_AUDIO_KIND_PROFILE &&
           ESP32_OPTION_AUDIO_PROFILE == ESP32_AUDIO_PROFILE_FREENOVE;
}

static int current_shared_i2c_enabled(void) {
    return esp32_ft6336u_touch_is_ready() || current_audio_profile_uses_shared_i2c();
}

static int current_profile_shared_i2c_pin(int pin) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (profile->id != ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341) return 0;
    return profile_pin_matches_gpio(pin, profile->touch.sda) ||
           profile_pin_matches_gpio(pin, profile->touch.scl);
}

static void set_platform_name(const esp32_board_profile_t * profile) {
    strncpy((char *)Option.platform, profile->platform_name, sizeof(Option.platform) - 1);
    Option.platform[sizeof(Option.platform) - 1] = '\0';
}

void esp32_board_profile_apply_defaults(const esp32_board_profile_t * profile) {
    if (!profile) profile = &s_profiles[0];
    esp32_board_profile_set(profile->id);
    set_platform_name(profile);

    Option.SD_CS = 0;
    Option.SD_CLK_PIN = 0;
    Option.SD_MOSI_PIN = 0;
    Option.SD_MISO_PIN = 0;
    Option.SYSTEM_CLK = 0;
    Option.SYSTEM_MOSI = 0;
    Option.SYSTEM_MISO = 0;
    Option.LCD_CLK = 0;
    Option.LCD_MOSI = 0;
    Option.LCD_MISO = 0;
    Option.LCD_CD = 0;
    Option.LCD_CS = 0;
    Option.LCD_Reset = 0;
    Option.DISPLAY_TYPE = 0;
    Option.DISPLAY_CONSOLE = 0;

    Option.AUDIO_L = 0;
    Option.AUDIO_R = 0;
    Option.AUDIO_SLICE = 0;
    Option.audio_i2s_bclk = 0;
    Option.audio_i2s_data = 0;
    ESP32_OPTION_AUDIO_KIND = ESP32_AUDIO_KIND_OFF;
    ESP32_OPTION_AUDIO_PROFILE = ESP32_AUDIO_PROFILE_NONE;
    ESP32_OPTION_AUDIO_I2S_WS = 0;
    ESP32_OPTION_AUDIO_I2S_MCLK = 0;

    if (profile->has_sd) {
        Option.SD_CS = profile_pin(profile->sd.cs);
        Option.SD_CLK_PIN = profile_pin(profile->sd.sclk);
        Option.SD_MOSI_PIN = profile_pin(profile->sd.mosi);
        Option.SD_MISO_PIN = profile_pin(profile->sd.miso);
    }
    if (profile->has_lcd) {
        Option.LCD_CLK = profile_pin(profile->lcd.sclk);
        Option.LCD_MOSI = profile_pin(profile->lcd.mosi);
        Option.LCD_MISO = profile_pin(profile->lcd.miso);
        Option.LCD_CD = profile_pin(profile->lcd.dc);
        Option.LCD_CS = profile_pin(profile->lcd.cs);
        Option.LCD_Reset = profile_pin(profile->lcd.rst);
        Option.DISPLAY_TYPE = ILI9341;
        Option.DISPLAY_ORIENTATION = LANDSCAPE;
        Option.DefaultFont = 0x01;
        Option.DefaultFC = WHITE;
        Option.DefaultBC = BLACK;
        Option.ColourCode = 1;
    }

    if (profile->audio.sink == ESP32_AUDIO_SINK_I2S_DAC) {
        Option.audio_i2s_bclk = profile_pin(profile->audio.bclk);
        Option.audio_i2s_data = profile_pin(profile->audio.dout);
        ESP32_OPTION_AUDIO_I2S_WS = profile_pin(profile->audio.ws);
        ESP32_OPTION_AUDIO_KIND = ESP32_AUDIO_KIND_I2S;
    } else if (profile->audio.sink == ESP32_AUDIO_SINK_ES8311) {
        Option.audio_i2s_bclk = profile_pin(profile->audio.bclk);
        Option.audio_i2s_data = profile_pin(profile->audio.dout);
        ESP32_OPTION_AUDIO_I2S_WS = profile_pin(profile->audio.ws);
        ESP32_OPTION_AUDIO_I2S_MCLK = profile_pin(profile->audio.mclk);
        ESP32_OPTION_AUDIO_KIND = ESP32_AUDIO_KIND_PROFILE;
        ESP32_OPTION_AUDIO_PROFILE = ESP32_AUDIO_PROFILE_FREENOVE;
    }
}

int esp32_board_profile_option_setter(unsigned char * cmdline) {
    unsigned char * tp = checkstring(cmdline, (unsigned char *)"PLATFORM");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");

    const esp32_board_profile_t * profile = NULL;
    skipspace(tp);
    if (checkstring(tp, (unsigned char *)"LIST")) {
        for (size_t i = 0; i < s_profile_count; i++) {
            MMPrintString((char *)s_profiles[i].configure_name);
            MMPrintString("\r\n");
        }
        return 1;
    }
    if (*tp == '"') {
        char text[STRINGSIZE];
        strcpy(text, (char *)getCstring(tp));
        profile = esp32_board_profile_by_name((unsigned char *)text);
    } else {
        profile = esp32_board_profile_by_name(tp);
    }
    if (!profile) error("Invalid platform");

    esp32_board_profile_apply_defaults(profile);
    SaveOptions();
    MMPrintString("Restarting\r\n");
    esp_restart();
    return 1;
}

void esp32_board_profile_reserve_pins(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (profile->has_sd) {
        reserve_profile_gpio(profile->sd.sclk);
        reserve_profile_gpio(profile->sd.mosi);
        reserve_profile_gpio(profile->sd.miso);
        reserve_profile_gpio(profile->sd.cs);
        reserve_profile_gpio(profile->sd.d1);
        reserve_profile_gpio(profile->sd.d2);
        s_sd_pins_reserved = 1;
    }
    if (profile->has_ws2812) {
        reserve_profile_gpio(profile->ws2812_pin);
        s_ws2812_pins_reserved = 1;
    }
}

void esp32_board_profile_reserve_lcd_pins(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (!profile->has_lcd) return;
    reserve_profile_gpio(profile->lcd.sclk);
    reserve_profile_gpio(profile->lcd.mosi);
    reserve_profile_gpio(profile->lcd.miso);
    reserve_profile_gpio(profile->lcd.cs);
    reserve_profile_gpio(profile->lcd.dc);
    reserve_profile_gpio(profile->lcd.rst);
    reserve_profile_gpio(profile->lcd.backlight);
    s_lcd_pins_reserved = 1;
}

void esp32_board_profile_reserve_touch_pins(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (!profile->has_touch) return;
    esp32_board_profile_update_shared_i2c_pins();
    reserve_profile_gpio(profile->touch.interrupt);
    reserve_profile_gpio(profile->touch.reset);
    s_touch_pins_reserved = 1;
}

void esp32_board_profile_update_shared_i2c_pins(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (profile->id != ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341) return;
    if (current_shared_i2c_enabled()) {
        reserve_profile_gpio(profile->touch.sda);
        reserve_profile_gpio(profile->touch.scl);
    } else {
        release_profile_gpio(profile->touch.sda);
        release_profile_gpio(profile->touch.scl);
    }
}

int esp32_board_profile_pin_owned_by_shared_i2c(int pin) {
    return current_profile_shared_i2c_pin(pin) &&
           current_shared_i2c_enabled() &&
           ExtCurrentConfig[pin] == EXT_BOOT_RESERVED;
}

static int pin_is_audio_profile_pin(int pin) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (ESP32_OPTION_AUDIO_KIND != ESP32_AUDIO_KIND_PROFILE ||
        ESP32_OPTION_AUDIO_PROFILE != ESP32_AUDIO_PROFILE_FREENOVE ||
        profile->id != ESP32_BOARD_PROFILE_ID_FREENOVE_ILI9341)
        return 0;
    return profile_pin_matches_gpio(pin, profile->audio.mclk) ||
           profile_pin_matches_gpio(pin, profile->audio.bclk) ||
           profile_pin_matches_gpio(pin, profile->audio.ws) ||
           profile_pin_matches_gpio(pin, profile->audio.dout) ||
           profile_pin_matches_gpio(pin, profile->audio.amp_enable);
}

static int pin_is_generic_audio_pin(int pin) {
    if (Option.AUDIO_L && (pin == Option.AUDIO_L || pin == Option.AUDIO_R))
        return 1;
    if (!Option.audio_i2s_bclk) return 0;
    return pin == Option.audio_i2s_bclk ||
           pin == ESP32_OPTION_AUDIO_I2S_WS ||
           pin == Option.audio_i2s_data ||
           pin == ESP32_OPTION_AUDIO_I2S_MCLK;
}

static int pin_is_vga_pin(int pin) {
    if (pin == Option.VGA_HSYNC || pin == ESP32_OPTION_VGA_VSYNC)
        return 1;
    for (int i = 0; i < ESP32_OPTION_VGA_DATA_COUNT; i++)
        if (pin == ESP32_OPTION_VGA_DATA[i]) return 1;
    return 0;
}

const char * port_pin_reserved_label(int pin) {
    if (profile_pin_invalid(pin) || ExtCurrentConfig[pin] != EXT_BOOT_RESERVED)
        return NULL;

    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (s_sd_pins_reserved &&
        (profile_pin_matches_gpio(pin, profile->sd.sclk) ||
         profile_pin_matches_gpio(pin, profile->sd.mosi) ||
         profile_pin_matches_gpio(pin, profile->sd.miso) ||
         profile_pin_matches_gpio(pin, profile->sd.cs) ||
         profile_pin_matches_gpio(pin, profile->sd.d1) ||
         profile_pin_matches_gpio(pin, profile->sd.d2)))
        return "Boot Reserved : SD";

    if (s_lcd_pins_reserved &&
        (profile_pin_matches_gpio(pin, profile->lcd.sclk) ||
         profile_pin_matches_gpio(pin, profile->lcd.mosi) ||
         profile_pin_matches_gpio(pin, profile->lcd.miso) ||
         profile_pin_matches_gpio(pin, profile->lcd.cs) ||
         profile_pin_matches_gpio(pin, profile->lcd.dc) ||
         profile_pin_matches_gpio(pin, profile->lcd.rst) ||
         profile_pin_matches_gpio(pin, profile->lcd.backlight)))
        return "Boot Reserved : LCD";

    if (esp32_board_profile_pin_owned_by_shared_i2c(pin))
        return "Boot Reserved : Shared I2C touch/audio";

    if (s_touch_pins_reserved &&
        (profile_pin_matches_gpio(pin, profile->touch.interrupt) ||
         profile_pin_matches_gpio(pin, profile->touch.reset)))
        return "Boot Reserved : Touch";

    if (pin_is_audio_profile_pin(pin) || pin_is_generic_audio_pin(pin))
        return "Boot Reserved : Audio";

    if (pin_is_vga_pin(pin))
        return "Boot Reserved : VGA";

    if (s_ws2812_pins_reserved && profile_pin_matches_gpio(pin, profile->ws2812_pin))
        return "Boot Reserved : WS2812";

    return NULL;
}

void esp32_board_profile_print_option(void) {
    MMPrintString("OPTION PLATFORM ");
    MMPrintString((char *)esp32_board_profile_current()->platform_name);
    MMPrintString("\r\n");
}
