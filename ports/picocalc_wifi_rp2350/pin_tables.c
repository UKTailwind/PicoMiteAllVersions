/*
 * ports/picocalc_wifi_rp2350/pin_tables.c -- PINMAP[] + codemap() for
 * the ClockworkPi PicoCalc Pico 2 W port.
 */

#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ports/pico_sdk_common/pindef_blocks.h"

/* rp2350 WiFi (PicoMiteWEB) — CYW43 claims GP23/24/25/29. The radio
 * also overlays GP30+ on the QSPI lines, so the rp2350-extras block
 * is omitted. */
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,
    PINDEF_BLOCK_PINS_16_25_GENERIC,
    PINDEF_BLOCK_PINS_26_40,
};

const uint8_t PINMAP[48] = {
    1, 2, 4, 5, 6, 7, 9, 10, 11, 12,
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
    55, 56, 57, 58, 59, 60, 61, 62};

int codemap(int pin) {
    if (pin > 29 || pin < 0 ||
        pin == 23 || pin == 24 || pin == 25 || pin == 29)
        error("Invalid GPIO");
    return (int)PINMAP[pin];
}
