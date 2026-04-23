/*
 * VM syscall conversion rule:
 * - copy/adapt legacy implementation code as closely as possible
 * - copy/adapt dependent legacy helpers too when needed
 * - do not invent new algorithms when legacy code already exists
 * - do not call, wrap, or dispatch back into legacy handlers
 * Any deviation from legacy implementation shape must be explicit and justified.
 */

#include "vm_sys_pin.h"
#include "vm_device_support.h"

#define VM_PWM_SLICE_COUNT 12

static int vm_pin_mode_is_pwm(int mode) {
    /* PWM8A..PWM11B enum values are unconditional (bc_source accepts the
     * keywords on every port); they only map to real hardware slices on
     * rp2350. The range check below returns true for them on rp2040 too,
     * but vm_sys_pin_setpin errors at configure time against
     * vm_pwm_max_slice(). */
    return (mode >= VM_PIN_MODE_PWM0A && mode <= VM_PIN_MODE_PWM11B);
}

static int vm_pin_pwm_mode_to_slice_chan(int mode, int *slice, int *chan) {
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

static int vm_pin_pwm_mode_for_auto(int pin) {
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
    /* PWM8A..PWM11B bits are defined unconditionally (configuration.h)
     * but only set in PinDef[] on rp2350 — on rp2040 these checks fall
     * through. */
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

static int vm_pwm_max_slice(void) {
    /* rp2350a is unconditionally true on rp2040 (stubbed in PicoMite.c)
     * so this returns 7 there — matching the rp2040 PWM slice count.
     * On rp2350 non-WEB, runtime-detected: A package has 8 slices
     * (max index 7), B has 12 slices (max index 11). */
    extern bool rp2350a;
    return rp2350a ? 7 : 11;
}

static int vm_pwm_pin_a[VM_PWM_SLICE_COUNT];
static int vm_pwm_pin_b[VM_PWM_SLICE_COUNT];
static unsigned char vm_pwm_started[VM_PWM_SLICE_COUNT];

#ifdef MMBASIC_HOST

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

#else

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

enum {
    VM_PIN_EXT_NOT_CONFIG = 0,
    VM_PIN_EXT_DIG_IN = 2,
    VM_PIN_EXT_DIG_OUT = 8,
    VM_PIN_EXT_ADC_RAW = 46
};

/* vm_pin_gpio_map is the superset — 48 entries so rp2350b (QFN-80,
 * 48 GPIOs) is covered. rp2040 and rp2350a both bound-check at
 * runtime via `max_gpio_index` in vm_pin_codemap and never reach past
 * index 29. */
static const uint8_t vm_pin_gpio_map[48] = {
    1, 2, 4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17, 19, 20,
    21, 22, 24, 25, 26, 27, 29, 41, 42, 43, 31, 32, 34, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62
};

extern volatile int ExtCurrentConfig[NBRPINS + 1];
extern uint32_t pinmask;
extern int last_adc;
extern int BacklightSlice;
extern int CameraSlice;
/* rp2350a is unconditionally true on rp2040 (stubbed in PicoMite.c),
 * so portable code can branch on the package-variant flag without a
 * target gate. fast_timer_active is defined everywhere via
 * port-stubs; KeyboardlightSlice is PICOMITE-specific but declared
 * here unconditionally because vm_sys_pin's PWM detach path touches
 * it only after a PinDef[].mode check that's always false elsewhere. */
extern bool fast_timer_active;
extern bool rp2350a;
extern int KeyboardlightSlice;

static int vm_pin_codemap(int64_t gpio_index) {
    /* rp2350a = 30-pin package (QFN-60, max GPIO index 29),
     * rp2350b = 48-pin package (QFN-80, max GPIO index 47).
     * rp2040 = QFN-56 with 30 GPIOs (also index 29); rp2350a stub is
     * true on rp2040 so the same ternary works there. */
    int max_gpio_index = rp2350a ? 29 : 47;
    if (gpio_index < 0 || gpio_index > max_gpio_index)
        error("Invalid GPIO");
    return vm_pin_gpio_map[(int)gpio_index];
}

static int vm_pin_resolve(int64_t encoded_pin) {
    int pin = 0;
    if (encoded_pin < 0)
        pin = vm_pin_codemap(-encoded_pin - 1);
    else if (encoded_pin <= INT32_MAX)
        pin = (int)encoded_pin;
    else
        error("Invalid pin");

    /* Pin range: rp2350b has 62 GPIOs (NBRPINS=62 there); rp2040 +
     * rp2350a cap at 44. rp2040's NBRPINS (44) is the same number the
     * rp2350a arm uses, so the condition below collapses to one branch
     * covering every target. */
    if (pin < 1 || pin > (rp2350a ? 44 : NBRPINS))
        error("Invalid pin");
    if (PinDef[pin].mode & UNUSED)
        error("Invalid pin");
    return pin;
}

static void vm_pin_prepare_sio(int pin) {
    gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_SIO);
    gpio_set_input_hysteresis_enabled(PinDef[pin].GPno, true);
}

static void vm_pin_set_low(int pin) {
    gpio_set_pulls(PinDef[pin].GPno, false, false);
    gpio_pull_down(PinDef[pin].GPno);
    gpio_put(PinDef[pin].GPno, GPIO_PIN_RESET);
}

static void vm_pin_set_high(int pin) {
    gpio_set_pulls(PinDef[pin].GPno, false, false);
    gpio_pull_up(PinDef[pin].GPno);
    gpio_put(PinDef[pin].GPno, GPIO_PIN_SET);
}

static void vm_pin_set_input(int pin) {
    gpio_set_dir(PinDef[pin].GPno, GPIO_IN);
    gpio_set_input_enabled(PinDef[pin].GPno, true);
    uSec(2);
}

static void vm_pin_set_output(int pin) {
    gpio_set_dir(PinDef[pin].GPno, GPIO_OUT);
    gpio_set_input_enabled(PinDef[pin].GPno, false);
    gpio_set_drive_strength(PinDef[pin].GPno, GPIO_DRIVE_STRENGTH_8MA);
    uSec(2);
}

static uint32_t vm_pin_mask_bit(int pin) {
    int gp = PinDef[pin].GPno;
    return (gp >= 0 && gp < 32) ? (1u << gp) : 0;
}

static int vm_pin_is_safe_config(int cfg) {
    return cfg == VM_PIN_EXT_NOT_CONFIG ||
           cfg == VM_PIN_EXT_DIG_IN ||
           cfg == VM_PIN_EXT_DIG_OUT ||
           cfg == VM_PIN_EXT_ADC_RAW ||
           vm_pin_mode_is_pwm(cfg);
}

static void vm_pin_clear_pwm_assignment(int pin) {
    for (int slice = 0; slice < VM_PWM_SLICE_COUNT; slice++) {
        if (vm_pwm_pin_a[slice] == pin) vm_pwm_pin_a[slice] = 0;
        if (vm_pwm_pin_b[slice] == pin) vm_pwm_pin_b[slice] = 0;
    }
}

static int vm_pin_pwm_mode_valid_for_pin(int pin, int mode) {
    switch (mode) {
        case VM_PIN_MODE_PWM0A: return (PinDef[pin].mode & PWM0A) != 0;
        case VM_PIN_MODE_PWM0B: return (PinDef[pin].mode & PWM0B) != 0;
        case VM_PIN_MODE_PWM1A: return (PinDef[pin].mode & PWM1A) != 0;
        case VM_PIN_MODE_PWM1B: return (PinDef[pin].mode & PWM1B) != 0;
        case VM_PIN_MODE_PWM2A: return (PinDef[pin].mode & PWM2A) != 0;
        case VM_PIN_MODE_PWM2B: return (PinDef[pin].mode & PWM2B) != 0;
        case VM_PIN_MODE_PWM3A: return (PinDef[pin].mode & PWM3A) != 0;
        case VM_PIN_MODE_PWM3B: return (PinDef[pin].mode & PWM3B) != 0;
        case VM_PIN_MODE_PWM4A: return (PinDef[pin].mode & PWM4A) != 0;
        case VM_PIN_MODE_PWM4B: return (PinDef[pin].mode & PWM4B) != 0;
        case VM_PIN_MODE_PWM5A: return (PinDef[pin].mode & PWM5A) != 0;
        case VM_PIN_MODE_PWM5B: return (PinDef[pin].mode & PWM5B) != 0;
        case VM_PIN_MODE_PWM6A: return (PinDef[pin].mode & PWM6A) != 0;
        case VM_PIN_MODE_PWM6B: return (PinDef[pin].mode & PWM6B) != 0;
        case VM_PIN_MODE_PWM7A: return (PinDef[pin].mode & PWM7A) != 0;
        case VM_PIN_MODE_PWM7B: return (PinDef[pin].mode & PWM7B) != 0;
        /* PWM8..11 mask bits are unconditional in configuration.h;
         * on rp2040 no PinDef entry ever sets them so these return 0. */
        case VM_PIN_MODE_PWM8A: return (PinDef[pin].mode & PWM8A) != 0;
        case VM_PIN_MODE_PWM8B: return (PinDef[pin].mode & PWM8B) != 0;
        case VM_PIN_MODE_PWM9A: return (PinDef[pin].mode & PWM9A) != 0;
        case VM_PIN_MODE_PWM9B: return (PinDef[pin].mode & PWM9B) != 0;
        case VM_PIN_MODE_PWM10A: return (PinDef[pin].mode & PWM10A) != 0;
        case VM_PIN_MODE_PWM10B: return (PinDef[pin].mode & PWM10B) != 0;
        case VM_PIN_MODE_PWM11A: return (PinDef[pin].mode & PWM11A) != 0;
        case VM_PIN_MODE_PWM11B: return (PinDef[pin].mode & PWM11B) != 0;
        default: return 0;
    }
}

static void vm_pwm_apply(int slice, MMFLOAT duty1, MMFLOAT duty2,
                         int high1, int high2, int delaystart) {
    if (duty1 >= 0.0) {
        if (vm_pwm_pin_a[slice] == 0) error("Pin not set for PWM");
        ExtCurrentConfig[vm_pwm_pin_a[slice]] = EXT_COM_RESERVED;
        gpio_set_function(PinDef[vm_pwm_pin_a[slice]].GPno, GPIO_FUNC_PWM);
        pwm_set_chan_level(slice, PWM_CHAN_A, high1);
    }
    if (duty2 >= 0.0) {
        if (vm_pwm_pin_b[slice] == 0) error("Pin not set for PWM");
        ExtCurrentConfig[vm_pwm_pin_b[slice]] = EXT_COM_RESERVED;
        gpio_set_function(PinDef[vm_pwm_pin_b[slice]].GPno, GPIO_FUNC_PWM);
        pwm_set_chan_level(slice, PWM_CHAN_B, high2);
    }
    if (!delaystart) pwm_set_enabled(slice, true);
    vm_pwm_started[slice] = 1;
}

void vm_sys_pin_setpin(int64_t encoded_pin, int mode, int option) {
    int pin = vm_pin_resolve(encoded_pin);
    uint32_t bit = vm_pin_mask_bit(pin);
    int slice = -1, chan = -1;

    if (mode == VM_PIN_MODE_PWM_AUTO)
        mode = vm_pin_pwm_mode_for_auto(pin);

    if (mode == VM_PIN_MODE_OFF) {
        if (ExtCurrentConfig[pin] == EXT_COM_RESERVED)
            error("Pin in use");
        gpio_set_input_enabled(PinDef[pin].GPno, false);
        gpio_deinit(PinDef[pin].GPno);
        ExtCurrentConfig[pin] = VM_PIN_EXT_NOT_CONFIG;
        vm_pin_clear_pwm_assignment(pin);
        pinmask &= ~bit;
        return;
    }

    if (option != VM_PIN_OPT_NONE &&
        mode != VM_PIN_MODE_DIN)
        error("Unsupported SETPIN option");

    gpio_disable_pulls(PinDef[pin].GPno);
    if (!vm_pin_is_safe_config(ExtCurrentConfig[pin]))
        error("Pin in use");
    gpio_set_input_enabled(PinDef[pin].GPno, false);
    gpio_deinit(PinDef[pin].GPno);

    if (mode == VM_PIN_MODE_DIN) {
        if (!(PinDef[pin].mode & DIGITAL_IN))
            error("Invalid configuration");
        gpio_init(PinDef[pin].GPno);
        vm_pin_prepare_sio(pin);
        if (option == VM_PIN_OPT_PULLUP)
            gpio_pull_up(PinDef[pin].GPno);
        else if (option == VM_PIN_OPT_PULLDOWN)
            gpio_pull_down(PinDef[pin].GPno);
        else if (option != VM_PIN_OPT_NONE)
            error("Unsupported SETPIN option");
        vm_pin_clear_pwm_assignment(pin);
        vm_pin_set_input(pin);
        ExtCurrentConfig[pin] = VM_PIN_EXT_DIG_IN;
        pinmask &= ~bit;
        return;
    }

    if (mode == VM_PIN_MODE_ARAW) {
        if (!(PinDef[pin].mode & ANALOG_IN))
            error("Invalid configuration");
        /* rp2350 package check: rp2350a has ADC on pins > 44 only.
         * rp2350a is unconditionally true on rp2040, so on rp2040 the
         * first branch short-circuits false and the second is
         * unreachable (NBRPINS = 44). */
        if (pin <= 44 && rp2350a == 0) error("Invalid configuration");
        if (pin > 44 && rp2350a) error("Invalid configuration");
        gpio_init(PinDef[pin].GPno);
        gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_NULL);
        vm_pin_clear_pwm_assignment(pin);
        ExtCurrentConfig[pin] = VM_PIN_EXT_ADC_RAW;
        pinmask &= ~bit;
        return;
    }

    if (mode == VM_PIN_MODE_DOUT) {
        if (!(PinDef[pin].mode & DIGITAL_OUT))
            error("Invalid configuration");
        gpio_init(PinDef[pin].GPno);
        vm_pin_prepare_sio(pin);
        vm_pin_clear_pwm_assignment(pin);
        vm_pin_set_output(pin);
        if (bit && (pinmask & bit))
            gpio_put(PinDef[pin].GPno, GPIO_PIN_SET);
        ExtCurrentConfig[pin] = VM_PIN_EXT_DIG_OUT;
        pinmask &= ~bit;
        return;
    }

    if (!vm_pin_mode_is_pwm(mode) || !vm_pin_pwm_mode_valid_for_pin(pin, mode))
        error("Invalid configuration");
    vm_pin_pwm_mode_to_slice_chan(mode, &slice, &chan);
    if (slice > vm_pwm_max_slice())
        error("Invalid configuration");
    if (chan == 0) {
        if (vm_pwm_pin_a[slice] && vm_pwm_pin_a[slice] != pin)
            error("Already Set to pin %", vm_pwm_pin_a[slice]);
        vm_pwm_pin_a[slice] = pin;
    } else {
        if (vm_pwm_pin_b[slice] && vm_pwm_pin_b[slice] != pin)
            error("Already Set to pin %", vm_pwm_pin_b[slice]);
        vm_pwm_pin_b[slice] = pin;
    }
    gpio_init(PinDef[pin].GPno);
    gpio_set_function(PinDef[pin].GPno, GPIO_FUNC_PWM);
    ExtCurrentConfig[pin] = mode;
    pinmask &= ~bit;
}

int64_t vm_sys_pin_read(int64_t encoded_pin) {
    int pin = vm_pin_resolve(encoded_pin);
    if (ExtCurrentConfig[pin] == VM_PIN_EXT_DIG_OUT)
        return gpio_get_out_level(PinDef[pin].GPno);
    if (ExtCurrentConfig[pin] == VM_PIN_EXT_DIG_IN)
        return gpio_get(PinDef[pin].GPno);
    if (ExtCurrentConfig[pin] == VM_PIN_EXT_ADC_RAW) {
        if (ADCDualBuffering || dmarunning) error("ADC in use");
        adc_init();
        adc_select_input(PinDef[pin].ADCpin);
        last_adc = PinDef[pin].ADCpin;
        return adc_read();
    }
    error("Pin not configured");
    return 0;
}

void vm_sys_pin_write(int64_t encoded_pin, int64_t value) {
    int pin = vm_pin_resolve(encoded_pin);
    uint32_t bit = vm_pin_mask_bit(pin);

    if (ExtCurrentConfig[pin] == VM_PIN_EXT_NOT_CONFIG) {
        vm_pin_prepare_sio(pin);
        vm_pin_set_output(pin);
        pinmask |= bit;
        last_adc = 99;
    } else if (ExtCurrentConfig[pin] != VM_PIN_EXT_DIG_OUT) {
        error("Pin is not an output");
    }

    if (value)
        vm_pin_set_high(pin);
    else
        vm_pin_set_low(pin);
}

void vm_sys_pwm_configure(int slice, MMFLOAT frequency,
                          int has_duty1, MMFLOAT duty1,
                          int has_duty2, MMFLOAT duty2,
                          int phase_correct, int delaystart) {
    int div = 1;
    int high1 = 0, high2 = 0;
    int phase1 = 0, phase2 = 0;
    int cpu_speed = Option.CPU_Speed;
    int wrap;

    if (slice < 0 || slice > vm_pwm_max_slice())
        error("Number out of bounds");
    /* fast_timer_active stays false on rp2040 + host, so this check
     * is live only on rp2350 where the feature is driven from
     * ports/pico_sdk_common/hal_fast_timer_pico.c. */
    if (slice == 0 && fast_timer_active)
        error("Channel 0 in use for fast timer");
    if (slice == BacklightSlice)
        error("Channel in use for backlight");
    if (slice == Option.AUDIO_SLICE)
        error("Channel in use for Audio");
    if (slice == CameraSlice)
        error("Channel in use for Camera");
    /* KeyboardlightSlice starts at -1 on non-PicoCalc ports so the
     * check below is a no-op there. */
    if (slice == KeyboardlightSlice)
        error("Channel in use for keyboard backlight");

    if (frequency > (MMFLOAT)(cpu_speed >> 2) * 1000.0 || frequency <= 0.0)
        error("Invalid frequency");
    if (has_duty1 && (duty1 > 100.0 || duty1 < -100.0))
        error("Syntax");
    if (has_duty2 && (duty2 > 100.0 || duty2 < -100.0))
        error("Syntax");
    if (has_duty1 && duty1 < 0.0) {
        duty1 = -duty1;
        phase1 = 1;
    }
    if (has_duty2 && duty2 < 0.0) {
        duty2 = -duty2;
        phase2 = 1;
    }

    wrap = (cpu_speed * 1000) / frequency;
    if (has_duty1) high1 = (int)((MMFLOAT)cpu_speed / frequency * duty1 * 10.0);
    if (has_duty2) high2 = (int)((MMFLOAT)cpu_speed / frequency * duty2 * 10.0);
    while (wrap > 65535) {
        wrap >>= 1;
        if (has_duty1) high1 >>= 1;
        if (has_duty2) high2 >>= 1;
        div <<= 1;
    }
    if (div > 256)
        error("Invalid frequency");
    wrap--;
    if (high1) high1--;
    if (high2) high2--;
    pwm_set_clkdiv(slice, (float)div);
    pwm_set_wrap(slice, wrap);
    pwm_set_output_polarity(slice, phase1, phase2);
    pwm_set_phase_correct(slice, phase_correct ? true : false);
    vm_pwm_apply(slice, has_duty1 ? duty1 : -1.0, has_duty2 ? duty2 : -1.0,
                 high1, high2, delaystart);
}

void vm_sys_pwm_sync(uint16_t present_mask, const MMFLOAT *counts) {
    uint32_t enabled = pwm_hw->en;

    for (int slice = 0; slice <= vm_pwm_max_slice(); slice++) {
        if (!(present_mask & (1u << slice)))
            continue;
        if ((counts[slice] < 0.0 || counts[slice] > 100.0) && counts[slice] != -1.0)
            error("Syntax");
        if (!vm_pwm_started[slice])
            continue;
        pwm_set_enabled(slice, false);
        if (counts[slice] >= 0.0) {
            MMFLOAT count = (MMFLOAT)pwm_hw->slice[slice].top * (100.0 - counts[slice]) / 100.0;
            pwm_set_counter(slice, (int)count);
        }
        enabled |= (1u << slice);
    }
    pwm_hw->en = enabled;
}

void vm_sys_pwm_off(int slice) {
    if (slice < 0 || slice > vm_pwm_max_slice())
        error("Number out of bounds");
    if (vm_pwm_pin_a[slice]) {
        gpio_deinit(PinDef[vm_pwm_pin_a[slice]].GPno);
        ExtCurrentConfig[vm_pwm_pin_a[slice]] = VM_PIN_EXT_NOT_CONFIG;
    }
    if (vm_pwm_pin_b[slice]) {
        gpio_deinit(PinDef[vm_pwm_pin_b[slice]].GPno);
        ExtCurrentConfig[vm_pwm_pin_b[slice]] = VM_PIN_EXT_NOT_CONFIG;
    }
    pwm_set_enabled(slice, false);
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
    memset(vm_pwm_pin_a, 0, sizeof(vm_pwm_pin_a));
    memset(vm_pwm_pin_b, 0, sizeof(vm_pwm_pin_b));
    memset(vm_pwm_started, 0, sizeof(vm_pwm_started));
}

#endif
