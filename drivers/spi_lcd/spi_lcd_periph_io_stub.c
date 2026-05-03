/*
 * drivers/spi_lcd/spi_lcd_periph_io_stub.c — no-op stub of
 * hal_periph_reserve_io for VGA / HDMI / DVI-WiFi ports. The real
 * impl (spi_lcd_periph_io.c) references symbols defined in Touch.c
 * and macros from SSD1963.h that are only available when the SPI-LCD
 * + touch driver pair is linked, which excludes the VGA-family
 * ports.
 */

#include "hal/hal_periph_io.h"

void hal_periph_reserve_io(void) { }

/* SPI-LCD / SSD1963 / XPT2046 init entry-point stubs for VGA-family
 * ports. The boot path in PicoMite.c calls these unconditionally; on
 * VGA they no-op (Touch.c / SSD1963.c / spi_lcd.c bodies aren't
 * linked here). Symbols match the signatures in SPI-LCD.h, SSD1963.h,
 * Touch.h. */
void InitDisplaySSD(void) { }
void InitDisplaySPI(int InitOnly) { (void)InitOnly; }
void InitDisplayI2C(int InitOnly) { (void)InitOnly; }
void InitTouch(void) { }
