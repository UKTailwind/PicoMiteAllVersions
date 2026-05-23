/*
 * ansi_terminal_resize_posix.c — POSIX terminal-resize backend.
 *
 * SIGWINCH fires when the controlling terminal is resized; the
 * handler flips ansi_terminal_resized, and the render loop re-reads
 * the size via ioctl(TIOCGWINSZ) on the next iteration.
 */

#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ansi_terminal.h"
#include "ansi_terminal_resize.h"

static void ansi_sigwinch(int sig) {
    (void)sig;
    ansi_terminal_resized = 1;
}

void ansi_terminal_install_resize_handler(void) {
    /* host_raw_mode_signal hooks SIGINT/TERM/HUP/QUIT/PIPE/ABRT to
     * die cleanly; SIGWINCH isn't in that list, so we own it here. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ansi_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);
}

int ansi_terminal_query_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return -1;
    if (ws.ws_row == 0 || ws.ws_col == 0) return -1;
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
    return 0;
}
