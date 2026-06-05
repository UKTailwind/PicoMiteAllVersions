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

static inline int audio_option_parse_gp_pin(unsigned char *arg)
{
    unsigned char *p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') &&
        (p[1] == 'P' || p[1] == 'p') &&
        isdigit((unsigned char)p[2]))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

static inline int audio_option_parse_mmbasic_pin(unsigned char **arg)
{
    unsigned char code;
    if (!(code = codecheck(*arg))) *arg += 2;
    int pin = getinteger(*arg);
    if (!code) pin = codemap(pin);
    return pin;
}

static inline void audio_option_clear_common_fields(int audio_slice)
{
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

#endif /* AUDIO_OPTION_COMMON_H */
