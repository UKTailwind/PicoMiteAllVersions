/*
 * drivers/hdmi/hdmi_prompt_font.c — HDMI build of the
 * port_select_error_prompt_font() hook from MMBasic.c.
 *
 * Linked into the two HDMI ports (hdmi_rp2350, dvi_wifi_rp2350); other
 * ports link ports/pico_sdk_common/mmbasic_port_pico.c instead. Mutually
 * exclusive at CMake source-selection time.
 *
 * After an error, MMBasic asks the port to pick a prompt font. Non-HDMI
 * ports use a simple rule (gui_font_width>8 → narrow font). HDMI adds
 * extra cases for FullColour / SCREENMODE3 because 1280-pixel DVI modes
 * keep the wide 8x16 font readable.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "SPI-LCD.h" /* SCREENMODE3 */

void port_select_error_prompt_font(void) {
    if (((FullColour) || DISPLAY_TYPE == SCREENMODE3) && gui_font_width > 8) {
        SetFont(1);
        PromptFont = 1;
    } else if (gui_font_width > 16) {
        SetFont((2 << 4) | 1);
        PromptFont = (2 << 4) | 1;
    }
}
