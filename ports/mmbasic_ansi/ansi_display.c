/*
 * ansi_display.c — half-block framebuffer → terminal renderer.
 *
 * Reads host_framebuffer (24-bit packed in uint32_t, row-major, HRes
 * × VRes) and paints two vertically stacked pixels per terminal cell
 * using the Unicode half-block glyph ▀ (U+2580):
 *
 *     \x1b[38;2;R1;G1;B1m  → foreground colour = TOP pixel
 *     \x1b[48;2;R2;G2;B2m  → background colour = BOTTOM pixel
 *     ▀                   → fill top half of cell with fg,
 *                           bottom half with bg
 *
 * A 320×320 framebuffer maps to 320 columns × 160 rows of cells.
 *
 * Performance strategy:
 *   - shadow[]: last-emitted (top, bot) uint64 per cell. Skip cells
 *     that match.
 *   - Contiguous-run cursor move: only emit \x1b[R;C H when we
 *     actually jump rather than advance by one cell.
 *   - Contiguous-run colour change: only emit the SGR block when
 *     (fg, bg) differs from the previous cell we emitted.
 *   - Poll host_fb_generation every 16ms; skip the whole repaint if
 *     unchanged since last time.
 *   - One write(1, buf, len) per frame.
 */

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ansi_display.h"
#include "ansi_terminal.h"

/* From ports/host_native/host_fb.c. */
extern uint32_t * host_framebuffer;
extern int host_fb_width;
extern int host_fb_height;
extern volatile uint32_t host_fb_generation;
extern void host_fb_ensure(void);

static pthread_t render_tid;
static atomic_int render_stop = 0;
static int render_running = 0;

/* Shadow: one uint64 per cell, high 32 = top pixel, low 32 = bottom.
 * Uses 0xFF in the top byte of each half as an "invalid" sentinel so
 * the very first paint always emits every cell. */
static uint64_t * shadow = NULL;
static int shadow_w = 0;
static int shadow_h_cells = 0;

/* Output buffer — reusable across frames to avoid the per-frame
 * malloc. Grows as needed. */
static char * outbuf = NULL;
static size_t outbuf_cap = 0;
static size_t outbuf_len = 0;

/* Terminal cell dimensions. Recomputed on SIGWINCH. */
static int term_rows = 0;
static int term_cols = 0;

/* Tracks the last SGR we emitted this frame to elide redundant
 * colour changes. INVALID_SGR indicates "no colour emitted yet this
 * frame" — forces the first real cell to emit its SGR. */
#define INVALID_SGR 0xFFFFFFFFFFFFFFFFULL

/* Terminals that honour 24-bit truecolor SGR (\x1b[38;2;R;G;Bm) set
 * COLORTERM=truecolor or COLORTERM=24bit (iTerm2, Ghostty, Alacritty,
 * kitty, WezTerm, modern xterm). macOS's built-in Terminal.app does
 * not — it silently drops those SGR codes and prints glyphs in the
 * terminal's default fg/bg. Detect once at startup and fall back to
 * 256-colour SGR (\x1b[38;5;Nm) where N indexes the 6×6×6 color cube
 * at offset 16. */
static int truecolor_supported = 0;

static int detect_truecolor(void) {
    const char * ct = getenv("COLORTERM");
    if (!ct) return 0;
    return (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0);
}

/* Map a 24-bit RGB triple to the closest entry in the 6×6×6 color
 * cube (palette indices 16..231). Each channel's six steps are at
 * 0, 95, 135, 175, 215, 255 — so we bucket by midpoints (47, 115,
 * 155, 195, 235). The grayscale ramp at 232..255 would give better
 * accuracy for near-gray inputs, but the cube alone is enough for
 * the green-phosphor palette used by the REPL. */
static inline unsigned int rgb_to_256(uint32_t rgb) {
    unsigned int r = (rgb >> 16) & 0xFF;
    unsigned int g = (rgb >> 8) & 0xFF;
    unsigned int b = rgb & 0xFF;
    unsigned int r6 = (r < 48) ? 0 : (r < 115) ? 1
                                 : (r < 155)   ? 2
                                 : (r < 195)   ? 3
                                 : (r < 235)   ? 4
                                               : 5;
    unsigned int g6 = (g < 48) ? 0 : (g < 115) ? 1
                                 : (g < 155)   ? 2
                                 : (g < 195)   ? 3
                                 : (g < 235)   ? 4
                                               : 5;
    unsigned int b6 = (b < 48) ? 0 : (b < 115) ? 1
                                 : (b < 155)   ? 2
                                 : (b < 195)   ? 3
                                 : (b < 235)   ? 4
                                               : 5;
    return 16 + 36 * r6 + 6 * g6 + b6;
}

/* Invalidate shadow — next frame will emit every cell. */
void ansi_display_force_full_repaint(void) {
    if (!shadow) return;
    memset(shadow, 0xFF, (size_t)shadow_w * (size_t)shadow_h_cells * sizeof(*shadow));
}

static void outbuf_reserve(size_t extra) {
    if (outbuf_len + extra <= outbuf_cap) return;
    size_t new_cap = outbuf_cap ? outbuf_cap * 2 : 64 * 1024;
    while (new_cap < outbuf_len + extra) new_cap *= 2;
    char * nb = realloc(outbuf, new_cap);
    if (!nb) return; /* drop this cell rather than crash */
    outbuf = nb;
    outbuf_cap = new_cap;
}

static inline void outbuf_puts_n(const char * s, size_t n) {
    outbuf_reserve(n);
    if (!outbuf) return;
    memcpy(outbuf + outbuf_len, s, n);
    outbuf_len += n;
}

static inline void outbuf_append(const char * s) {
    outbuf_puts_n(s, strlen(s));
}

/* Small integer → decimal, no snprintf overhead. */
static inline void outbuf_dec(unsigned int v) {
    char tmp[12];
    int i = 0;
    if (v == 0) {
        outbuf_puts_n("0", 1);
        return;
    }
    while (v) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    outbuf_reserve((size_t)i);
    if (!outbuf) return;
    while (i-- > 0) outbuf[outbuf_len++] = tmp[i];
}

/* Emit cursor-move to 1-based (row, col). */
static void emit_cursor_move(int row, int col) {
    outbuf_puts_n("\x1b[", 2);
    outbuf_dec((unsigned)row);
    outbuf_puts_n(";", 1);
    outbuf_dec((unsigned)col);
    outbuf_puts_n("H", 1);
}

/* Emit combined SGR for fg+bg. Format depends on truecolor_supported:
 *   24-bit: \x1b[38;2;R;G;B;48;2;R;G;Bm
 *    8-bit: \x1b[38;5;N;48;5;Nm   (6×6×6 cube indices) */
static void emit_sgr(uint32_t fg, uint32_t bg) {
    if (truecolor_supported) {
        outbuf_puts_n("\x1b[38;2;", 7);
        outbuf_dec((fg >> 16) & 0xFF);
        outbuf_puts_n(";", 1);
        outbuf_dec((fg >> 8) & 0xFF);
        outbuf_puts_n(";", 1);
        outbuf_dec(fg & 0xFF);
        outbuf_puts_n(";48;2;", 6);
        outbuf_dec((bg >> 16) & 0xFF);
        outbuf_puts_n(";", 1);
        outbuf_dec((bg >> 8) & 0xFF);
        outbuf_puts_n(";", 1);
        outbuf_dec(bg & 0xFF);
        outbuf_puts_n("m", 1);
    } else {
        outbuf_puts_n("\x1b[38;5;", 7);
        outbuf_dec(rgb_to_256(fg));
        outbuf_puts_n(";48;5;", 6);
        outbuf_dec(rgb_to_256(bg));
        outbuf_puts_n("m", 1);
    }
}

/* UTF-8 encoding of ▀ (U+2580). */
static const char HALF_BLOCK_UP[] = {(char)0xE2, (char)0x96, (char)0x80};

static void render_frame(void) {
    int fb_w = host_fb_width;
    int fb_h = host_fb_height;
    if (!host_framebuffer || fb_w <= 0 || fb_h <= 0) return;

    int cells_w = fb_w;
    int cells_h = fb_h / 2;

    /* Resize shadow on first call or when framebuffer geometry
     * changed. */
    int clear_terminal = 0;
    if (shadow == NULL || shadow_w != cells_w || shadow_h_cells != cells_h) {
        free(shadow);
        shadow = calloc((size_t)cells_w * (size_t)cells_h, sizeof(*shadow));
        if (!shadow) return;
        memset(shadow, 0xFF,
               (size_t)cells_w * (size_t)cells_h * sizeof(*shadow));
        shadow_w = cells_w;
        shadow_h_cells = cells_h;
        clear_terminal = 1;
    }

    /* Clamp output to fit the terminal. If the terminal is smaller
     * than the framebuffer, we paint a letterboxed top-left region
     * — better than nothing, and the user can enlarge the window. */
    int paint_cols = cells_w;
    int paint_rows = cells_h;
    if (term_cols > 0 && term_cols < paint_cols) paint_cols = term_cols;
    if (term_rows > 0 && term_rows < paint_rows) paint_rows = term_rows;
    if (paint_cols <= 0 || paint_rows <= 0) return;

    outbuf_len = 0;
    if (clear_terminal) outbuf_append("\x1b[0m\x1b[2J\x1b[H");

    /* Cursor + SGR run-state across the whole frame. */
    int cursor_row = -1, cursor_col = -1; /* -1 = unknown, force move */
    uint64_t last_sgr = INVALID_SGR;

    /* Row-major scan of cells. Rows are the only axis we can't
     * walk sequentially at the byte level (each cell needs a lookup
     * into two rows of the framebuffer), but the hot path is memory-
     * already-in-cache once we're in this pair of rows. */
    for (int cy = 0; cy < paint_rows; ++cy) {
        int y_top = cy * 2;
        int y_bot = y_top + 1;
        const uint32_t * row_top = host_framebuffer + (size_t)y_top * (size_t)fb_w;
        const uint32_t * row_bot = (y_bot < fb_h)
                                       ? host_framebuffer + (size_t)y_bot * (size_t)fb_w
                                       : row_top;
        uint64_t * shadow_row = shadow + (size_t)cy * (size_t)shadow_w;

        for (int cx = 0; cx < paint_cols; ++cx) {
            uint32_t top = row_top[cx] & 0x00FFFFFFu;
            uint32_t bot = row_bot[cx] & 0x00FFFFFFu;
            uint64_t packed = ((uint64_t)top << 32) | (uint64_t)bot;
            if (shadow_row[cx] == packed) continue;
            shadow_row[cx] = packed;

            /* Cursor position management: if we're not already at
             * (cy+1, cx+1), emit a move. After emission the cursor
             * advances to (cy+1, cx+2). */
            int want_row = cy + 1;
            int want_col = cx + 1;
            if (cursor_row != want_row || cursor_col != want_col) {
                emit_cursor_move(want_row, want_col);
                cursor_row = want_row;
                cursor_col = want_col;
            }

            /* SGR: skip if already set to this (fg, bg) pair. */
            uint64_t sgr = packed;
            if (last_sgr != sgr) {
                emit_sgr(top, bot);
                last_sgr = sgr;
            }

            /* The glyph itself. */
            outbuf_puts_n(HALF_BLOCK_UP, sizeof(HALF_BLOCK_UP));
            cursor_col++;
        }
    }

    if (outbuf_len == 0) return; /* nothing changed */

    /* Drain the whole frame. stdin was put in O_NONBLOCK by
     * host_raw_mode_enter, and on a pty stdin/stdout share the file
     * description → stdout is also non-blocking. EAGAIN on a write means
     * the pty buffer is full; we MUST wait and retry rather than bail,
     * because the shadow was already updated to claim these cells are
     * painted. Dropping bytes here means subsequent frames see shadow
     * matches and never repaint — you end up with one row of content
     * and everything else stays blank for the rest of the run. */
    ssize_t off = 0;
    while ((size_t)off < outbuf_len) {
        ssize_t n = write(STDOUT_FILENO, outbuf + off,
                          outbuf_len - (size_t)off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timespec ts = {0, 1 * 1000 * 1000}; /* 1 ms */
                nanosleep(&ts, NULL);
                continue;
            }
            /* Real failure (EIO, EPIPE, etc). Invalidate the shadow so
             * the next frame retries the whole paint; don't leave the
             * renderer convinced stale bytes are on screen. */
            ansi_display_force_full_repaint();
            return;
        }
        off += n;
    }
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void * render_main(void * arg) {
    (void)arg;

    /* Get initial terminal size. */
    ansi_terminal_get_size(&term_rows, &term_cols);

    uint32_t last_gen = 0xFFFFFFFFu; /* force first paint */
    while (!atomic_load(&render_stop)) {
        if (ansi_terminal_resized) {
            ansi_terminal_get_size(&term_rows, &term_cols);
            ansi_display_force_full_repaint();
        }
        uint32_t gen = host_fb_generation;
        if (gen != last_gen) {
            host_fb_ensure();
            render_frame();
            last_gen = gen;
        }
        sleep_ms(16); /* ~60 Hz */
    }
    return NULL;
}

int ansi_display_start(void) {
    if (render_running) return 0;
    truecolor_supported = detect_truecolor();
    host_fb_ensure();
    atomic_store(&render_stop, 0);
    if (pthread_create(&render_tid, NULL, render_main, NULL) != 0) {
        return -1;
    }
    render_running = 1;
    return 0;
}

void ansi_display_stop(void) {
    if (!render_running) return;
    atomic_store(&render_stop, 1);
    pthread_join(render_tid, NULL);
    render_running = 0;
    free(shadow);
    shadow = NULL;
    shadow_w = 0;
    shadow_h_cells = 0;
    free(outbuf);
    outbuf = NULL;
    outbuf_cap = 0;
    outbuf_len = 0;
}
