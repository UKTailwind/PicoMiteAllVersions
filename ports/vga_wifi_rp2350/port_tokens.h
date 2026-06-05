/*
 * ports/vga_wifi_rp2350/port_tokens.h — COMPILE=VGAWIFIRP2350.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

#define HAL_PORT_VIDEO_CMD_TOKENS                             \
    {(unsigned char *)"TILE", T_CMD, 0, cmd_tile},            \
        {(unsigned char *)"MODE", T_CMD, 0, cmd_mode},        \
        {(unsigned char *)"Map(", T_CMD | T_FUN, 0, cmd_map}, \
        {(unsigned char *)"Map", T_CMD, 0, cmd_map},          \
        {(unsigned char *)"Colour Map", T_CMD, 0, cmd_colourmap},

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN

/* WiFi: Backlight + WEB cmds. */
#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS                       \
    {(unsigned char *)"Backlight", T_CMD, 0, cmd_backlight}, \
        {(unsigned char *)"WEB", T_CMD, 0, cmd_web},

#if HAL_PORT_KEYBOARD_USB_HOST
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Gamepad", T_CMD, 0, cmd_gamepad},
#else
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Update Firmware", T_CMD, 0, cmd_update},
#endif

/* WiFi rp2350: CYW43 owns QSPI pins, no PSRAM cmd. */
#define HAL_PORT_RAM_CMD_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

#define HAL_PORT_VIDEO_FUN_TOKENS                                            \
    {(unsigned char *)"DRAW3D(", T_FUN | T_INT, 0, fun_3D},                  \
        {(unsigned char *)"GetScanLine", T_FNA | T_INT, 0, fun_getscanline}, \
        {(unsigned char *)"Map(", T_FUN | T_INT, 0, fun_map},

#define HAL_PORT_WIFI_JSON_FUN_TOKEN \
    {(unsigned char *)"Json$(", T_FUN | T_STR, 0, fun_json},

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
