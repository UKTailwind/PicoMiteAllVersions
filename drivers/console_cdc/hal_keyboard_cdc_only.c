/*
 * drivers/console_cdc/hal_keyboard_cdc_only.c — hal_keyboard +
 * port-adapter impl for headless ports that drive the BASIC REPL and
 * printf traces over the RP2350 native USB peripheral in CDC device
 * mode, with no physical keyboard.
 *
 * The actual CDC plumbing lives in console_cdc.c (shared with the PS/2
 * backend). This file's HAL bodies are thin shims onto those helpers
 * plus stubs for everything else (no key scan, no PS/2 mouse, no USB
 * gamepad, no I²C keypad).
 *
 * Mutually exclusive with the USB-host keyboard backend: TinyUSB host
 * and pico_stdio_usb both want the same USB controller.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_keyboard.h"
#include "console_cdc.h"
#include "drivers/ps2_matrix/PS2Keyboard.h"

void hal_keyboard_service(void) { }

void hal_keyboard_clear_repeat_state(void) { }

void hal_keyboard_init(void) { }

void hal_keyboard_init_external_mouse(void) { }

void hal_keyboard_i2c_probe_at_boot(void) { }

int hal_keyboard_external_mouse_active(void) { return 0; }

int hal_keyboard_keydown_count(void) { return 0; }

int hal_keyboard_keydown_slot(int slot) { (void)slot; return 0; }

uint32_t hal_keyboard_lock_state(void) { return 0u; }

int hal_keyboard_set_layout(int layout) {
    if (layout < HAL_KBD_LAYOUT_US || layout > HAL_KBD_LAYOUT_BR) return -1;
    Option.KeyboardConfig = NO_KEYBOARD;
    return 0;
}

void hal_keyboard_quiesce_for_reset(void) { }

int hal_keyboard_usb_raw_report(int slot, unsigned char *out, int max_len) {
    (void)slot; (void)out; (void)max_len;
    return 0;
}

void hal_keyboard_on_external_io_clear(void) { }

void hal_keyboard_on_gpio_edge(uint32_t gpio) { (void)gpio; }

void hal_keyboard_timer_tick(void) { }

void hal_console_usb_cdc_boot_init(void) {
    console_cdc_boot_setup();
}

void hal_console_usb_cdc_putc(char c, int flush) {
    console_cdc_putc(c, flush);
}

void hal_keyboard_routinechecks_pump(void) {
    console_cdc_drain_to_rxbuf();
}

/* ------------------------------------------------------------------------ */
/*  Port-adapter stubs.                                                     */
/*                                                                          */
/*  These functions normally come from drivers/usb_host_kbd/USBKeyboard.c   */
/*  or drivers/ps2_matrix/Keyboard.c. With neither backend compiled in,     */
/*  they get stubbed here.                                                  */
/* ------------------------------------------------------------------------ */

int port_usb_count(void) { return 0; }

int port_usb_hid_field(int n, int field) { (void)n; (void)field; return 0; }

void port_print_kb_layout(void) { }

void port_print_kb_repeat(void) { }

int port_setter_keyboard_repeat(unsigned char *cmdline) { (void)cmdline; return 0; }
int port_setter_ps2_pins(unsigned char *cmdline)        { (void)cmdline; return 0; }
int port_setter_mouse_pins(unsigned char *cmdline)      { (void)cmdline; return 0; }

/* ------------------------------------------------------------------------ */
/*  MOUSE / GAMEPAD command stubs.                                          */
/*                                                                          */
/*  Core External.c::cmd_device dispatches MOUSE / GAMEPAD subcommands      */
/*  unconditionally; mouse.c (PS/2) and USBKeyboard.c (USB) provide real    */
/*  bodies. Without either we error at runtime — neither command makes      */
/*  sense on a headless CDC-stdio diagnostic port.                          */
/* ------------------------------------------------------------------------ */

void cmd_mouse(void)   { error("Not supported on this port"); }
void cmd_gamepad(void) { error("Not supported on this port"); }

bool mouse0 = false;
void initMouse0(int sensitivity) { (void)sensitivity; }

/* PS/2 mouse state — referenced unconditionally by Functions.c::fun_tilde
 * (MM.PS2 / MM.PS2INT). Always-zero gives MM.PS2 = 0, MM.PS2INT = false. */
volatile int  PS2code = 0;
volatile bool PS2int  = false;
