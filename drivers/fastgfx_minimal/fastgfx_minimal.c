/*
 * drivers/fastgfx_minimal/fastgfx_minimal.c — FASTGFX for scanout
 * ports (HDMI / DVI / VGA / WEB) with a back buffer.
 *
 * The picomite SPI-LCD FASTGFX has a small RGB121-packed back buffer
 * + dual-core DMA push to the panel. The scanout-port equivalent is
 * conceptually simpler:
 *
 *   CREATE   alloc a back buffer the size of the scanout, seed it from
 *            the current display, redirect WriteBuf so primitives draw
 *            into the back buffer.
 *   SWAP     vblank-align, then memcpy back → front. The scanout reads
 *            a stable frame for the rest of the active period.
 *   SYNC     no-op — the present in SWAP is synchronous.
 *   FPS n    deadline-based framerate cap.
 *   CLOSE    one final present, restore WriteBuf, free the back buffer.
 *
 * Memory: the back buffer is `framebuffersize` bytes. On rp2350 ports
 * with PSRAM (dvi_wifi, hdmi_rp2350) BC_ALLOC routes to PSRAM for the
 * 240 KB SVGA buffer; drawing primitives then write through the QMI to
 * PSRAM, which is slower than SRAM but bounded enough that 60 fps is
 * still reachable for typical games. SRAM-only ports without PSRAM
 * will fail CREATE if the budget doesn't fit — that's correct.
 */

#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_time.h"
#include "hal/hal_vga_ops.h"
#include "bc_alloc.h"

static int fastgfx_active = 0;
static uint32_t fastgfx_frame_us = 0; /* 0 = unlimited */
static uint64_t fastgfx_last_swap_us = 0;
static uint8_t * fastgfx_back_buf = NULL;
static unsigned char * fastgfx_saved_writebuf = NULL;

/* Copy the back buffer onto the scanout. Called from SWAP and
 * during CLOSE so the last drawn frame stays on screen. */
static void fastgfx_present(void) {
    if (!fastgfx_back_buf) return;
    if (FRAMEBUFFER == NULL || framebuffersize == 0) return;
    memcpy((void *)FRAMEBUFFER, fastgfx_back_buf, framebuffersize);
}

void bc_fastgfx_create(void) {
    if (fastgfx_active) return;
    if (FRAMEBUFFER == NULL || framebuffersize == 0) {
        error("FASTGFX requires an active scanout buffer");
    }
    fastgfx_back_buf = (uint8_t *)bc_alloc((size_t)framebuffersize);
    if (!fastgfx_back_buf) error("FASTGFX: out of memory");
    /* Seed the back buffer with the current scanout so existing
     * content (HUD, static blocks) isn't wiped out at the start of
     * the first frame. */
    memcpy(fastgfx_back_buf, (const void *)FRAMEBUFFER, framebuffersize);
    fastgfx_saved_writebuf = WriteBuf;
    WriteBuf = (unsigned char *)fastgfx_back_buf;
    fastgfx_active = 1;
    fastgfx_last_swap_us = hal_time_us_64();
    /* Don't touch frame_us — FPS may have been set before CREATE, and
     * we want it to survive a CLOSE+CREATE cycle. bc_fastgfx_reset()
     * at FRUN entry clears everything. */
}

void bc_fastgfx_close(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    /* One final present so the last drawn frame is visible after CLOSE. */
    fastgfx_present();
    if (fastgfx_saved_writebuf) {
        WriteBuf = fastgfx_saved_writebuf;
        fastgfx_saved_writebuf = NULL;
    }
    if (fastgfx_back_buf) {
        bc_free(fastgfx_back_buf);
        fastgfx_back_buf = NULL;
    }
    fastgfx_active = 0;
    /* frame_us preserved — see bc_fastgfx_create. */
}

void bc_fastgfx_swap(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    /* Vsync-align: wait for top-of-frame so the back→front memcpy
     * lands before the scanout starts reading the same RAM. On HDMI
     * this watches v_scanline; on VGA it watches QVgaScanLine; on WEB
     * it's a no-op (vga_ops_stub). */
    hal_vga_ops_wait_scanline_zero();
    fastgfx_present();
    if (fastgfx_frame_us > 0) {
        uint64_t deadline = fastgfx_last_swap_us + fastgfx_frame_us;
        while (hal_time_us_64() < deadline) { /* spin */
        }
    }
    fastgfx_last_swap_us = hal_time_us_64();
}

void bc_fastgfx_sync(void) {
    /* SYNC waits for the previous SWAP's async work to finish. Our
     * present is synchronous (memcpy completes before SWAP returns),
     * so there's nothing to wait for. */
    if (!fastgfx_active) error("FASTGFX not active");
}

void bc_fastgfx_set_fps(int fps) {
    if (fps < 0 || fps > 1000) error("Number out of bounds");
    fastgfx_frame_us = (fps == 0) ? 0u : (1000000u / (uint32_t)fps);
}

void bc_fastgfx_reset(void) {
    /* Called at FRUN entry. If a previous run left FASTGFX active
     * (error / abort), tear it down so the next program gets clean
     * state — restore WriteBuf, free the back buffer, zero pacing. */
    if (fastgfx_saved_writebuf) {
        WriteBuf = fastgfx_saved_writebuf;
        fastgfx_saved_writebuf = NULL;
    }
    if (fastgfx_back_buf) {
        bc_free(fastgfx_back_buf);
        fastgfx_back_buf = NULL;
    }
    fastgfx_active = 0;
    fastgfx_frame_us = 0;
    fastgfx_last_swap_us = 0;
}

/* SPI-LCD merge fast-path — never reached on scanout ports (the
 * vm_framebuffer_unsupported stub errors on FRAMEBUFFER MERGE before
 * it can fire), but the symbol's declared in Draw.h so we provide a
 * no-op definition to keep the link surface complete. */
void merge_optimized(uint8_t colour) {
    (void)colour;
}

void cmd_fastgfx(void) {
    unsigned char * p = NULL;
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        checkend(p);
        bc_fastgfx_create();
    } else if ((p = checkstring(cmdline, (unsigned char *)"SWAP"))) {
        checkend(p);
        bc_fastgfx_swap();
    } else if ((p = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        checkend(p);
        bc_fastgfx_sync();
    } else if ((p = checkstring(cmdline, (unsigned char *)"FPS"))) {
        bc_fastgfx_set_fps(getint(p, 0, 1000));
    } else if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        checkend(p);
        bc_fastgfx_close();
    } else {
        error("Syntax");
    }
}
