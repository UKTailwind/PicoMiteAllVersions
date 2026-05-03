/*
 * ports/pico/pin_tables.c — PINMAP[] + codemap() for COMPILE=PICO /
 * COMPILE=PICOUSB (RP2040 PicoMite).
 *
 * PINMAP indexes BASIC pin numbers to `PinDef[]` slot indices.
 * codemap() validates a GPIO number and returns the PinDef slot.
 * Core references these through `extern const uint8_t PINMAP[]` and
 * a plain `int codemap(int pin)` prototype — no target macros in core.
 */

#include <stdint.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ports/pico_sdk_common/pindef_blocks.h"

const uint8_t PINMAP[30] = {
    1,  2,  4,  5,  6,  7,  9, 10, 11, 12,
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44
};

/* rp2040 SPI-LCD PicoMite — no HDMI, no WiFi.
 * Standard 30 GPIO pins + 4 pseudo entries for GP23/24/25/29. */
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,
    PINDEF_BLOCK_PINS_16_25_GENERIC,
    PINDEF_BLOCK_PINS_26_40,
    PINDEF_BLOCK_PSEUDO_GP23_29,
};

int codemap(int pin)
{
    if (pin > 29 || pin < 0) error("Invalid GPIO");
    return (int)PINMAP[pin];
}
