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
 *   USBKEYBOARD builds — USB host HID (USBKeyboard.c). service() is a
 *     no-op here; the 1 kHz tuh_task() / hid_app_task() pump lives in
 *     PicoMite.c::routinechecks and will migrate later. clear_repeat_state
 *     calls into USBKeyboard.c's clearrepeat().
 *   Non-USB builds — PS/2 matrix (Keyboard.c::CheckKeyboard). I²C
 *     keyboard polling (I2C.c::CheckI2CKeyboard) stays in PicoMite.c's
 *     routinechecks because it is rate-limited via KeyCheck and only
 *     runs at 1 kHz; pumping it from check_interrupt (much higher rate)
 *     would add unnecessary I²C traffic.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_keyboard.h"

#ifndef USBKEYBOARD
#include "PS2Keyboard.h"  /* CheckKeyboard */
#endif

void hal_keyboard_service(void)
{
#ifdef USBKEYBOARD
    /* 1 kHz USB pump lives in PicoMite.c::routinechecks for now. */
#else
    if (Option.KeyboardConfig) {
        CheckKeyboard();
    }
#endif
}

void hal_keyboard_clear_repeat_state(void)
{
#ifdef USBKEYBOARD
    clearrepeat();
#else
    /* PS/2 / I²C backends do not run a software repeat state machine. */
#endif
}
