/*
 * ansi_terminal.c — alt screen + raw-mode setup for the ANSI port.
 *
 * The host_terminal interface (host_terminal.c on POSIX,
 * host_terminal_win32.c on Windows) already handles the raw-mode
 * console toggle and signal-on-exit restoration of the original
 * console state. Here we add:
 *
 *   1. Alt screen enter/exit (\x1b[?1049h / \x1b[?1049l) so the
 *      terminal contents on exit match what was there on entry.
 *   2. Cursor hide/show (\x1b[?25l / \x1b[?25h).
 *   3. Terminal-resize notifier (POSIX: SIGWINCH; Win32: no-op) —
 *      the render loop polls ansi_terminal_resized and refreshes
 *      its shadow copy + terminal-size clamp.
 *
 * The signal handlers here chain onto the ones the host_terminal
 * backend installs: host_raw_mode_enter()'s handler restores the
 * console mode and re-raises, which fires our atexit() hook to exit
 * the alt screen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ansi_terminal.h"
#include "ansi_terminal_resize.h"
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

    /* Install the terminal-resize notifier. The render thread polls
     * ansi_terminal_resized to know when to re-fetch the size. The
     * POSIX backend installs a SIGWINCH handler; the Win32 backend
     * is a no-op for now (resize re-fetch happens on demand). */
    ansi_terminal_install_resize_handler();

    return 0;
}

int ansi_terminal_get_size(int *rows, int *cols) {
    ansi_terminal_resized = 0;
    return ansi_terminal_query_size(rows, cols);
}
