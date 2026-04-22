/*
 * hal/hal_pin.h — GPIO / PWM / ADC / edge-IRQ HAL.
 *
 * Pin numbers are the target's raw GPIO numbers (e.g. `PinDef[pin].GPno`
 * on RP2040/RP2350). Translating from MMBasic's 1-based physical-pin
 * numbering to the GPIO number remains a caller responsibility — that
 * mapping lives in the per-board `PinDef[]` table and is not a HAL
 * concern.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md).
 */

#ifndef HAL_PIN_H
#define HAL_PIN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Pin modes
 *
 * Passed to hal_pin_set_mode to configure direction / pull / function.
 * Values are bitwise-OR-able for pull + direction but most callers set
 * one mode enum directly.
 * ---------------------------------------------------------------------- */

typedef enum {
    HAL_PIN_MODE_DISABLED = 0,  /* no pulls, no drive */
    HAL_PIN_MODE_INPUT,         /* digital input, no pull */
    HAL_PIN_MODE_INPUT_PULLUP,
    HAL_PIN_MODE_INPUT_PULLDOWN,
    HAL_PIN_MODE_OUTPUT,        /* push-pull driver */
    HAL_PIN_MODE_OPEN_DRAIN,
    HAL_PIN_MODE_ANALOG,        /* ADC channel */
    HAL_PIN_MODE_PWM,           /* PWM output — use hal_pin_pwm_* to configure */
} hal_pin_mode_t;

void hal_pin_set_mode(uint32_t gpio, hal_pin_mode_t mode);

/* -----------------------------------------------------------------------
 * Digital I/O
 *
 * Read / write semantics mirror pico/gpio.h. `hal_pin_toggle` XORs the
 * output latch — useful for fast heartbeat / debug blinks.
 * ---------------------------------------------------------------------- */

bool hal_pin_read(uint32_t gpio);
void hal_pin_write(uint32_t gpio, bool high);
void hal_pin_toggle(uint32_t gpio);

/* Read the output latch value regardless of whether the pin is currently
 * configured as input or output. MMBasic's `fun_pin` returns this for
 * pins in output mode so the user sees what they last wrote, not whatever
 * the pad is being driven to externally. */
bool hal_pin_read_output_latch(uint32_t gpio);

/* Strong / weak drive strength for outputs. Default after
 * HAL_PIN_MODE_OUTPUT is implementation-defined (SDK default on RP2040
 * is 4 mA). Callers that care call this explicitly.
 *
 * `mA` is the target drive current; the impl rounds to the closest
 * supported value. */
void hal_pin_set_drive_mA(uint32_t gpio, uint8_t mA);

/* -----------------------------------------------------------------------
 * Low-level pad / pull / direction knobs.
 *
 * These exist so PinSetBit (External.c) — a legacy per-register-offset
 * switch that mirrors the PIC32 LATCLR/LATSET/TRISSET/ODCCLR/etc.
 * semantics of the original MMBasic — can be expressed without calling
 * pico SDK gpio_* primitives directly. Callers that set a "real" mode
 * should use hal_pin_set_mode; these knobs are for the legacy bit-twiddle
 * paths only.
 * ---------------------------------------------------------------------- */

typedef enum {
    HAL_PIN_PULL_NONE = 0,
    HAL_PIN_PULL_UP,
    HAL_PIN_PULL_DOWN,
} hal_pin_pull_t;

typedef enum {
    HAL_PIN_DIR_IN = 0,
    HAL_PIN_DIR_OUT,
} hal_pin_dir_t;

void hal_pin_set_pulls(uint32_t gpio, hal_pin_pull_t pull);
void hal_pin_set_dir(uint32_t gpio, hal_pin_dir_t dir);
void hal_pin_set_input_enabled(uint32_t gpio, bool enabled);

/* Route `gpio` to the SIO function and leave it as a plain digital pin
 * (no ADC, no peripheral MUX). Used when transitioning a pin away from
 * analog-in. */
void hal_pin_select_digital(uint32_t gpio);

/* Bind an ADC peripheral channel for subsequent single-shot reads. The
 * channel is *not* a GPIO number — it is the per-target ADC input index
 * (`PinDef[].ADCpin` on pico). */
void hal_pin_adc_select(uint32_t adc_channel);

/* -----------------------------------------------------------------------
 * Input pad tuning (hysteresis, slew rate).
 *
 * Both default to hysteresis-on, slow-slew on pico SDK. Callers flip them
 * only where the datasheet cares — e.g. open-collector counter inputs,
 * high-speed parallel-LCD data buses.
 * ---------------------------------------------------------------------- */

void hal_pin_set_input_hysteresis(uint32_t gpio, bool enabled);
void hal_pin_set_slew_fast(uint32_t gpio, bool fast);

/* -----------------------------------------------------------------------
 * Edge-trigger enable (no callback registration).
 *
 * Lets a caller arm or disarm a GPIO edge trigger without owning the
 * IRQ handler. The handler itself is installed elsewhere (e.g. via the
 * PicoMite shared-handler dispatch in picomite_gpio_irq.c on device).
 * A richer hal_pin_irq_attach(cb, ctx) lives in Phase 11.
 * ---------------------------------------------------------------------- */

typedef enum {
    HAL_PIN_EDGE_NONE = 0,
    HAL_PIN_EDGE_RISE = 1 << 0,
    HAL_PIN_EDGE_FALL = 1 << 1,
    HAL_PIN_EDGE_BOTH = HAL_PIN_EDGE_RISE | HAL_PIN_EDGE_FALL,
} hal_pin_edge_t;

void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled);

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 * Tier-B hot-path variants (hal_pin_*_fast).
 *
 * Provided by each port as a `static inline` header so functions placed
 * in SRAM with __not_in_flash_func can write/read/toggle a pin without a
 * cross-section call into flash. Every port directory on the compiler's
 * `-I` path supplies its own `hal_pin_inlines.h`.
 * ---------------------------------------------------------------------- */
#include "hal_pin_inlines.h"

#endif  /* HAL_PIN_H */
