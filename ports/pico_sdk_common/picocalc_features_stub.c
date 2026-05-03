/*
 * ports/pico_sdk_common/picocalc_features_stub.c — fall-back impls
 * of port_picocalc_* hooks for ports without the PicoCalc keypad MCU.
 * Linked everywhere except the PicoCalc profile.
 *
 * Each hook errors with "Not supported on this board" so a BASIC
 * program that asks for keypad-backlight / battery / charging on a
 * non-PicoCalc port gets a clear message instead of silent zero.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_picocalc_set_keyboard_backlight(int level) { (void)level; error("Not supported on this board"); }
int  port_picocalc_battery_pct(void)                 { error("Not supported on this board"); return 0; }
int  port_picocalc_is_charging(void)                 { error("Not supported on this board"); return 0; }
void port_picocalc_factory_reset_options(void)       { error("Not supported on this board"); }
