/*
 * VM syscall conversion rule:
 * - copy/adapt legacy implementation code as closely as possible
 * - copy/adapt dependent legacy helpers too when needed
 * - do not invent new algorithms when legacy code already exists
 * - do not call, wrap, or dispatch back into legacy handlers
 * Any deviation from legacy implementation shape must be explicit and justified.
 */

#include "vm_sys_input.h"
#include "vm_device_support.h"
#include "MMBasic_Includes.h"

#include "hal/hal_keyboard.h"

int vm_sys_input_keydown(int n) {
    if (n < 0 || n > 8) error("Number out of bounds");
    if (n == 8) return (int)hal_keyboard_lock_state();
    if (n == 0) return hal_keyboard_keydown_count();
    return hal_keyboard_keydown_slot(n);
}

/* fun_keydown is the BASIC-level `KEYDOWN(n)` handler. It lives here
 * (not in MM_Misc.c) so the host build — which swaps MM_Misc.c for
 * mm_misc_shared.c — still picks it up via the VM-syscall translation
 * unit that both builds share. */
void fun_keydown(void) {
    int n = getint(ep, 0, 8);
    while (getConsole() != -1);         /* drain any buffered console input */
    if (n == 8) {
        iret = (int)hal_keyboard_lock_state();
    } else if (n) {
        iret = hal_keyboard_keydown_slot(n);
    } else {
        iret = hal_keyboard_keydown_count();
    }
    targ = T_INT;
}
