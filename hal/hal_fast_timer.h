/*
 * hal/hal_fast_timer.h — optional high-resolution counter on PWM slice 0.
 *
 * The fast timer is an RP2350-only feature: PWM slice 0 is used as a
 * clock-divided counter, with wrap interrupts serviced by PWM_IRQ_WRAP_1.
 * The feature lets BASIC sample pulse counts and periods with ~1 MHz
 * resolution on pin GP1. RP2040 has a single PWM wrap IRQ and cannot
 * free up slice 0 alongside the PWM subsystem, so the feature is absent;
 * `hal_fast_timer_available()` reports false on RP2040 and
 * `hal_fast_timer_configure()` is a no-op returning false.
 *
 * The ISR body (increment INT5Count, clear the IRQ) lives in the core
 * External.c path and is wired in by `hal_fast_timer_configure()` on
 * RP2350. Core sees a single callable surface; preprocessor gating for
 * `PWM_IRQ_WRAP_1` / `pwm_set_irq1_enabled` stays on the impl side.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md).
 */

#ifndef HAL_FAST_TIMER_H
#define HAL_FAST_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns true if the port supports the fast-timer feature. Callers that
 * report "not supported" at the BASIC layer should test this before
 * configuring. The return value is a compile-time constant on every port;
 * the compiler folds the `if (hal_fast_timer_available())` branch away. */
bool hal_fast_timer_available(void);

/* Configure PWM slice 0 as a fast counter with the given wrap value and
 * install the wrap-IRQ handler. The handler increments the `INT5Count`
 * global and clears the IRQ. Returns true on success, false on ports that
 * don't support the feature (no-op on RP2040).
 *
 * `isr_fn` is the ISR body core wants wired up. On RP2350 this is stored
 * behind PWM_IRQ_WRAP_1; on RP2040 it's ignored.
 */
bool hal_fast_timer_configure(uint32_t wrap_count, void (*isr_fn)(void));

/* Tear down the fast-timer ISR and disable the slice-0 wrap interrupt.
 * Called from pin-reconfiguration paths when a pin leaves EXT_FAST_TIMER
 * mode. No-op on ports where `hal_fast_timer_available()` is false. */
void hal_fast_timer_disable(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FAST_TIMER_H */
