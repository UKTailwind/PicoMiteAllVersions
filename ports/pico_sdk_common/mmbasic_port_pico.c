/*
 * ports/pico_sdk_common/mmbasic_port_pico.c — non-HDMI device impl of
 * shared MMBasic.c port hooks. The HDMI build links the HDMI-aware
 * variant in ports/hdmi_rp2350/mmbasic_port_hdmi.c instead; the two
 * are mutually exclusive at CMake source-selection time.
 *
 *   - port_select_error_prompt_font() : after an error, pick the prompt
 *     font. Non-HDMI rule is simple: if gui_font_width>8, drop to the
 *     narrow 6x8 font. The HDMI variant has extra cases for FullColour
 *     / SCREENMODE3 so a wide 8x16 font stays usable on 1280-wide DVI.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_select_error_prompt_font(void) {
    if (gui_font_width > 8) {
        SetFont(1);
        PromptFont = 1;
    }
}
