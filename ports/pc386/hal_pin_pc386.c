/*
 * ports/pc386/hal_pin_pc386.c — GPIO HAL.
 *
 * Stage 3 ships error stubs: any BASIC program touching SETPIN /
 * PIN() / PULSE / IR ON receives a clear "GPIO not available"
 * error rather than corrupting nonexistent hardware.
 *
 * Stage 6.5 replaces this file with a real LPT1-backed impl
 * (parallel port at 0x378, DB-25 pin numbering): SETPIN 1..17
 * mirror connector pins, with data on 2..9, control on 1/14/16/17,
 * and read-only status on 10..15.
 */

#include "hal/hal_pin.h"
#include "pc386_panic.h"

__attribute__((noreturn))
static void pin_unsupported(void) {
    pc386_panic("GPIO not available on PC386 until stage 6.5 (LPT1)");
}

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode)
{
    (void)gpio; (void)mode;
    pin_unsupported();
}

bool hal_pin_read(uint32_t gpio) { (void)gpio; pin_unsupported(); }
void hal_pin_write(uint32_t gpio, bool high) { (void)gpio; (void)high; pin_unsupported(); }
void hal_pin_toggle(uint32_t gpio) { (void)gpio; pin_unsupported(); }
bool hal_pin_read_output_latch(uint32_t gpio) { (void)gpio; pin_unsupported(); }
void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA) { (void)gpio; (void)mA; pin_unsupported(); }
void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull) { (void)gpio; (void)pull; pin_unsupported(); }
void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir) { (void)gpio; (void)dir; pin_unsupported(); }
void hal_pin_set_input_enabled(uint32_t gpio, bool enabled) { (void)gpio; (void)enabled; pin_unsupported(); }
void hal_pin_select_digital(uint32_t gpio) { (void)gpio; pin_unsupported(); }
void hal_pin_adc_select(uint32_t adc_channel) { (void)adc_channel; pin_unsupported(); }
void hal_pin_adc_init(void) { pin_unsupported(); }
void hal_pin_adc_set_temp_sensor(bool enabled) { (void)enabled; pin_unsupported(); }
uint16_t hal_pin_adc_read(void) { pin_unsupported(); }
void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled) { (void)gpio; (void)enabled; pin_unsupported(); }
void hal_pin_set_slew_fast(uint32_t gpio, bool fast) { (void)gpio; (void)fast; pin_unsupported(); }
void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled)
{ (void)gpio; (void)edge_mask; (void)enabled; pin_unsupported(); }
void hal_pin_init_digital(uint32_t gpio) { (void)gpio; pin_unsupported(); }
void hal_pin_deinit(uint32_t gpio) { (void)gpio; pin_unsupported(); }
void hal_pin_set_function(uint32_t gpio, hal_pin_func_t func) { (void)gpio; (void)func; pin_unsupported(); }
uint64_t hal_pin_bank_read_all(void) { pin_unsupported(); }
uint64_t hal_pin_bank_read_out_latch(void) { pin_unsupported(); }
void hal_pin_bank_set_mask(uint64_t mask) { (void)mask; pin_unsupported(); }
void hal_pin_bank_clr_mask(uint64_t mask) { (void)mask; pin_unsupported(); }
void hal_pin_bank_xor_mask(uint64_t mask) { (void)mask; pin_unsupported(); }
