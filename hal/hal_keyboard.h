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

#include <stdint.h>

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

/* Bring the keyboard backend hardware up. Called once from the port's
 * boot sequence (PicoMite.c / host main). PS/2 backends enable GPIO
 * edge IRQs on the clock pin and seed caps/num lock LEDs. USB host
 * backends run hcd_port_reset() + tuh_init() and flag the stack ready.
 * I²C keyboards (PicoCalc) may be a no-op if their poll loop is
 * driven elsewhere.
 *
 * Safe to call a second time — backend-specific re-init semantics. */
void hal_keyboard_init(void);

/* fun_keydown plumbing. Backends that report USB HID slots or a
 * local scanned key vector expose them through these accessors; other
 * backends return zeros. All three are thread-context only.
 *
 * hal_keyboard_keydown_count() — number of keys currently pressed
 *   (0..6). Corresponds to fun_keydown(0).
 *
 * hal_keyboard_keydown_slot(slot) — slot is 1-based (1..6); returns
 *   the HID keycode currently held in that slot, or 0 if empty.
 *   Corresponds to fun_keydown(n) for n in 1..6.
 *
 * hal_keyboard_lock_state() — returns a bitmap, bit0 = caps lock,
 *   bit1 = num lock, bit2 = scroll lock. Corresponds to
 *   fun_keydown(8). */
int      hal_keyboard_keydown_count(void);
int      hal_keyboard_keydown_slot(int slot);
uint32_t hal_keyboard_lock_state(void);

/* OPTION KEYBOARD layout select. Codes mirror the legacy CONFIG_*
 * namespace so backend code can index tables directly.
 * USB backends reject BE/BR/I2C (returns -1). Non-USB
 * backends accept the full set.
 *
 * On success, the backend has written the appropriate Option field
 * (Option.KeyboardConfig vs Option.USBKeyboard) and the caller is
 * responsible for SaveOptions() + reboot. On unsupported input the
 * HAL returns -1 without touching Option. */
enum {
    HAL_KBD_LAYOUT_US  = 1,
    HAL_KBD_LAYOUT_FR  = 2,
    HAL_KBD_LAYOUT_GR  = 3,
    HAL_KBD_LAYOUT_IT  = 4,
    HAL_KBD_LAYOUT_BE  = 5,
    HAL_KBD_LAYOUT_UK  = 6,
    HAL_KBD_LAYOUT_ES  = 7,
    HAL_KBD_LAYOUT_BR  = 8,
    HAL_KBD_LAYOUT_I2C = 128,
};

int hal_keyboard_set_layout(int layout);

/* Quiesce the keyboard backend ahead of a software reset. USB host
 * backends clear the "USB enabled" flag and sleep briefly so outstanding
 * transfers drain; PS/2 / I²C backends have nothing to quiesce. Callers
 * invoke this right before triggering the watchdog reset in SoftReset(). */
void hal_keyboard_quiesce_for_reset(void);

/* Copy the most recent USB HID report for device slot `n` (1..4) into
 * `out` (must hold at least `max_len` bytes). Returns the number of
 * bytes written. On non-USB backends returns 0 and leaves `out`
 * untouched. Exists so fun_nunchuck's "RAW" query can reach the USB HID
 * report buffer without core touching HID[] directly. */
int hal_keyboard_usb_raw_report(int slot, unsigned char *out, int max_len);

/* Invoked from ClearExternalIO / gpio_callback teardown paths. PS/2
 * backends clear their GOSUB / code / int globals and shut down the
 * software mouse pump; USB / I²C backends have nothing to do. */
void hal_keyboard_on_external_io_clear(void);

/* Dispatched from gpio_callback on every GPIO edge. PS/2 backends
 * match `gpio` against their clock / mouse-clock pins and decode the
 * edge into scan-code state; USB backends are IRQ-driven from
 * tinyusb and ignore this. */
void hal_keyboard_on_gpio_edge(uint32_t gpio);

/* Routinechecks-tier console-input pump. Runs from PicoMite.c's
 * routinechecks at 1 kHz when the BASIC interpreter has no current
 * line. USB-host-keyboard ports drive tuh_task + hid_app_task here;
 * non-USB ports drain USB-CDC stdio characters into the console
 * ring buffer. No-op stub for ports without an input pump. */
void hal_keyboard_routinechecks_pump(void);

/* Output a single character to the USB-CDC stdio console if
 * available. On non-USB-host-keyboard ports the USB-A peripheral is
 * in device-CDC mode and the real impl checks tud_cdc_connected();
 * USB-host-keyboard ports' USB-A is in host mode and the stub
 * no-ops. */
void hal_console_usb_cdc_putc(char c, int flush);

/* 1 kHz timer tick — bumps per-backend timer state. USB ports tick
 * keytimer + HID[].report_timer; non-USB ports tick nunstruct/mouse
 * timers. Stub no-op for ports without timer state. */
void hal_keyboard_timer_tick(void);

/* Initialise the external (PS/2) mouse if the port supports one.
 * USB-host-keyboard ports have no PS/2 mouse — stub no-op. */
void hal_keyboard_init_external_mouse(void);

/* USB-CDC stdio boot setup. PS/2 ports run stdio_set_translate_crlf
 * + a 5 s wait for the host to connect so kicked-off serial output
 * isn't lost; USB-host-keyboard ports have no USB-CDC stdio and
 * stub no-op. */
void hal_console_usb_cdc_boot_init(void);

/* Boot-time I²C-keyboard probe. Non-USB-keyboard ports check
 * Option.KeyboardConfig==CONFIG_I2C and run two CheckI2CKeyboard()
 * passes to drain any pending keystroke; USB-keyboard ports no-op
 * (CheckI2CKeyboard isn't compiled there). Called from
 * InitReservedIO() after the SYSTEM_I2C bus comes up. */
void hal_keyboard_i2c_probe_at_boot(void);

/* Whether an external pointer device (USB HID mouse on USB-keyboard
 * ports, PS/2 mouse on PS/2 ports) is currently attached. Used by
 * the editor console to decide whether to run the mouse-cursor
 * pump. Returns 0 on host / mmbasic_stdio / wasm. */
int hal_keyboard_external_mouse_active(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_KEYBOARD_H */
