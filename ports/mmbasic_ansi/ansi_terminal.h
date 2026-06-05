#ifndef ANSI_TERMINAL_H
#define ANSI_TERMINAL_H

/*
 * Terminal setup for the ANSI half-block port.
 *
 * ansi_terminal_enter() switches the terminal into the alt screen,
 * hides the cursor, installs SIGWINCH/SIGINT/SIGTERM handlers that
 * restore the terminal before exiting, and flips stdin into raw mode
 * via host_raw_mode_enter(). Registers an atexit() hook so a normal
 * exit() also leaves the alt screen.
 *
 * ansi_terminal_get_size() returns the current terminal cell size via
 * TIOCGWINSZ; returns 0 on success, -1 on failure.
 */
int ansi_terminal_enter(void);
int ansi_terminal_get_size(int * rows, int * cols);

/* Non-zero after a SIGWINCH fires. Cleared by ansi_terminal_get_size. */
extern volatile int ansi_terminal_resized;

#endif
