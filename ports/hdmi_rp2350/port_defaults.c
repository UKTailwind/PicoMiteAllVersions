/*
 * ports/hdmi_rp2350/port_defaults.c — COMPILE=HDMI / HDMIUSB defaults.
 * PICOMITEVGA + HDMI. Both variants set HDMI output pins regardless
 * of keyboard; HDMIUSB additionally routes USB-keyboard serial config.
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
    Option.HDMIclock = 2;
    Option.HDMId0 = 0;
    Option.HDMId1 = 6;
    Option.HDMId2 = 4;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
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
    MMPrintString("HDMIUSB\r\n");
    MMPrintString("OLIMEX USB\r\n");
    MMPrintString("PICO COMPUTER\r\n");
    MMPrintString("HDMIUSBI2S\r\n");
#else
    MMPrintString("OLIMEX\r\n");
    MMPrintString("HDMIBasic\r\n");
#endif
}
