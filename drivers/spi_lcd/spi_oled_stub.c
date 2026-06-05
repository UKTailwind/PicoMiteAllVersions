/*
 * drivers/spi_lcd/spi_oled_stub.c — no-op stub for hal_oled_spi_*.
 * Linked on ports that don't drive an SSD1306SPI OLED (VGA-family,
 * host). DISPLAY_TYPE never reaches SSD1306SPI on those ports.
 */

#include "hal/hal_display_oled_spi.h"

void hal_oled_spi_set_contrast(int level_percent) {
    (void)level_percent;
}
