#ifndef DRIVERS_LPT_CENTRONICS_H
#define DRIVERS_LPT_CENTRONICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LPT_CENTRONICS_DEFAULT_BASE 0x378u

typedef enum {
    LPT_PIN_MODE_OFF = 0,
    LPT_PIN_MODE_INPUT,
    LPT_PIN_MODE_OUTPUT,
} lpt_pin_mode_t;

void lpt_centronics_init(uint16_t base);
uint16_t lpt_centronics_base(void);

bool lpt_centronics_pin_valid(uint32_t pin);
bool lpt_centronics_pin_can_input(uint32_t pin);
bool lpt_centronics_pin_can_output(uint32_t pin);
bool lpt_centronics_pin_set_mode(uint32_t pin, lpt_pin_mode_t mode);
lpt_pin_mode_t lpt_centronics_pin_mode(uint32_t pin);
bool lpt_centronics_pin_read(uint32_t pin);
bool lpt_centronics_pin_read_latch(uint32_t pin);
bool lpt_centronics_pin_write(uint32_t pin, bool high);
bool lpt_centronics_pin_toggle(uint32_t pin);

bool lpt_centronics_write_byte(uint8_t byte);
size_t lpt_centronics_write(const void * buf, size_t len);
void lpt_centronics_flush(void);

#ifdef __cplusplus
}
#endif

#endif
