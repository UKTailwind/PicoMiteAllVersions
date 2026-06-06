/*
 * drivers/vga_pio/vga_ops_stub.c — no-op stubs for the hal_vga_ops
 * surface on non-VGA targets (PICOMITE, WEB, host).
 *
 * Non-VGA ports don't have SCREENMODE1-5 tile framebuffers, so every
 * hook returns 0 and the caller falls through to its default path
 * (ClearScreen, DrawRectangle, …).
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "hal/hal_vga_ops.h"

#define STUB_SCREENMODE4 32
#define STUB_SCREENMODE5 33
#define STUB_SCREENMODE6 34
#define STUB_SCREENMODE7 35

extern volatile int DISPLAY_TYPE;
extern void *GetMemory(int msize);
extern void FreeMemory(unsigned char *addr);
extern void (*DrawBufferFast)(int x1, int y1, int x2, int y2, int blank, unsigned char *c);
extern void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char *c);

int hal_vga_ops_handle_cls(int c) {
    (void)c;
    return 0;
}
int hal_vga_ops_handle_tile_cls(int c) {
    (void)c;
    return 0;
}
int hal_vga_ops_handle_layer_clear(void) {
    return 0;
}
void hal_vga_ops_retile_for_font(void) {}
void hal_vga_ops_wait_scanline_zero(void) {}
uint8_t hal_vga_ops_layer_merge_byte(uint8_t primary, int x, int y) {
    (void)x;
    (void)y;
    return primary;
}
uint8_t hal_vga_ops_layer_merge_rgb8(uint8_t primary, int x, int y) {
    (void)x;
    (void)y;
    return primary;
}
volatile unsigned char * hal_vga_ops_fb_n_target(void) {
    return NULL;
}
volatile unsigned char * hal_vga_ops_fb_t_target(void) {
    return NULL;
}
int hal_vga_ops_fb_t_supported(void) {
    return 0;
}
int hal_vga_ops_fb2_tilematch(int x1, int y1, int w_px, int h_px) {
    (void)x1;
    (void)y1;
    (void)w_px;
    (void)h_px;
    return 0;
}
void hal_vga_ops_fb2_fill_tile_colours(int x1, int y1, int w_px, int h_px, int fc, int bc) {
    (void)x1;
    (void)y1;
    (void)w_px;
    (void)h_px;
    (void)fc;
    (void)bc;
}
void hal_vga_ops_scroll_tile_colours(int lines) {
    (void)lines;
}
void hal_vga_ops_tile_colour(int x, int y, int * front, int * back) {
    (void)x;
    (void)y;
    *front = 0xFFFFFF;
    *back = 0;
}
int hal_vga_ops_handle_blit_move(int x1, int y1, int x2, int y2, int w, int h) {
    if (DISPLAY_TYPE == STUB_SCREENMODE4 || DISPLAY_TYPE == STUB_SCREENMODE5 ||
        DISPLAY_TYPE == STUB_SCREENMODE6 || DISPLAY_TYPE == STUB_SCREENMODE7) {
        const int bytes_per_pixel = DISPLAY_TYPE == STUB_SCREENMODE4 ? 2 : 1;
        unsigned char *buff = GetMemory(h * bytes_per_pixel);
        if (x1 >= x2) {
            while (w-- > 0) {
                ReadBufferFast(x1, y1, x1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2, y2 + h - 1, -1, buff);
                x1++;
                x2++;
            }
        } else {
            x1 += w - 1;
            x2 += w - 1;
            while (w-- > 0) {
                ReadBufferFast(x1, y1, x1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2, y2 + h - 1, -1, buff);
                x1--;
                x2--;
            }
        }
        FreeMemory(buff);
        return 1;
    }
    return 0;
}
void hal_vga_ops_reset_display_vga(void) {}
void hal_vga_ops_reserved_io_recovery(void) {}

/* VGA-memory stubs — the real impls live in drivers/vga_pio/vga_memory.c
 * for PICOMITEVGA builds. Non-VGA targets (PICOMITE SPI-LCD, WEB, host)
 * get the no-op/null versions below so Memory.c + display-merge HAL
 * stubs + vm_sys_graphics reference these symbols unconditionally. */
unsigned char * WriteBuf = NULL;
unsigned char * LayerBuf = NULL;
unsigned char * FrameBuf = NULL;
unsigned char * ShadowBuf = NULL;
int fb_dma_chan = -1;

/* No VGA framebuffer on SPI-LCD / WEB / host — FRAMEBUFFER is NULL,
 * framebuffersize is 0. Memory.c's AllMemory is sized via
 * HAL_PORT_FRAMEBUFFER_TRAILER_BYTES which is 0 on non-VGA ports, so
 * there's no trailer region to point into. */
unsigned char * FRAMEBUFFER = NULL;
uint32_t framebuffersize = 0;

/* ytileheight is read by MM_Misc.c's MM.INFO("TILE HEIGHT") on every
 * target. VGA builds set it to 16 or 480/12; non-VGA default is 0. */
volatile int ytileheight = 0;

/* Called from Memory.c::InitHeap — no-op on non-VGA (no framebuffer
 * planes to rebind). */
void vga_memory_init_planes(void) {}

/* setmode is a VGA-only entry for switching SCREENMODE (1-5) live.
 * Draw.h extern-declares it unconditionally; Commands.c calls it from
 * cmd_new / cmd_end. Non-VGA ports don't have QVGA SCREENMODEs and
 * Option.DISPLAY_TYPE never takes a SCREENMODE value there, so the
 * stub is safely unreachable at runtime. */
__attribute__((weak)) void setmode(int mode, bool clear) {
    (void)mode;
    (void)clear;
}
