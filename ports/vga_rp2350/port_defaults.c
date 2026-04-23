/*
 * ports/vga_rp2350/port_defaults.c — COMPILE=VGARP2350 / VGAUSBRP2350.
 * PICOMITEVGA on RP2350 — same defaults as ports/vga/.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_set_default_options(void)
{
    Option.DISPLAY_CONSOLE = 1;
    Option.DISPLAY_TYPE = SCREENMODE1;
    Option.X_TILE = 80;
    Option.Y_TILE = 40;
    Option.CPU_Speed = Freq252P;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
    Option.VGA_HSYNC = 21;
    Option.VGA_BLUE = 24;
    Option.KEYBOARD_CLOCK = KEYBOARDCLOCK;
    Option.KEYBOARD_DATA = KEYBOARDDATA;
    Option.KeyboardConfig = CONFIG_US;
#endif
}

/* Boards advertised by `CONFIGURE LIST`. */
#include "MMBasic.h"
void port_print_supported_boards(void)
{
#ifdef USBKEYBOARD
    MMPrintString("CMM1.5\r\n");
#else
    MMPrintString("PICOMITEVGA V1.1\r\n");
    MMPrintString("PICOMITEVGA V1.0\r\n");
    MMPrintString("VGA Design 1\r\n");
    MMPrintString("VGA Design 2\r\n");
    MMPrintString("SWEETIEPI\r\n");
    MMPrintString("VGA Basic\r\n");
#endif
}
