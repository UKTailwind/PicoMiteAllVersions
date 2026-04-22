/*
 * ports/pico_sdk_common/hal_pin_pico.c — hal_pin over pico SDK GPIO.
 *
 * Thin wrapper over hardware/gpio.h. PWM and ADC live in separate TUs
 * when added.
 */

#include "pico/stdlib.h"
#include "hardware/adc.h"
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

bool hal_pin_read_output_latch(uint32_t gpio)
{
    return gpio_get_out_level(gpio);
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

void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull)
{
    switch (pull) {
    case HAL_PIN_PULL_NONE: gpio_set_pulls(gpio, false, false); break;
    case HAL_PIN_PULL_UP:   gpio_set_pulls(gpio, true,  false); break;
    case HAL_PIN_PULL_DOWN: gpio_set_pulls(gpio, false, true);  break;
    }
}

void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir)
{
    gpio_set_dir(gpio, dir == HAL_PIN_DIR_OUT ? GPIO_OUT : GPIO_IN);
}

void hal_pin_set_input_enabled(uint32_t gpio, bool enabled)
{
    gpio_set_input_enabled(gpio, enabled);
}

void hal_pin_select_digital(uint32_t gpio)
{
    gpio_set_function(gpio, GPIO_FUNC_SIO);
}

void hal_pin_adc_select(uint32_t adc_channel)
{
    adc_select_input(adc_channel);
}

void hal_pin_adc_init(void)
{
    adc_init();
}

void hal_pin_adc_set_temp_sensor(bool enabled)
{
    adc_set_temp_sensor_enabled(enabled);
}

uint16_t hal_pin_adc_read(void)
{
    return adc_read();
}

void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled)
{
    gpio_set_input_hysteresis_enabled(gpio, enabled);
}

void hal_pin_set_slew_fast(uint32_t gpio, bool fast)
{
    gpio_set_slew_rate(gpio, fast ? GPIO_SLEW_RATE_FAST : GPIO_SLEW_RATE_SLOW);
}

void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled)
{
    uint32_t sdk = 0;
    if (edge_mask & HAL_PIN_EDGE_RISE) sdk |= GPIO_IRQ_EDGE_RISE;
    if (edge_mask & HAL_PIN_EDGE_FALL) sdk |= GPIO_IRQ_EDGE_FALL;
    gpio_set_irq_enabled(gpio, sdk, enabled);
}
