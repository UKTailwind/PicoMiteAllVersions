/*
 * ports/pico/port_defaults.c — COMPILE=PICO / PICOUSB board-specific
 * default Option.* values set during factory reset. See FileIO.c's
 * ResetOptions(), which calls port_set_default_options() at the end
 * of its shared defaults.
 *
 * Port impl files may use target-macro #ifdef dispatch (this one has
 * USBKEYBOARD inside) per the fixup-plan rules — the macros don't
 * leak back into core because core only sees the function call.
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
    /* Non-VGA targets default touch scale to 1:1. */
    Option.TOUCH_XSCALE = 1.0f;
    Option.TOUCH_YSCALE = 1.0f;
}

/* Boards advertised by `CONFIGURE LIST`. The body's #ifdef gates stay
 * inside this port impl file (port files are exempt from the purity
 * gate); MM_Misc.c calls port_print_supported_boards() unconditionally.
 */
#include "MMBasic.h"  /* for MMPrintString */
void port_print_supported_boards(void)
{
#ifndef USBKEYBOARD
    MMPrintString("Game*Mite\r\n");
#  ifdef PICOCALC
    MMPrintString("PicoCalc\r\n");
#  endif
    MMPrintString("Pico-ResTouch-LCD-3.5\r\n");
    MMPrintString("Pico-ResTouch-LCD-2.8\r\n");
    MMPrintString("PICO BACKPACK\r\n");
    MMPrintString("RP2040-LCD-1.28\r\n");
    MMPrintString("RP2040LCD-0.96\r\n");
    MMPrintString("RP2040-GEEK\r\n");
#else
    MMPrintString("USB Edition V1.0\r\n");
#endif
}
