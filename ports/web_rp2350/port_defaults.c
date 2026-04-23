/*
 * ports/web_rp2350/port_defaults.c — COMPILE=WEBRP2350 defaults.
 * Same as ports/web/ but on RP2350.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_set_default_options(void)
{
    Option.CPU_Speed = FreqDefault;
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
    Option.ServerResponceTime = 5000;
    Option.TOUCH_XSCALE = 1.0f;
    Option.TOUCH_YSCALE = 1.0f;
}
