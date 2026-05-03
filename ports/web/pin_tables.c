/*
 * ports/web/pin_tables.c — PINMAP[] + codemap() for COMPILE=WEB
 * (RP2040 WebMite). Identical layout to PicoMite except codemap
 * rejects GP23/24/25/29 which the CYW43 radio claims.
 */

#include <stdint.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ports/pico_sdk_common/pindef_blocks.h"

/* rp2040 WiFi (PicoMiteWEB) — CYW43 claims GP23/24/25/29 so the
 * pseudo-pin block is omitted. */
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,
    PINDEF_BLOCK_PINS_16_25_GENERIC,
    PINDEF_BLOCK_PINS_26_40,
};

const uint8_t PINMAP[30] = {
    1,  2,  4,  5,  6,  7,  9, 10, 11, 12,
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44
};

int codemap(int pin)
{
    if (pin > 29 || pin < 0 ||
        pin == 23 || pin == 24 || pin == 25 || pin == 29)
        error("Invalid GPIO");
    return (int)PINMAP[pin];
}
