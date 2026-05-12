/*
 * ports/pc386/hal_pin_pc386.c — GPIO HAL over LPT1.
 *
 * GPIO numbers are DB-25 connector pin numbers. Data pins 2..9 and
 * control pins 1/14/16/17 are writable. Status pins 10/11/12/13/15 are
 * read-only inputs. The PC parallel-port inversion on BUSY and on three
 * control bits is hidden here so BASIC sees connector-level logic.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "hal/hal_pin.h"
#include "drivers/lpt_centronics/lpt_centronics.h"

static void pin_validate(uint32_t gpio) {
    if (!lpt_centronics_pin_valid(gpio)) error("Invalid pin");
}

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode) {
    pin_validate(gpio);
    switch (mode) {
        case HAL_PIN_MODE_DISABLED:
            (void)lpt_centronics_pin_set_mode(gpio, LPT_PIN_MODE_OFF);
            return;
        case HAL_PIN_MODE_INPUT:
        case HAL_PIN_MODE_INPUT_PULLUP:
        case HAL_PIN_MODE_INPUT_PULLDOWN:
            if (!lpt_centronics_pin_can_input(gpio)) error("Pin is output-only");
            (void)lpt_centronics_pin_set_mode(gpio, LPT_PIN_MODE_INPUT);
            return;
        case HAL_PIN_MODE_OUTPUT:
            if (!lpt_centronics_pin_can_output(gpio)) error("Pin is read-only");
            (void)lpt_centronics_pin_set_mode(gpio, LPT_PIN_MODE_OUTPUT);
            return;
        default:
            error("Unsupported SETPIN mode");
    }
}

bool hal_pin_read(uint32_t gpio) {
    pin_validate(gpio);
    return lpt_centronics_pin_read(gpio);
}

void hal_pin_write(uint32_t gpio, bool high) {
    pin_validate(gpio);
    if (!lpt_centronics_pin_write(gpio, high)) error("Pin is not an output");
}

void hal_pin_toggle(uint32_t gpio) {
    pin_validate(gpio);
    if (!lpt_centronics_pin_toggle(gpio)) error("Pin is not an output");
}

bool hal_pin_read_output_latch(uint32_t gpio) {
    pin_validate(gpio);
    return lpt_centronics_pin_read_latch(gpio);
}

void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA) { (void)gpio; (void)mA; }
void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull) { (void)gpio; (void)pull; }
void hal_pin_pulldown_reset(int pin) { (void)pin; }

void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir) {
    hal_pin_set_mode(gpio, dir == HAL_PIN_DIR_OUT ? HAL_PIN_MODE_OUTPUT : HAL_PIN_MODE_INPUT);
}

void hal_pin_set_input_enabled(uint32_t gpio, bool enabled) { (void)gpio; (void)enabled; }
void hal_pin_select_digital(uint32_t gpio) { (void)gpio; }
void hal_pin_adc_select(uint32_t adc_channel) { (void)adc_channel; error("ADC not available on PC386 LPT1"); }
void hal_pin_adc_init(void) {}
void hal_pin_adc_set_temp_sensor(bool enabled) { (void)enabled; }
uint16_t hal_pin_adc_read(void) { error("ADC not available on PC386 LPT1"); return 0; }
void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled) { (void)gpio; (void)enabled; }
void hal_pin_set_slew_fast(uint32_t gpio, bool fast) { (void)gpio; (void)fast; }
void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled)
{ (void)gpio; (void)edge_mask; (void)enabled; error("GPIO interrupts not available on PC386 LPT1"); }
void hal_pin_init_digital(uint32_t gpio) { pin_validate(gpio); }
void hal_pin_deinit(uint32_t gpio) { hal_pin_set_mode(gpio, HAL_PIN_MODE_DISABLED); }
void hal_pin_set_function(uint32_t gpio, hal_pin_func_t func) {
    pin_validate(gpio);
    if (func != HAL_PIN_FUNC_NONE && func != HAL_PIN_FUNC_SIO) error("Unsupported pin function");
}

uint64_t hal_pin_bank_read_all(void) {
    uint64_t v = 0;
    for (uint32_t pin = 1; pin <= 17; pin++) {
        if (lpt_centronics_pin_valid(pin) && lpt_centronics_pin_read(pin))
            v |= 1ull << pin;
    }
    return v;
}

uint64_t hal_pin_bank_read_out_latch(void) {
    uint64_t v = 0;
    for (uint32_t pin = 1; pin <= 17; pin++) {
        if (lpt_centronics_pin_valid(pin) && lpt_centronics_pin_read_latch(pin))
            v |= 1ull << pin;
    }
    return v;
}

void hal_pin_bank_set_mask(uint64_t mask) {
    for (uint32_t pin = 1; pin <= 17; pin++)
        if (mask & (1ull << pin)) hal_pin_write(pin, true);
}

void hal_pin_bank_clr_mask(uint64_t mask) {
    for (uint32_t pin = 1; pin <= 17; pin++)
        if (mask & (1ull << pin)) hal_pin_write(pin, false);
}

void hal_pin_bank_xor_mask(uint64_t mask) {
    for (uint32_t pin = 1; pin <= 17; pin++)
        if (mask & (1ull << pin)) hal_pin_toggle(pin);
}
