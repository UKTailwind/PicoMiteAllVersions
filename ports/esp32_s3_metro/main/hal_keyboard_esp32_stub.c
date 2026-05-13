/*
 * hal_keyboard_esp32_stub.c — Phase B stub for hal/hal_keyboard.h.
 * No keyboard yet. Phase D wires USB Serial/JTAG stdin into MMgetchar.
 */

#include <stdint.h>
#include "hal/hal_keyboard.h"

void hal_keyboard_service(void) {}
void hal_keyboard_clear_repeat_state(void) {}
void hal_keyboard_init(void) {}
int hal_keyboard_keydown_count(void) { return 0; }
int hal_keyboard_keydown_slot(int s) { (void)s; return 0; }
uint32_t hal_keyboard_lock_state(void) { return 0; }
int hal_keyboard_set_layout(int l) { (void)l; return 0; }
void hal_keyboard_quiesce_for_reset(void) {}
int hal_keyboard_usb_raw_report(int s, unsigned char *o, int m) { (void)s; (void)o; (void)m; return 0; }
void hal_keyboard_on_external_io_clear(void) {}
void hal_keyboard_on_gpio_edge(uint32_t g) { (void)g; }
void hal_keyboard_routinechecks_pump(void) {}
void hal_console_usb_cdc_putc(char c, int flush) { (void)c; (void)flush; }
void hal_keyboard_timer_tick(void) {}
void hal_keyboard_init_external_mouse(void) {}
void hal_console_usb_cdc_boot_init(void) {}
void hal_keyboard_i2c_probe_at_boot(void) {}
int hal_keyboard_external_mouse_active(void) { return 0; }
