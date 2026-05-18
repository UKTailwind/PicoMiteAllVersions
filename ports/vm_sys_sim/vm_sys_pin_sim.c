/*
 * ports/vm_sys_sim/vm_sys_pin_sim.c — simulator impl of the VM's pin syscalls.
 *
 * Simulates SETPIN / PIN-read / PIN-write / PWM / SERVO with plain C
 * arrays — no hardware access. vm_sys_pin_reset wipes state on VM
 * runtime reset. Paired with runtime/vm/vm_sys_pin.c (device-side impl); the
 * build links exactly one implementation body per target.
 *
 * Shared helpers (vm_pin_mode_is_pwm, vm_pin_pwm_mode_to_slice_chan,
 * vm_pin_pwm_mode_for_auto, vm_pwm_max_slice) are `static inline` in
 * vm_sys_pin_internal.h so both TUs compile their own copies.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "vm_sys_pin_internal.h"

static int vm_pwm_pin_a[VM_PWM_SLICE_COUNT];
static int vm_pwm_pin_b[VM_PWM_SLICE_COUNT];
static unsigned char vm_pwm_started[VM_PWM_SLICE_COUNT];

static int host_pin_mode[NBRPINS + 1];
static int host_pin_value[NBRPINS + 1];
static int host_pin_option[NBRPINS + 1];
static MMFLOAT host_pwm_frequency[VM_PWM_SLICE_COUNT];
static MMFLOAT host_pwm_duty_a[VM_PWM_SLICE_COUNT];
static MMFLOAT host_pwm_duty_b[VM_PWM_SLICE_COUNT];
static unsigned char host_pwm_enabled[VM_PWM_SLICE_COUNT];
static unsigned char host_pwm_phase_correct[VM_PWM_SLICE_COUNT];

static int vm_pin_resolve_host(int64_t pin) {
    if (pin < 0)
        pin = -pin;
    if (pin < 1 || pin > NBRPINS || (PinDef[pin].mode & UNUSED))
        error("Invalid pin");
    return (int)pin;
}

static void vm_host_pwm_detach_pin(int pin) {
    for (int slice = 0; slice < VM_PWM_SLICE_COUNT; slice++) {
        if (vm_pwm_pin_a[slice] == pin) vm_pwm_pin_a[slice] = 0;
        if (vm_pwm_pin_b[slice] == pin) vm_pwm_pin_b[slice] = 0;
    }
}

void vm_sys_pin_setpin(int64_t pin, int mode, int option) {
    int resolved = vm_pin_resolve_host(pin);
    int slice, chan;

    if (mode == VM_PIN_MODE_PWM_AUTO)
        mode = VM_PIN_MODE_PWM0A;

    if (mode != VM_PIN_MODE_OFF &&
        mode != VM_PIN_MODE_DIN &&
        mode != VM_PIN_MODE_DOUT &&
        mode != VM_PIN_MODE_ARAW &&
        mode != VM_PIN_MODE_PWM0A && mode != VM_PIN_MODE_PWM0B &&
        mode != VM_PIN_MODE_PWM1A && mode != VM_PIN_MODE_PWM1B &&
        mode != VM_PIN_MODE_PWM2A && mode != VM_PIN_MODE_PWM2B &&
        mode != VM_PIN_MODE_PWM3A && mode != VM_PIN_MODE_PWM3B &&
        mode != VM_PIN_MODE_PWM4A && mode != VM_PIN_MODE_PWM4B &&
        mode != VM_PIN_MODE_PWM5A && mode != VM_PIN_MODE_PWM5B &&
        mode != VM_PIN_MODE_PWM6A && mode != VM_PIN_MODE_PWM6B &&
        mode != VM_PIN_MODE_PWM7A && mode != VM_PIN_MODE_PWM7B &&
        mode != VM_PIN_MODE_PWM8A && mode != VM_PIN_MODE_PWM8B &&
        mode != VM_PIN_MODE_PWM9A && mode != VM_PIN_MODE_PWM9B &&
        mode != VM_PIN_MODE_PWM10A && mode != VM_PIN_MODE_PWM10B &&
        mode != VM_PIN_MODE_PWM11A && mode != VM_PIN_MODE_PWM11B
    )
        error("Unsupported SETPIN mode");

    if (mode == VM_PIN_MODE_OFF) {
        vm_host_pwm_detach_pin(resolved);
        host_pin_mode[resolved] = VM_PIN_MODE_OFF;
        host_pin_value[resolved] = 0;
        host_pin_option[resolved] = VM_PIN_OPT_NONE;
        return;
    }

    if (mode == VM_PIN_MODE_DIN) {
        if (option != VM_PIN_OPT_NONE &&
            option != VM_PIN_OPT_PULLUP &&
            option != VM_PIN_OPT_PULLDOWN)
            error("Unsupported SETPIN option");
        vm_host_pwm_detach_pin(resolved);
        host_pin_option[resolved] = option;
        host_pin_value[resolved] = (option == VM_PIN_OPT_PULLUP) ? 1 : 0;
        host_pin_mode[resolved] = mode;
        return;
    }

    if (mode == VM_PIN_MODE_DOUT || mode == VM_PIN_MODE_ARAW) {
        if (option != VM_PIN_OPT_NONE)
            error("Unsupported SETPIN option");
        vm_host_pwm_detach_pin(resolved);
        host_pin_option[resolved] = VM_PIN_OPT_NONE;
        if (mode == VM_PIN_MODE_ARAW) host_pin_value[resolved] = 0;
        host_pin_mode[resolved] = mode;
        return;
    }

    if (option != VM_PIN_OPT_NONE)
        error("Unsupported SETPIN option");
    vm_pin_pwm_mode_to_slice_chan(mode, &slice, &chan);
    if (chan == 0) {
        if (vm_pwm_pin_a[slice] && vm_pwm_pin_a[slice] != resolved)
            error("Already Set to pin %", vm_pwm_pin_a[slice]);
        vm_pwm_pin_a[slice] = resolved;
    } else {
        if (vm_pwm_pin_b[slice] && vm_pwm_pin_b[slice] != resolved)
            error("Already Set to pin %", vm_pwm_pin_b[slice]);
        vm_pwm_pin_b[slice] = resolved;
    }
    host_pin_mode[resolved] = mode;
    host_pin_option[resolved] = VM_PIN_OPT_NONE;
}

int64_t vm_sys_pin_read(int64_t pin) {
    int resolved = vm_pin_resolve_host(pin);
    switch (host_pin_mode[resolved]) {
        case VM_PIN_MODE_DIN:
        case VM_PIN_MODE_DOUT:
        case VM_PIN_MODE_ARAW:
            return host_pin_value[resolved];
        default:
            error("Pin not configured");
            return 0;
    }
}

void vm_sys_pin_write(int64_t pin, int64_t value) {
    int resolved = vm_pin_resolve_host(pin);
    (void)value;
    if (host_pin_mode[resolved] == VM_PIN_MODE_OFF) {
        host_pin_mode[resolved] = VM_PIN_MODE_DOUT;
    } else if (host_pin_mode[resolved] != VM_PIN_MODE_DOUT) {
        error("Pin is not an output");
    }
    host_pin_value[resolved] = 0;
}

void vm_sys_pwm_configure(int slice, MMFLOAT frequency,
                          int has_duty1, MMFLOAT duty1,
                          int has_duty2, MMFLOAT duty2,
                          int phase_correct, int delaystart) {
    if (slice < 0 || slice > vm_pwm_max_slice())
        error("Number out of bounds");
    if (frequency <= 0) error("Invalid frequency");
    if (has_duty1 && (duty1 < -100.0 || duty1 > 100.0)) error("Syntax");
    if (has_duty2 && (duty2 < -100.0 || duty2 > 100.0)) error("Syntax");
    if (has_duty1 && vm_pwm_pin_a[slice] == 0) error("Pin not set for PWM");
    if (has_duty2 && vm_pwm_pin_b[slice] == 0) error("Pin not set for PWM");
    host_pwm_frequency[slice] = frequency;
    host_pwm_duty_a[slice] = has_duty1 ? duty1 : -1.0;
    host_pwm_duty_b[slice] = has_duty2 ? duty2 : -1.0;
    host_pwm_phase_correct[slice] = phase_correct ? 1 : 0;
    vm_pwm_started[slice] = 1;
    if (!delaystart) host_pwm_enabled[slice] = 1;
}

void vm_sys_pwm_sync(uint16_t present_mask, const MMFLOAT *counts) {
    for (int slice = 0; slice < VM_PWM_SLICE_COUNT; slice++) {
        if (!(present_mask & (1u << slice)))
            continue;
        if (counts[slice] != -1.0 && (counts[slice] < 0.0 || counts[slice] > 100.0))
            error("Syntax");
        if (vm_pwm_started[slice])
            host_pwm_enabled[slice] = 1;
    }
}

void vm_sys_pwm_off(int slice) {
    if (slice < 0 || slice > vm_pwm_max_slice())
        error("Number out of bounds");
    host_pwm_frequency[slice] = 0;
    host_pwm_duty_a[slice] = -1.0;
    host_pwm_duty_b[slice] = -1.0;
    host_pwm_phase_correct[slice] = 0;
    host_pwm_enabled[slice] = 0;
    vm_pwm_started[slice] = 0;
}

void vm_sys_servo_configure(int slice,
                            int has_pos1, MMFLOAT pos1,
                            int has_pos2, MMFLOAT pos2) {
    MMFLOAT duty1 = 0, duty2 = 0;
    if (has_pos1) {
        if (pos1 < -20.0 || pos1 > 120.0) error("Syntax");
        duty1 = 5.0 + pos1 * 0.05;
    }
    if (has_pos2) {
        if (pos2 < -20.0 || pos2 > 120.0) error("Syntax");
        duty2 = 5.0 + pos2 * 0.05;
    }
    vm_sys_pwm_configure(slice, 50.0, has_pos1, duty1, has_pos2, duty2, 0, 0);
}

void vm_sys_pin_reset(void) {
    memset(host_pin_mode, 0, sizeof(host_pin_mode));
    memset(host_pin_value, 0, sizeof(host_pin_value));
    memset(host_pin_option, 0, sizeof(host_pin_option));
    memset(vm_pwm_pin_a, 0, sizeof(vm_pwm_pin_a));
    memset(vm_pwm_pin_b, 0, sizeof(vm_pwm_pin_b));
    memset(vm_pwm_started, 0, sizeof(vm_pwm_started));
    memset(host_pwm_enabled, 0, sizeof(host_pwm_enabled));
    memset(host_pwm_phase_correct, 0, sizeof(host_pwm_phase_correct));
    for (int i = 0; i < VM_PWM_SLICE_COUNT; i++) {
        host_pwm_frequency[i] = 0;
        host_pwm_duty_a[i] = -1.0;
        host_pwm_duty_b[i] = -1.0;
    }
}
