/*
 * vm_sys_pin_internal.h — shared helpers + PWM-slice storage used by
 * both the device body (vm_sys_pin.c) and the host body
 * (host/vm_sys_pin_host.c).
 *
 * Both TUs compile exactly one of the vm_sys_pin_* public entries
 * (vm_sys_pin_setpin, vm_sys_pin_pin, vm_sys_pin_pulse, …) via their
 * respective implementation bodies; the build system links only one
 * TU per target (device links vm_sys_pin.c; host links
 * host/vm_sys_pin_host.c). The helpers below are
 * `static inline` so each TU gets its own copy — no shared
 * definitions means no cross-file linkage issues.
 *
 * Per-TU storage (vm_pwm_pin_a/b, vm_pwm_started) is also declared
 * `static` in each TU — each implementation tracks its own PWM
 * assignments independently.
 */

#ifndef VM_SYS_PIN_INTERNAL_H
#define VM_SYS_PIN_INTERNAL_H

#include "vm_sys_pin.h"
#include "vm_device_support.h"

#define VM_PWM_SLICE_COUNT 12

static inline int vm_pin_mode_is_pwm(int mode) {
    return (mode >= VM_PIN_MODE_PWM0A && mode <= VM_PIN_MODE_PWM11B);
}

static inline int vm_pin_pwm_mode_to_slice_chan(int mode, int *slice, int *chan) {
    if (mode >= VM_PIN_MODE_PWM0A && mode <= VM_PIN_MODE_PWM7B) {
        int index = mode - VM_PIN_MODE_PWM0A;
        *slice = index / 2;
        *chan = index & 1;
        return 1;
    }
    if (mode >= VM_PIN_MODE_PWM8A && mode <= VM_PIN_MODE_PWM11B) {
        int index = mode - VM_PIN_MODE_PWM8A;
        *slice = 8 + index / 2;
        *chan = index & 1;
        return 1;
    }
    return 0;
}

static inline int vm_pin_pwm_mode_for_auto(int pin) {
    if (PinDef[pin].mode & PWM0A) return VM_PIN_MODE_PWM0A;
    if (PinDef[pin].mode & PWM0B) return VM_PIN_MODE_PWM0B;
    if (PinDef[pin].mode & PWM1A) return VM_PIN_MODE_PWM1A;
    if (PinDef[pin].mode & PWM1B) return VM_PIN_MODE_PWM1B;
    if (PinDef[pin].mode & PWM2A) return VM_PIN_MODE_PWM2A;
    if (PinDef[pin].mode & PWM2B) return VM_PIN_MODE_PWM2B;
    if (PinDef[pin].mode & PWM3A) return VM_PIN_MODE_PWM3A;
    if (PinDef[pin].mode & PWM3B) return VM_PIN_MODE_PWM3B;
    if (PinDef[pin].mode & PWM4A) return VM_PIN_MODE_PWM4A;
    if (PinDef[pin].mode & PWM4B) return VM_PIN_MODE_PWM4B;
    if (PinDef[pin].mode & PWM5A) return VM_PIN_MODE_PWM5A;
    if (PinDef[pin].mode & PWM5B) return VM_PIN_MODE_PWM5B;
    if (PinDef[pin].mode & PWM6A) return VM_PIN_MODE_PWM6A;
    if (PinDef[pin].mode & PWM6B) return VM_PIN_MODE_PWM6B;
    if (PinDef[pin].mode & PWM7A) return VM_PIN_MODE_PWM7A;
    if (PinDef[pin].mode & PWM7B) return VM_PIN_MODE_PWM7B;
    if (PinDef[pin].mode & PWM8A) return VM_PIN_MODE_PWM8A;
    if (PinDef[pin].mode & PWM8B) return VM_PIN_MODE_PWM8B;
    if (PinDef[pin].mode & PWM9A) return VM_PIN_MODE_PWM9A;
    if (PinDef[pin].mode & PWM9B) return VM_PIN_MODE_PWM9B;
    if (PinDef[pin].mode & PWM10A) return VM_PIN_MODE_PWM10A;
    if (PinDef[pin].mode & PWM10B) return VM_PIN_MODE_PWM10B;
    if (PinDef[pin].mode & PWM11A) return VM_PIN_MODE_PWM11A;
    if (PinDef[pin].mode & PWM11B) return VM_PIN_MODE_PWM11B;
    return 0;
}

static inline int vm_pwm_max_slice(void) {
    extern bool rp2350a;
    return rp2350a ? 7 : 11;
}

#endif  /* VM_SYS_PIN_INTERNAL_H */
