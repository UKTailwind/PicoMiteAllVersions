/* Stub for host build */
#ifndef _HARDWARE_ADC_H
#define _HARDWARE_ADC_H
static inline void adc_init(void) {}
static inline void adc_gpio_init(int pin) { (void)pin; }
static inline void adc_select_input(int input) { (void)input; }
static inline int adc_read(void) { return 0; }
#endif
