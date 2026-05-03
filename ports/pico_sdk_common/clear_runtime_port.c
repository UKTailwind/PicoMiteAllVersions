/*
 * ports/pico_sdk_common/clear_runtime_port.c — device impl of the
 * MMBasic.c error-recovery and ClearRuntime() display port hooks.
 *
 *   - port_clear_runtime_display_reset() : reset scroll offset on
 *     SPI-LCD / MEM332 panels, re-prime the RGB332 LUT on rp2350
 *     NEXTGEN buffered modes, and clear the SSD16xx / IPS_4_16 /
 *     SPI480 framebuffer. Skipped on VGA variants.
 *   - port_error_restore_console_surface() : after an error, point
 *     the console at the correct surface. VGA sets WriteBuf/DisplayBuf
 *     to FRAMEBUFFER; SPI-LCD calls restorepanel().
 *   - port_error_show_lcd_banner() : draw the LCD error banner on
 *     SPI-LCD panels; no-op on VGA.
 *
 * Links on every device variant (ports/pico_sdk_common/*.c are in
 * PICOMITE_SOURCES). Per-target gates inside the file select the
 * right body.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"

void port_clear_runtime_display_reset(void) {
#if !HAL_PORT_IS_VGA
#if HAL_PORT_HAS_PICOMITE && defined(rp2350)
    if (Option.DISPLAY_TYPE >= NEXTGEN) {
        Option.Refresh = 1;
        if (Option.DISPLAY_TYPE == ILI9488BUFF || Option.DISPLAY_TYPE == ILI9488PBUFF)
            init_RGB332_to_RGB888_LUT();
        else
            init_RGB332_to_RGB565_LUT();
    }
#endif
    if (ScrollLCD == ScrollLCDSPISCR) {
        ScrollStart = 0;
        spi_write_command(CMD_SET_SCROLL_START);
        spi_write_data(0);
        spi_write_data(0);
    }
    if (ScrollLCD == ScrollLCDSPISCR) {
        ScrollStart = 0;
        WriteComand(CMD_SET_SCROLL_START);
        WriteData(0);
        WriteData(0);
    }
    if (ScrollLCD == ScrollLCDMEM332) {
        hal_display_nextgen_scroll_reset();
    }
    if (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16 || SPI480)
        clear320();
#endif
}

void port_error_restore_console_surface(void) {
#if HAL_PORT_IS_VGA
    WriteBuf   = (unsigned char *)FRAMEBUFFER;
    DisplayBuf = (unsigned char *)FRAMEBUFFER;
#else
    restorepanel();
#endif
}

/* LCD_error lives in MMBasic.c — no header declares it. */
extern void LCD_error(int line_num, const char *line_txt, const char *error_msg);

void port_error_show_lcd_banner(int line_num, const char *source_line, const char *err_msg) {
#if HAL_PORT_IS_VGA
    (void)line_num; (void)source_line; (void)err_msg;
#else
    if (!Option.DISPLAY_CONSOLE && Option.DISPLAY_TYPE > I2C_PANEL) {
        int width  = Option.Width;
        int height = Option.Height;
        LCD_error(line_num, source_line, err_msg);
        Option.Width  = width;
        Option.Height = height;
    }
#endif
}
