/*
 * ports/pico_sdk_common/hal_pin_pico.c — hal_pin over pico SDK GPIO.
 *
 * Thin wrapper over hardware/gpio.h. PWM and ADC live in separate TUs
 * when added.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "hal/hal_pin.h"

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode)
{
    switch (mode) {
    case HAL_PIN_MODE_DISABLED:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_input_enabled(gpio, false);
        gpio_disable_pulls(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        break;

    case HAL_PIN_MODE_INPUT:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_pulls(gpio, false, false);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_set_input_enabled(gpio, true);
        break;

    case HAL_PIN_MODE_INPUT_PULLUP:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_pulls(gpio, true, false);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_set_input_enabled(gpio, true);
        break;

    case HAL_PIN_MODE_INPUT_PULLDOWN:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_pulls(gpio, false, true);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_set_input_enabled(gpio, true);
        break;

    case HAL_PIN_MODE_OUTPUT:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_set_input_enabled(gpio, false);
        gpio_set_drive_strength(gpio, GPIO_DRIVE_STRENGTH_8MA);
        break;

    case HAL_PIN_MODE_OPEN_DRAIN:
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_put(gpio, 0);
        gpio_set_pulls(gpio, true, false);
        gpio_set_dir(gpio, GPIO_IN);
        break;

    case HAL_PIN_MODE_ANALOG:
        /* ADC channels are initialised via hal_pin_adc_*() in a follow-up
         * commit. Until then, leave the pin in the SDK's ADC-friendly
         * default. */
        gpio_set_function(gpio, GPIO_FUNC_NULL);
        break;

    case HAL_PIN_MODE_PWM:
        gpio_set_function(gpio, GPIO_FUNC_PWM);
        break;
    }
}

bool hal_pin_read(uint32_t gpio)
{
    return gpio_get(gpio);
}

void hal_pin_write(uint32_t gpio, bool high)
{
    gpio_put(gpio, high);
}

void hal_pin_toggle(uint32_t gpio)
{
    gpio_xor_mask64(1ULL << gpio);
}

void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA)
{
    enum gpio_drive_strength s = GPIO_DRIVE_STRENGTH_4MA;
    if      (mA <= 2)  s = GPIO_DRIVE_STRENGTH_2MA;
    else if (mA <= 4)  s = GPIO_DRIVE_STRENGTH_4MA;
    else if (mA <= 8)  s = GPIO_DRIVE_STRENGTH_8MA;
    else               s = GPIO_DRIVE_STRENGTH_12MA;
    gpio_set_drive_strength(gpio, s);
}
