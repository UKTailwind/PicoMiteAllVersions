/*
 * ports/hdmi_rp2350/mmbasic_port_hdmi.c — HDMI build of shared MMBasic.c
 * port hooks. Replaces ports/pico_sdk_common/mmbasic_port_pico.c at
 * CMake source-selection time.
 *
 *   - port_select_error_prompt_font() : after an error, pick the prompt
 *     font. HDMI adds rules for FullColour / SCREENMODE3 on top of the
 *     non-HDMI `gui_font_width > 8 → narrow font` baseline, because
 *     1280-pixel DVI modes keep the wide 8x16 readable.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "SPI-LCD.h"   /* SCREENMODE3 */

void port_select_error_prompt_font(void) {
    if (((FullColour) || DISPLAY_TYPE == SCREENMODE3) && gui_font_width > 8) {
        SetFont(1);
        PromptFont = 1;
    } else if (gui_font_width > 16) {
        SetFont((2 << 4) | 1);
        PromptFont = (2 << 4) | 1;
    }
}
