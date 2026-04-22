#ifndef I2C_KEYBOARD_H
#define I2C_KEYBOARD_H
#include <pico/stdlib.h>
#include <pico/platform.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>

#define I2C_KBD_ADDR 0x1F

void init_i2c_kbd();
int read_i2c_kbd();
int read_battery();
int set_kbd_backlight(uint8_t);

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitClear(value, bit) ((value) &= ~(1 << (bit)))
#define bitSet(value, bit) ((value) |= (1 << (bit)))

#endif
