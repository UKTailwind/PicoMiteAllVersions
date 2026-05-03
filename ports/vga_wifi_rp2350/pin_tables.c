/*
 * ports/vga_wifi_rp2350/pin_tables.c — pin map for the F2 VGA + WiFi
 * validation port. Same package-pin layout as ports/vga_rp2350, but
 * GP23/24/25/29 are claimed by the CYW43 radio (exposed as virtual
 * pins 41-44 by port_pinno_alias_for_name).
 */

#include <stdint.h>
#include <stdbool.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ports/pico_sdk_common/pindef_blocks.h"

extern bool rp2350a;

/* rp2350 VGA-PIO + WiFi — CYW43 claims GP23/24/25/29 plus the QSPI
 * overlay, so the GP23-29 pseudo block and the rp2350-extras block
 * are both excluded. */
const struct s_PinDef PinDef[] = {
    PINDEF_BLOCK_HEADER_AND_GP0_15,
    PINDEF_BLOCK_PINS_16_25_GENERIC,
    PINDEF_BLOCK_PINS_26_40,
};

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
