/*
 * hal/hal_option_setters.h — per-port-shape OPTION setter sub-hooks
 * called from ports/pico_sdk_common/misc_option_setters.c. Each
 * sub-hook returns 1 if it consumed the cmdline, 0 otherwise. Real
 * impls live in the per-port driver TUs (drivers/<peripheral>/);
 * the unused half of each pair is a no-op stub.
 */

#ifndef HAL_OPTION_SETTERS_H
#define HAL_OPTION_SETTERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HDMI PINS — drivers/hdmi/hdmi_scanout.c (HDMI), stub elsewhere. */
int port_setter_hdmi_pins(unsigned char * cmdline);

/* KEYBOARD BACKLIGHT — display_merge_pico.c on PicoMite rp2350 (real),
 * stub elsewhere. */
int port_setter_keyboard_backlight(unsigned char * cmdline);

/* KEYBOARD REPEAT — usb_host_kbd (real, no Option.LOCAL_KEYBOARD
 * gate), ps2_matrix (real, requires Option.LOCAL_KEYBOARD). */
int port_setter_keyboard_repeat(unsigned char * cmdline);

/* PS2 PINS / KEYBOARD PINS — ps2_matrix (real), usb_host_kbd stub. */
int port_setter_ps2_pins(unsigned char * cmdline);

/* MOUSE — ps2_matrix (real), usb_host_kbd stub. */
int port_setter_mouse_pins(unsigned char * cmdline);

/* OPTION PICO ON/OFF — non-WiFi (MMweb_stubs.c) real, MMsetwifi.c stub. */
int port_setter_pico_pins(unsigned char * cmdline);

/* OPTION HEARTBEAT — non-WiFi (MMweb_stubs.c) full pin path, WiFi
 * (MMsetwifi.c) ON/OFF only. Whole port_heartbeat_option_setter
 * body is per-port. */
int port_setter_heartbeat(unsigned char * cmdline);

/* OPTION SYSTEM SPI / OPTION LCD SPI — non-VGA only. PicoMite rp2350
 * (display_merge_pico.c) mirrors SYSTEM SPI to LCD SPI on first config;
 * Web (main_init_stub.c) doesn't mirror; VGA family stubs. */
int port_setter_system_lcd_spi(unsigned char * cmdline);

/* MM.INFO TOUCH — non-VGA returns calibration state, VGA stub. */
int port_setter_touch_status(unsigned char * out_sret);

/* POKE DISPLAY — non-VGA dispatches by panel class, VGA stub. */
int port_setter_poke_display(unsigned char * p);

/* MM.INFO SCROLL / MM.INFO SCREENBUFF — PicoMite rp2350 (real), stub
 * elsewhere. */
int port_setter_scroll_start(int64_t * out_iret);
int port_setter_screenbuff(int64_t * out_iret);

#ifdef __cplusplus
}
#endif

#endif /* HAL_OPTION_SETTERS_H */
