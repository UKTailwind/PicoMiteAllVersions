/*
 * drivers/i2c_picocalc_kbd/i2c_keypad_stub.c — fall-back impls for
 * ports without an I²C-attached keypad MCU
 * (HAL_PORT_HAS_I2C_KEYPAD=0). Linked everywhere except the PicoCalc
 * profile.
 *
 * Most hooks no-op. The scancode translator implements the legacy
 * generic-I²C-keyboard map (0x1203/0x1202 ctrl sentinels and
 * ESC/F1/F2/F4 substitutions) — preserves the previous #else branch
 * behaviour from I2C.c so users with a generic I²C keyboard board
 * continue to work.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_i2c_keypad.h"

void hal_i2c_keypad_boot_init(void) {}
int  hal_i2c_keypad_owns_i2c_bus(void) { return 0; }

int hal_i2c_keypad_translate(uint16_t buff, int *ctrlheld_inout) {
    if (buff == 0x1203) { *ctrlheld_inout = 0; return -1; }
    if (buff == 0x1202) { *ctrlheld_inout = 1; return -1; }
    if ((buff & 0xff) != 1) return -1;

    int c = buff >> 8;
    if (c == 6)    c = ESC;
    if (c == 0x11) c = F1;
    if (c == 5)    c = F2;
    if (c == 0x7)  c = F4;
    return c;
}

void hal_i2c_keypad_print_options(void) {}
void hal_i2c_keypad_apply_spi480_resolution(void) {
    /* HRes / VRes / DisplayHRes / DisplayVRes are all `short`,
     * declared extern in Draw.h (pulled in via Hardware_Includes.h). */
    if (Option.DISPLAY_ORIENTATION & 1) {
        HRes = DisplayHRes;
        VRes = DisplayVRes;
    } else {
        HRes = DisplayVRes;
        VRes = DisplayHRes;
    }
}
