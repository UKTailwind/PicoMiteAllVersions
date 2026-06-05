/*
 * KeyboardMap.h -- shared HID keyboard report decoder + international
 * keymap, used by both the USB-HID-host build (USBKeyboard.c) and the
 * BLE-HID-host build (BTKeyboard.c).
 *
 * The 8-byte boot-keyboard report format is identical across both
 * transports, so the lookup tables, scancode-to-ASCII mapping, debounced
 * console push, and pressed/released tracking are shared. The transport
 * layers each handle their own discovery, pairing, and notification
 * delivery, then hand the raw report to process_kbd_report() here.
 *
 * LED feedback (Caps/Num/Scroll lock on the remote device) is gated by
 * #ifdef USBKEYBOARD inside process_key — the BLE build doesn't drive
 * remote-keyboard LEDs because the phone-side firmware manages them
 * locally.
 */

#ifndef KEYBOARDMAP_H
#define KEYBOARDMAP_H

#include <stdint.h>
#include <stdbool.h>

/* Both tinyusb (class/hid/hid.h) and btstack (src/btstack_hid.h)
   define `HID_REPORT_TYPE_INPUT/OUTPUT/FEATURE`, `hid_report_type_t`,
   `HID_USAGE_PAGE_*`, and `hid_mouse_report_t`. We can't pull in
   tinyusb's hid.h here because the BLE-HID-host build also includes
   btstack.h via BTKeyboard.c and the two clash.
   Instead, declare a local copy of just the boot-keyboard report
   struct we actually need. Guarded against tinyusb's header by its
   TUSB_HID_H_ sentinel — when tinyusb's hid.h is included first
   (the USB-host build, via tusb.h), our typedef is skipped and
   tinyusb's wins; otherwise (BLE build), our local one is used.
   Both layouts are byte-identical (modifier, reserved, 6 keycodes). */
#ifndef TUSB_HID_H_
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} hid_keyboard_report_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Look up a HID scancode + modifier byte against the currently-active
   keylayout (selected via Option.KeyboardConfig — US/UK/DE/FR/ES/BE).
   Returns an ASCII / extended-code byte (0x00 means no character to
   emit; e.g. modifier-only keypresses). */
uint8_t APP_MapKeyToUsage(uint8_t keyCode, int modifier);

/* Push a single byte into the BASIC console RX ring. Handles the
   Ctrl-C abort key (BreakKey), the ON KEY interrupt-selector, the
   25 ms debounce that filters key-repeat noise, and ring-buffer
   overflow (oldest byte dropped). Safe to call from IRQ context. */
void USR_KEYBRD_ProcessData(uint8_t data);

/* Decode an 8-byte HID boot-keyboard report. Tracks key-up/down
   transitions across calls, populates KeyDown[] (for the KEYDOWN()
   BASIC function), handles Caps/Num/Scroll lock, and emits the
   resulting characters via USR_KEYBRD_ProcessData. The `n` parameter
   is the HID device index for LED feedback in the USB build; the
   BLE build passes 0 (LED writes are gated by #ifdef USBKEYBOARD). */
void process_kbd_report(hid_keyboard_report_t const *report, uint8_t n);

/* ---- Consumer-control (media key) pseudo-ASCII codes --------------- *
 * Many Bluetooth/USB keyboards carry dedicated media keys (volume,
 * play/pause, track skip). These arrive as a separate HID
 * Consumer-Control report rather than in the boot-keyboard report, so
 * they have no HID keyboard scancode. We extend the special-key code
 * range used by the arrow / function keys with the codes below; a BASIC
 * program reads them through INKEY$ (e.g. IF INKEY$ = CHR$(&H8A) ...).
 *
 * The values sit in the 0x8A-0x90 gap left between the navigation keys
 * (0x80-0x89) and the function keys (0x91-0x9C) in the keymap tables,
 * so they don't collide with any US/UK special key. */
#define MM_KEY_VOLUME_UP   0x8A
#define MM_KEY_VOLUME_DOWN 0x8B
#define MM_KEY_MUTE        0x8C
#define MM_KEY_PLAY_PAUSE  0x8D
#define MM_KEY_STOP        0x8E
#define MM_KEY_PREV_TRACK  0x8F
#define MM_KEY_NEXT_TRACK  0x90

/* Decode a HID Consumer-Control report — a 16-bit little-endian
   consumer Usage ID (HID Usage Page 0x0C) — and queue the matching
   media-key pseudo-ASCII code (above) into the console RX ring via
   USR_KEYBRD_ProcessData. Returns true if the report was consumed:
   a recognised media key, or the all-zero key-release that follows
   every press. Returns false for any other report so the caller can
   fall back to its raw-notification logging. */
bool process_consumer_report(const uint8_t *report, uint16_t len);

/* Shared mouse post-decode: takes already-extracted x/y/wheel/button
   values and updates the nunstruct[n] / nunfoundc[n] arrays that the
   DEVICE(MOUSE n, "...") BASIC function reads. Called from both
   USBKeyboard.c (after its mouse-type dispatch) and BTKeyboard.c
   (after its HID-descriptor-based decode). n is the mouse slot:
   1..4, matching the existing DEVICE(MOUSE n, ...) numbering. */
void process_mouse_input(int16_t x_delta,
                         int16_t y_delta,
                         int8_t  wheel_delta,
                         uint8_t buttons,
                         uint8_t n);

/* ---- State shared with other translation units --------------------- */

extern int caps_lock;
extern int num_lock;
extern int scroll_lock;

/* KeyDown[0..5] = currently-pressed ASCII characters (up to 6
   simultaneous keys), KeyDown[6] = modifier bitmask. Read by the
   KEYDOWN() BASIC function in Functions.c. */
extern int KeyDown[7];

/* Currently-selected layout table — set by external code when
   Option.KeyboardConfig changes (the file MM_Misc.c does this via
   KBrdList[] selection). */
extern const int *keylayout;

#ifdef __cplusplus
}
#endif

#endif /* KEYBOARDMAP_H */
