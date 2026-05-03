/*
 * hal/hal_display_oled_spi.h — backlight/contrast hook for the
 * SSD1306SPI OLED panel. Real impl lives in drivers/spi_lcd/spi_lcd.c
 * (linked on SPI-LCD ports); stub in
 * drivers/spi_lcd/spi_oled_stub.c. The DISPLAY_TYPE never reaches
 * SSD1306SPI on VGA-family ports, but the call site in External.c
 * setBacklight() must still link, so the hook keeps the symbol
 * unconditional.
 */

#ifndef HAL_DISPLAY_OLED_SPI_H
#define HAL_DISPLAY_OLED_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

void hal_oled_spi_set_contrast(int level_percent);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_OLED_SPI_H */
