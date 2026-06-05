#ifndef VM_SYS_PIN_H
#define VM_SYS_PIN_H

#include <stdint.h>
#include "configuration.h"

#define VM_PIN_MODE_PWM_AUTO (-1)

enum {
    VM_PIN_MODE_OFF = 0,
    VM_PIN_MODE_DIN = 2,
    VM_PIN_MODE_DOUT = 8,
    VM_PIN_MODE_PWM0A = 30,
    VM_PIN_MODE_PWM0B = 31,
    VM_PIN_MODE_PWM1A = 32,
    VM_PIN_MODE_PWM1B = 33,
    VM_PIN_MODE_PWM2A = 34,
    VM_PIN_MODE_PWM2B = 35,
    VM_PIN_MODE_PWM3A = 36,
    VM_PIN_MODE_PWM3B = 37,
    VM_PIN_MODE_PWM4A = 38,
    VM_PIN_MODE_PWM4B = 39,
    VM_PIN_MODE_PWM5A = 40,
    VM_PIN_MODE_PWM5B = 41,
    VM_PIN_MODE_PWM6A = 42,
    VM_PIN_MODE_PWM6B = 43,
    VM_PIN_MODE_PWM7A = 44,
    VM_PIN_MODE_PWM7B = 45,
    VM_PIN_MODE_ARAW = 46,
    /* Extended PWM slice modes only drive hardware on rp2350 (slices
     * 8-11 exist only there); the enum values are unconditional so
     * bc_source's keyword parser can accept `PWM8A..PWM11B` on every
     * target. On rp2040 the VM's setpin routes these to an error. */
    VM_PIN_MODE_PWM8A = 47,
    VM_PIN_MODE_PWM8B = 48,
    VM_PIN_MODE_PWM9A = 49,
    VM_PIN_MODE_PWM9B = 50,
    VM_PIN_MODE_PWM10A = 51,
    VM_PIN_MODE_PWM10B = 52,
    VM_PIN_MODE_PWM11A = 53,
    VM_PIN_MODE_PWM11B = 54
};

enum {
    VM_PIN_OPT_NONE = 0,
    VM_PIN_OPT_PULLUP = -1,
    VM_PIN_OPT_PULLDOWN = -2
};

void vm_sys_pin_setpin(int64_t pin, int mode, int option);
int64_t vm_sys_pin_read(int64_t pin);
void vm_sys_pin_write(int64_t pin, int64_t value);
void vm_sys_pwm_configure(int slice, MMFLOAT frequency,
                          int has_duty1, MMFLOAT duty1,
                          int has_duty2, MMFLOAT duty2,
                          int phase_correct, int delaystart);
void vm_sys_pwm_sync(uint16_t present_mask, const MMFLOAT * counts);
void vm_sys_pwm_off(int slice);
void vm_sys_servo_configure(int slice,
                            int has_pos1, MMFLOAT pos1,
                            int has_pos2, MMFLOAT pos2);
void vm_sys_pin_reset(void);

#endif
