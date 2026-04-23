/*
 * ports/pico_sdk_common/misc_option_setters.c — peripheral OPTION
 * setters whose validity depends on rp2350 features (HDMI PINS,
 * KEYBOARD BACKLIGHT, PSRAM PIN) or PS/2 vs USB keyboard backends
 * (KEYBOARD REPEAT, PS2 PINS / KEYBOARD PINS, MOUSE).
 *
 * Single entry point port_misc_option_setter() returns 1 if the
 * cmdline matches a setter (which then SaveOptions / SoftReset /
 * error() before falling out), 0 otherwise. Internal #ifdef gating
 * lives here in the port file so MM_Misc.c carries no PICOMITE /
 * rp2350 / HDMI / USBKEYBOARD ifdefs around these blocks.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#if !defined(MMBASIC_HOST)

extern int KeyboardlightSlice, KeyboardlightChannel;

int MIPS16 port_misc_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;

#ifdef rp2350
#ifdef HDMI
    tp = checkstring(cmdline, (unsigned char *)"HDMI PINS");
    if (tp) {
        getargs(&tp, 7, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (argc != 7) error("Syntax");
        uint8_t clock = getint(argv[0], 0, 7);
        uint8_t d0 = getint(argv[2], 0, 7);
        uint8_t d1 = getint(argv[4], 0, 7);
        uint8_t d2 = getint(argv[6], 0, 7);
        if ((clock & 0x6) == (d0 & 0x6) || (clock & 0x6) == (d1 & 0x6) ||
            (clock & 0x6) == (d2 & 0x6) || (d0 & 0x6) == (d1 & 0x6) ||
            (d0 & 0x6) == (d2 & 0x6) || (d1 & 0x6) == (d2 & 0x6))
            error("Channels not unique");
        Option.HDMIclock = clock;
        Option.HDMId0 = d0;
        Option.HDMId1 = d1;
        Option.HDMId2 = d2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
#if defined(PICOMITE) && defined(rp2350)
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD BACKLIGHT");
    if (tp) {
        if (!Option.LOCAL_KEYBOARD) error("Invalid option");
        Option.KeyboardBrightness = getint(tp, 0, 100);
        setpwm(PINMAP[43], &KeyboardlightChannel, &KeyboardlightSlice, 50000.0, Option.KeyboardBrightness);
        SaveOptions();
        return 1;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PSRAM PIN");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            Option.PSRAM_CS_PIN = 0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return 1;
        }
        int pin1;
        unsigned char code;
        getargs(&tp, 1, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (!(code = codecheck(argv[0]))) argv[0] += 2;
        pin1 = getinteger(argv[0]);
        if (!code) pin1 = codemap(pin1);
        if (IsInvalidPin(pin1)) error("Invalid pin");
        if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin | is in use", pin1);
        if (!(pin1 == 1 || pin1 == 11 || pin1 == 25 || pin1 == 62))
            error("Invalid pin for PSRAM chip select (GP0,GP8,GP19,GP47)");
        Option.PSRAM_CS_PIN = pin1;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif

#ifdef USBKEYBOARD
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        Option.RepeatStart = getint(argv[0], 100, 2000);
        Option.RepeatRate = getint(argv[2], 25, 2000);
        SaveOptions();
        return 1;
    }
#else
#if defined(PICOMITE) && defined(rp2350)
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if (!Option.LOCAL_KEYBOARD) error("Syntax");
        Option.RepeatStart = getint(argv[0], 100, 2000);
        Option.RepeatRate = getint(argv[2], 25, 2000);
        SaveOptions();
        return 1;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PS2 PINS");
    if (tp == NULL) tp = checkstring(cmdline, (unsigned char *)"KEYBOARD PINS");
    if (tp) {
        int pin1, pin2;
        unsigned char code;
        getargs(&tp, 3, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (Option.KEYBOARD_CLOCK) error("Keyboard must be disabled to change pins");
        if (argc != 3) error("Syntax");
        if (!(code = codecheck(argv[0]))) argv[0] += 2;
        pin1 = getinteger(argv[0]);
        if (!code) pin1 = codemap(pin1);
        if (IsInvalidPin(pin1)) error("Invalid pin");
        if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
        if (!(code = codecheck(argv[2]))) argv[2] += 2;
        pin2 = getinteger(argv[2]);
        if (!code) pin2 = codemap(pin2);
        if (IsInvalidPin(pin2)) error("Invalid pin");
        if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin2, pin2);
        Option.KEYBOARD_CLOCK = pin1;
        Option.KEYBOARD_DATA = pin2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"MOUSE");
    if (tp) {
        if (CurrentLinePtr) error("Invalid in a program");
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            Option.MOUSE_CLOCK = 0;
            Option.MOUSE_DATA = 0;
        } else {
            int pin1, pin2;
            unsigned char code;
            getargs(&tp, 3, (unsigned char *)",");
            if (Option.MOUSE_CLOCK) error("Mouse must be disabled to change pins");
            if (argc != 3) error("Syntax");
            if (!(code = codecheck(argv[0]))) argv[0] += 2;
            pin1 = getinteger(argv[0]);
            if (!code) pin1 = codemap(pin1);
            if (IsInvalidPin(pin1)) error("Invalid pin");
            if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
            if (!(code = codecheck(argv[2]))) argv[2] += 2;
            pin2 = getinteger(argv[2]);
            if (!code) pin2 = codemap(pin2);
            if (IsInvalidPin(pin2)) error("Invalid pin");
            if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin2, pin2);
            Option.MOUSE_CLOCK = pin1;
            Option.MOUSE_DATA = pin2;
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    (void)tp;
    return 0;
}

#endif /* !MMBASIC_HOST */
