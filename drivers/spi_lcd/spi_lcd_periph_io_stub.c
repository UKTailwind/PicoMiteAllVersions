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
