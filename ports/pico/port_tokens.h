/*
 * ports/pico/port_tokens.h — COMPILE=PICO and COMPILE=PICOUSB
 * (PicoMite SPI-LCD, rp2040).
 *
 * Per-port token-table palettes for AllCommands.h. Each macro
 * expands to a comma-separated list of MMBasic token init-list
 * entries (or to nothing). The USB-axis palette uses the
 * HAL_PORT_KEYBOARD_USB_HOST flag set by the per-build target_compile
 * options; this file is per-port impl and so is allowed to gate on it.
 */

#ifndef PORT_TOKENS_H
#define PORT_TOKENS_H

/* Non-VGA: Camera + Refresh. */
#define HAL_PORT_VIDEO_CMD_TOKENS                      \
    {(unsigned char *)"Camera", T_CMD, 0, cmd_camera}, \
        {(unsigned char *)"Refresh", T_CMD, 0, cmd_refresh},

/* PicoMite: Backlight cmd. */
#define HAL_PORT_BACKLIGHT_PIC_CMD_TOKEN \
    {(unsigned char *)"Backlight", T_CMD, 0, cmd_backlight},

/* Non-WiFi: Draw3D cmd. */
#define HAL_PORT_WIFI_OR_3D_CMD_TOKENS \
    {(unsigned char *)"Draw3D", T_CMD, 0, cmd_3D},

/* USB axis: Update Firmware on PS/2, Gamepad on USB-host. */
#if HAL_PORT_KEYBOARD_USB_HOST
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Gamepad", T_CMD, 0, cmd_gamepad},
#else
#define HAL_PORT_USB_OR_FIRMWARE_CMD_TOKEN \
    {(unsigned char *)"Update Firmware", T_CMD, 0, cmd_update},
#endif

/* RP2040 — no PSRAM cmd. */
#define HAL_PORT_RAM_CMD_TOKEN

/* RP2040 PicoMite — no rp2350 Map cmd. */
#define HAL_PORT_RP2350_PIC_MAP_CMD_TOKENS

/* Non-VGA: Touch( fn. */
#define HAL_PORT_VIDEO_FUN_TOKENS \
    {(unsigned char *)"Touch(", T_FUN | T_INT, 0, fun_touch},

/* Non-WiFi — no Json$( fn. */
#define HAL_PORT_WIFI_JSON_FUN_TOKEN

/* RP2040 — no rp2350 Map( fn. */
#define HAL_PORT_RP2350_PIC_MAP_FUN_TOKEN

#endif /* PORT_TOKENS_H */
