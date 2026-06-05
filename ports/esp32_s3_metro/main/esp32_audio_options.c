#include <stdint.h>
#include <string.h>

#include "esp_system.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "OptionCommands.h"
#include "port_config.h"
#include "hal/hal_pin.h"
#include "shared/audio/audio_option_common.h"

#include "esp32_audio_options.h"

static int esp32_audio_pin_invalid(int pin)
{
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

static int esp32_audio_i2s_ws_pin(void)
{
    if (esp32_audio_pin_invalid(Option.audio_i2s_bclk))
        return 0;
    int ws_gpio = PinDef[Option.audio_i2s_bclk].GPno + 1;
    if (ws_gpio < 0 || ws_gpio >= HAL_PORT_GPIO_COUNT)
        return 0;
    return codemap(ws_gpio);
}

static int esp32_audio_pin_reserved(int pin)
{
    return !esp32_audio_pin_invalid(pin) && ExtCurrentConfig[pin] == EXT_BOOT_RESERVED;
}

static int esp32_audio_is_current_audio_pin(int pin)
{
    if (esp32_audio_pin_invalid(pin))
        return 0;

    if (!esp32_audio_pin_reserved(pin))
        return 0;

    if (Option.AUDIO_L && (pin == Option.AUDIO_L || pin == Option.AUDIO_R))
        return 1;

    if (Option.audio_i2s_bclk && (pin == Option.audio_i2s_bclk ||
                                  pin == Option.audio_i2s_data ||
                                  pin == esp32_audio_i2s_ws_pin()))
        return 1;

    return 0;
}

static void esp32_audio_release_pin(int pin)
{
    if (!esp32_audio_pin_reserved(pin))
        return;
    hal_pin_deinit((uint32_t)PinDef[pin].GPno);
    ExtCurrentConfig[pin] = EXT_NOT_CONFIG;
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
    if (Option.AUDIO_L) {
        esp32_audio_release_pin(Option.AUDIO_L);
        esp32_audio_release_pin(Option.AUDIO_R);
    }
    if (Option.audio_i2s_bclk) {
        esp32_audio_release_pin(Option.audio_i2s_bclk);
        esp32_audio_release_pin(esp32_audio_i2s_ws_pin());
        esp32_audio_release_pin(Option.audio_i2s_data);
    }
    audio_option_clear_common_fields(0);
}

void disable_audio(void)
{
    esp32_audio_clear_options();
}

static void esp32_audio_require_valid_pin(int pin)
{
    if (esp32_audio_pin_invalid(pin))
        error("Invalid pin");
}

static void esp32_audio_require_available_pin(int pin)
{
    esp32_audio_require_valid_pin(pin);
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG && !esp32_audio_is_current_audio_pin(pin))
        error("Pin %/| is in use", pin, pin);
}

static void esp32_audio_require_distinct_pin(int first_pin, int second_pin)
{
    if (first_pin == second_pin)
        error("Pin %/| is in use", second_pin, second_pin);
}

static void esp32_audio_reserve_i2s(int bclk_pin, int data_pin, int ws_pin)
{
    Option.audio_i2s_bclk = bclk_pin;
    Option.audio_i2s_data = data_pin;
    Option.AUDIO_L = 0;
    Option.AUDIO_R = 0;
    Option.AUDIO_SLICE = 0;
    ExtCurrentConfig[bclk_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[ws_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[data_pin] = EXT_BOOT_RESERVED;
}

static void esp32_audio_reserve_pdm(int left_pin, int right_pin)
{
    Option.AUDIO_L = left_pin;
    Option.AUDIO_R = right_pin;
    Option.AUDIO_SLICE = 0;
    Option.audio_i2s_bclk = 0;
    Option.audio_i2s_data = 0;
    ExtCurrentConfig[left_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[right_pin] = EXT_BOOT_RESERVED;
}

static void esp32_audio_require_available_pins3(int pin1, int pin2, int pin3)
{
    esp32_audio_require_available_pin(pin1);
    esp32_audio_require_available_pin(pin2);
    esp32_audio_require_available_pin(pin3);
}

static void esp32_audio_require_available_pins2(int pin1, int pin2)
{
    esp32_audio_require_available_pin(pin1);
    esp32_audio_require_available_pin(pin2);
}

static int esp32_audio_ws_from_bclk(int bclk_pin)
{
    esp32_audio_require_valid_pin(bclk_pin);
    int ws_pin = codemap(PinDef[bclk_pin].GPno + 1);
    esp32_audio_require_valid_pin(ws_pin);
    return ws_pin;
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
        bclk_pin = audio_option_parse_gp_pin(argv[0]);
        data_pin = audio_option_parse_gp_pin(argv[2]);
        esp32_audio_require_valid_pin(data_pin);
        ws_pin = esp32_audio_ws_from_bclk(bclk_pin);
        esp32_audio_require_distinct_pin(bclk_pin, data_pin);
        esp32_audio_require_distinct_pin(ws_pin, data_pin);
        esp32_audio_require_available_pins3(bclk_pin, data_pin, ws_pin);
        esp32_audio_clear_options();
        esp32_audio_reserve_i2s(bclk_pin, data_pin, ws_pin);
        esp32_audio_save_options();
        return 1;
    }

    {
        int left_pin, right_pin;
        line = checkstring(tp, (unsigned char *)"PDM");
        if (!line) line = tp;
        getargs(&line, 3, (unsigned char *)",");
        if (argc != 3) error("Syntax");
        left_pin = audio_option_parse_gp_pin(argv[0]);
        right_pin = audio_option_parse_gp_pin(argv[2]);
        esp32_audio_require_valid_pin(left_pin);
        esp32_audio_require_valid_pin(right_pin);
        esp32_audio_require_distinct_pin(left_pin, right_pin);
        esp32_audio_require_available_pins2(left_pin, right_pin);
        esp32_audio_clear_options();
        esp32_audio_reserve_pdm(left_pin, right_pin);
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
