/* Stub for host build */
#ifndef _HARDWARE_PWM_H
#define _HARDWARE_PWM_H
#include <stdint.h>
#define PWM_CHAN_A 0
#define PWM_CHAN_B 1
static inline uint32_t pwm_gpio_to_slice_num(uint32_t pin) { (void)pin; return 0; }
static inline uint32_t pwm_gpio_to_channel(uint32_t pin) { (void)pin; return 0; }
static inline void pwm_set_wrap(uint32_t s, uint32_t w) { (void)s; (void)w; }
static inline void pwm_set_chan_level(uint32_t s, uint32_t ch, uint32_t l) { (void)s; (void)ch; (void)l; }
static inline void pwm_set_enabled(uint32_t s, int en) { (void)s; (void)en; }
static inline void pwm_set_clkdiv(uint32_t s, float d) { (void)s; (void)d; }
#endif
