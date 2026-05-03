/*
 * drivers/i2c_picocalc_kbd/i2c_keypad_real.c — real implementations
 * of the hal_i2c_keypad surface (linked when HAL_PORT_HAS_I2C_KEYPAD=1).
 *
 * The PicoCalc board's keypad MCU manages keyboard input, LCD backlight,
 * and battery state over I²C. It needs a slower bus clock (10 kHz),
 * skews the boot init sequence (display init has to wait for the
 * keypad MCU), and supplies a comprehensive scancode keymap.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_i2c_keypad.h"

/* PicoCalc scan-word translator. Returns the cooked character to
 * enqueue, or -1 if the byte was a modifier / state change / unknown
 * sentinel that should NOT be enqueued. */
int hal_i2c_keypad_translate(uint16_t buff, int *ctrlheld_inout) {
    if (buff == 0xA503) { *ctrlheld_inout = 0; return -1; }
    if (buff == 0xA502) { *ctrlheld_inout = 1; return -1; }
    if ((buff & 0xff) != 1) return -1;          /* not a press */

    int c = buff >> 8;
    int realc;
    switch (c) {
        case 0xd4: realc = DEL; break;
        case 0xb5: realc = UP; break;
        case 0xb6: realc = DOWN; break;
        case 0xb4: realc = LEFT; break;
        case 0xb7: realc = RIGHT; break;
        case 0xd1: realc = INSERT; break;
        case 0xd2: realc = HOME; break;
        case 0xd5: realc = END; break;
        case 0xd6: realc = PUP; break;
        case 0xd7: realc = PDOWN; break;
        case 0xa1: realc = ALT; break;
        case 0x81: realc = F1; break;
        case 0x82: realc = F2; break;
        case 0x83: realc = F3; break;
        case 0x84: realc = F4; break;
        case 0x85: realc = F5; break;
        case 0x86: realc = F6; break;
        case 0x87: realc = F7; break;
        case 0x88: realc = F8; break;
        case 0x89: realc = F9; break;
        case 0x90: realc = F10; break;
        case 0xd0: realc = BreakKey; break;
        case 0xb1: realc = ESC; break;
        case 0x0a: realc = ENTER; break;
        case 0x91: realc = 0x66; break;        /* USB HID keypad keyboard-power */
        case 0xa2: case 0xa3: case 0xa5: case 0xc1: return -1;  /* modifiers */
        default:   realc = c; break;
    }
    return realc;
}

/* Boot-init: keypad takes the I²C bus first; the LCD/I²C display
 * helpers don't run on this port (their sub-systems aren't wired
 * up). Stub does the standard SSD/I²C display init that this real
 * impl skips. The 300 ms wait gives the keypad MCU time to enumerate.
 */
void hal_i2c_keypad_boot_init(void) {
    uSec(300000);
}

int hal_i2c_keypad_owns_i2c_bus(void) { return 1; }

extern void PO2Int(char *s1, int n);

void hal_i2c_keypad_print_options(void) {
    if (Option.KEYBOARDBL) PO2Int("BACKLIGHT KB", Option.KEYBOARDBL);
}

/* SPI480 panel resolution: PicoCalc fixes the LCD at 320x480 in
 * portrait. Stub queries DisplayHRes/Option.DISPLAY_ORIENTATION.
 * HRes/VRes are short ints declared extern in Draw.h. */
void hal_i2c_keypad_apply_spi480_resolution(void) {
    HRes = 320;
    VRes = 480;
}

extern void cmd_keyscan(void);

/* PicoCalc periodic keypad-matrix scan — runs at LOCALKEYSCANRATE
 * (10 ms) from the timer-tick path. Skipped when the user has
 * disabled the local keyboard. */
void hal_i2c_keypad_periodic_scan(uint64_t mSecTimer) {
    if (Option.LOCAL_KEYBOARD && (mSecTimer % LOCALKEYSCANRATE == 0)) {
        cmd_keyscan();
    }
}

/* PicoCalc keypad-matrix pin reservation, called from
 * InitReservedIO() at boot. Reserves the analogue backlight pin, the
 * keypad-power digital out, and the 15 matrix-row digital inputs so
 * the user's BASIC code can't reassign them. Skipped when the user
 * has disabled the local keyboard. */
void hal_i2c_keypad_reserve_io(void) {
    if (!Option.LOCAL_KEYBOARD) return;
    ExtCfg(PINMAP[47], EXT_ANA_IN, 0);
    ExtCfg(PINMAP[47], EXT_BOOT_RESERVED, 0);
    ExtCfg(PINMAP[24], EXT_DIG_OUT, 0);
    ExtCfg(PINMAP[24], EXT_BOOT_RESERVED, 0);
    for (int i = 26; i < 41; i++) {
        ExtCfg(PINMAP[i], EXT_DIG_IN, ODCSET);
        ExtCfg(PINMAP[i], EXT_BOOT_RESERVED, 0);
    }
}
