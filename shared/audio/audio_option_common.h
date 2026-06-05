#ifndef AUDIO_OPTION_COMMON_H
#define AUDIO_OPTION_COMMON_H

#include <ctype.h>

#include "MMBasic.h"
#include "FileIO.h"
#include "External.h"

enum {
    AUDIO_OPTION_SLICE_DISABLED = 98,
    AUDIO_OPTION_SLICE_NONE = 99
};

static inline int audio_option_parse_gp_pin(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') &&
        (p[1] == 'P' || p[1] == 'p') &&
        isdigit((unsigned char)p[2]))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

static inline int audio_option_parse_mmbasic_pin(unsigned char ** arg) {
    unsigned char code;
    if (!(code = codecheck(*arg))) *arg += 2;
    int pin = getinteger(*arg);
    if (!code) pin = codemap(pin);
    return pin;
}

static inline void audio_option_clear_common_fields(int audio_slice) {
    Option.AUDIO_L = 0;
    Option.AUDIO_R = 0;
    Option.AUDIO_CLK_PIN = 0;
    Option.AUDIO_CS_PIN = 0;
    Option.AUDIO_DCS_PIN = 0;
    Option.AUDIO_DREQ_PIN = 0;
    Option.AUDIO_RESET_PIN = 0;
    Option.AUDIO_MOSI_PIN = 0;
    Option.AUDIO_MISO_PIN = 0;
    Option.audio_i2s_bclk = 0;
    Option.audio_i2s_data = 0;
    Option.AUDIO_SLICE = audio_slice;
}

static inline int audio_option_i2s_lrck_pin(void) {
    if (IsInvalidPin(Option.audio_i2s_bclk)) return 0;
    int gp = PinDef[Option.audio_i2s_bclk].GPno + 1;
    if (gp < 0 || gp > 100) return 0;
    return PINMAP[gp];
}

static inline int audio_option_pin_matches_current(int pin) {
    if (IsInvalidPin(pin)) return 0;
    if (pin == Option.AUDIO_L || pin == Option.AUDIO_R) return 1;
    if (pin == Option.AUDIO_CLK_PIN || pin == Option.AUDIO_MOSI_PIN ||
        pin == Option.AUDIO_MISO_PIN || pin == Option.AUDIO_CS_PIN ||
        pin == Option.AUDIO_DCS_PIN || pin == Option.AUDIO_DREQ_PIN ||
        pin == Option.AUDIO_RESET_PIN) return 1;
    if (pin == Option.audio_i2s_bclk || pin == Option.audio_i2s_data ||
        pin == audio_option_i2s_lrck_pin()) return 1;
    return 0;
}

static inline void audio_option_require_pin_available(int pin) {
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG &&
        !audio_option_pin_matches_current(pin))
        error("Pin %/| is in use", pin, pin);
}

#endif /* AUDIO_OPTION_COMMON_H */
