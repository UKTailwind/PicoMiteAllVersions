/*
 * hal/hal_periph_io.h — boot-time pin reservation for non-VGA
 * peripheral families (SSD1963 panels, SPI-LCD CD/CS/Reset, XPT2046
 * touch).
 *
 * Real impl in drivers/spi_lcd/spi_lcd_periph_io.c is linked on the
 * four non-VGA SPI-LCD device ports (pico, pico_rp2350, web,
 * web_rp2350); VGA / HDMI / DVI-WiFi ports link the no-op stub in
 * drivers/spi_lcd/spi_lcd_periph_io_stub.c. The body references
 * symbols (TOUCH_*_PIN, SSD1963_GPDAT*) that only exist when Touch.c
 * + SSD1963.c are linked, which is exactly that non-VGA set.
 */

#ifndef HAL_PERIPH_IO_H
#define HAL_PERIPH_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reserve / configure pins for the panel + touch peripherals. Each
 * sub-block is internally Option-gated so unused peripherals stay
 * inert. Stub no-ops on VGA-family ports. */
void hal_periph_reserve_io(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PERIPH_IO_H */
