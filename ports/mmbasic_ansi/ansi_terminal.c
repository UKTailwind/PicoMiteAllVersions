/*
 * ansi_terminal.c — alt screen + raw-mode setup for the ANSI port.
 *
 * host_native's host_terminal.c already handles termios raw mode and
 * signal-on-exit restoration of the original termios. Here we add:
 *
 *   1. Alt screen enter/exit (\x1b[?1049h / \x1b[?1049l) so the
 *      terminal contents on exit match what was there on entry.
 *   2. Cursor hide/show (\x1b[?25l / \x1b[?25h).
 *   3. SIGWINCH capture — the render loop polls ansi_terminal_resized
 *      and refreshes its shadow copy + terminal-size clamp.
 *
 * The signal handlers here chain onto the ones host_terminal.c
 * installs: host_raw_mode_enter()'s handler restores termios and
 * re-raises, which fires our atexit() hook to exit the alt screen.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ansi_terminal.h"
#include "host_terminal.h"

volatile int ansi_terminal_resized = 0;

static int ansi_entered = 0;

static void ansi_terminal_exit(void) {
    if (!ansi_entered) return;
    ansi_entered = 0;
    /* Restore SGR, auto-wrap on (was off during render), cursor visible,
     * exit alt screen. Order matters only in that the alt-screen exit
     * snaps back to the primary screen — the wrap-mode and cursor-
     * visibility restores apply to the primary screen too, so do them
     * before the swap so the user lands in a sane state. */
    const char restore[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    write(STDOUT_FILENO, restore, sizeof(restore) - 1);
    fflush(stdout);
}

static void ansi_sigwinch(int sig) {
    (void)sig;
    ansi_terminal_resized = 1;
}

int ansi_terminal_enter(void) {
    if (ansi_entered) return 0;

    /* Must be a TTY — no point running the half-block renderer into a
     * pipe. */
    if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
        fprintf(stderr,
                "mmbasic_ansi: stdin/stdout must be a TTY\n");
        return -1;
    }

    /* Enter alt screen + hide cursor + disable auto-wrap (DECAWM off) +
     * clear. Auto-wrap is the critical bit for a pixel-buffer renderer:
     * when we emit a cell at the rightmost column, terminals default to
     * wrapping the cursor to col 1 of the next line, which desyncs our
     * cursor bookkeeping and causes overlap on every row boundary. With
     * DECAWM off the cursor stays put after the last column; we always
     * emit an explicit cursor-move for the next row anyway. */
    const char enter[] = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J\x1b[H";
    if (write(STDOUT_FILENO, enter, sizeof(enter) - 1) < 0) return -1;

    atexit(ansi_terminal_exit);
    ansi_entered = 1;

    /* Raw stdin — host_terminal.c installs its own exit/signal hooks
     * that restore termios. Our atexit() runs in registration order
     * (last registered runs first), so we register AFTER host's hook:
     * the raw-mode restore fires first, then our alt-screen exit. But
     * host_raw_mode_enter() only registers atexit the first time — we
     * got here without it having run, so the order is:
     *   1. ansi_terminal_exit (our atexit, registered just above)
     *   2. host_raw_mode_restore (host's atexit, registered by
     *      host_raw_mode_enter below).
     * That order is correct: exit alt screen, then restore termios. */
    host_raw_mode_enter();

    /* SIGWINCH — the render thread polls ansi_terminal_resized to
     * know when to re-read TIOCGWINSZ. host_raw_mode_signal hooks
     * SIGINT/TERM/HUP/QUIT/PIPE/ABRT to die cleanly; SIGWINCH isn't
     * in that list, so we own it here. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = ansi_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

    return 0;
}

int ansi_terminal_get_size(int *rows, int *cols) {
    struct winsize ws;
    ansi_terminal_resized = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return -1;
    if (ws.ws_row == 0 || ws.ws_col == 0) return -1;
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
    return 0;
}
