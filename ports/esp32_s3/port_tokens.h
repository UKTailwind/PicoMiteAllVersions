/*
 * ports/esp32_s3/port_tokens.h - ESP32-S3 build.
 *
 * Mirrors the host stdio token palette, with WEB enabled for the
 * ESP-IDF native WiFi command surface.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

#define HAL_PORT_VIDEO_CMD_TOKENS                          \
    {(unsigned char *)"MODE", T_CMD, 0, cmd_mode},         \
        {(unsigned char *)"Camera", T_CMD, 0, cmd_camera}, \
        {(unsigned char *)"Refresh", T_CMD, 0, cmd_refresh},

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN \
    {(unsigned char *)"Backlight", T_CMD, 0, cmd_backlight},

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    {(unsigned char *)"WEB", T_CMD, 0, cmd_web},

#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Update Firmware", T_CMD, 0, cmd_update},

/* ESP32 ports the shared PSRAM `RAM` command — slab is acquired from
 * heap_caps_aligned_alloc() by hal_psram_esp32.c at boot. */
#define HAL_PORT_RAM_CMD_TOKEN \
    {(unsigned char *)"Ram", T_CMD, 0, cmd_psram},

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

#define HAL_PORT_VIDEO_FUN_TOKENS \
    {(unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch},

#define HAL_PORT_WIFI_JSON_FUN_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
