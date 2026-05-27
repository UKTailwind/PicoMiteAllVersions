/*
 * BTKeyboard.h — BLE HID host (HOG) keyboard input for PicoMiteBTH.
 *
 * Pairs with a BLE keyboard and feeds incoming HID reports into the
 * same console-RX path the USB HID host uses (USR_KEYBRD_ProcessData /
 * process_kbd_report). The USB CDC console stays as-is — this module
 * is an *input source*, not a console replacement.
 *
 * Step 1 (this file): brings up cyw43_arch + btstack HCI and blinks
 * the cyw43 LED to confirm Bluetooth firmware actually loaded. No
 * scanning, no pairing, no HID yet.
 */

#ifndef BTKEYBOARD_H
#define BTKEYBOARD_H

#if defined(PICOMITEBTH) || defined(PICOMITEHDMIBTH)

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up cyw43_arch + btstack, powers on HCI. Call once after the
   rest of the hardware is up (same place WEB calls cyw43_arch_init,
   same place PICOMITEBT calls bt_console_init). */
void bt_keyboard_init(void);

/* Cooperative poll — call from the main loop. Pumps cyw43_arch_poll
   and runs a heartbeat that toggles the cyw43 LED so we can visually
   confirm the BT firmware is alive. */
void bt_keyboard_poll(void);

/* True once HCI has reached HCI_STATE_WORKING. */
bool bt_keyboard_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* PICOMITEBTH || PICOMITEHDMIBTH */
#endif /* BTKEYBOARD_H */
