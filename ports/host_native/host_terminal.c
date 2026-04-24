/*
 * host_terminal.c — termios raw-mode plumbing for the host REPL / EDIT.
 *
 * Kept in its own translation unit so we can pull in the POSIX headers
 * (unistd.h in particular) without colliding with MMBasic's global names
 * like setmode / putchar that appear once we include MMBasic_Includes.h.
 *
 * Exposed functions (declared in host_terminal.h):
 *   host_raw_mode_enter()      — switch stdin to raw mode, register atexit
 *   host_raw_mode_is_active()  — query for callers that need to know
 *   host_read_byte_nonblock()  — read one byte, -1 if none available
 *   host_read_byte_blocking_ms(ms)
 *                              — read one byte, waiting up to ms millis
 *   host_push_back_byte(int)   — put a byte back for the next read
 */

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "host_terminal.h"

static struct termios host_orig_termios;
static int host_raw_mode_active = 0;
static int host_stdin_saved_flags = 0;
static int host_pending_byte = -1;

static void host_raw_mode_restore(void) {
    if (!host_raw_mode_active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &host_orig_termios);
    fcntl(STDIN_FILENO, F_SETFL, host_stdin_saved_flags);
    host_raw_mode_active = 0;
}

/*
 * Signal handler that restores termios then re-raises the signal with
 * its default disposition. atexit() only fires on normal exit() — a
 * SIGTERM / SIGHUP / SIGPIPE / Ctrl-\ leaves OPOST disabled, ICANON
 * off, and stdin non-blocking, so the user's shell stair-steps newlines
 * until they run `stty sane`. Catching the common terminating signals
 * and calling the restore hook closes that hole. SIGKILL and SIGSTOP
 * can't be caught, but those are rare in practice. */
static void host_raw_mode_signal(int sig) {
    host_raw_mode_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void host_raw_mode_install_signal_handlers(void) {
    static int installed = 0;
    if (installed) return;
    installed = 1;
    /* SA_RESTART isn't necessary — we don't return to the syscall
     * that was interrupted, we re-raise to die. Use signal() instead
     * of sigaction() to keep the footprint small. */
    signal(SIGINT,  host_raw_mode_signal);
    signal(SIGTERM, host_raw_mode_signal);
    signal(SIGHUP,  host_raw_mode_signal);
    signal(SIGQUIT, host_raw_mode_signal);
    signal(SIGPIPE, host_raw_mode_signal);
    signal(SIGABRT, host_raw_mode_signal);
}

void host_raw_mode_enter(void) {
    if (host_raw_mode_active) return;
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &host_orig_termios) != 0) return;
    host_stdin_saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);

    struct termios raw = host_orig_termios;
    /* Disable line buffering, echo, and CR->NL translation. Keep ISIG so
     * Ctrl-C still raises SIGINT (the default terminates the process, which
     * is fine for now; a follow-up can trap it and set MMAbort). */
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL | INLCR);
    raw.c_oflag &= ~OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;

    fcntl(STDIN_FILENO, F_SETFL, host_stdin_saved_flags | O_NONBLOCK);

    /* EDIT writes VT100 escape sequences mid-line; disable stdout buffering
     * so those reach the terminal without waiting for a newline. */
    setvbuf(stdout, NULL, _IONBF, 0);

    atexit(host_raw_mode_restore);
    host_raw_mode_install_signal_handlers();
    host_raw_mode_active = 1;
}

int host_raw_mode_is_active(void) {
    return host_raw_mode_active;
}

int host_read_byte_nonblock(void) {
    if (host_pending_byte >= 0) {
        int c = host_pending_byte;
        host_pending_byte = -1;
        return c;
    }
    unsigned char b;
    ssize_t n = read(STDIN_FILENO, &b, 1);
    if (n == 1) return (int)b;
    return -1;
}

int host_read_byte_blocking_ms(int ms) {
    /* Poll in 1 ms slices rather than reconfiguring VMIN/VTIME mid-stream. */
    for (int i = 0; i < ms; ++i) {
        int c = host_read_byte_nonblock();
        if (c >= 0) return c;
        usleep(1000);
    }
    return -1;
}

void host_push_back_byte(int c) {
    host_pending_byte = c;
}

int host_terminal_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return -1;
    if (ws.ws_row == 0 || ws.ws_col == 0) return -1;
    if (rows) *rows = ws.ws_row;
    if (cols) *cols = ws.ws_col;
    return 0;
}
