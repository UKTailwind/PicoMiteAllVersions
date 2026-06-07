/*
 * ESP32-S3 TOUCH() function surface.
 *
 * The full GUI control/touch calibration stack is still stubbed for this port.
 * This gives Freenove FNK0104B a small direct read path without pulling that
 * larger Pico-centric stack into the ESP32 display bring-up.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "esp32_ft6336u_touch.h"

#define ESP32_TOUCH_ERROR -1

static int read_axis(int index, int want_y) {
    int x = 0, y = 0;
    if (!esp32_ft6336u_touch_read(index, &x, &y)) return ESP32_TOUCH_ERROR;
    return want_y ? y : x;
}

void fun_touch(void) {
    if (checkstring(ep, (unsigned char *)"X"))
        iret = read_axis(0, 0);
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = read_axis(0, 1);
    else if (checkstring(ep, (unsigned char *)"X2"))
        iret = read_axis(1, 0);
    else if (checkstring(ep, (unsigned char *)"Y2"))
        iret = read_axis(1, 1);
    else if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = esp32_ft6336u_touch_down() ? 1 : 0;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = esp32_ft6336u_touch_down() ? 0 : 1;
    else
        error("Invalid argument");
    targ = T_INT;
}
