/*
 * ports/pico_rp2350/port_defaults.c — COMPILE=PICORP2350 / PICOUSBRP2350
 * board-specific default Option.* values. See ports/pico/port_defaults.c
 * for the mechanism; this port shares the PicoMite (non-VGA) defaults.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_set_default_options(void)
{
    Option.CPU_Speed = FreqDefault;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.RepeatStart = 600;
    Option.RepeatRate = 150;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
#endif
    Option.TOUCH_XSCALE = 1.0f;
    Option.TOUCH_YSCALE = 1.0f;
}
