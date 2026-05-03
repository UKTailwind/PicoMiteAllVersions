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

struct s_ctrl *Ctrl = NULL;

void hal_gui_controls_alloc_array(void) {}
void hal_gui_controls_clear_for_program(void) {}
void hal_gui_controls_post_irq_redraw(void) {}
int  hal_gui_controls_option_set(unsigned char *cmdline) { (void)cmdline; return 0; }
char *hal_gui_controls_pending_interrupt(void) { return NULL; }
void hal_gui_controls_periodic(void) {}
int  hal_gui_controls_print_char_escape(int fnt, int fc, int bc,
                                        char **str_pp,
                                        int orientation) {
    (void)fnt; (void)fc; (void)bc; (void)str_pp; (void)orientation;
    return 0;
}
void hal_gui_controls_hide_all(void) {}
void hal_gui_controls_reset(void) {}
void hal_gui_controls_reset_interrupts(void) {}
