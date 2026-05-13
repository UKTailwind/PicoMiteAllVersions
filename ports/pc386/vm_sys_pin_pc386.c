/*
 * ports/pc386/vm_sys_pin_pc386.c — VM pin syscalls over LPT1 GPIO.
 *
 * The bytecode VM calls this surface for SETPIN, PIN(), and PIN()=.
 * Keep it pointed at the same LPT-backed HAL as the interactive command
 * stubs so compiled BASIC and REPL BASIC see identical DB-25 behavior.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"
#include "vm_sys_pin.h"

static int pin_mode[NBRPINS + 1];

static int resolve_pin(int64_t pin) {
    if (pin < 0) pin = -pin;
    if (pin < 1 || pin > NBRPINS) error("Invalid pin");
    return (int)pin;
}

void vm_sys_pin_setpin(int64_t pin, int mode, int option) {
    int p = resolve_pin(pin);
    if (option != VM_PIN_OPT_NONE) error("Unsupported SETPIN option");

    switch (mode) {
        case VM_PIN_MODE_OFF:
            hal_pin_set_mode((uint32_t)p, HAL_PIN_MODE_DISABLED);
            pin_mode[p] = VM_PIN_MODE_OFF;
            ExtCurrentConfig[p] = EXT_NOT_CONFIG;
            break;
        case VM_PIN_MODE_DIN:
            hal_pin_set_mode((uint32_t)p, HAL_PIN_MODE_INPUT);
            pin_mode[p] = VM_PIN_MODE_DIN;
            ExtCurrentConfig[p] = EXT_DIG_IN;
            break;
        case VM_PIN_MODE_DOUT:
            hal_pin_set_mode((uint32_t)p, HAL_PIN_MODE_OUTPUT);
            pin_mode[p] = VM_PIN_MODE_DOUT;
            ExtCurrentConfig[p] = EXT_DIG_OUT;
            break;
        default:
            error("Unsupported SETPIN mode");
    }
}

int64_t vm_sys_pin_read(int64_t pin) {
    int p = resolve_pin(pin);
    if (pin_mode[p] == VM_PIN_MODE_DOUT) return hal_pin_read_output_latch((uint32_t)p) ? 1 : 0;
    if (pin_mode[p] == VM_PIN_MODE_DIN) return hal_pin_read((uint32_t)p) ? 1 : 0;
    error("Pin not configured");
    return 0;
}

void vm_sys_pin_write(int64_t pin, int64_t value) {
    int p = resolve_pin(pin);
    if (pin_mode[p] == VM_PIN_MODE_OFF) {
        vm_sys_pin_setpin(p, VM_PIN_MODE_DOUT, VM_PIN_OPT_NONE);
    } else if (pin_mode[p] != VM_PIN_MODE_DOUT) {
        error("Pin is not an output");
    }
    hal_pin_write((uint32_t)p, value != 0);
}

void vm_sys_pwm_configure(int slice, MMFLOAT frequency,
                          int has_duty1, MMFLOAT duty1,
                          int has_duty2, MMFLOAT duty2,
                          int phase_correct, int delaystart) {
    (void)slice; (void)frequency; (void)has_duty1; (void)duty1;
    (void)has_duty2; (void)duty2; (void)phase_correct; (void)delaystart;
    error("PWM not available on PC386 LPT1");
}

void vm_sys_pwm_sync(uint16_t present_mask, const MMFLOAT *counts) {
    (void)present_mask; (void)counts;
    error("PWM not available on PC386 LPT1");
}

void vm_sys_pwm_off(int slice) { (void)slice; }

void vm_sys_servo_configure(int slice,
                            int has_pos1, MMFLOAT pos1,
                            int has_pos2, MMFLOAT pos2) {
    (void)slice; (void)has_pos1; (void)pos1; (void)has_pos2; (void)pos2;
    error("Servo not available on PC386 LPT1");
}

void vm_sys_pin_reset(void) {
    for (int i = 0; i <= NBRPINS; i++) {
        pin_mode[i] = VM_PIN_MODE_OFF;
        ExtCurrentConfig[i] = EXT_NOT_CONFIG;
    }
}
