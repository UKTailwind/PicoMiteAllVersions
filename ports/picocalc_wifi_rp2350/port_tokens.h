/*
 * ports/picocalc_wifi_rp2350/port_tokens.h -- token contributions for
 * the ClockworkPi PicoCalc Pico 2 W port.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

#define HAL_PORT_VIDEO_CMD_TOKENS                      \
    {(unsigned char *)"Camera", T_CMD, 0, cmd_camera}, \
        {(unsigned char *)"Refresh", T_CMD, 0, cmd_refresh},

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS                       \
    {(unsigned char *)"Backlight", T_CMD, 0, cmd_backlight}, \
        {(unsigned char *)"WEB", T_CMD, 0, cmd_web},

#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Update Firmware", T_CMD, 0, cmd_update},

/* PicoCalc Pico 2 W keeps QSPI PSRAM available. */
#define HAL_PORT_RAM_CMD_TOKEN \
    {(unsigned char *)"Ram", T_CMD, 0, cmd_psram},

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

#define HAL_PORT_VIDEO_FUN_TOKENS \
    {(unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch},

#define HAL_PORT_WIFI_JSON_FUN_TOKEN \
    {(unsigned char *)"Json$(", T_FUN | T_STR, 0, fun_json},

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
