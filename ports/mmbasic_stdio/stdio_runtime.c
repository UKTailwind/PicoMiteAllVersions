/*
 * ports/mmbasic_stdio/stdio_runtime.c — null / stdout-only port
 * runtime for mmbasic_stdio.
 *
 * Replaces host_native's host_fb.c (framebuffer) + host_fastgfx.c
 * (FASTGFX backbuffer) + host_terminal.c (raw tty) + host_main.c
 * (host_output_hook) + Editor.c (EDIT command) with the minimum
 * symbols MMBasic core + VM core need at link time.
 *
 * PRINT routes to stdout through MMputchar (defined in
 * host_runtime.c; its dispatch calls host_output_hook which we
 * wire straight to fputc(stdout)).  INKEY$ / INPUT read stdin
 * raw — no termios dance, no sim-server.
 *
 * Every hardware-only entry (framebuffer / fastgfx / edit / pixel
 * draw) errors with "Not supported on mmbasic_stdio" so BASIC
 * programs that reach those paths surface the HAL leak loudly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

static void stdio_nope(const char * op) {
    fprintf(stderr, "Error: '%s' not supported on mmbasic_stdio\n", op);
    error("Not supported on mmbasic_stdio");
}

/* ------------------------------------------------------------- */
/*  Output hook — PRINT lands here via host_runtime.c's           */
/*  MMputchar -> host_output_hook(text, len).  Function-pointer   */
/*  variable, not a function — host_runtime.c declares it as      */
/*  `extern void (*host_output_hook)(const char *, int);`.        */
/* ------------------------------------------------------------- */
static void stdio_output(const char * text, int len) {
    fwrite(text, 1, (size_t)len, stdout);
}
void (*host_output_hook)(const char * text, int len) = stdio_output;

/* ------------------------------------------------------------- */
/*  Terminal stubs — stdin / no-tty.                              */
/* ------------------------------------------------------------- */
int host_raw_mode_is_active(void) {
    return 0;
}

int host_read_byte_nonblock(void) {
    /* stdin isn't set to nonblocking; return -1 to signal "nothing
     * ready" so MMInkey polls cooperate with its own waiters. */
    return -1;
}

int host_read_byte_blocking_ms(int ms) {
    (void)ms;
    int c = fgetc(stdin);
    return (c == EOF) ? -1 : c;
}

void host_push_back_byte(int c) {
    (void)c; /* no push-back on stdio */
}

/* ------------------------------------------------------------- */
/*  Framebuffer HAL — every entry is an error.                    */
/* ------------------------------------------------------------- */
void host_framebuffer_reset_runtime(int colour) {
    (void)colour;
}
void host_framebuffer_shutdown_runtime(void) {}
void host_framebuffer_service(void) {}
void host_framebuffer_create(void) {
    stdio_nope("FRAMEBUFFER CREATE");
}
void host_framebuffer_layer(int hc, int c) {
    (void)hc;
    (void)c;
    stdio_nope("FRAMEBUFFER LAYER");
}
void host_framebuffer_write(char w) {
    (void)w;
    stdio_nope("FRAMEBUFFER WRITE");
}
void host_framebuffer_close(char w) {
    (void)w;
}
void host_framebuffer_merge(int hc, int c, int m, int hr, int r) {
    (void)hc;
    (void)c;
    (void)m;
    (void)hr;
    (void)r;
    stdio_nope("FRAMEBUFFER MERGE");
}
void host_framebuffer_sync(void) {}
void host_framebuffer_wait(void) {}
void host_framebuffer_copy(char f, char t, int bg) {
    (void)f;
    (void)t;
    (void)bg;
    stdio_nope("FRAMEBUFFER COPY");
}

/* Pixel read (for fun_pixel). No framebuffer → returns 0. */
uint32_t host_runtime_get_pixel(int x, int y) {
    (void)x;
    (void)y;
    return 0;
}

/* host_fb drawing primitives — error on any pixel op. */
void host_fb_put_pixel(int x, int y, int c) {
    (void)x;
    (void)y;
    (void)c;
    stdio_nope("PIXEL");
}
void host_fb_draw_rectangle(int x1, int y1, int x2, int y2, int c) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)c;
    stdio_nope("BOX/LINE");
}
void host_fb_draw_bitmap(int x1, int y1, int w, int h, int s, int fc, int bc, unsigned char * bmp) {
    (void)x1;
    (void)y1;
    (void)w;
    (void)h;
    (void)s;
    (void)fc;
    (void)bc;
    (void)bmp;
    stdio_nope("BITMAP");
}
void host_fb_scroll_lcd(int lines) {
    (void)lines;
}
void host_fb_read_buffer(int x1, int y1, int x2, int y2, unsigned char * c) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)c;
}
void host_fb_write_screenshot(const char * path) {
    (void)path;
}

int host_fb_width = 0;
int host_fb_height = 0;

/* ------------------------------------------------------------- */
/*  FASTGFX / framebuffer back-buffer — null stubs.               */
/* ------------------------------------------------------------- */
void host_fastgfx_reset_state(void) {}

void bc_fastgfx_reset(void) {}
void bc_fastgfx_create(int fps, int bg) {
    (void)fps;
    (void)bg;
    stdio_nope("FASTGFX CREATE");
}
void bc_fastgfx_close(void) {}
void bc_fastgfx_swap(void) {
    stdio_nope("FASTGFX SWAP");
}
void bc_fastgfx_sync(void) {}
void bc_fastgfx_set_fps(int fps) {
    (void)fps;
}
void cmd_fastgfx(void) {
    stdio_nope("FASTGFX");
}
void cmd_framebuffer(void) {
    stdio_nope("FRAMEBUFFER");
}

/* ------------------------------------------------------------- */
/*  Editor — not linked; cmd_edit is a BASIC command that prompts */
/*  for interactive editing.  Error on mmbasic_stdio.             */
/* ------------------------------------------------------------- */
void cmd_edit(void) {
    stdio_nope("EDIT");
}
void cmd_editfile(void) {
    stdio_nope("EDIT FILE");
}
int editactive = 0;
unsigned char * StartEditPoint = NULL;
int StartEditChar = 0;

/* flash_prog_buf backs flash_progmemory on host ports — the
 * buffer MMBasic tokenises into.  First half is program area,
 * second half mimics erased flash (0xFF) so PrepareProgramExt
 * can find the CFunction terminator.  host_main.c does this in
 * its argv parser; mmbasic_stdio has no argv parser, so main.c
 * initialises it directly before InitBasic(). */
unsigned char flash_prog_buf[MAX_PROG_SIZE * 2];
