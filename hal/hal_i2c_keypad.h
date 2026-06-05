/*
 * hal/hal_i2c_keypad.h — I²C-attached keypad controller (PicoCalc
 * profile). Real impls live in drivers/i2c_picocalc_kbd/ and link
 * only on ports with HAL_PORT_HAS_I2C_KEYPAD=1; other ports link the
 * stub.
 *
 * The keypad MCU manages keyboard input, LCD backlight, and battery
 * state. It shares the system I²C bus, requires a slower clock
 * (10 kHz), and skews the boot init sequence (display init has to
 * wait for the keypad MCU to come up).
 */

#ifndef HAL_I2C_KEYPAD_H
#define HAL_I2C_KEYPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot-time delay after the keypad MCU comes online. Real waits
 * 300 ms; stub no-op. */
void hal_i2c_keypad_boot_init(void);

/* Whether the keypad MCU is sharing the system I²C bus on this port.
 * When 1, the boot path skips InitDisplaySSD / InitDisplayI2C /
 * InitDisplayVirtual (the keypad MCU is busy on those addresses).
 * When 0, those init helpers run normally. */
int hal_i2c_keypad_owns_i2c_bus(void);

/* Translate a 16-bit I²C-keyboard scan word into a cooked character.
 * Updates *ctrlheld_inout for ctrl-down/ctrl-up sentinels. Returns
 * -1 to skip (modifier / state change / non-press); >=0 is the
 * character to enqueue. Real impl is the PicoCalc scancode map;
 * stub is the legacy generic-I²C-keyboard map (0x1203/0x1202
 * sentinels, ESC/F1/F2/F4 only). */
int hal_i2c_keypad_translate(uint16_t buff, int * ctrlheld_inout);

/* OPTION LIST line for the keypad-controlled keyboard backlight.
 * Real prints `Option.KEYBOARDBL` if non-zero; stub no-op. */
void hal_i2c_keypad_print_options(void);

/* SPI480 panel resolution apply. Real (PicoCalc) fixes 320x480 in
 * portrait. Stub picks DisplayHRes/VRes via Option.DISPLAY_ORIENTATION. */
void hal_i2c_keypad_apply_spi480_resolution(void);

/* Boot-time keypad-matrix pin reservation (rp2350 only). Real
 * (PicoCalc) reserves the keypad backlight + matrix-row pins via
 * ExtCfg(...) when Option.LOCAL_KEYBOARD is set; stub no-op. */
void hal_i2c_keypad_reserve_io(void);

/* 1 kHz periodic keypad scan tick. Real (PicoCalc) checks
 * Option.LOCAL_KEYBOARD + LOCALKEYSCANRATE rate-limit and dispatches
 * cmd_keyscan(). Stub no-op. Called from the timer_callback path in
 * PicoMite.c with mSecTimer as input. */
void hal_i2c_keypad_periodic_scan(uint64_t mSecTimer);

/* Backlight level write. Real (PicoCalc) writes the keypad-controller
 * I²C register (0x1f reg 0x05); stub returns 0 to signal "fall through
 * to the PWM/SSD1963 backlight paths in External.c". */
int hal_i2c_keypad_set_backlight(int level);

/* OPTION BACKLIGHT pre-flight validation. PicoCalc accepts any
 * DISPLAY_TYPE because the keypad MCU drives backlight regardless;
 * other ports require an SPI-LCD-class panel. Real impl on PicoCalc
 * is a no-op; stub validates Option.DISPLAY_TYPE / Option.DISPLAY_BL
 * and errors when neither path can drive backlight. */
void hal_i2c_keypad_validate_backlight_supported(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_KEYPAD_H */
