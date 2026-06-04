#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "esp_system.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "OptionCommands.h"
#include "port_config.h"
#include "hal/hal_pin.h"

#include "esp32_audio_options.h"

static int esp32_audio_parse_pin_arg(unsigned char *arg)
{
    unsigned char *p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p') && isdigit(p[2]))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

static int esp32_audio_pin_invalid(int pin)
{
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

void esp32_audio_print_options(void)
{
    if (Option.AUDIO_L || Option.audio_i2s_bclk) {
        MMPrintString("OPTION AUDIO ");
        if (Option.AUDIO_L) {
            MMPrintString((char *)PinDef[Option.AUDIO_L].pinname);
            MMputchar(',', 1);
            MMPrintString((char *)PinDef[Option.AUDIO_R].pinname);
        } else {
            MMPrintString("I2S ");
            MMPrintString((char *)PinDef[Option.audio_i2s_bclk].pinname);
            MMputchar(',', 1);
            MMPrintString((char *)PinDef[Option.audio_i2s_data].pinname);
        }
        PRet();
    }
}

static void esp32_audio_clear_options(void)
{
    if (!esp32_audio_pin_invalid(Option.AUDIO_L)) {
        hal_pin_deinit((uint32_t)PinDef[Option.AUDIO_L].GPno);
        ExtCurrentConfig[Option.AUDIO_L] = EXT_NOT_CONFIG;
    }
    if (!esp32_audio_pin_invalid(Option.AUDIO_R)) {
        hal_pin_deinit((uint32_t)PinDef[Option.AUDIO_R].GPno);
        ExtCurrentConfig[Option.AUDIO_R] = EXT_NOT_CONFIG;
    }
    if (!esp32_audio_pin_invalid(Option.audio_i2s_bclk)) {
        int ws_gpio = PinDef[Option.audio_i2s_bclk].GPno + 1;
        hal_pin_deinit((uint32_t)PinDef[Option.audio_i2s_bclk].GPno);
        ExtCurrentConfig[Option.audio_i2s_bclk] = EXT_NOT_CONFIG;
        if (ws_gpio >= 0 && ws_gpio < HAL_PORT_GPIO_COUNT) {
            int ws_pin = codemap(ws_gpio);
            if (!esp32_audio_pin_invalid(ws_pin)) {
                hal_pin_deinit((uint32_t)PinDef[ws_pin].GPno);
                ExtCurrentConfig[ws_pin] = EXT_NOT_CONFIG;
            }
        }
    }
    if (!esp32_audio_pin_invalid(Option.audio_i2s_data)) {
        hal_pin_deinit((uint32_t)PinDef[Option.audio_i2s_data].GPno);
        ExtCurrentConfig[Option.audio_i2s_data] = EXT_NOT_CONFIG;
    }
    Option.AUDIO_L = 0;
    Option.AUDIO_R = 0;
    Option.AUDIO_SLICE = 0;
    Option.AUDIO_CLK_PIN = 0;
    Option.AUDIO_MOSI_PIN = 0;
    Option.AUDIO_MISO_PIN = 0;
    Option.AUDIO_CS_PIN = 0;
    Option.AUDIO_DCS_PIN = 0;
    Option.AUDIO_DREQ_PIN = 0;
    Option.AUDIO_RESET_PIN = 0;
    Option.audio_i2s_bclk = 0;
    Option.audio_i2s_data = 0;
}

void disable_audio(void)
{
    esp32_audio_clear_options();
}

static void esp32_audio_require_free_pin(int pin)
{
    if (esp32_audio_pin_invalid(pin))
        error("Invalid pin");
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG)
        error("Pin %/| is in use", pin, pin);
}

static void esp32_audio_save_options(void)
{
    SaveOptions();
    _excep_code = RESET_COMMAND;
    esp_restart();
}

int esp32_audio_option_setter(unsigned char *line)
{
    unsigned char *tp = checkstring(line, (unsigned char *)"AUDIO");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");

    if (checkstring(tp, (unsigned char *)"DISABLE")) {
        esp32_audio_clear_options();
        esp32_audio_save_options();
        return 1;
    }

    if (checkstring(tp, (unsigned char *)"VS1053") ||
        checkstring(tp, (unsigned char *)"SPI"))
        error("Not supported on this port");
    if (checkstring(tp, (unsigned char *)"PWM"))
        error("PWM not supported on this port");

    if ((line = checkstring(tp, (unsigned char *)"I2S"))) {
        int bclk_pin, data_pin, ws_pin;
        getargs(&line, 3, (unsigned char *)",");
        if (argc != 3) error("Syntax");
        if (Option.AUDIO_L || Option.audio_i2s_bclk || Option.AUDIO_CLK_PIN)
            error("Audio already configured");
        bclk_pin = esp32_audio_parse_pin_arg(argv[0]);
        data_pin = esp32_audio_parse_pin_arg(argv[2]);
        esp32_audio_require_free_pin(bclk_pin);
        esp32_audio_require_free_pin(data_pin);
        ws_pin = codemap(PinDef[bclk_pin].GPno + 1);
        esp32_audio_require_free_pin(ws_pin);
        if (ws_pin == data_pin) error("Pin %/| is in use", data_pin, data_pin);
        Option.audio_i2s_bclk = bclk_pin;
        Option.audio_i2s_data = data_pin;
        Option.AUDIO_L = 0;
        Option.AUDIO_R = 0;
        Option.AUDIO_SLICE = 0;
        ExtCurrentConfig[bclk_pin] = EXT_BOOT_RESERVED;
        ExtCurrentConfig[ws_pin] = EXT_BOOT_RESERVED;
        ExtCurrentConfig[data_pin] = EXT_BOOT_RESERVED;
        esp32_audio_save_options();
        return 1;
    }

    {
        int left_pin, right_pin;
        line = checkstring(tp, (unsigned char *)"PDM");
        if (!line) line = tp;
        getargs(&line, 3, (unsigned char *)",");
        if (argc != 3) error("Syntax");
        if (Option.AUDIO_L || Option.audio_i2s_bclk || Option.AUDIO_CLK_PIN)
            error("Audio already configured");
        left_pin = esp32_audio_parse_pin_arg(argv[0]);
        right_pin = esp32_audio_parse_pin_arg(argv[2]);
        esp32_audio_require_free_pin(left_pin);
        esp32_audio_require_free_pin(right_pin);
        if (left_pin == right_pin) error("Pin %/| is in use", right_pin, right_pin);
        Option.AUDIO_L = left_pin;
        Option.AUDIO_R = right_pin;
        Option.AUDIO_SLICE = 0;
        Option.audio_i2s_bclk = 0;
        Option.audio_i2s_data = 0;
        ExtCurrentConfig[left_pin] = EXT_BOOT_RESERVED;
        ExtCurrentConfig[right_pin] = EXT_BOOT_RESERVED;
        esp32_audio_save_options();
        return 1;
    }
}

int esp32_audio_mminfo(unsigned char *ep, unsigned char *out_sret, int *out_targ)
{
    if (!checkstring(ep, (unsigned char *)"AUDIO")) return 0;
    if (Option.AUDIO_L) strcpy((char *)out_sret, "PDM");
    else if (Option.audio_i2s_bclk) strcpy((char *)out_sret, "I2S");
    else strcpy((char *)out_sret, "OFF");
    CtoM(out_sret);
    *out_targ = T_STR;
    return 1;
}
