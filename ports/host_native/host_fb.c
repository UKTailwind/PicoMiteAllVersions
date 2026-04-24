/*
 * host_fb.c -- Host-side framebuffer + FRAMEBUFFER/LAYER/FASTGFX backend.
 *
 * Consolidates the pixel plane state, the WriteBuf dispatch, the
 * FRAMEBUFFER CREATE/WRITE/CLOSE/MERGE/SYNC/WAIT/COPY implementations,
 * and the DrawRectangle/DrawBitmap/ScrollLCD function-pointer backings
 * that used to live in host_stubs_legacy.c + host_framebuffer_backend.h.
 *
 * Drawing primitives and cmd_* handlers in host_stubs_legacy.c still
 * consume host_fb_put_pixel / host_fb_fill_rect / host_fb_current_target
 * via host_fb.h until Phase 2 moves them to Draw.c.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "host_fb.h"
#include "host_time.h"
#ifdef MMBASIC_SIM
#include "host_sim_server.h"  /* host_sim_emit_* / host_sim_cmds_target_is_front */
#endif

/* ------------------------------------------------------------------------
 * State.
 *
 * host_framebuffer is the visible / front plane; the FRAMEBUFFER CREATE
 * and LAYER commands allocate the back buffers (host_fb_framebuffer,
 * host_fb_layerbuffer) lazily. host_fastgfx_back is the FASTGFX back
 * buffer, non-static because the FASTGFX swap stub in stubs points
 * WriteBuf directly at it.
 * ------------------------------------------------------------------------ */

/* MMBasic's graphics-target pointers. Memory.h declares WriteBuf /
 * FrameBuf / LayerBuf / ShadowBuf in the PICOMITE (non-VGA) host path
 * but not DisplayBuf — that one is owned by host_stubs_legacy.c. Re-
 * declare here so every backend function below can see them. */
extern unsigned char *DisplayBuf;

uint32_t *host_framebuffer = NULL;
/* PicoCalc is a 320x320 square IPS LCD. */
int host_fb_width  = 320;
int host_fb_height = 320;

/* See host_fb.h. Bumped by every path that mutates the visible plane;
 * read by the WASM rAF loop to skip redundant putImageData calls. */
volatile uint32_t host_fb_generation = 0;

uint32_t *host_fastgfx_back = NULL;

static uint32_t *host_fb_framebuffer = NULL;      /* FRAMEBUFFER CREATE back plane */
static uint32_t *host_fb_layerbuffer = NULL;      /* LAYER back plane */
static uint32_t  host_fb_layer_transparent = 0;
static uint64_t  host_fb_next_merge_us = 0;
static uint32_t *host_fb_copy_src = NULL;
static uint32_t *host_fb_copy_dst = NULL;
static int       host_fb_copy_pending = 0;

/* ------------------------------------------------------------------------
 * Internal helpers.
 * ------------------------------------------------------------------------ */

static inline int host_clamp_int(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void host_fb_ensure(void) {
    size_t pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    if (!host_framebuffer) {
        host_framebuffer = calloc(pixels, sizeof(*host_framebuffer));
    }
}

uint32_t host_fb_colour24(int c) {
    return (uint32_t)c & 0x00FFFFFFu;
}

static void host_fb_bind_display(void) {
    if (!host_framebuffer) return;
    DisplayBuf = (unsigned char *)host_framebuffer;
    if (FrameBuf == NULL) FrameBuf = DisplayBuf;
    if (LayerBuf == NULL) LayerBuf = DisplayBuf;
}

static uint32_t *host_fb_buffer_for_target(unsigned char *target) {
    if (!host_framebuffer) host_fb_ensure();
    if (!host_framebuffer) return NULL;
    host_fb_bind_display();
    if (target == NULL || target == DisplayBuf) return host_framebuffer;
    if (target == FrameBuf && host_fb_framebuffer) return host_fb_framebuffer;
    if (target == LayerBuf && host_fb_layerbuffer) return host_fb_layerbuffer;
    if (host_fastgfx_back && target == (unsigned char *)host_fastgfx_back)
        return host_fastgfx_back;
    return host_framebuffer;
}

uint32_t *host_fb_current_target(void) {
    return host_fb_buffer_for_target(WriteBuf);
}

static void host_fb_fill_buffer(uint32_t *buffer, uint32_t colour) {
    size_t pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    if (!buffer) return;
    for (size_t i = 0; i < pixels; ++i) buffer[i] = colour;
}

static void host_fb_merge_now(uint32_t transparent) {
    size_t pixels;
    if (!host_framebuffer) host_fb_ensure();
    if (!host_framebuffer || !host_fb_framebuffer || !host_fb_layerbuffer) return;
    pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    for (size_t i = 0; i < pixels; ++i) {
        uint32_t layer = host_fb_layerbuffer[i];
        host_framebuffer[i] = (layer == transparent) ? host_fb_framebuffer[i] : layer;
    }
    host_fb_bump_generation();
}

static void host_fb_copy_now(uint32_t *src, uint32_t *dst) {
    size_t pixels;
    if (!src || !dst) return;
    pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    memcpy(dst, src, pixels * sizeof(*dst));
    if (dst == host_framebuffer) host_fb_bump_generation();
#ifdef MMBASIC_SIM
    /* If we just wrote the front buffer, tell the browser — otherwise
     * FRAMEBUFFER COPY F,N updates pixels locally but nothing reaches
     * the WS client. One BLIT per presented frame, same as FASTGFX. */
    if (dst == host_framebuffer) {
        host_sim_emit_blit(0, 0, host_fb_width, host_fb_height, dst);
    }
#endif
}

static void host_fb_complete_pending_copy(void) {
    if (!host_fb_copy_pending) return;
    host_fb_copy_now(host_fb_copy_src, host_fb_copy_dst);
    host_fb_copy_src = NULL;
    host_fb_copy_dst = NULL;
    host_fb_copy_pending = 0;
}

/* ------------------------------------------------------------------------
 * Pixel / rect primitives — callable from stubs (drawing helpers that
 * will be deleted when Draw.c lands in Phase 2).
 * ------------------------------------------------------------------------ */

void host_fb_put_pixel(int x, int y, int c) {
    uint32_t *target = host_fb_current_target();
    if (!target) return;
    if (x < 0 || y < 0 || x >= host_fb_width || y >= host_fb_height) return;
    target[(size_t)y * (size_t)host_fb_width + (size_t)x] = host_fb_colour24(c);
    if (target == host_framebuffer) host_fb_bump_generation();
#ifdef MMBASIC_SIM
    host_sim_emit_pixel(x, y, c);
#endif
}

void host_fb_fill_rect(int x1, int y1, int x2, int y2, int c) {
    uint32_t *target = host_fb_current_target();
    if (!target) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    x1 = host_clamp_int(x1, 0, host_fb_width - 1);
    x2 = host_clamp_int(x2, 0, host_fb_width - 1);
    y1 = host_clamp_int(y1, 0, host_fb_height - 1);
    y2 = host_clamp_int(y2, 0, host_fb_height - 1);
    if (x1 > x2 || y1 > y2) return;

    uint32_t colour = host_fb_colour24(c);
    for (int y = y1; y <= y2; ++y) {
        uint32_t *row = target + (size_t)y * (size_t)host_fb_width;
        for (int x = x1; x <= x2; ++x) {
            row[x] = colour;
        }
    }
    if (target == host_framebuffer) host_fb_bump_generation();
#ifdef MMBASIC_SIM
    host_sim_emit_rect(x1, y1, x2, y2, c);
#endif
}

/* ------------------------------------------------------------------------
 * DrawRectangle / DrawBitmap / ScrollLCD function-pointer backings.
 * Assigned in host_runtime_begin so gfx_console_shared.c (PrintChar,
 * DisplayPutC) dispatches into these.
 * ------------------------------------------------------------------------ */

void host_fb_draw_rectangle(int x1, int y1, int x2, int y2, int c) {
    /* Device DrawRectangle{16,SPISCR,…} fills [x1,x2] × [y1,y2] — inclusive
     * on both endpoints (see Draw.c:DrawRectangle16 `for(y=y1;y<=y2;y++)`).
     * Draw.c's DrawBox / DrawRBox / ClearScreen all rely on that
     * convention, so we must not decrement here. */
    host_fb_fill_rect(x1, y1, x2, y2, c);
}

void host_fb_draw_bitmap(int x1, int y1, int width, int height, int scale,
                         int fc, int bc, unsigned char *bitmap) {
    uint32_t *target = host_fb_current_target();
    if (!target || !bitmap) return;
    if (scale < 1) scale = 1;
    int total_bits = width * height;
    uint32_t fg = host_fb_colour24(fc);
    uint32_t bg = host_fb_colour24(bc);
    int want_bg = (bc >= 0);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            int bit_number = row * width + col;
            int on = (bitmap[bit_number / 8] >>
                      ((total_bits - bit_number - 1) % 8)) & 1;
            if (on) {
                host_fb_fill_rect(x1 + col * scale,
                                  y1 + row * scale,
                                  x1 + (col + 1) * scale - 1,
                                  y1 + (row + 1) * scale - 1,
                                  (int)fg);
            } else if (want_bg) {
                host_fb_fill_rect(x1 + col * scale,
                                  y1 + row * scale,
                                  x1 + (col + 1) * scale - 1,
                                  y1 + (row + 1) * scale - 1,
                                  (int)bg);
            }
        }
    }
    (void)total_bits; (void)fg; (void)bg;
}

void host_fb_read_buffer(int x1, int y1, int x2, int y2, unsigned char *c) {
    /* Backs the ReadBuffer function pointer. Device ReadBuffer (e.g.
     * ReadBuffer555 in Draw.c) writes THREE bytes per pixel packed as
     * B, G, R — the BMP on-wire order. Every shared caller assumes that
     * stride (SAVE IMAGE / BLIT READ / TRIANGLE SAVE / GUI LCD scans).
     *
     * fun_pixel passes a 1x1 rect and a 4-byte int; it only reads the
     * low 24 bits, so it doesn't care that we leave the fourth byte
     * untouched — matches device behaviour exactly. */
    if (!c) return;
    if (!host_framebuffer) host_fb_ensure();
    if (!host_framebuffer) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            uint32_t px = 0;
            if (x >= 0 && y >= 0 && x < host_fb_width && y < host_fb_height) {
                px = host_framebuffer[(size_t)y * (size_t)host_fb_width + (size_t)x];
            }
            c[0] = (unsigned char)(px & 0xFF);          /* B */
            c[1] = (unsigned char)((px >> 8) & 0xFF);   /* G */
            c[2] = (unsigned char)((px >> 16) & 0xFF);  /* R */
            c += 3;
        }
    }
}

void host_fb_scroll_lcd(int lines) {
    /* Positive lines: shift content UP by that many pixel rows, clear bottom.
     * Negative lines: shift content DOWN, clear top. The Editor uses the
     * negative form in ScrollDown to reveal earlier lines when the user
     * page-ups past the top of the viewport. */
    host_fb_ensure();
    if (!host_framebuffer || lines == 0) return;
#ifdef MMBASIC_SIM
    host_sim_emit_scroll(lines, gui_bcolour);
#endif

    int row_pixels = host_fb_width;
    uint32_t fill = host_fb_colour24(gui_bcolour);
    int abs_lines = lines < 0 ? -lines : lines;
    if (abs_lines >= host_fb_height) {
        host_fb_fill_buffer(host_framebuffer, fill);
        return;
    }

    if (lines > 0) {
        memmove(host_framebuffer,
                host_framebuffer + lines * row_pixels,
                (size_t)(host_fb_height - lines) * (size_t)row_pixels * sizeof(uint32_t));
        uint32_t *tail = host_framebuffer + (host_fb_height - lines) * row_pixels;
        for (int i = 0; i < lines * row_pixels; ++i) tail[i] = fill;
    } else {
        memmove(host_framebuffer + abs_lines * row_pixels,
                host_framebuffer,
                (size_t)(host_fb_height - abs_lines) * (size_t)row_pixels * sizeof(uint32_t));
        for (int i = 0; i < abs_lines * row_pixels; ++i) host_framebuffer[i] = fill;
    }
    host_fb_bump_generation();
}

/* ------------------------------------------------------------------------
 * Public FRAMEBUFFER API.
 * ------------------------------------------------------------------------ */

void host_framebuffer_reset_runtime(int colour) {
    if (!host_framebuffer) host_fb_ensure();
    if (host_fb_framebuffer) free(host_fb_framebuffer);
    if (host_fb_layerbuffer) free(host_fb_layerbuffer);
    host_fb_framebuffer = NULL;
    host_fb_layerbuffer = NULL;
    host_fb_layer_transparent = 0;
    mergerunning = 0;
    mergetimer = 0;
    host_fb_next_merge_us = 0;
    host_fb_copy_src = NULL;
    host_fb_copy_dst = NULL;
    host_fb_copy_pending = 0;
    host_fb_bind_display();
    FrameBuf = DisplayBuf;
    LayerBuf = DisplayBuf;
    WriteBuf = NULL;
    host_fb_fill_buffer(host_framebuffer, host_fb_colour24(colour));
    host_fb_bump_generation();
}

void host_framebuffer_shutdown_runtime(void) {
    mergerunning = 0;
    mergetimer = 0;
    host_fb_next_merge_us = 0;
    host_fb_copy_src = NULL;
    host_fb_copy_dst = NULL;
    host_fb_copy_pending = 0;
    if (host_fb_framebuffer) free(host_fb_framebuffer);
    if (host_fb_layerbuffer) free(host_fb_layerbuffer);
    host_fb_framebuffer = NULL;
    host_fb_layerbuffer = NULL;
    host_fb_layer_transparent = 0;
    host_fb_bind_display();
    if (DisplayBuf != NULL) {
        FrameBuf = DisplayBuf;
        LayerBuf = DisplayBuf;
    }
    WriteBuf = NULL;
}

void host_framebuffer_clear_target(int colour) {
    uint32_t *target = host_fb_current_target();
    host_fb_fill_buffer(target, host_fb_colour24(colour));
    if (target == host_framebuffer) host_fb_bump_generation();
#ifdef MMBASIC_SIM
    if (host_sim_cmds_target_is_front()) host_sim_emit_cls(colour);
#endif
}

void host_framebuffer_create(void) {
    size_t pixels;
    if (!host_framebuffer) host_fb_ensure();
    host_fb_bind_display();
    if (FrameBuf != NULL && FrameBuf != DisplayBuf) error("Framebuffer already exists");
    pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    host_fb_framebuffer = calloc(pixels, sizeof(*host_fb_framebuffer));
    if (!host_fb_framebuffer) error("Not enough memory");
    FrameBuf = (unsigned char *)host_fb_framebuffer;
}

void host_framebuffer_layer(int has_colour, int colour) {
    size_t pixels;
    uint32_t fill = has_colour ? host_fb_colour24(colour) : 0;
    if (!host_framebuffer) host_fb_ensure();
    host_fb_bind_display();
    if (LayerBuf != NULL && LayerBuf != DisplayBuf) error("Framebuffer already exists");
    pixels = (size_t)host_fb_width * (size_t)host_fb_height;
    host_fb_layerbuffer = calloc(pixels, sizeof(*host_fb_layerbuffer));
    if (!host_fb_layerbuffer) error("Not enough memory");
    host_fb_layer_transparent = fill;
    host_fb_fill_buffer(host_fb_layerbuffer, fill);
    LayerBuf = (unsigned char *)host_fb_layerbuffer;
}

void host_framebuffer_write(char which) {
    host_fb_bind_display();
    switch ((char)toupper((unsigned char)which)) {
        case 'N':
            if (mergerunning) error("Display in use for merged operation");
            WriteBuf = NULL;
            return;
        case 'F':
            if (FrameBuf == NULL || FrameBuf == DisplayBuf) error("Frame buffer not created");
            WriteBuf = FrameBuf;
            return;
        case 'L':
            if (LayerBuf == NULL || LayerBuf == DisplayBuf) error("Layer buffer not created");
            WriteBuf = LayerBuf;
            return;
        default:
            error("Syntax");
    }
}

void host_framebuffer_close(char which) {
    host_fb_bind_display();
    if (which == 0 || which == BC_FB_TARGET_DEFAULT) which = 'A';
    which = (char)toupper((unsigned char)which);
    mergerunning = 0;
    mergetimer = 0;
    host_fb_next_merge_us = 0;
    host_fb_copy_src = NULL;
    host_fb_copy_dst = NULL;
    host_fb_copy_pending = 0;
    if (which == 'A' || which == 'F') {
        if (WriteBuf == FrameBuf) WriteBuf = NULL;
        if (host_fb_framebuffer) free(host_fb_framebuffer);
        host_fb_framebuffer = NULL;
        FrameBuf = DisplayBuf;
    }
    if (which == 'A' || which == 'L') {
        if (WriteBuf == LayerBuf) WriteBuf = NULL;
        if (host_fb_layerbuffer) free(host_fb_layerbuffer);
        host_fb_layerbuffer = NULL;
        LayerBuf = DisplayBuf;
    }
    if (which != 'A' && which != 'F' && which != 'L') error("Syntax");
}

void host_framebuffer_merge(int has_colour, int colour,
                            int mode, int has_rate, int rate_ms) {
    uint32_t transparent = has_colour ? host_fb_colour24(colour) : 0;
    if (LayerBuf == NULL || LayerBuf == DisplayBuf) error("Layer not created");
    if (FrameBuf == NULL || FrameBuf == DisplayBuf) error("Framebuffer not created");
    if (has_rate && rate_ms < 0) error("Number out of bounds");
    switch (mode) {
        case BC_FB_MERGE_MODE_NOW:
        case BC_FB_MERGE_MODE_B:
            mergerunning = 0;
            mergetimer = 0;
            host_fb_next_merge_us = 0;
            host_fb_merge_now(transparent);
            return;
        case BC_FB_MERGE_MODE_R:
            host_fb_layer_transparent = transparent;
            mergerunning = 1;
            mergetimer = (uint32_t)(has_rate ? rate_ms : 0);
            if (WriteBuf == NULL || WriteBuf == DisplayBuf) WriteBuf = FrameBuf;
            host_fb_merge_now(transparent);
            host_fb_next_merge_us = host_now_us() + (uint64_t)mergetimer * 1000ULL;
            return;
        case BC_FB_MERGE_MODE_A:
            mergerunning = 0;
            mergetimer = 0;
            host_fb_next_merge_us = 0;
            return;
        default:
            error("Syntax");
    }
}

void host_framebuffer_sync(void) {
    host_fb_complete_pending_copy();
    if (!mergerunning) return;
    host_fb_merge_now(host_fb_layer_transparent);
    host_fb_next_merge_us = host_now_us() + (uint64_t)mergetimer * 1000ULL;
}

void host_framebuffer_wait(void) {
    host_fb_complete_pending_copy();
}

void host_framebuffer_copy(char from, char to, int background) {
    uint32_t *src = NULL;
    uint32_t *dst = NULL;

    host_fb_bind_display();
    from = (char)toupper((unsigned char)from);
    to = (char)toupper((unsigned char)to);

    if (from == 'N') src = host_framebuffer;
    else if (from == 'F') {
        if (FrameBuf == NULL || FrameBuf == DisplayBuf) error("Frame buffer not created");
        src = host_fb_framebuffer;
    } else if (from == 'L') {
        if (LayerBuf == NULL || LayerBuf == DisplayBuf) error("Layer buffer not created");
        src = host_fb_layerbuffer;
    } else error("Syntax");

    if (to == 'N') dst = host_framebuffer;
    else if (to == 'F') {
        if (FrameBuf == NULL || FrameBuf == DisplayBuf) error("Frame buffer not created");
        dst = host_fb_framebuffer;
    } else if (to == 'L') {
        if (LayerBuf == NULL || LayerBuf == DisplayBuf) error("Layer buffer not created");
        dst = host_fb_layerbuffer;
    } else error("Syntax");

    if (src == dst) return;
    if (background && dst == host_framebuffer) {
        host_fb_copy_src = src;
        host_fb_copy_dst = dst;
        host_fb_copy_pending = 1;
        return;
    }
    host_fb_copy_now(src, dst);
}

void host_framebuffer_service(void) {
    uint64_t now;
    host_fb_complete_pending_copy();
    if (!mergerunning) return;
    now = host_now_us();
    if (mergetimer != 0 && now < host_fb_next_merge_us) return;
    host_fb_merge_now(host_fb_layer_transparent);
    if (mergetimer != 0) host_fb_next_merge_us = now + (uint64_t)mergetimer * 1000ULL;
}

/* ------------------------------------------------------------------------
 * Test harness + simulator-server accessors.
 * ------------------------------------------------------------------------ */

uint32_t host_runtime_get_pixel(int x, int y) {
    if (!host_framebuffer) host_fb_ensure();
    if (!host_framebuffer) return 0;
    if (x < 0 || y < 0 || x >= host_fb_width || y >= host_fb_height) return 0;
    return host_framebuffer[(size_t)y * (size_t)host_fb_width + (size_t)x];
}

int host_runtime_width(void)  { return host_fb_width; }
int host_runtime_height(void) { return host_fb_height; }

/*
 * The --sim build runs a background Mongoose thread that reads the
 * framebuffer and pushes RGBA frames over WebSocket. The MMBasic thread
 * writes the buffer without locking; at worst a torn frame is visible
 * for 16ms before the next broadcast overwrites it.
 */
size_t host_sim_framebuffer_copy(uint32_t *dst, size_t dst_pixels) {
    if (!host_framebuffer) host_fb_ensure();
    if (!host_framebuffer || !dst) return 0;
    size_t have = (size_t)host_fb_width * (size_t)host_fb_height;
    size_t n = dst_pixels < have ? dst_pixels : have;
    memcpy(dst, host_framebuffer, n * sizeof(*dst));
    return n;
}

void host_sim_framebuffer_dims(int *w, int *h) {
    if (w) *w = host_fb_width;
    if (h) *h = host_fb_height;
}

/* Override the simulated display resolution. Must be called before
 * host_sim_server_start / any draw call — the framebuffer is allocated
 * lazily on first use, so we just need to set the dimensions before that
 * happens. HRes/VRes mirror host_fb_width/height so MMBasic's geometry
 * math (Option.Width = HRes/font_width, etc.) sees the same values. */
void host_sim_set_framebuffer_size(int w, int h) {
    if (w < 80)   w = 80;
    if (h < 60)   h = 60;
    if (w > 2048) w = 2048;
    if (h > 2048) h = 2048;
    host_fb_width  = w;
    host_fb_height = h;
    HRes = (short)w;
    VRes = (short)h;
}

void host_fb_write_screenshot(const char *path) {
    if (!path || !*path) return;
    host_fb_ensure();
    if (!host_framebuffer) return;

    FILE *fp = fopen(path, "wb");
    if (!fp) return;

    fprintf(fp, "P6\n%d %d\n255\n", host_fb_width, host_fb_height);
    for (int y = 0; y < host_fb_height; ++y) {
        for (int x = 0; x < host_fb_width; ++x) {
            uint32_t pixel = host_framebuffer[(size_t)y * (size_t)host_fb_width + (size_t)x];
            unsigned char rgb[3] = {
                (unsigned char)((pixel >> 16) & 0xFF),
                (unsigned char)((pixel >> 8) & 0xFF),
                (unsigned char)(pixel & 0xFF),
            };
            fwrite(rgb, 1, 3, fp);
        }
    }
    fclose(fp);
}
