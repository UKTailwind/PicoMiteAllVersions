/*
 * drivers/usb_host_kbd/hal_keyboard_usb.c — hal_keyboard real impl
 * for USB-host-keyboard ports. Linked when HAL_PORT_HAS_USB_KEYBOARD=1.
 *
 * Walks the TinyUSB host stack (tuh_*) for keyboard input. The
 * 1 kHz tuh_task / hid_app_task pump still runs out of
 * PicoMite.c::routinechecks for now.
 *
 * Non-USB-keyboard ports link drivers/ps2_matrix/hal_keyboard_ps2.c
 * instead.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_keyboard.h"
#include "hal/hal_pin.h"
#include "tusb.h"
#include "host/hcd.h"

#include <string.h>

extern void clearrepeat(void);
extern int KeyDown[7];
extern int caps_lock;
extern int num_lock;
extern int scroll_lock;
extern bool USBenabled;

void hal_keyboard_service(void) {
    /* 1 kHz USB pump lives in PicoMite.c::routinechecks for now. */
}

void hal_keyboard_clear_repeat_state(void) {
    clearrepeat();
}

void hal_keyboard_init(void) {
    clearrepeat();
    for (int i = 0; i < 4; i++) {
        memset((void *)&HID[i], 0, sizeof(struct s_HID));
        HID[i].report_requested = true;
    }
    hcd_port_reset(BOARD_TUH_RHPORT);
    uSec(10000);                 /* wait for any hub to power up */
    hcd_port_reset_end(BOARD_TUH_RHPORT);
    tuh_init(BOARD_TUH_RHPORT);
    USBenabled = true;
}

int hal_keyboard_keydown_count(void) {
    int count = 0;
    for (int i = 0; i < 6; i++) if (KeyDown[i]) count++;
    return count;
}

int hal_keyboard_keydown_slot(int slot) {
    if (slot < 1 || slot > 6) return 0;
    return KeyDown[slot - 1];
}

uint32_t hal_keyboard_lock_state(void) {
    return (caps_lock   ? 1u : 0u) |
           (num_lock    ? 2u : 0u) |
           (scroll_lock ? 4u : 0u);
}

int hal_keyboard_set_layout(int layout) {
    /* USB backend accepts US/FR/GR/IT/UK/ES only. */
    if (layout != HAL_KBD_LAYOUT_US && layout != HAL_KBD_LAYOUT_FR &&
        layout != HAL_KBD_LAYOUT_GR && layout != HAL_KBD_LAYOUT_IT &&
        layout != HAL_KBD_LAYOUT_UK && layout != HAL_KBD_LAYOUT_ES) {
        return -1;
    }
    Option.USBKeyboard = layout;
    return 0;
}

void hal_keyboard_quiesce_for_reset(void) {
    USBenabled = false;
    uSec(50000);   /* let outstanding USB transfers complete */
}

int hal_keyboard_usb_raw_report(int slot, unsigned char *out, int max_len) {
    if (slot < 1 || slot > 4 || !out || max_len < 2) return 0;
    /* HID[slot-1].report is a length-prefixed MMBasic string (report[0]
     * is the byte count). Copy the length byte plus payload, clamped to
     * the caller's buffer. */
    int total = HID[slot - 1].report[0] + 1;
    if (total > max_len) total = max_len;
    memcpy(out, (const void *)HID[slot - 1].report, (size_t)total);
    return total;
}

void hal_keyboard_on_external_io_clear(void) {
    /* USB backend has no PS/2 GOSUB / mouse state to clear. */
}

void hal_keyboard_on_gpio_edge(uint32_t gpio) {
    /* USB host owns USB-bus interrupts via TinyUSB; nothing to dispatch
     * from the GPIO edge handler. */
    (void)gpio;
}

extern void hid_app_task(void);

void hal_keyboard_routinechecks_pump(void) {
    /* TinyUSB host pump. Skip the first 2s after boot — TinyUSB's
     * own enumeration is still settling. */
    if (USBenabled && mSecTimer > 2000) {
        tuh_task();
        hid_app_task();
    }
}

void hal_console_usb_cdc_putc(char c, int flush) {
    /* USB-host-keyboard port: USB-A is in host mode so there's no
     * USB-CDC device-side stdio to write to. */
    (void)c; (void)flush;
}

extern volatile int keytimer;

void hal_keyboard_timer_tick(void) {
    keytimer++;
    for (int i = 0; i < 4; i++) {
        if (HID[i].Device_type) HID[i].report_timer++;
    }
}

void hal_keyboard_init_external_mouse(void) {
    /* USB host keyboard backend has no PS/2 mouse to initialise. */
}

void hal_console_usb_cdc_boot_init(void) {
    /* USB host keyboard owns USB-A — no USB-CDC device-side stdio. */
}
