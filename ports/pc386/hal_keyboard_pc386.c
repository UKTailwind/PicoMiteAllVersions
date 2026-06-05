/*
 * ports/pc386/hal_keyboard_pc386.c — keyboard HAL.
 *
 * Stage 3 reads input from COM1 (16550) — the developer is talking
 * to the kernel through `qemu -serial stdio` or a real RS-232 cable
 * to a host terminal, not yet through PS/2.
 *
 * Stage 4 swaps for the real PS/2 (8042) driver with IDT + PIC
 * remap; the same hal_keyboard surface absorbs that change.
 *
 * Most entries on this surface are stubs everywhere except RP2040 /
 * RP2350 (USB keyboard, layout selection, mouse). They no-op here
 * unconditionally; only `hal_keyboard_service` and the lock-state
 * accessors need real bodies, and those land in 3e.
 */

#include "hal/hal_keyboard.h"
#include "pc386_panic.h"
#include "../../drivers/i8042_kbd/i8042_kbd.h"

void hal_keyboard_service(void) {
    /* Pumped from check_interrupt; must not panic just because input
     * isn't wired yet. The real serial drain lands in 3e. */
}

void hal_keyboard_clear_repeat_state(void) {
    kbd_clear_repeat_state();
}

void hal_keyboard_init(void) {
    /* serial_init() already happens in kmain; nothing to do until
     * stage 4 brings up PS/2. */
}

int hal_keyboard_keydown_count(void) {
    return 0;
}
int hal_keyboard_keydown_slot(int slot) {
    (void)slot;
    return 0;
}
uint32_t hal_keyboard_lock_state(void) {
    return 0;
}

int hal_keyboard_set_layout(int layout) {
    (void)layout;
    return -1; /* No reconfigurable layout on a serial-fed REPL. */
}

void hal_keyboard_quiesce_for_reset(void) {}

int hal_keyboard_usb_raw_report(int slot, unsigned char * out, int max_len) {
    (void)slot;
    (void)out;
    (void)max_len;
    return 0; /* No USB host. */
}

void hal_keyboard_on_external_io_clear(void) {}
void hal_keyboard_on_gpio_edge(uint32_t gpio) {
    (void)gpio;
}
void hal_keyboard_routinechecks_pump(void) {}
void hal_console_usb_cdc_putc(char c, int flush) {
    (void)c;
    (void)flush;
}
void hal_keyboard_timer_tick(void) {}
void hal_keyboard_init_external_mouse(void) {}
void hal_console_usb_cdc_boot_init(void) {}
void hal_keyboard_i2c_probe_at_boot(void) {}
int hal_keyboard_external_mouse_active(void) {
    return 0;
}
