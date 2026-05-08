/*
 * hal_pin_esp32_stub.c — Phase B stub for hal/hal_pin.h.
 *
 * No real GPIO yet. All entries are no-ops; reads return 0. Phase D+
 * replaces this with a real impl over esp_driver_gpio + esp_adc.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/hal_pin.h"

void hal_pin_set_mode(uint32_t g, hal_pin_mode_t m) { (void)g; (void)m; }
bool hal_pin_read(uint32_t g) { (void)g; return false; }
void hal_pin_write(uint32_t g, bool h) { (void)g; (void)h; }
void hal_pin_toggle(uint32_t g) { (void)g; }
bool hal_pin_read_output_latch(uint32_t g) { (void)g; return false; }
void hal_pin_set_drive_mA(uint32_t g, uint8_t mA) { (void)g; (void)mA; }
void hal_pin_set_pulls(uint32_t g, hal_pin_pull_t p) { (void)g; (void)p; }
void hal_pin_set_dir(uint32_t g, hal_pin_dir_t d) { (void)g; (void)d; }
void hal_pin_set_input_enabled(uint32_t g, bool e) { (void)g; (void)e; }
void hal_pin_pulldown_reset(int p) { (void)p; }
void hal_pin_select_digital(uint32_t g) { (void)g; }
void hal_pin_adc_select(uint32_t c) { (void)c; }
void hal_pin_adc_init(void) {}
void hal_pin_adc_set_temp_sensor(bool e) { (void)e; }
uint16_t hal_pin_adc_read(void) { return 0; }
void hal_pin_set_input_hysteresis(uint32_t g, bool e) { (void)g; (void)e; }
void hal_pin_set_slew_fast(uint32_t g, bool f) { (void)g; (void)f; }
void hal_pin_irq_set_edge(uint32_t g, uint32_t m, bool e) { (void)g; (void)m; (void)e; }
void hal_pin_init_digital(uint32_t g) { (void)g; }
void hal_pin_deinit(uint32_t g) { (void)g; }
void hal_pin_set_function(uint32_t g, hal_pin_func_t f) { (void)g; (void)f; }
uint64_t hal_pin_bank_read_all(void) { return 0; }
uint64_t hal_pin_bank_read_out_latch(void) { return 0; }
void hal_pin_bank_set_mask(uint64_t m) { (void)m; }
void hal_pin_bank_clr_mask(uint64_t m) { (void)m; }
void hal_pin_bank_xor_mask(uint64_t m) { (void)m; }
