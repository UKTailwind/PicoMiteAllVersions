/*
 * ports/pc386/port_tokens.h — bare-metal IBM-PC 386+.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

void cmd_sb16(void);
void cmd_sys(void);

#define HAL_PORT_VIDEO_CMD_TOKENS \
    {(unsigned char *)"MODE", T_CMD, 0, cmd_mode},

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN \
    {(unsigned char *)"SB16", T_CMD, 0, cmd_sb16},

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    {(unsigned char *)"Draw3D", T_CMD, 0, cmd_3D},

#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"SYS", T_CMD, 0, cmd_sys},

#define HAL_PORT_RAM_CMD_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

#define HAL_PORT_VIDEO_FUN_TOKENS \
    {(unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch},

#define HAL_PORT_WIFI_JSON_FUN_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
