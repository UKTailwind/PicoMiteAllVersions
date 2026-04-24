/*
 * ports/pico_sdk_common/clear_runtime_port.c — device impl of the
 * MMBasic.c ClearRuntime() display-reset port hook.
 *
 *   - port_clear_runtime_display_reset() : reset scroll offset on
 *     SPI-LCD / MEM332 panels, re-prime the RGB332 LUT on rp2350
 *     NEXTGEN buffered modes, and clear the SSD16xx / IPS_4_16 /
 *     SPI480 framebuffer. Skipped on VGA variants — VGA has no
 *     scrollable panel state.
 *
 * Links on every device variant (ports/pico_sdk_common/*.c are in
 * PICOMITE_SOURCES). VGA gates out the body internally since its
 * spi_write_* / ScrollLCD symbols aren't linked.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"

void port_clear_runtime_display_reset(void) {
#ifndef PICOMITEVGA
#if defined(PICOMITE) && defined(rp2350)
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
