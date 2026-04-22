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
