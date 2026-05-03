/*
 * hal/hal_spi_lcd_mem332.h — MEM332 buffered SPI-LCD family hooks.
 *
 * The MEM332 family (ILI9488WBUFF, ST7796SPBUFF, ILI9341BUFF,
 * ST7796SBUFF, ILI9488BUFF, ILI9488PBUFF, ST7789C) drives SPI display
 * controllers through an 8-bit RGB332 shadow framebuffer in RAM.
 * Real impl in drivers/spi_lcd/spi_lcd_mem332.c (linked on ports with
 * the RAM budget — currently rp2350 PicoMite). Other ports link the
 * stub in drivers/spi_lcd/spi_lcd_mem332_stub.c.
 */

#ifndef HAL_SPI_LCD_MEM332_H
#define HAL_SPI_LCD_MEM332_H

#ifdef __cplusplus
extern "C" {
#endif

/* OPTION DISPLAY <name> matcher for the MEM332 family.
 *   name -> the unsigned char buffer the user typed.
 *   returns: matching DISPLAY_TYPE constant, or 0 if `name` doesn't
 *   match any MEM332 entry (caller falls through to its default
 *   "unknown display type" error). */
int hal_spi_lcd_mem332_match_option(unsigned char *name);

/* Run the MEM332-specific display init when DISPLAY_TYPE is set to a
 * MEM332 constant. Stub no-op; runtime never reaches the stub
 * because the option setter rejects the MEM332 names on stub ports. */
void hal_spi_lcd_mem332_init_display(int display_type);

/* Populate the RGB332 -> RGB565 / RGB888 lookup tables. Real does
 * the work; stub no-op. */
void hal_spi_lcd_mem332_init_luts(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_LCD_MEM332_H */
