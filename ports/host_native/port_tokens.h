/*
 * ports/host_native/port_tokens.h — host (macOS native) build.
 *
 * Mirrors the rp2040 PicoMite SPI-LCD palette so the host token table
 * has the same layout as the canonical PICO build. host_peripheral_stubs.c
 * provides cmd_camera / cmd_backlight / cmd_update / fun_touch stubs.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

#define HAL_PORT_VIDEO_CMD_TOKENS \
    { (unsigned char *)"Camera",  T_CMD, 0, cmd_camera }, \
    { (unsigned char *)"Refresh", T_CMD, 0, cmd_refresh },

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN \
    { (unsigned char *)"Backlight", T_CMD, 0, cmd_backlight },

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    { (unsigned char *)"Draw3D", T_CMD, 0, cmd_3D },

#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    { (unsigned char *)"Update Firmware", T_CMD, 0, cmd_update },

#define HAL_PORT_RAM_CMD_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

#define HAL_PORT_VIDEO_FUN_TOKENS \
    { (unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch },

#define HAL_PORT_WIFI_JSON_FUN_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
