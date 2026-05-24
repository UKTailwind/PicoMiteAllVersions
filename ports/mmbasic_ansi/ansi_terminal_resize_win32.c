/*
 * ansi_terminal_resize_win32.c — Windows terminal-resize backend.
 *
 * Windows has no SIGWINCH; resize events are delivered as
 * WINDOW_BUFFER_SIZE_EVENT records on the console input handle. The
 * renderer currently polls the size on demand, so the install hook
 * is a no-op — query_size() is canonical and is called every frame
 * anyway. A future revision can run a watcher thread that consumes
 * the input-event stream and sets ansi_terminal_resized.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ansi_terminal.h"
#include "ansi_terminal_resize.h"

void ansi_terminal_install_resize_handler(void) {
    /* No-op. See file header. */
}

int ansi_terminal_query_size(int *rows, int *cols) {
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
