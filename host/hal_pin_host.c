/*
 * host/hal_pin_host.c — hal_pin stub for the native + WASM host.
 *
 * No real GPIO. Keeps a RAM table of pin modes and output values so that
 * tests and the --sim harness can query pin state after a BASIC program
 * touches a pin. Input reads always return false.
 */

#include <string.h>

#include "hal/hal_pin.h"

/* Cover both RP2040 (30 GPIOs) and RP2350 (48 GPIOs on the larger part). */
#define HAL_HOST_GPIO_MAX 64

static hal_pin_mode_t host_pin_modes[HAL_HOST_GPIO_MAX];
static bool           host_pin_levels[HAL_HOST_GPIO_MAX];

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode)
{
    if (gpio < HAL_HOST_GPIO_MAX) host_pin_modes[gpio] = mode;
}

bool hal_pin_read(uint32_t gpio)
{
    if (gpio >= HAL_HOST_GPIO_MAX) return false;
    return host_pin_levels[gpio];
}

void hal_pin_write(uint32_t gpio, bool high)
{
    if (gpio < HAL_HOST_GPIO_MAX) host_pin_levels[gpio] = high;
}

void hal_pin_toggle(uint32_t gpio)
{
    if (gpio < HAL_HOST_GPIO_MAX) host_pin_levels[gpio] = !host_pin_levels[gpio];
}

bool hal_pin_read_output_latch(uint32_t gpio)
{
    if (gpio >= HAL_HOST_GPIO_MAX) return false;
    return host_pin_levels[gpio];
}

void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA)
{
    (void)gpio;
    (void)mA;
}

void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull)
{
    (void)gpio;
    (void)pull;
}

void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir)
{
    (void)gpio;
    (void)dir;
}

void hal_pin_set_input_enabled(uint32_t gpio, bool enabled)
{
    (void)gpio;
    (void)enabled;
}

void hal_pin_select_digital(uint32_t gpio)
{
    (void)gpio;
}

void hal_pin_adc_select(uint32_t adc_channel)
{
    (void)adc_channel;
}

void hal_pin_adc_init(void)
{
}

void hal_pin_adc_set_temp_sensor(bool enabled)
{
    (void)enabled;
}

uint16_t hal_pin_adc_read(void)
{
    return 0;
}

void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled)
{
    (void)gpio;
    (void)enabled;
}

void hal_pin_set_slew_fast(uint32_t gpio, bool fast)
{
    (void)gpio;
    (void)fast;
}

void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled)
{
    (void)gpio;
    (void)edge_mask;
    (void)enabled;
}

void hal_pin_init_digital(uint32_t gpio)
{
    if (gpio < HAL_HOST_GPIO_MAX) {
        host_pin_modes[gpio]  = HAL_PIN_MODE_INPUT;
        host_pin_levels[gpio] = false;
    }
}

void hal_pin_deinit(uint32_t gpio)
{
    if (gpio < HAL_HOST_GPIO_MAX) {
        host_pin_modes[gpio]  = HAL_PIN_MODE_DISABLED;
        host_pin_levels[gpio] = false;
    }
}

void hal_pin_set_function(uint32_t gpio, hal_pin_func_t func)
{
    (void)gpio;
    (void)func;
}

uint64_t hal_pin_bank_read_all(void)
{
    uint64_t m = 0;
    for (uint32_t i = 0; i < HAL_HOST_GPIO_MAX; i++) {
        if (host_pin_levels[i]) m |= 1ULL << i;
    }
    return m;
}

uint64_t hal_pin_bank_read_out_latch(void)
{
    return hal_pin_bank_read_all();
}

void hal_pin_bank_set_mask(uint64_t mask)
{
    for (uint32_t i = 0; i < HAL_HOST_GPIO_MAX; i++) {
        if (mask & (1ULL << i)) host_pin_levels[i] = true;
    }
}

void hal_pin_bank_clr_mask(uint64_t mask)
{
    for (uint32_t i = 0; i < HAL_HOST_GPIO_MAX; i++) {
        if (mask & (1ULL << i)) host_pin_levels[i] = false;
    }
}

void hal_pin_bank_xor_mask(uint64_t mask)
{
    for (uint32_t i = 0; i < HAL_HOST_GPIO_MAX; i++) {
        if (mask & (1ULL << i)) host_pin_levels[i] = !host_pin_levels[i];
    }
}
