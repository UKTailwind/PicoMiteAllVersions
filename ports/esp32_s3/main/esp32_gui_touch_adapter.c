/*
 * Minimal ESP32-S3 bridge for the legacy GUI-control touch API.
 *
 * Phase 1 links the shared GUI control implementation but does not yet wire
 * FT6336U touch transitions into the GUI state machine. Direct TOUCH() support
 * remains in esp32_touch_function.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

int TOUCH_GETIRQTRIS = 0;
int TOUCH_IRQ_PIN = 0;
int TOUCH_CS_PIN = 0;
int TOUCH_Click_PIN = 0;

int esp32_gui_touch_down_for_gui(void) {
    return 0;
}

void InitTouch(void) {
    TOUCH_GETIRQTRIS = 0;
}

void ConfigTouch(unsigned char * p) {
    (void)p;
    error("Touch GUI adapter not implemented");
}

void GetCalibration(int x, int y, int * xval, int * yval) {
    (void)x;
    (void)y;
    if (xval) *xval = TOUCH_ERROR;
    if (yval) *yval = TOUCH_ERROR;
}

int GetTouchValue(int cmd) {
    (void)cmd;
    return TOUCH_ERROR;
}

int GetTouchAxis(int cmd) {
    (void)cmd;
    return TOUCH_ERROR;
}

int GetTouchAxisCap(int axis) {
    (void)axis;
    return TOUCH_ERROR;
}

int GetTouch(int axis) {
    (void)axis;
    return TOUCH_ERROR;
}
