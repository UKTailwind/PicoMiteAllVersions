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

#ifdef __cplusplus
}
#endif

#endif  /* HAL_PIN_H */
