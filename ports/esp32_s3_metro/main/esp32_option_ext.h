/*
 * esp32_option_ext.h - ESP32-S3 private option bytes.
 *
 * Keep struct option_s ABI stable for every port. ESP32-S3 stores its
 * port-only VGA and USB role settings in the tail of Option.extensions[].
 */

#ifndef ESP32_OPTION_EXT_H
#define ESP32_OPTION_EXT_H

#define USB_ROLE_SERIAL 0
#define USB_ROLE_KEYBOARD 1

#define ESP32_OPTION_VGA_EXT_BASE 85
#define ESP32_OPTION_VGA_DATA_COUNT 8
#define ESP32_OPTION_VGA_DATA (&Option.extensions[ESP32_OPTION_VGA_EXT_BASE])
#define ESP32_OPTION_VGA_VSYNC (Option.extensions[93])
#define ESP32_OPTION_VGA_PCLK (Option.extensions[94])
#define ESP32_OPTION_USB_ROLE (Option.extensions[95])

#endif /* ESP32_OPTION_EXT_H */
