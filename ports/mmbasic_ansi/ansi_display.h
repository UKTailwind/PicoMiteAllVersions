#ifndef ANSI_DISPLAY_H
#define ANSI_DISPLAY_H

/*
 * Half-block framebuffer renderer.
 *
 * ansi_display_start() spawns a pthread that wakes at ~60 Hz, reads
 * host_framebuffer, and paints changed half-block cells to stdout as
 * Unicode ▀ (U+2580) with 24-bit ANSI truecolor for the top pixel
 * (fg) and bottom pixel (bg).
 *
 * ansi_display_stop() joins the thread and leaves the terminal in a
 * sane state (cursor home, default colors). Signal handlers in
 * ansi_terminal.c take care of cleanup if the process dies abruptly.
 *
 * ansi_display_force_full_repaint() invalidates the shadow so the
 * next frame emits every cell. Called after SIGWINCH.
 */

int  ansi_display_start(void);
void ansi_display_stop(void);
void ansi_display_force_full_repaint(void);

#endif
