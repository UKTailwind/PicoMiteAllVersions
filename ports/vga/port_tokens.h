/*
 * ports/vga/port_tokens.h — COMPILE=VGA / VGAUSB (PicoMiteVGA, rp2040).
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

/* VGA family: TILE / MODE / Map / Colour Map. */
#define HAL_PORT_VIDEO_CMD_TOKENS \
    { (unsigned char *)"TILE",       T_CMD,             0, cmd_tile      }, \
    { (unsigned char *)"MODE",       T_CMD,             0, cmd_mode      }, \
    { (unsigned char *)"Map(",       T_CMD | T_FUN,     0, cmd_map       }, \
    { (unsigned char *)"Map",        T_CMD,             0, cmd_map       }, \
    { (unsigned char *)"Colour Map", T_CMD,             0, cmd_colourmap },

#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN

#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    { (unsigned char *)"Draw3D", T_CMD, 0, cmd_3D },

#if HAL_PORT_HAS_USB_KEYBOARD
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    { (unsigned char *)"Gamepad", T_CMD, 0, cmd_gamepad },
#else
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    { (unsigned char *)"Update Firmware", T_CMD, 0, cmd_update },
#endif

#define HAL_PORT_RAM_CMD_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

/* VGA: DRAW3D / GetScanLine / Map( fns. */
#define HAL_PORT_VIDEO_FUN_TOKENS \
    { (unsigned char *)"DRAW3D(",     T_FUN | T_INT, 0, fun_3D          }, \
    { (unsigned char *)"GetScanLine", T_FNA | T_INT, 0, fun_getscanline }, \
    { (unsigned char *)"Map(",        T_FUN | T_INT, 0, fun_map         },

#define HAL_PORT_WIFI_JSON_FUN_TOKEN

#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
