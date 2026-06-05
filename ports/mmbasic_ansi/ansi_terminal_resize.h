#ifndef ANSI_TERMINAL_RESIZE_H
#define ANSI_TERMINAL_RESIZE_H

/*
 * Terminal-size backend. Two implementations:
 *   ansi_terminal_resize_posix.c — SIGWINCH + ioctl(TIOCGWINSZ)
 *   ansi_terminal_resize_win32.c — no-op handler + GetConsoleScreenBufferInfo
 *
 * The Makefile picks one based on host OS. The render loop in
 * ansi_display.c polls ansi_terminal_resized (set by the resize
 * notifier) to know when to re-fetch the size; both backends
 * cooperate with that flag.
 */

/* Install whatever resize notifier the platform offers. On Win32
 * this is a no-op for now — resize is detected on the next
 * ansi_terminal_get_size() call. */
void ansi_terminal_install_resize_handler(void);

/* Query current terminal cell size. Returns 0 on success and writes
 * rows/cols; returns -1 if the size is unavailable. */
int ansi_terminal_query_size(int * rows, int * cols);

#endif
