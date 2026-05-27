/*
 * host_terminal_win32.c — Windows backend for the host REPL / EDIT
 * raw-mode plumbing. Mirrors the POSIX implementation in
 * host_terminal.c; the mmbasic_ansi Makefile picks this file when
 * targeting Windows (mingw-w64).
 *
 * Maps POSIX termios + non-blocking stdin onto the Win32 console:
 *   - Raw mode is SetConsoleMode with ENABLE_LINE_INPUT /
 *     ENABLE_ECHO_INPUT / ENABLE_PROCESSED_INPUT cleared, and
 *     ENABLE_VIRTUAL_TERMINAL_INPUT / _PROCESSING set so the
 *     escape-sequence I/O the renderer emits/consumes works on
 *     Windows Terminal, conhost (Win10+), and most modern shells.
 *   - Non-blocking single-byte read uses _kbhit + _getch. Extended
 *     keys (arrows, F-keys) are dropped for now — the canonical fix
 *     is to switch to ReadConsoleInput once the renderer needs them.
 *   - Terminal size comes from GetConsoleScreenBufferInfo's srWindow
 *     rectangle (visible window, not the back-scroll buffer).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <conio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <timeapi.h>

#include "host_keyrepeat.h"
#include "host_terminal.h"

static int   host_raw_mode_active = 0;
static DWORD host_orig_in_mode    = 0;
static DWORD host_orig_out_mode   = 0;
static UINT  host_orig_in_cp      = 0;
static UINT  host_orig_out_cp     = 0;

#define HOST_PENDING_CAP 16384
static unsigned char host_pending_bytes[HOST_PENDING_CAP];
static int host_pending_head = 0;
static int host_pending_count = 0;

static void host_pending_clear(void) {
    host_pending_head = 0;
    host_pending_count = 0;
}

static int host_pending_pop_front(void) {
    if (host_pending_count == 0) return -1;
    int c = host_pending_bytes[host_pending_head];
    host_pending_head = (host_pending_head + 1) % HOST_PENDING_CAP;
    host_pending_count--;
    return c;
}

static void host_pending_push_front(int c) {
    if (host_pending_count == HOST_PENDING_CAP) return;
    host_pending_head = (host_pending_head + HOST_PENDING_CAP - 1) % HOST_PENDING_CAP;
    host_pending_bytes[host_pending_head] = (unsigned char)c;
    host_pending_count++;
}

static void host_pending_push_back(int c) {
    if (host_pending_count == HOST_PENDING_CAP) {
        host_pending_head = (host_pending_head + 1) % HOST_PENDING_CAP;
        host_pending_count--;
    }
    int tail = (host_pending_head + host_pending_count) % HOST_PENDING_CAP;
    host_pending_bytes[tail] = (unsigned char)c;
    host_pending_count++;
}

static int host_read_os_byte_nonblock(void) {
    if (!_kbhit()) return -1;
    int ch = _getch();
    if (ch == 0 || ch == 0xE0) {
        if (_kbhit()) (void)_getch();
        return -1;
    }
    return ch & 0xff;
}

static void host_raw_mode_restore(void) {
    if (!host_raw_mode_active) return;
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hin  != INVALID_HANDLE_VALUE) SetConsoleMode(hin,  host_orig_in_mode);
    if (hout != INVALID_HANDLE_VALUE) SetConsoleMode(hout, host_orig_out_mode);
    if (host_orig_in_cp)  SetConsoleCP(host_orig_in_cp);
    if (host_orig_out_cp) SetConsoleOutputCP(host_orig_out_cp);
    timeEndPeriod(1);
    host_raw_mode_active = 0;
}

static void host_raw_mode_signal(int sig) {
    host_raw_mode_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Windows console control handler. With ENABLE_PROCESSED_INPUT
 * cleared, the OS no longer turns Ctrl-C into a SIGINT delivered to
 * the C runtime — the byte just flows through as 0x03. That means
 * the standard signal() handler above never fires for Ctrl-C, and
 * the user can't break out of a running script. CTRL_CLOSE_EVENT
 * (window-close) and CTRL_BREAK_EVENT also need a cleanup path.
 * Install a console handler that restores the console mode + code
 * pages before exiting so the user's shell isn't left in raw mode. */
static BOOL WINAPI host_win32_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            host_raw_mode_restore();
            ExitProcess(1);
            return TRUE;
    }
    return FALSE;
}

void host_raw_mode_enter(void) {
    if (host_raw_mode_active) return;
    if (!_isatty(_fileno(stdin))) return;

    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(hin,  &host_orig_in_mode))  return;
    if (!GetConsoleMode(hout, &host_orig_out_mode)) return;

    DWORD in_mode = host_orig_in_mode;
    in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    in_mode |=  ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hin, in_mode);

    DWORD out_mode = host_orig_out_mode;
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    /* DISABLE_NEWLINE_AUTO_RETURN keeps writes that cross the right
     * margin from forcing a CR+LF — matches the POSIX path's OPOST
     * clear and the renderer's manual cursor placement. */
    out_mode |= DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(hout, out_mode);

    /* The half-block renderer emits UTF-8 (▀ = U+2580 → E2 96 80).
     * Without this, cmd / PowerShell decode those bytes as CP437 /
     * CP850 / OEM and paint mojibake. Save the original code pages
     * so the restore hook puts the user's shell back the way it
     * was found. */
    host_orig_in_cp  = GetConsoleCP();
    host_orig_out_cp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* Bump Windows timer resolution to 1 ms so nanosleep(1 ms) actually
     * sleeps ~1 ms instead of the default 15.6 ms tick. Required for
     * --slowdown to have a useful effect. timeEndPeriod is paired in
     * the restore hook. */
    timeBeginPeriod(1);

    setvbuf(stdout, NULL, _IONBF, 0);

    atexit(host_raw_mode_restore);
    signal(SIGINT,  host_raw_mode_signal);
    signal(SIGTERM, host_raw_mode_signal);
    signal(SIGABRT, host_raw_mode_signal);
    SetConsoleCtrlHandler(host_win32_ctrl_handler, TRUE);
    host_raw_mode_active = 1;
}

int host_raw_mode_is_active(void) {
    return host_raw_mode_active;
}

int host_read_byte_nonblock(void) {
    int pending = host_pending_pop_front();
    if (pending >= 0) {
        return host_keyrepeat_filter(pending);
    }
    int c = host_read_os_byte_nonblock();
    if (c >= 0) return host_keyrepeat_filter(c);
    return -1;
}

int host_poll_break_key(int break_key) {
    int seen = 0;
    int n = host_pending_count;
    for (int i = 0; i < n; ++i) {
        int c = host_pending_pop_front();
        if (c < 0) break;
        if (c == (break_key & 0xff)) {
            host_pending_clear();
            seen = 1;
            break;
        } else {
            host_pending_push_back(c);
        }
    }

    while (1) {
        int c = host_read_os_byte_nonblock();
        if (c < 0) break;
        if (c == (break_key & 0xff)) {
            host_pending_clear();
            seen = 1;
            continue;
        }
        host_pending_push_back(c);
    }
    return seen;
}

int host_read_byte_blocking_ms(int ms) {
    for (int i = 0; i < ms; ++i) {
        int c = host_read_byte_nonblock();
        if (c >= 0) return c;
        Sleep(1);
    }
    return -1;
}

void host_push_back_byte(int c) {
    host_pending_push_front(c);
}

int host_terminal_get_size(int *rows, int *cols) {
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout == INVALID_HANDLE_VALUE) return -1;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hout, &csbi)) return -1;
    int r = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    int c = (int)(csbi.srWindow.Right  - csbi.srWindow.Left + 1);
    if (r <= 0 || c <= 0) return -1;
    if (rows) *rows = r;
    if (cols) *cols = c;
    return 0;
}
