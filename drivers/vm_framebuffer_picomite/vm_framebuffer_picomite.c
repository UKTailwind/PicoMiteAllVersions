/*
 * drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c — SPI-LCD
 * implementation of the VM FRAMEBUFFER command HAL.
 *
 * Linked only on PICOMITE variants (PICO, PICOUSB, PICORP2350,
 * PICOUSBRP2350). Drives the core1 merge pipeline via the
 * hal_display_merge_* hooks + owns the VM-side merge/copy scratch
 * state that was previously embedded in vm_sys_graphics.c's
 * `#elif defined(PICOMITE)` branches.
 */

#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "bc_alloc.h"
#include "hal/hal_vm_framebuffer.h"
#include "hal/hal_display_merge.h"
#include "hardware/dma.h"

/* --- VM-side merge / copy state ----------------------------------------- */
static int vm_fb_merge_running = 0;
static uint8_t vm_fb_merge_colour = 0;
static uint32_t vm_fb_merge_interval_us = 0;
static uint8_t * vm_fb_copy_src = NULL;
static int vm_fb_copy_pending = 0;

/* Shared scanline scratch for the merge + copy fallback paths. */
static uint8_t * vm_fb_linebuf = NULL;
static size_t vm_fb_linebuf_bytes = 0;

static uint8_t * vm_fb_reserve_line(size_t bytes) {
    if (bytes == 0) return NULL;
    if (vm_fb_linebuf != NULL && vm_fb_linebuf_bytes >= bytes) return vm_fb_linebuf;
    uint8_t * p = (uint8_t *)BC_ALLOC(bytes);
    if (!p) error("NEM[gfx:scratch] want=%", (int)bytes);
    if (vm_fb_linebuf) BC_FREE(vm_fb_linebuf);
    vm_fb_linebuf = p;
    vm_fb_linebuf_bytes = bytes;
    return vm_fb_linebuf;
}

/* --- Helpers ------------------------------------------------------------- */
static size_t vm_fb_bytes(void) {
    return (size_t)HRes * (size_t)VRes / 2u;
}

static uint8_t vm_fb_rgb121(uint32_t c) {
    return (uint8_t)(((c & 0x800000u) >> 20) |
                     ((c & 0x00C000u) >> 13) |
                     ((c & 0x000080u) >> 7));
}

static void vm_fb_stop_merge(void) {
    hal_display_merge_abort();
    vm_fb_merge_running = 0;
    mergerunning = 0;
    mergetimer = 0;
    vm_fb_merge_interval_us = 0;
}

static void vm_fb_copy_to_screen(uint8_t * src) {
    if (src == NULL) return;
    copyframetoscreen(src, 0, HRes - 1, 0, VRes - 1, 0);
}

static void vm_fb_complete_pending_copy(void) {
    if (!vm_fb_copy_pending || vm_fb_copy_src == NULL) return;
    vm_fb_copy_to_screen(vm_fb_copy_src);
    vm_fb_copy_src = NULL;
    vm_fb_copy_pending = 0;
}

static void vm_fb_merge_now(uint8_t transparent) {
    int stride;
    uint8_t * linebuf;
    uint8_t * src_layer;
    uint8_t * src_frame;
    int y;

    if (LayerBuf == NULL || FrameBuf == NULL) return;

    if (ShadowBuf != NULL) {
        merge_optimized(transparent);
        return;
    }

    stride = HRes / 2;
    if (stride <= 0) return;
    linebuf = vm_fb_reserve_line((size_t)stride);
    src_layer = LayerBuf;
    src_frame = FrameBuf;

    for (y = 0; y < VRes; ++y) {
        uint8_t * layer_row = src_layer + (size_t)y * (size_t)stride;
        uint8_t * frame_row = src_frame + (size_t)y * (size_t)stride;
        memcpy(linebuf, frame_row, (size_t)stride);
        for (int x = 0; x < stride; ++x) {
            uint8_t layer = layer_row[x];
            uint8_t top = layer & 0xF0u;
            uint8_t bottom = layer & 0x0Fu;
            if (top == (uint8_t)(transparent << 4) && bottom == transparent) continue;
            if (top != (uint8_t)(transparent << 4) && bottom != transparent) {
                linebuf[x] = layer;
            } else if (top != (uint8_t)(transparent << 4)) {
                linebuf[x] = (uint8_t)((linebuf[x] & 0x0Fu) | top);
            } else {
                linebuf[x] = (uint8_t)((linebuf[x] & 0xF0u) | bottom);
            }
        }
        copyframetoscreen(linebuf, 0, HRes - 1, y, y, 0);
    }
}

/* --- HAL entries --------------------------------------------------------- */

void hal_vm_framebuffer_shutdown_runtime(void) {
    vm_fb_stop_merge();
    vm_fb_copy_src = NULL;
    vm_fb_copy_pending = 0;
    if (WriteBuf == FrameBuf || WriteBuf == LayerBuf) restorepanel();
    if (ShadowBuf) BC_FREE(ShadowBuf);
    ShadowBuf = NULL;
    if (fb_dma_chan >= 0) {
        dma_channel_unclaim(fb_dma_chan);
        fb_dma_chan = -1;
    }
    if (FrameBuf) BC_FREE(FrameBuf);
    if (LayerBuf) BC_FREE(LayerBuf);
    FrameBuf = NULL;
    LayerBuf = NULL;
    WriteBuf = NULL;
    if (vm_fb_linebuf) {
        BC_FREE(vm_fb_linebuf);
        vm_fb_linebuf = NULL;
        vm_fb_linebuf_bytes = 0;
    }
}

void hal_vm_framebuffer_service(void) {
    vm_fb_complete_pending_copy();
}

void hal_vm_framebuffer_create(int fast) {
    size_t bytes;
    if (FrameBuf != NULL) error("Framebuffer already exists");
    if (HRes <= 0 || VRes <= 0) error("Display not configured");
    bytes = vm_fb_bytes();
    FrameBuf = (unsigned char *)BC_ALLOC(bytes);
    if (FrameBuf == NULL) error("NEM[gfx:fb] want=%", (int)bytes);
    memset(FrameBuf, 0, bytes);
    if (fast) {
        ShadowBuf = (unsigned char *)BC_ALLOC(bytes);
        if (ShadowBuf == NULL) error("NEM[gfx:shadow] want=%", (int)bytes);
        memset(ShadowBuf, 0, bytes);
        fb_dma_chan = dma_claim_unused_channel(true);
    }
}

void hal_vm_framebuffer_layer(int has_colour, int colour) {
    size_t bytes;
    uint8_t transparent = 0;
    if (LayerBuf != NULL) error("Framebuffer already exists");
    if (HRes <= 0 || VRes <= 0) error("Display not configured");
    bytes = vm_fb_bytes();
    if (has_colour) transparent = vm_fb_rgb121((uint32_t)colour);
    LayerBuf = (unsigned char *)BC_ALLOC(bytes);
    if (LayerBuf == NULL) error("NEM[gfx:layer] want=%", (int)bytes);
    memset(LayerBuf, (int)(transparent | (transparent << 4)), bytes);
}

void hal_vm_framebuffer_write(char which) {
    switch (which) {
    case BC_FB_TARGET_N:
        if (mergerunning) error("Display in use for merged operation");
        restorepanel();
        return;
    case BC_FB_TARGET_F:
        if (FrameBuf == NULL) error("Frame buffer not created");
        WriteBuf = FrameBuf;
        setframebuffer();
        return;
    case BC_FB_TARGET_L:
        if (LayerBuf == NULL) error("Layer buffer not created");
        WriteBuf = LayerBuf;
        setframebuffer();
        return;
    default:
        error("Syntax");
    }
}

void hal_vm_framebuffer_close(char which) {
    if (which == BC_FB_TARGET_DEFAULT) which = 'A';
    vm_fb_stop_merge();
    if ((which == 'A' || which == BC_FB_TARGET_F) && FrameBuf != NULL) {
        if (WriteBuf == FrameBuf) restorepanel();
        BC_FREE(FrameBuf);
        FrameBuf = NULL;
        if (ShadowBuf) {
            BC_FREE(ShadowBuf);
            ShadowBuf = NULL;
        }
        if (fb_dma_chan >= 0) {
            dma_channel_unclaim(fb_dma_chan);
            fb_dma_chan = -1;
        }
    }
    if ((which == 'A' || which == BC_FB_TARGET_L) && LayerBuf != NULL) {
        if (WriteBuf == LayerBuf) restorepanel();
        BC_FREE(LayerBuf);
        LayerBuf = NULL;
    }
    if (which != 'A' && which != BC_FB_TARGET_F && which != BC_FB_TARGET_L)
        error("Syntax");
}

void hal_vm_framebuffer_merge(int has_colour, int colour, int mode, int has_rate, int rate_ms) {
    uint8_t transparent = has_colour ? vm_fb_rgb121((uint32_t)colour) : 0;
    if (LayerBuf == NULL) error("Layer not created");
    if (FrameBuf == NULL) error("Framebuffer not created");
    if (has_rate && rate_ms < 0) error("Number out of bounds");

    switch (mode) {
    case BC_FB_MERGE_MODE_NOW:
        vm_fb_stop_merge();
        vm_fb_merge_now(transparent);
        return;
    case BC_FB_MERGE_MODE_B:
        vm_fb_stop_merge();
        if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) ||
               (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL)
#if defined(rp2350)
               || Option.DISPLAY_TYPE >= NEXTGEN
#endif
               ))) {
            error("Not available on this display");
        }
        if (diskchecktimer < 200 && SPIatRisk) diskchecktimer = 200;
        hal_display_merge_post_fill(transparent);
        return;
    case BC_FB_MERGE_MODE_R:
        if (!(((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) ||
               (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL)))) {
            error("Not available on this display");
        }
        if (WriteBuf == NULL) {
            WriteBuf = FrameBuf;
            setframebuffer();
        }
        vm_fb_stop_merge();
        vm_fb_merge_running = 1;
        vm_fb_merge_colour = transparent;
        mergetimer = (uint32_t)(has_rate ? rate_ms : 0);
        vm_fb_merge_interval_us = (uint32_t)(has_rate ? rate_ms * 1000 : 0);
        hal_display_merge_post_bg(transparent, vm_fb_merge_interval_us);
        return;
    case BC_FB_MERGE_MODE_A:
        vm_fb_stop_merge();
        return;
    default:
        error("Syntax");
    }
}

void hal_vm_framebuffer_sync(void) {
    vm_fb_complete_pending_copy();
    if (mergerunning) {
        mergedone = false;
        while (mergedone == false) CheckAbort();
    }
}

void hal_vm_framebuffer_wait(void) {
    if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP ||
        Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B ||
        Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P) {
        while (GetLineILI9341() != 0) {
        }
    }
}

void hal_vm_framebuffer_copy(char from, char to, int background) {
    int complex = 0;
    unsigned char * saved = WriteBuf;
    uint8_t * s = NULL;
    uint8_t * d = NULL;

    from = (char)toupper((unsigned char)from);
    to = (char)toupper((unsigned char)to);
    if (from == to) return;

    if (from == BC_FB_TARGET_N) {
        complex = 1;
        if ((void *)ReadBuffer == (void *)DisplayNotSet) error("Invalid on this display");
    } else if (from == BC_FB_TARGET_L) {
        if (LayerBuf == NULL) error("Layer buffer not created");
        s = LayerBuf;
    } else if (from == BC_FB_TARGET_F) {
        if (FrameBuf == NULL) error("Frame buffer not created");
        s = FrameBuf;
    } else
        error("Syntax");

    if (to == BC_FB_TARGET_N) {
        complex = 2;
    } else if (to == BC_FB_TARGET_L) {
        if (LayerBuf == NULL) error("Layer buffer not created");
        d = LayerBuf;
    } else if (to == BC_FB_TARGET_F) {
        if (FrameBuf == NULL) error("Frame buffer not created");
        d = FrameBuf;
    } else
        error("Syntax");

    if (!complex) {
        if (d != s) memcpy(d, s, vm_fb_bytes());
    } else if (complex == 1) {
        unsigned char * linebuf = vm_fb_reserve_line((size_t)HRes * 3u);
        int y;
        WriteBuf = d;
        setframebuffer();
        for (y = 0; y < VRes; ++y) {
            restorepanel();
            ReadBuffer(0, y, HRes - 1, y, linebuf);
            WriteBuf = d;
            setframebuffer();
            DrawBuffer(0, y, HRes - 1, y, linebuf);
        }
    } else {
        if (background) {
            vm_fb_copy_src = s;
            vm_fb_copy_pending = 1;
        } else {
            vm_fb_copy_to_screen(s);
        }
    }

    WriteBuf = saved;
    if (WriteBuf == FrameBuf || WriteBuf == LayerBuf)
        setframebuffer();
    else
        restorepanel();
}
