/*
 * ESP32-S3 peripheral include surface.
 *
 * The stdio ESP32 port has no LCD, touch, or legacy Pico peripheral headers
 * to pull through Hardware_Includes.h. ESP32-owned stubs and HAL files provide
 * the symbols shared code needs.
 */

#ifndef ESP32_S3_PORT_PERIPHERALS_H
#define ESP32_S3_PORT_PERIPHERALS_H

#include "Touch.h"
#include "GUI.h"
#include "esp32_gui_touch_adapter.h"

#undef TOUCH_DOWN
#define TOUCH_DOWN (esp32_gui_touch_down_for_gui())

#endif /* ESP32_S3_PORT_PERIPHERALS_H */
