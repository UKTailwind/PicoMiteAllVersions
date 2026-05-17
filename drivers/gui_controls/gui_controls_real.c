/*
 * drivers/gui_controls/gui_controls_real.c — real implementation of
 * the HAL_GUI_CONTROLS hooks. Linked on ports that set
 * HAL_PORT_HAS_GUICONTROLS=1 (PICORP2350, WEBRP2350) and that link
 * GUI.c.
 *
 * Owns the CTRLS[] array and the global Ctrl pointer. Wires touch
 * events, OPTION GUI CONTROLS, and FORMATBOX-escape rendering through
 * GUI.c / Draw.c without leaving #if gates in core source.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "gfx_console_shared.h"
#include "hal/hal_gui_controls.h"

extern int InvokingCtrl;

static struct s_ctrl CTRLS[MAXCONTROLS];
struct s_ctrl *Ctrl = CTRLS;

void hal_gui_controls_alloc_array(void) {}

void hal_gui_controls_clear_for_program(void) {
    for (int i = 1; i < Option.MaxCtrls; i++) {
        memset(&Ctrl[i], 0, sizeof(struct s_ctrl));
        Ctrl[i].state = Ctrl[i].type = 0;
        Ctrl[i].s = NULL;
    }
}

void hal_gui_controls_post_irq_redraw(void) {
    if (DelayedDrawKeyboard) {
        DrawKeyboard(1);
        DelayedDrawKeyboard = false;
    }
    if (DelayedDrawFmtBox) {
        DrawFmtBox(1);
        DelayedDrawFmtBox = false;
    }
}

int hal_gui_controls_option_set(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"GUI CONTROLS");
    if (!tp) return 0;
    getargs(&tp, 1, (unsigned char *)",");
    if (CurrentLinePtr) error("Invalid in a program");
    Option.MaxCtrls = getint(argv[0], 0, MAXCONTROLS - 1);
    if (Option.MaxCtrls) Option.MaxCtrls++;
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return 1;
}

char *hal_gui_controls_pending_interrupt(void) {
    if (Ctrl == NULL) return NULL;
    if (gui_int_down && GuiIntDownVector) {
        gui_int_down = false;
        return GuiIntDownVector;
    }
    if (gui_int_up && GuiIntUpVector) {
        gui_int_up = false;
        return GuiIntUpVector;
    }
    return NULL;
}

void hal_gui_controls_periodic(void) {
    if (Ctrl == NULL) return;
    if (!(DelayedDrawKeyboard || DelayedDrawFmtBox || calibrate)) ProcessTouch();
    if (CheckGuiFlag) CheckGui();
}

int hal_gui_controls_print_char_escape(int fnt, int fc, int bc,
                                       char **str_pp,
                                       int orientation) {
    char *str = *str_pp;
    if ((unsigned char)*str == 0xff && Ctrl[InvokingCtrl].type == 10) {
        str++;
        GUIPrintChar(fnt, bc, fc, *str++, orientation);
        *str_pp = str;
        return 1;
    }
    return 0;
}

void hal_gui_controls_hide_all(void) { HideAllControls(); }

void hal_gui_controls_reset(void) { ResetGUI(); }

void hal_gui_controls_reset_interrupts(void) {
    gui_int_down = false;
    gui_int_up = false;
    GuiIntDownVector = NULL;
    GuiIntUpVector = NULL;
}

/* Touch.c globals + helpers used by the routine/timer ticks. */
extern int TOUCH_GETIRQTRIS;
extern volatile bool TouchDown, TouchUp, TouchState;

int hal_gui_controls_get_touch_attr(unsigned char *p, int64_t *iret_out) {
    if      (checkstring(p, (unsigned char *)"REF"))      *iret_out = CurrentRef;
    else if (checkstring(p, (unsigned char *)"LASTREF"))  *iret_out = LastRef;
    else if (checkstring(p, (unsigned char *)"LASTX"))    *iret_out = LastX;
    else if (checkstring(p, (unsigned char *)"LASTY"))    *iret_out = LastY;
    else return 0;
    return 1;
}

void hal_gui_controls_set_beep_timer(int ms) {
    if (Option.TOUCH_Click == 0) error("Click option not set");
    ClickTimer = ms + 1;
}

void hal_gui_controls_routine_check_touch(void) {
    if (Ctrl && TOUCH_GETIRQTRIS && !calibrate) ProcessTouch();
}

void hal_gui_controls_timer_tick(void) {
    TouchTimer++;
    if (CheckGuiFlag) CheckGuiTimeouts();

    if (TOUCH_GETIRQTRIS) {
        if (TOUCH_DOWN) {
            if (!TouchState) {
                TouchState = TouchDown = true;
            }
        } else {
            if (TouchState) {
                TouchState = false;
                TouchUp = true;
            }
        }
    }

    if (ClickTimer) {
        ClickTimer--;
        if (Option.TOUCH_Click)
            PinSetBit(Option.TOUCH_Click, ClickTimer ? LATSET : LATCLR);
    }
}

extern void PO2Int(char *s1, int n);

void hal_gui_controls_print_options(void) {
    if (Option.MaxCtrls) PO2Int("GUI CONTROLS", Option.MaxCtrls - 1);
}
