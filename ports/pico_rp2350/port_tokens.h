/*
 * ports/pico_rp2350/port_tokens.h — COMPILE=PICORP2350 / PICOUSBRP2350
 * (PicoMite SPI-LCD, rp2350).
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

#define HAL_PORT_VIDEO_CMD_TOKENS                      \
    {(unsigned char *)"Camera", T_CMD, 0, cmd_camera}, \
        {(unsigned char *)"Refresh", T_CMD, 0, cmd_refresh},

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN \
    {(unsigned char *)"Backlight", T_CMD, 0, cmd_backlight},

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    {(unsigned char *)"Draw3D", T_CMD, 0, cmd_3D},

#if HAL_PORT_KEYBOARD_USB_HOST
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Gamepad", T_CMD, 0, cmd_gamepad},
#else
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Update Firmware", T_CMD, 0, cmd_update},
#endif

/* RP2350 non-WiFi: PSRAM cmd. */
#define HAL_PORT_RAM_CMD_TOKEN \
    {(unsigned char *)"Ram", T_CMD, 0, cmd_psram},

/* RP2350 PicoMite: extra Map cmds (MEM332 framebuffer). */
#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS                \
    {(unsigned char *)"Map(", T_CMD | T_FUN, 0, cmd_map}, \
        {(unsigned char *)"Map", T_CMD, 0, cmd_map},

#define HAL_PORT_VIDEO_FUN_TOKENS \
    {(unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch},

#define HAL_PORT_WIFI_JSON_FUN_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN \
    {(unsigned char *)"Map(", T_FUN | T_INT, 0, fun_map},

#endif /* PORT_TOKENS_H */
