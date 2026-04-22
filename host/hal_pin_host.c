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

void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA)
{
    (void)gpio;
    (void)mA;
}
