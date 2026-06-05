/*
 * Shared BASIC WS2812 command parser/packer.
 *
 * Hardware timing is delegated to hal_ws2812_write(). The packed byte stream
 * is in WS2812 wire order: G, R, B, optional W.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#include "MMBasic_Includes.h"
#include "MATHS.h"
#include "Memory.h"
#include "vm_sys_pin.h"
#include "hal/hal_ws2812.h"

struct s_PinDef {
    int pin;
    int GPno;
    char pinname[5];
    uint64_t mode;
    unsigned char ADCpin;
    unsigned char slice;
};

extern const struct s_PinDef PinDef[NBRPINS + 1];
extern volatile int ExtCurrentConfig[NBRPINS + 1];
extern int codemap(int pin);

enum {
    WS2812_EXT_NOT_CONFIG = 0,
    WS2812_EXT_DIG_OUT = 8
};

static int ws2812_parse_pin_arg(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p') && isdigit(p[2]))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

void cmd_WS2812(void) {
    int64_t * src = NULL;
    int64_t single_colour = 0;
    hal_ws2812_type_t type = HAL_WS2812_B;
    int colours = 3;
    int nbr;
    int pin;
    uint8_t * bytes;

    getargs(&cmdline, 7, (unsigned char *)",");
    if (argc != 7) error("Argument count");

    switch (toupper(*argv[0])) {
    case 'O':
        type = HAL_WS2812_ORIGINAL;
        break;
    case 'B':
        type = HAL_WS2812_B;
        break;
    case 'S':
        type = HAL_WS2812_SK6812;
        break;
    case 'W':
        type = HAL_WS2812_SK6812W;
        colours = 4;
        break;
    default:
        error("Syntax");
    }

    pin = ws2812_parse_pin_arg(argv[2]);
    if (pin < 1 || pin > NBRPINS || (PinDef[pin].mode & UNUSED))
        error("Invalid pin");
    if (!(PinDef[pin].mode & DIGITAL_OUT))
        error("Invalid configuration");
    if (!(ExtCurrentConfig[pin] == WS2812_EXT_DIG_OUT || ExtCurrentConfig[pin] == WS2812_EXT_NOT_CONFIG))
        error("Pin %/| is not off or an output", pin, pin);
    if (ExtCurrentConfig[pin] == WS2812_EXT_NOT_CONFIG)
        vm_sys_pin_setpin(pin, VM_PIN_MODE_DOUT, VM_PIN_OPT_NONE);

    nbr = getint(argv[4], 1, 256);
    if (nbr > 1) {
        parseintegerarray(argv[6], &src, 4, 1, NULL, false);
    } else {
        single_colour = getinteger(argv[6]);
        src = &single_colour;
    }

    bytes = GetTempMemory(nbr * colours);
    for (int i = 0; i < nbr; i++) {
        uint32_t colour = (uint32_t)src[i];
        bytes[i * colours + 0] = (uint8_t)((colour >> 8) & 0xff);  /* G */
        bytes[i * colours + 1] = (uint8_t)((colour >> 16) & 0xff); /* R */
        bytes[i * colours + 2] = (uint8_t)(colour & 0xff);         /* B */
        if (colours == 4)
            bytes[i * colours + 3] = (uint8_t)((colour >> 24) & 0xff);
    }

    if (hal_ws2812_write((uint32_t)PinDef[pin].GPno, type, bytes, (size_t)(nbr * colours)) != 0)
        error("WS2812 failed");
}
