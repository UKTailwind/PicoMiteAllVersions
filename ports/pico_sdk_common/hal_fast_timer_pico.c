/*
 * ports/pico_sdk_common/hal_fast_timer_pico.c — fast-timer HAL impl.
 *
 * RP2350 has a second PWM wrap IRQ (PWM_IRQ_WRAP_1) that lets slice 0
 * be repurposed as a pulse-count/period-measurement timer while the rest
 * of the PWM block services normal slices. RP2040 has only one wrap IRQ
 * and can't spare it; the feature is reported as unavailable there and
 * the configure call becomes a no-op.
 *
 * Target macros live in this file, not in hal/hal_fast_timer.h.
 */

#include <stdint.h>
#include <stdbool.h>

#include "hardware/pwm.h"
#include "hardware/irq.h"

#include "hal/hal_fast_timer.h"

bool hal_fast_timer_available(void) {
#ifdef rp2350
    return true;
#else
    return false;
#endif
}

bool hal_fast_timer_configure(uint32_t wrap_count, void (*isr_fn)(void)) {
#ifdef rp2350
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv(&cfg, 1);
    pwm_init(0, &cfg, false);
    pwm_set_wrap(0, wrap_count);
    pwm_clear_irq(0);
    if (isr_fn) {
        irq_set_exclusive_handler(PWM_IRQ_WRAP_1, isr_fn);
        irq_set_enabled(PWM_IRQ_WRAP_1, true);
        irq_set_priority(PWM_IRQ_WRAP_1, 0);
        pwm_set_irq1_enabled(0, true);
    }
    pwm_set_enabled(0, true);
    return true;
#else
    (void)wrap_count;
    (void)isr_fn;
    return false;
#endif
}

void hal_fast_timer_disable(void) {
#ifdef rp2350
    pwm_set_irq1_enabled(0, false);
#endif
}
