/*
 * hal/hal_gui_controls.h — GUI control suite hooks (touch widgets,
 * pop-up keyboard, FORMATBOX, on-screen LED/spinbox/button family).
 *
 * Real impls live in drivers/gui_controls/gui_controls_real.c and link
 * only on ports that set HAL_PORT_HAS_GUICONTROLS=1. Other ports link
 * drivers/gui_controls/gui_controls_stub.c.
 *
 * Keeps GUICONTROLS as a port-config flag (palette flag in
 * port_config.h) without leaking #if HAL_PORT_HAS_GUICONTROLS gates
 * into strict-scope core files.
 */

#ifndef HAL_GUI_CONTROLS_H
#define HAL_GUI_CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Allocate and zero the CTRLS array; install the global Ctrl pointer.
 * No-op (Ctrl stays NULL) on stub ports. Called once during memory
 * init in Memory.c. */
void hal_gui_controls_alloc_array(void);

/* Zero per-control state for an ERASE / NEW / RUN-time program clear.
 * No-op on stub ports. Called from ClearProgram(). */
void hal_gui_controls_clear_for_program(void);

/* Run any deferred redraws (DelayedDrawKeyboard / DelayedDrawFmtBox)
 * after a pen-down BASIC interrupt has returned. No-op on stub ports.
 * Called from the IRQ-return tail of MMReturnFromInterrupt(). */
void hal_gui_controls_post_irq_redraw(void);

/* OPTION GUI CONTROLS <count> sub-form. Returns 1 if the cmdline
 * matched (and SoftReset() was triggered, so this never returns to
 * caller in practice); 0 if not matched (caller continues parsing). */
int hal_gui_controls_option_set(unsigned char *cmdline);

/* If a pen-up/pen-down GUI BASIC interrupt is pending, return its
 * vector and clear the pending flag; otherwise return NULL. Called
 * from check_interrupt(). */
char *hal_gui_controls_pending_interrupt(void);

/* Per-tick GUI service: ProcessTouch + CheckGui (LED-flash timer
 * etc.). No-op on stub ports. Called from check_interrupt(). */
void hal_gui_controls_periodic(void);

/* Special-case the FORMATBOX 0xff escape during text drawing. If
 * `*str == 0xff` and the invoking control is a FORMATBOX, this
 * consumes both the escape byte and the following glyph (advancing
 * `*str` by 2), emits the glyph with swapped fc/bc, and returns 1.
 * Otherwise leaves `*str` untouched and returns 0. */
int hal_gui_controls_print_char_escape(int fnt, int fc, int bc,
                                       char **str_pp,
                                       int orientation);

/* Hide all GUI controls. Called at the start of cmd_cls(). No-op on
 * stub ports. */
void hal_gui_controls_hide_all(void);

/* Reset GUI control state during a display-mode reset. No-op on stub
 * ports. */
void hal_gui_controls_reset(void);

/* Clear the four GUI interrupt globals (gui_int_down/up,
 * GuiIntDown/UpVector). Called from the soft-reset cleanup path in
 * External.c. No-op on stub ports. */
void hal_gui_controls_reset_interrupts(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GUI_CONTROLS_H */
