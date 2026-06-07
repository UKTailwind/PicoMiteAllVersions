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
#include "esp32_audio_profile.h"
#include "esp32_board_profile.h"
#include "esp32_option_ext.h"

extern void esp32_audio_status_string(char * out, size_t out_len);

static int esp32_audio_pin_invalid(int pin) {
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

static int esp32_audio_i2s_ws_pin(void) {
    if (!esp32_audio_pin_invalid(ESP32_OPTION_AUDIO_I2S_WS))
        return ESP32_OPTION_AUDIO_I2S_WS;
    if (esp32_audio_pin_invalid(Option.audio_i2s_bclk))
        return 0;
    int ws_gpio = PinDef[Option.audio_i2s_bclk].GPno + 1;
    if (ws_gpio < 0 || ws_gpio >= HAL_PORT_GPIO_COUNT)
        return 0;
    return codemap(ws_gpio);
}

static int esp32_audio_i2s_mclk_pin(void) {
    return esp32_audio_pin_invalid(ESP32_OPTION_AUDIO_I2S_MCLK)
               ? 0
               : ESP32_OPTION_AUDIO_I2S_MCLK;
}

static const esp32_audio_profile_t * esp32_audio_selected_profile(void) {
    if (ESP32_OPTION_AUDIO_KIND != ESP32_AUDIO_KIND_PROFILE) return NULL;
    return esp32_audio_profile_by_id(ESP32_OPTION_AUDIO_PROFILE);
}

static int esp32_audio_pin_reserved(int pin) {
    return !esp32_audio_pin_invalid(pin) && ExtCurrentConfig[pin] == EXT_BOOT_RESERVED;
}

static int esp32_audio_is_current_audio_pin(int pin) {
    if (esp32_audio_pin_invalid(pin))
        return 0;

    if (!esp32_audio_pin_reserved(pin))
        return 0;

    if (Option.AUDIO_L && (pin == Option.AUDIO_L || pin == Option.AUDIO_R))
        return 1;

    if (Option.audio_i2s_bclk && (pin == Option.audio_i2s_bclk ||
                                  pin == Option.audio_i2s_data ||
                                  pin == esp32_audio_i2s_ws_pin() ||
                                  pin == esp32_audio_i2s_mclk_pin()))
        return 1;

    const esp32_audio_profile_t * profile = esp32_audio_selected_profile();
    if (profile) {
        if (pin == esp32_audio_profile_pin_to_option(profile->i2s.bclk) ||
            pin == esp32_audio_profile_pin_to_option(profile->i2s.ws) ||
            pin == esp32_audio_profile_pin_to_option(profile->i2s.dout) ||
            pin == esp32_audio_profile_pin_to_option(profile->i2s.mclk))
            return 1;
        int amp_pin = esp32_audio_profile_pin_to_option(profile->amp_enable);
        if (pin == amp_pin) return 1;
    }

    return 0;
}

static void esp32_audio_release_pin(int pin) {
    if (!esp32_audio_pin_reserved(pin))
        return;
    hal_pin_deinit((uint32_t)PinDef[pin].GPno);
    ExtCurrentConfig[pin] = EXT_NOT_CONFIG;
}

void esp32_audio_print_options(void) {
    if (Option.AUDIO_L || Option.audio_i2s_bclk) {
        MMPrintString("OPTION AUDIO ");
        if (Option.AUDIO_L) {
            MMPrintString("PDM ");
            MMPrintString((char *)PinDef[Option.AUDIO_L].pinname);
            MMputchar(',', 1);
            MMPrintString((char *)PinDef[Option.AUDIO_R].pinname);
        } else if (ESP32_OPTION_AUDIO_KIND == ESP32_AUDIO_KIND_PROFILE) {
            const esp32_audio_profile_t * profile = esp32_audio_selected_profile();
            MMPrintString((char *)(profile ? profile->option_name : "UNKNOWN"));
        } else {
            int ws_pin = esp32_audio_i2s_ws_pin();
            int mclk_pin = esp32_audio_i2s_mclk_pin();
            MMPrintString("I2S ");
            MMPrintString((char *)PinDef[Option.audio_i2s_bclk].pinname);
            MMputchar(',', 1);
            if (!esp32_audio_pin_invalid(ws_pin)) {
                MMPrintString((char *)PinDef[ws_pin].pinname);
                MMputchar(',', 1);
            }
            MMPrintString((char *)PinDef[Option.audio_i2s_data].pinname);
            if (!esp32_audio_pin_invalid(mclk_pin)) {
                MMputchar(',', 1);
                MMPrintString((char *)PinDef[mclk_pin].pinname);
            }
        }
        PRet();
    }
}

static void esp32_audio_clear_options(void) {
    const esp32_audio_profile_t * profile = esp32_audio_selected_profile();
    int amp_pin = profile ? esp32_audio_profile_pin_to_option(profile->amp_enable) : 0;
    if (profile && profile->deinit) profile->deinit(profile);
    if (profile) {
        esp32_audio_release_pin(esp32_audio_profile_pin_to_option(profile->i2s.bclk));
        esp32_audio_release_pin(esp32_audio_profile_pin_to_option(profile->i2s.ws));
        esp32_audio_release_pin(esp32_audio_profile_pin_to_option(profile->i2s.dout));
        esp32_audio_release_pin(esp32_audio_profile_pin_to_option(profile->i2s.mclk));
    }
    if (Option.AUDIO_L) {
        esp32_audio_release_pin(Option.AUDIO_L);
        esp32_audio_release_pin(Option.AUDIO_R);
    }
    if (Option.audio_i2s_bclk) {
        esp32_audio_release_pin(Option.audio_i2s_bclk);
        esp32_audio_release_pin(esp32_audio_i2s_ws_pin());
        esp32_audio_release_pin(Option.audio_i2s_data);
        esp32_audio_release_pin(esp32_audio_i2s_mclk_pin());
    }
    esp32_audio_release_pin(amp_pin);
    audio_option_clear_common_fields(0);
    ESP32_OPTION_AUDIO_KIND = ESP32_AUDIO_KIND_OFF;
    ESP32_OPTION_AUDIO_PROFILE = ESP32_AUDIO_PROFILE_NONE;
    ESP32_OPTION_AUDIO_I2S_WS = 0;
    ESP32_OPTION_AUDIO_I2S_MCLK = 0;
    esp32_board_profile_update_shared_i2c_pins();
}

void disable_audio(void) {
    esp32_audio_clear_options();
}

static void esp32_audio_require_valid_pin(int pin) {
    if (esp32_audio_pin_invalid(pin))
        error("Invalid pin");
}

static void esp32_audio_require_available_pin(int pin) {
    esp32_audio_require_valid_pin(pin);
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG && !esp32_audio_is_current_audio_pin(pin))
        error("Pin %/| is in use", pin, pin);
}

static void esp32_audio_require_available_shared_i2c_pin(int pin) {
    esp32_audio_require_valid_pin(pin);
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG &&
        !esp32_board_profile_pin_owned_by_shared_i2c(pin))
        error("Pin %/| is in use", pin, pin);
}

static void esp32_audio_require_distinct_pin(int first_pin, int second_pin) {
    if (first_pin == second_pin)
        error("Pin %/| is in use", second_pin, second_pin);
}

static void esp32_audio_reserve_i2s(int bclk_pin, int ws_pin, int data_pin,
                                    int mclk_pin, uint8_t kind, uint8_t profile_id) {
    Option.audio_i2s_bclk = bclk_pin;
    Option.audio_i2s_data = data_pin;
    Option.AUDIO_L = 0;
    Option.AUDIO_R = 0;
    Option.AUDIO_SLICE = 0;
    ESP32_OPTION_AUDIO_KIND = kind;
    ESP32_OPTION_AUDIO_PROFILE = profile_id;
    ESP32_OPTION_AUDIO_I2S_WS = ws_pin;
    ESP32_OPTION_AUDIO_I2S_MCLK = mclk_pin;
    ExtCurrentConfig[bclk_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[ws_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[data_pin] = EXT_BOOT_RESERVED;
    if (mclk_pin) ExtCurrentConfig[mclk_pin] = EXT_BOOT_RESERVED;
}

static void esp32_audio_reserve_pdm(int left_pin, int right_pin) {
    Option.AUDIO_L = left_pin;
    Option.AUDIO_R = right_pin;
    Option.AUDIO_SLICE = 0;
    Option.audio_i2s_bclk = 0;
    Option.audio_i2s_data = 0;
    ESP32_OPTION_AUDIO_KIND = ESP32_AUDIO_KIND_PDM;
    ESP32_OPTION_AUDIO_PROFILE = ESP32_AUDIO_PROFILE_NONE;
    ESP32_OPTION_AUDIO_I2S_WS = 0;
    ESP32_OPTION_AUDIO_I2S_MCLK = 0;
    ExtCurrentConfig[left_pin] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[right_pin] = EXT_BOOT_RESERVED;
}

static void esp32_audio_reserve_profile(const esp32_audio_profile_t * profile,
                                        int bclk_pin, int ws_pin, int data_pin,
                                        int mclk_pin, int amp_pin) {
    esp32_audio_reserve_i2s(bclk_pin, ws_pin, data_pin, mclk_pin,
                            ESP32_AUDIO_KIND_PROFILE, profile->id);
    if (amp_pin) ExtCurrentConfig[amp_pin] = EXT_BOOT_RESERVED;
    esp32_board_profile_update_shared_i2c_pins();
}

static void esp32_audio_require_available_pins3(int pin1, int pin2, int pin3) {
    esp32_audio_require_available_pin(pin1);
    esp32_audio_require_available_pin(pin2);
    esp32_audio_require_available_pin(pin3);
}

static void esp32_audio_require_available_pins2(int pin1, int pin2) {
    esp32_audio_require_available_pin(pin1);
    esp32_audio_require_available_pin(pin2);
}

static void esp32_audio_require_available_pins4(int pin1, int pin2, int pin3,
                                                int pin4) {
    esp32_audio_require_available_pins3(pin1, pin2, pin3);
    if (pin4) esp32_audio_require_available_pin(pin4);
}

static int esp32_audio_ws_from_bclk(int bclk_pin) {
    esp32_audio_require_valid_pin(bclk_pin);
    int ws_pin = codemap(PinDef[bclk_pin].GPno + 1);
    esp32_audio_require_valid_pin(ws_pin);
    return ws_pin;
}

static void esp32_audio_save_options(void) {
    SaveOptions();
    _excep_code = RESET_COMMAND;
    esp_restart();
}

int esp32_audio_option_setter(unsigned char * line) {
    unsigned char * tp = checkstring(line, (unsigned char *)"AUDIO");
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

    const esp32_audio_profile_t * profile = esp32_audio_profile_by_name(tp);
    if (profile) {
        int bclk_pin, ws_pin, data_pin, mclk_pin, amp_pin, i2c_sda_pin, i2c_scl_pin;
        if (!esp32_audio_profile_available_for_current_board(profile))
            error("Audio profile not available for this board");
        const esp32_board_profile_t * board_profile = esp32_board_profile_current();
        bclk_pin = esp32_audio_profile_pin_to_option(profile->i2s.bclk);
        ws_pin = esp32_audio_profile_pin_to_option(profile->i2s.ws);
        data_pin = esp32_audio_profile_pin_to_option(profile->i2s.dout);
        mclk_pin = esp32_audio_profile_pin_to_option(profile->i2s.mclk);
        amp_pin = esp32_audio_profile_pin_to_option(profile->amp_enable);
        i2c_sda_pin = esp32_audio_profile_pin_to_option(board_profile->audio.i2c_sda);
        i2c_scl_pin = esp32_audio_profile_pin_to_option(board_profile->audio.i2c_scl);
        esp32_audio_require_valid_pin(bclk_pin);
        esp32_audio_require_valid_pin(ws_pin);
        esp32_audio_require_valid_pin(data_pin);
        if (mclk_pin) esp32_audio_require_valid_pin(mclk_pin);
        if (amp_pin) esp32_audio_require_valid_pin(amp_pin);
        esp32_audio_require_valid_pin(i2c_sda_pin);
        esp32_audio_require_valid_pin(i2c_scl_pin);
        esp32_audio_require_distinct_pin(bclk_pin, ws_pin);
        esp32_audio_require_distinct_pin(bclk_pin, data_pin);
        esp32_audio_require_distinct_pin(ws_pin, data_pin);
        if (mclk_pin) {
            esp32_audio_require_distinct_pin(mclk_pin, bclk_pin);
            esp32_audio_require_distinct_pin(mclk_pin, ws_pin);
            esp32_audio_require_distinct_pin(mclk_pin, data_pin);
        }
        if (amp_pin) {
            esp32_audio_require_distinct_pin(amp_pin, bclk_pin);
            esp32_audio_require_distinct_pin(amp_pin, ws_pin);
            esp32_audio_require_distinct_pin(amp_pin, data_pin);
            if (mclk_pin) esp32_audio_require_distinct_pin(amp_pin, mclk_pin);
        }
        esp32_audio_require_available_pins4(bclk_pin, ws_pin, data_pin, mclk_pin);
        if (amp_pin) esp32_audio_require_available_pin(amp_pin);
        esp32_audio_require_available_shared_i2c_pin(i2c_sda_pin);
        esp32_audio_require_available_shared_i2c_pin(i2c_scl_pin);
        esp32_audio_clear_options();
        esp32_audio_reserve_profile(profile, bclk_pin, ws_pin, data_pin,
                                    mclk_pin, amp_pin);
        esp32_audio_save_options();
        return 1;
    }

    if ((line = checkstring(tp, (unsigned char *)"I2S"))) {
        int bclk_pin, data_pin, ws_pin, mclk_pin = 0;
        getargs(&line, 7, (unsigned char *)",");
        if (argc != 3 && argc != 5 && argc != 7) error("Syntax");
        bclk_pin = audio_option_parse_gp_pin(argv[0]);
        if (argc == 5) {
            ws_pin = audio_option_parse_gp_pin(argv[2]);
            data_pin = audio_option_parse_gp_pin(argv[4]);
        } else if (argc == 7) {
            ws_pin = audio_option_parse_gp_pin(argv[2]);
            data_pin = audio_option_parse_gp_pin(argv[4]);
            mclk_pin = audio_option_parse_gp_pin(argv[6]);
        } else {
            ws_pin = esp32_audio_ws_from_bclk(bclk_pin);
            data_pin = audio_option_parse_gp_pin(argv[2]);
        }
        esp32_audio_require_valid_pin(ws_pin);
        esp32_audio_require_valid_pin(data_pin);
        if (mclk_pin) esp32_audio_require_valid_pin(mclk_pin);
        esp32_audio_require_distinct_pin(bclk_pin, ws_pin);
        esp32_audio_require_distinct_pin(bclk_pin, data_pin);
        esp32_audio_require_distinct_pin(ws_pin, data_pin);
        if (mclk_pin) {
            esp32_audio_require_distinct_pin(mclk_pin, bclk_pin);
            esp32_audio_require_distinct_pin(mclk_pin, ws_pin);
            esp32_audio_require_distinct_pin(mclk_pin, data_pin);
        }
        esp32_audio_require_available_pins4(bclk_pin, ws_pin, data_pin, mclk_pin);
        esp32_audio_clear_options();
        esp32_audio_reserve_i2s(bclk_pin, ws_pin, data_pin, mclk_pin,
                                ESP32_AUDIO_KIND_I2S, ESP32_AUDIO_PROFILE_NONE);
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

int esp32_audio_mminfo(unsigned char * ep, unsigned char * out_sret, int * out_targ) {
    if (checkstring(ep, (unsigned char *)"AUDIO STATUS")) {
        esp32_audio_status_string((char *)out_sret, STRINGSIZE);
        CtoM(out_sret);
        *out_targ = T_STR;
        return 1;
    }
    if (!checkstring(ep, (unsigned char *)"AUDIO")) return 0;
    if (Option.AUDIO_L)
        strcpy((char *)out_sret, "PDM");
    else if (ESP32_OPTION_AUDIO_KIND == ESP32_AUDIO_KIND_PROFILE) {
        const esp32_audio_profile_t * profile = esp32_audio_selected_profile();
        strcpy((char *)out_sret, profile ? profile->option_name : "UNKNOWN");
    }
    else if (Option.audio_i2s_bclk)
        strcpy((char *)out_sret, "I2S");
    else
        strcpy((char *)out_sret, "OFF");
    CtoM(out_sret);
    *out_targ = T_STR;
    return 1;
}
