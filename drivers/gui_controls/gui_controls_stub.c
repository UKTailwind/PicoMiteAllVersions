/*
 * drivers/gui_controls/gui_controls_stub.c — no-op stubs for ports
 * that do not link the GUI control suite (HAL_PORT_HAS_GUICONTROLS=0).
 *
 * The Ctrl pointer is declared as a tentative-definition NULL so that
 * any rare reference from shared code (e.g. legacy debug paths that
 * test Ctrl != NULL) link cleanly.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_gui_controls.h"

extern void cmd_guiMX170(void);

struct s_ctrl * Ctrl = NULL;

void hal_gui_controls_alloc_array(void) {}
void hal_gui_controls_clear_for_program(void) {}
void hal_gui_controls_post_irq_redraw(void) {}
int hal_gui_controls_option_set(unsigned char * cmdline) {
    (void)cmdline;
    return 0;
}
char * hal_gui_controls_pending_interrupt(void) {
    return NULL;
}
void hal_gui_controls_periodic(void) {}
int hal_gui_controls_print_char_escape(int fnt, int fc, int bc,
                                       char ** str_pp,
                                       int orientation) {
    (void)fnt;
    (void)fc;
    (void)bc;
    (void)str_pp;
    (void)orientation;
    return 0;
}
void hal_gui_controls_hide_all(void) {}
void hal_gui_controls_reset(void) {}
void hal_gui_controls_reset_interrupts(void) {}

int hal_gui_controls_get_touch_attr(unsigned char * p, int64_t * iret_out) {
    (void)p;
    (void)iret_out;
    return 0;
}
void hal_gui_controls_set_beep_timer(int ms) {
    (void)ms;
    error("Not supported on this board");
}
void hal_gui_controls_routine_check_touch(void) {}
void hal_gui_controls_timer_tick(void) {}
void hal_gui_controls_print_options(void) {}
void hal_gui_controls_note_create(void) {}
void hal_gui_controls_note_delete(void) {}
void hal_gui_controls_note_reset(void) {}
int hal_gui_controls_has_active(void) { return 0; }
int hal_gui_controls_service_needed(void) { return 0; }

/* GUI command/function stubs. The token table in AllCommands.h
 * references these unconditionally; on stub ports they all error.
 * cmd_gui falls through to cmd_guiMX170 (the legacy non-GUICONTROLS
 * GUI sub-command parser, which lives in Draw.c and is linked on
 * every port). */
void cmd_gui(void) {
    cmd_guiMX170();
}
void cmd_ctrlval(void) {
    error("Not supported on this board");
}
void fun_msgbox(void) {
    error("Not supported on this board");
}
void fun_ctrlval(void) {
    error("Not supported on this board");
}
