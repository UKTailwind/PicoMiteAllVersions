/*
 * hal/hal_keyboard.h — keyboard input HAL.
 *
 * Core code uses this surface to let the active keyboard backend drive
 * itself forward. All key characters feed into MMBasic's console RX ring
 * buffer (ConsoleRxBuf / getConsole()); this HAL does not return
 * characters — it only pumps the hardware. Core drains input via
 * getConsole() as it always has.
 *
 * A build target links exactly one hal_keyboard implementation. When a
 * board supports multiple physical keyboard peripherals at runtime —
 * e.g. a non-USB PicoMite / PicoCalc that can be configured as either a
 * PS/2 matrix or an I²C keyboard via `OPTION KEYBOARD` — that choice
 * lives inside the backend impl. Core does not know or care which
 * peripheral is wired up.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md §"Global conventions"):
 *   Caller owns all buffers. HAL impl never calls MMBasic's error().
 *   Functions declared here are *not* IRQ-safe unless documented so.
 */

#ifndef HAL_KEYBOARD_H
#define HAL_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pump the keyboard backend forward. Called from the interrupt-hook
 * path (check_interrupt in MM_Misc.c), which runs at high frequency
 * while the interpreter is executing BASIC code and polls the REPL
 * between statements.
 *
 * Side effect: any key that decodes to a console character is pushed
 * into ConsoleRxBuf by the backend's existing logic (USB HID callback,
 * PS/2 scan decoder). This HAL function returns nothing — callers
 * read characters through getConsole().
 *
 * An implementation may no-op if its backend is already pumped from
 * elsewhere in the board file (e.g. USB tuh_task() / I²C poll stay in
 * PicoMite.c::routinechecks for now; migration pending).
 *
 * IRQ-safety: *not* IRQ-safe. Thread context only. (USB host backends
 * require this — tuh_task() must run at thread priority.) */
void hal_keyboard_service(void);

/* Reset the backend's auto-repeat state machine. Called by the REPL and
 * Editor at context switches (new command, new edit, list-paging prompt)
 * so a key held across the transition doesn't generate spurious repeats
 * in the new context. A no-op on backends that don't implement
 * software repeat (PS/2, I²C, host scripted stdin). */
void hal_keyboard_clear_repeat_state(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_KEYBOARD_H */
