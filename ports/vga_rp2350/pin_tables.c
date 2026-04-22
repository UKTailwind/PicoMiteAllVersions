/*
 * ports/vga_rp2350/pin_tables.c — PINMAP[] + codemap() for RP2350
 * VGA variants. Identical to ports/pico_rp2350/pin_tables.c.
 */

#include <stdint.h>
#include <stdbool.h>

#include "MMBasic_Includes.h"

extern bool rp2350a;

const uint8_t PINMAP[48] = {
    1,  2,  4,  5,  6,  7,  9, 10, 11, 12,
    14, 15, 16, 17, 19, 20, 21, 22, 24, 25,
    26, 27, 29, 41, 42, 43, 31, 32, 34, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
    55, 56, 57, 58, 59, 60, 61, 62
};

int codemap(int pin)
{
    if (pin > (rp2350a ? 29 : 47) || pin < 0) error("Invalid GPIO");
    return (int)PINMAP[pin];
}
