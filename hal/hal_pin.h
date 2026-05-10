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

/* Pulldown-enable on RP2040 needs an explicit TRISCLR/LATCLR/TRISSET
 * dance to settle; RP2350 doesn't. The hook drives the dance on ports
 * where it's required and no-ops elsewhere. */
void hal_pin_pulldown_reset(int pin);

/* Route `gpio` to the SIO function and leave it as a plain digital pin
 * (no ADC, no peripheral MUX). Used when transitioning a pin away from
 * analog-in. */
void hal_pin_select_digital(uint32_t gpio);

/* Bind an ADC peripheral channel for subsequent single-shot reads. The
 * channel is *not* a GPIO number — it is the per-target ADC input index
 * (`PinDef[].ADCpin` on pico). */
void hal_pin_adc_select(uint32_t adc_channel);

/* Single-shot ADC subsystem:
 *   init            — one-time power-up; idempotent.
 *   set_temp_sensor — enable/disable the on-die temperature sensor;
 *                     after enabling, the caller selects the sensor's
 *                     channel via hal_pin_adc_select.
 *   read            — single 12-bit conversion on the currently-selected
 *                     channel; returns raw 0..4095.
 * DMA/round-robin/FIFO is a separate (later) hal_adc surface. */
void     hal_pin_adc_init(void);
void     hal_pin_adc_set_temp_sensor(bool enabled);
uint16_t hal_pin_adc_read(void);

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
 * Pico SDK shared-handler dispatch in pico_gpio_irq.c on device).
 * A richer hal_pin_irq_attach(cb, ctx) lives in Phase 11.
 * ---------------------------------------------------------------------- */

typedef enum {
    HAL_PIN_EDGE_NONE = 0,
    HAL_PIN_EDGE_RISE = 1 << 0,
    HAL_PIN_EDGE_FALL = 1 << 1,
    HAL_PIN_EDGE_BOTH = HAL_PIN_EDGE_RISE | HAL_PIN_EDGE_FALL,
} hal_pin_edge_t;

void hal_pin_irq_set_edge(uint32_t gpio, uint32_t edge_mask, bool enabled);

/* -----------------------------------------------------------------------
 * Pin init/deinit + function MUX.
 *
 * hal_pin_init_digital defaults the pad to SIO with output disabled; it
 * is the canonical replacement for pico SDK gpio_init() when the caller
 * is about to configure the pin for digital I/O itself. hal_pin_deinit
 * returns the pad to reset state.
 *
 * hal_pin_set_function routes a pin to a peripheral MUX. Enum values
 * map 1-for-1 to the pico SDK GPIO_FUNC_* selector space; the HAL does
 * not abstract which peripherals exist on which target — a caller that
 * asks for a MUX the target doesn't have gets the SDK's own error.
 * ---------------------------------------------------------------------- */

typedef enum {
    HAL_PIN_FUNC_NONE = 0,  /* GPIO_FUNC_NULL — disconnect pad */
    HAL_PIN_FUNC_SIO,       /* plain digital */
    HAL_PIN_FUNC_UART,
    HAL_PIN_FUNC_I2C,
    HAL_PIN_FUNC_SPI,
    HAL_PIN_FUNC_PWM,
    HAL_PIN_FUNC_PIO0,
    HAL_PIN_FUNC_PIO1,
    HAL_PIN_FUNC_PIO2,      /* rp2350 only */
} hal_pin_func_t;

void hal_pin_init_digital(uint32_t gpio);
void hal_pin_deinit(uint32_t gpio);
void hal_pin_set_function(uint32_t gpio, hal_pin_func_t func);

/* -----------------------------------------------------------------------
 * Wide (bank-0) GPIO operations.
 *
 * These exist for parallel-LCD bus bitbang and serial bit-bang loops
 * that need a single atomic write/read covering many pins at once.
 * Callers assemble the mask from `1ULL << gpio` for each pin they want
 * to touch. The HAL does not interpret the mask beyond delegating to
 * the SDK's bank-0 mask primitives.
 * ---------------------------------------------------------------------- */

uint64_t hal_pin_bank_read_all(void);
uint64_t hal_pin_bank_read_out_latch(void);
void     hal_pin_bank_set_mask(uint64_t mask);
void     hal_pin_bank_clr_mask(uint64_t mask);
void     hal_pin_bank_xor_mask(uint64_t mask);

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
