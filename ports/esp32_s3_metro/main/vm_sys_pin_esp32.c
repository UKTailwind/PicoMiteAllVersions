/*
 * ESP32-S3 VM pin syscalls.
 *
 * This is the real-hardware counterpart to ports/vm_sys_sim/. It implements
 * BASIC SETPIN/PIN for digital GPIO and raw ADC; PWM/servo remain explicit
 * unsupported features until LEDC is wired.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "vm_sys_pin_internal.h"
#include "hal/hal_pin.h"

extern int last_adc;

static int vm_pin_resolve_esp32(int64_t pin) {
    if (pin < 0)
        pin = codemap((int)(-pin - 1));
    if (pin < 1 || pin > NBRPINS || (PinDef[pin].mode & UNUSED))
        error("Invalid pin");
    return (int)pin;
}

static void vm_pin_require_option_none(int option) {
    if (option != VM_PIN_OPT_NONE)
        error("Unsupported SETPIN option");
}

void vm_sys_pin_setpin(int64_t pin, int mode, int option) {
    int resolved = vm_pin_resolve_esp32(pin);
    uint32_t gpio = (uint32_t)PinDef[resolved].GPno;

    if (mode == VM_PIN_MODE_PWM_AUTO || vm_pin_mode_is_pwm(mode))
        error("PWM not supported on this port yet");

    switch (mode) {
    case VM_PIN_MODE_OFF:
        hal_pin_deinit(gpio);
        ExtCurrentConfig[resolved] = EXT_NOT_CONFIG;
        return;

    case VM_PIN_MODE_DIN:
        if (!(PinDef[resolved].mode & DIGITAL_IN))
            error("Invalid configuration");
        if (option == VM_PIN_OPT_PULLUP)
            hal_pin_set_mode(gpio, HAL_PIN_MODE_INPUT_PULLUP);
        else if (option == VM_PIN_OPT_PULLDOWN)
            hal_pin_set_mode(gpio, HAL_PIN_MODE_INPUT_PULLDOWN);
        else if (option == VM_PIN_OPT_NONE)
            hal_pin_set_mode(gpio, HAL_PIN_MODE_INPUT);
        else
            error("Unsupported SETPIN option");
        ExtCurrentConfig[resolved] = EXT_DIG_IN;
        return;

    case VM_PIN_MODE_DOUT:
        vm_pin_require_option_none(option);
        if (!(PinDef[resolved].mode & DIGITAL_OUT))
            error("Invalid configuration");
        hal_pin_set_mode(gpio, HAL_PIN_MODE_OUTPUT);
        ExtCurrentConfig[resolved] = EXT_DIG_OUT;
        return;

    case VM_PIN_MODE_ARAW:
        vm_pin_require_option_none(option);
        if (!(PinDef[resolved].mode & ANALOG_IN))
            error("Invalid configuration");
        hal_pin_set_mode(gpio, HAL_PIN_MODE_ANALOG);
        hal_pin_adc_select(PinDef[resolved].ADCpin);
        last_adc = resolved;
        ExtCurrentConfig[resolved] = EXT_ADCRAW;
        return;

    default:
        error("Unsupported SETPIN mode");
    }
}

int64_t vm_sys_pin_read(int64_t pin) {
    int resolved = vm_pin_resolve_esp32(pin);
    uint32_t gpio = (uint32_t)PinDef[resolved].GPno;

    switch (ExtCurrentConfig[resolved]) {
    case EXT_DIG_IN:
        return hal_pin_read(gpio);

    case EXT_DIG_OUT:
        return hal_pin_read_output_latch(gpio);

    case EXT_ADCRAW:
        if (last_adc != resolved) {
            hal_pin_adc_select(PinDef[resolved].ADCpin);
            last_adc = resolved;
        }
        return hal_pin_adc_read();

    default:
        error("Pin not configured");
        return 0;
    }
}

void vm_sys_pin_write(int64_t pin, int64_t value) {
    int resolved = vm_pin_resolve_esp32(pin);
    uint32_t gpio = (uint32_t)PinDef[resolved].GPno;

    if (ExtCurrentConfig[resolved] == EXT_NOT_CONFIG) {
        if (!(PinDef[resolved].mode & DIGITAL_OUT))
            error("Invalid configuration");
        hal_pin_set_mode(gpio, HAL_PIN_MODE_OUTPUT);
        ExtCurrentConfig[resolved] = EXT_DIG_OUT;
    } else if (ExtCurrentConfig[resolved] != EXT_DIG_OUT) {
        error("Pin is not an output");
    }

    hal_pin_write(gpio, value != 0);
}

void vm_sys_pwm_configure(int slice, MMFLOAT frequency,
                          int has_duty1, MMFLOAT duty1,
                          int has_duty2, MMFLOAT duty2,
                          int phase_correct, int delaystart) {
    (void)slice;
    (void)frequency;
    (void)has_duty1;
    (void)duty1;
    (void)has_duty2;
    (void)duty2;
    (void)phase_correct;
    (void)delaystart;
    error("PWM not supported on this port yet");
}

void vm_sys_pwm_sync(uint16_t present_mask, const MMFLOAT * counts) {
    (void)present_mask;
    (void)counts;
    error("PWM not supported on this port yet");
}

void vm_sys_pwm_off(int slice) {
    if (slice < 0 || slice > vm_pwm_max_slice())
        error("Number out of bounds");
}

void vm_sys_servo_configure(int slice,
                            int has_pos1, MMFLOAT pos1,
                            int has_pos2, MMFLOAT pos2) {
    (void)slice;
    (void)has_pos1;
    (void)pos1;
    (void)has_pos2;
    (void)pos2;
    error("Servo not supported on this port yet");
}

void vm_sys_pin_reset(void) {
    for (int pin = 1; pin <= NBRPINS; pin++) {
        if (PinDef[pin].mode & UNUSED)
            continue;
        hal_pin_deinit((uint32_t)PinDef[pin].GPno);
        ExtCurrentConfig[pin] = EXT_NOT_CONFIG;
    }
    last_adc = 0;
}
