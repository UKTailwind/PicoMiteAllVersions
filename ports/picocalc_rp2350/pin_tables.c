/*
 * ports/picocalc_rp2350/pin_tables.c -- PINMAP[] + codemap() for the
 * ClockworkPi PicoCalc RP2350 port.
 */

#include <stdint.h>
#include <stdbool.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ports/pico_sdk_common/pindef_blocks.h"

extern bool rp2350a;

/* rp2350 SPI-LCD PicoMite — no HDMI, no WiFi. RP2350B exposes 48
 * GPIOs (GP0-GP47); the rp2350-extras block populates the upper 18. */
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,
    PINDEF_BLOCK_PINS_16_25_GENERIC,
    PINDEF_BLOCK_PINS_26_40,
    PINDEF_BLOCK_PSEUDO_GP23_29,
    PINDEF_BLOCK_PSEUDO_RP2350_EXTRAS,
};

const uint8_t PINMAP[48] = {
    1, 2, 4, 5, 6, 7, 9, 10, 11, 12,
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
    55, 56, 57, 58, 59, 60, 61, 62};

int codemap(int pin) {
    if (pin > (rp2350a ? 29 : 47) || pin < 0) error("Invalid GPIO");
    return (int)PINMAP[pin];
}
