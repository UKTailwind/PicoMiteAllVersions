/*
 * ports/web/port_defaults.c — COMPILE=WEB board-specific default
 * Option.* values. WebMite is non-VGA, non-USB.
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
