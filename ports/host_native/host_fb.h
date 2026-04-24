#ifndef HOST_FB_H
#define HOST_FB_H

#include <stdint.h>
#include <stddef.h>

/*
 * Host-side framebuffer + FRAMEBUFFER/LAYER/FASTGFX backend.
 *
 * The test-harness host has a single 24-bit RGB pixel plane that stands
 * in for the PicoCalc's 4-bit-packed LCD. When MMBasic's FRAMEBUFFER
 * CREATE / LAYER / FASTGFX commands allocate back buffers, they live
 * here; the drawing primitives in host_stubs_legacy.c (until Phase 2
 * moves them to the shared Draw.c) route pixels through the WriteBuf
 * dispatch implemented below.
 *
 * State lives in host_fb.c; this header exposes just the surface needed
 * by host_stubs_legacy.c (drawing primitives, runtime lifecycle),
 * host_main.c (test harness, pixel assert), and host_sim_server.c
 * (framebuffer snapshot for the WebSocket bootstrap frame).
 */

/* Front (visible) framebuffer — the pixel plane broadcast by the sim
 * server and captured by test-harness pixel asserts. Non-static so the
 * FASTGFX swap / screenshot helpers in host_stubs_legacy.c can memcpy
 * into/out of it. */
extern uint32_t *host_framebuffer;

/* FASTGFX back buffer — non-static so drawing primitives and cmd_fastgfx
 * helpers can recognize it as a valid WriteBuf target. */
extern uint32_t *host_fastgfx_back;

/* Framebuffer geometry. host_fb_width / host_fb_height are the live
 * dimensions and also mirrored into HRes/VRes by host_sim_set_framebuffer_size. */
extern int host_fb_width;
extern int host_fb_height;

/* Monotonic counter bumped whenever the visible front plane changes
 * (put_pixel / fill_rect / scroll / FASTGFX SWAP / FRAMEBUFFER write/
 * copy/merge). The WASM render loop reads this to skip putImageData when
 * nothing has changed since the last rAF — keeps idle REPL / PAUSE-heavy
 * programs from wasting main-thread time on redundant blits. Non-writing
 * code paths (FASTGFX back-plane draws) deliberately don't bump it; the
 * SWAP is the event the browser actually needs to repaint. */
extern volatile uint32_t host_fb_generation;
static inline void host_fb_bump_generation(void) { host_fb_generation++; }

/* Lazy allocator — first call allocates host_fb_width * host_fb_height
 * uint32_ts. All drawing paths call this before touching the buffer. */
void host_fb_ensure(void);

/* 24-bit RGB mask (strips alpha/overflow); pixel write that routes
 * through the WriteBuf dispatch and records a SIM event on the front
 * framebuffer only. */
uint32_t host_fb_colour24(int c);
uint32_t *host_fb_current_target(void);
void host_fb_put_pixel(int x, int y, int c);

/* Rectangle fill (axis-aligned). Records one sim RECT event when the
 * target is the front framebuffer. */
void host_fb_fill_rect(int x1, int y1, int x2, int y2, int c);

/* Public framebuffer API — back MMBasic's FRAMEBUFFER {CREATE|LAYER|
 * WRITE|CLOSE|MERGE|SYNC|WAIT|COPY} commands. Called from both the
 * interpreter cmd_framebuffer stub and the VM syscall dispatch. */
void host_framebuffer_reset_runtime(int colour);
void host_framebuffer_shutdown_runtime(void);
void host_framebuffer_clear_target(int colour);
void host_framebuffer_create(void);
void host_framebuffer_layer(int has_colour, int colour);
void host_framebuffer_write(char which);
void host_framebuffer_close(char which);
void host_framebuffer_merge(int has_colour, int colour,
                            int mode, int has_rate, int rate_ms);
void host_framebuffer_sync(void);
void host_framebuffer_wait(void);
void host_framebuffer_copy(char from, char to, int background);
void host_framebuffer_service(void);

/* Host-side backings for DrawRectangle / DrawBitmap / ScrollLCD / Read
 * Buffer function pointers that gfx_console_shared.c's GUIPrintChar /
 * DisplayPutC dispatch through. Assigned in host_runtime_begin.
 *
 * host_fb_read_buffer backs the ReadBuffer function pointer that
 * fun_pixel / fun_map / FRAMEBUFFER COPY N use to sample the visible
 * plane. It writes one 4-byte BGR-packed pixel per sample, matching
 * the device's uint32_t layout. */
void host_fb_draw_rectangle(int x1, int y1, int x2, int y2, int c);
void host_fb_draw_bitmap(int x1, int y1, int width, int height, int scale,
                         int fc, int bc, unsigned char *bitmap);
void host_fb_scroll_lcd(int lines);
void host_fb_read_buffer(int x1, int y1, int x2, int y2, unsigned char *c);

/* Test harness pixel assert + sim server snapshot. */
uint32_t host_runtime_get_pixel(int x, int y);
int  host_runtime_width(void);
int  host_runtime_height(void);
size_t host_sim_framebuffer_copy(uint32_t *dst, size_t dst_pixels);
void host_sim_framebuffer_dims(int *w, int *h);
void host_sim_set_framebuffer_size(int w, int h);

/* PPM screenshot dump — test harness --screenshot hook. No-op if path
 * is NULL/empty. Writes at most once per host_runtime_begin call. */
void host_fb_write_screenshot(const char *path);

#endif
