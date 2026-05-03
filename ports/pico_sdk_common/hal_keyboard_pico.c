/*
 * ports/pico_sdk_common/hal_keyboard_pico.c — hal_keyboard on Pico SDK targets.
 *
 * A single port-layer impl covers all 12 device CMake variants. The file
 * uses local target-macro #ifdef dispatch — permissible under the "drivers
 * and ports may have local #ifdef gates" rule in the HAL purity contract,
 * since nothing outside this file needs to know which keyboard backend
 * is selected.
 *
 * Backend selection:
 *   HAL_PORT_HAS_USB_KEYBOARD builds — USB host HID (USBKeyboard.c). service() is a
 *     no-op here; the 1 kHz tuh_task() / hid_app_task() pump lives in
 *     PicoMite.c::routinechecks and will migrate later. clear_repeat_state
 *     calls into USBKeyboard.c's clearrepeat(). init() walks the same
 *     hcd_port_reset / tuh_init sequence PicoMite.c used to run inline.
 *   Non-USB builds — PS/2 matrix (Keyboard.c::CheckKeyboard). I²C
 *     keyboard polling (I2C.c::CheckI2CKeyboard) stays in PicoMite.c's
 *     routinechecks because it is rate-limited via KeyCheck and only
 *     runs at 1 kHz; pumping it from check_interrupt (much higher rate)
 *     would add unnecessary I²C traffic. init() forwards to initKeyboard().
 *   PICOMITE + rp2350 (PicoCalc keypad) also exposes LocalKeyDown[] through
 *     the keydown accessors so fun_keydown works on that board.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_keyboard.h"
#include "hal/hal_pin.h"

#include <string.h>

#if !HAL_PORT_HAS_USB_KEYBOARD
#include "PS2Keyboard.h"  /* CheckKeyboard, initKeyboard, NO_KEYBOARD, CONFIG_* */
extern void mouse0close(void);
#endif

#if HAL_PORT_HAS_USB_KEYBOARD
#include "tusb.h"
#include "host/hcd.h"
extern void clearrepeat(void);
extern int KeyDown[7];
extern int caps_lock;
extern int num_lock;
extern int scroll_lock;
extern bool USBenabled;
#endif

#if HAL_PORT_HAS_PICOMITE && defined(rp2350)
extern int LocalKeyDown[7];
#endif

void hal_keyboard_service(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    /* 1 kHz USB pump lives in PicoMite.c::routinechecks for now. */
#else
    if (Option.KeyboardConfig) {
        CheckKeyboard();
    }
#endif
}

void hal_keyboard_clear_repeat_state(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    clearrepeat();
#else
    /* PS/2 / I²C backends do not run a software repeat state machine. */
#endif
}

void hal_keyboard_init(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
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
#else
    initKeyboard();
#endif
}

int hal_keyboard_keydown_count(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    int count = 0;
    for (int i = 0; i < 6; i++) if (KeyDown[i]) count++;
    return count;
#elif HAL_PORT_HAS_PICOMITE && defined(rp2350)
    int count = 0;
    for (int i = 0; i < 6; i++) if (LocalKeyDown[i]) count++;
    return count;
#else
    return 0;
#endif
}

int hal_keyboard_keydown_slot(int slot)
{
    if (slot < 1 || slot > 6) return 0;
#if HAL_PORT_HAS_USB_KEYBOARD
    return KeyDown[slot - 1];
#elif HAL_PORT_HAS_PICOMITE && defined(rp2350)
    return LocalKeyDown[slot - 1];
#else
    (void)slot;
    return 0;
#endif
}

uint32_t hal_keyboard_lock_state(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    return (caps_lock   ? 1u : 0u) |
           (num_lock    ? 2u : 0u) |
           (scroll_lock ? 4u : 0u);
#else
    return 0u;
#endif
}

int hal_keyboard_set_layout(int layout)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    /* USB backend accepts US/FR/GR/IT/UK/ES only. */
    if (layout != HAL_KBD_LAYOUT_US && layout != HAL_KBD_LAYOUT_FR &&
        layout != HAL_KBD_LAYOUT_GR && layout != HAL_KBD_LAYOUT_IT &&
        layout != HAL_KBD_LAYOUT_UK && layout != HAL_KBD_LAYOUT_ES) {
        return -1;
    }
    Option.USBKeyboard = layout;
    return 0;
#else
    switch (layout) {
        case HAL_KBD_LAYOUT_US:
        case HAL_KBD_LAYOUT_FR:
        case HAL_KBD_LAYOUT_GR:
        case HAL_KBD_LAYOUT_IT:
        case HAL_KBD_LAYOUT_BE:
        case HAL_KBD_LAYOUT_UK:
        case HAL_KBD_LAYOUT_ES:
        case HAL_KBD_LAYOUT_BR:
        case HAL_KBD_LAYOUT_I2C:
            Option.KeyboardConfig = layout;
            return 0;
        default:
            return -1;
    }
#endif
}

void hal_keyboard_quiesce_for_reset(void)
{
#if HAL_PORT_HAS_USB_KEYBOARD
    USBenabled = false;
    uSec(50000);   /* let outstanding USB transfers complete */
#endif
}

int hal_keyboard_usb_raw_report(int slot, unsigned char *out, int max_len)
{
    if (slot < 1 || slot > 4 || !out || max_len < 2) return 0;
#if HAL_PORT_HAS_USB_KEYBOARD
    /* HID[slot-1].report is a length-prefixed MMBasic string (report[0]
     * is the byte count). Copy the length byte plus payload, clamped to
     * the caller's buffer. */
    int total = HID[slot - 1].report[0] + 1;
    if (total > max_len) total = max_len;
    memcpy(out, (const void *)HID[slot - 1].report, (size_t)total);
    return total;
#else
    (void)slot; (void)out; (void)max_len;
    return 0;
#endif
}

void hal_keyboard_on_external_io_clear(void)
{
#if !HAL_PORT_HAS_USB_KEYBOARD
    OnPS2GOSUB = NULL;
    PS2code = 0;
    PS2int = false;
    if (!Option.MOUSE_CLOCK) mouse0close();
#endif
}

void hal_keyboard_on_gpio_edge(uint32_t gpio)
{
#if !HAL_PORT_HAS_USB_KEYBOARD
    uint64_t data = hal_pin_bank_read_all();
    if (Option.KEYBOARD_CLOCK) {
        if (!(Option.KeyboardConfig == NO_KEYBOARD || Option.KeyboardConfig == CONFIG_I2C) &&
            gpio == PinDef[Option.KEYBOARD_CLOCK].GPno)
            CNInterrupt(data);
    }
    if (MOUSE_CLOCK && gpio == PinDef[MOUSE_CLOCK].GPno) MNInterrupt(data);
#else
    (void)gpio;
#endif
}
