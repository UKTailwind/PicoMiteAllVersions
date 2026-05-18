/*
 * drivers/spi_lcd/spi_lcd_fastgfx.c — double-buffered FASTGFX swap +
 * FRAMEBUFFER MERGE scanline-diff DMA path for the PicoMite SPI-LCD
 * display family.
 *
 * Extracted from Draw.c (was under `#if defined(PICOMITE) &&
 * !defined(MMBASIC_HOST)` there). Linked into PICO / PICOUSB /
 * PICORP2350 / PICOUSBRP2350 only. VGA/HDMI/WEB get the stubs in
 * spi_lcd_fastgfx_stub.c; host gets its own simulator impl in
 * host_fastgfx.c.
 *
 * Uses: spi_write_fast, fastgfx_dma_chan, ShadowBuf, the FastGFX*Buf
 * ping-pong, RGB565 LUT, and the HAL_PORT_LCD_SPI_CLK_PIN macro.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "bc_alloc.h"
#include "hal/hal_time.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "pico/mutex.h"
#include "pico/multicore.h"

/* File-scope globals defined in Draw.c; we need them here too. */
extern int map[16];
extern int RGB121map[16];
extern unsigned char *ShadowBuf;
extern int fb_dma_chan;
extern mutex_t frameBufferMutex;

/* Host builds provide FASTGFX in the host port; they present/copy the host
 * framebuffer directly instead of DMA-ing dirty scanlines to an LCD controller. */

static uint8_t *FastGFXBackBuf = NULL;
static uint8_t *FastGFXFrontBuf = NULL;
static volatile bool fastgfx_done = true;
static bool fastgfx_active = false;
static int fastgfx_dma_chan = -1;
static uint32_t fastgfx_frame_us = 0;       // target frame time in microseconds (0 = unlimited)
static uint64_t fastgfx_last_swap_us = 0;   // timestamp of last swap

// Shared ping-pong line buffers for RGB565 conversion (max 320 pixels * 2 bytes)
// Used by both FASTGFX and FRAMEBUFFER FAST merge paths.
static uint16_t fastgfx_linebuf[2][320];

// Determine which SPI instance the LCD is on
static inline spi_inst_t *fastgfx_get_spi(void) {
    return (PinDef[HAL_PORT_LCD_SPI_CLK_PIN].mode & SPI0SCK) ? spi0 : spi1;
}

// Core1 swap: diff back vs front, DMA changed scanlines to SPI, copy back->front
void __not_in_flash_func(fastgfx_swap_core1)(void) {
    spi_inst_t *spi = fastgfx_get_spi();
    int stride = HRes / 2;  // bytes per scanline in 4-bit framebuffer
    uint8_t *back = FastGFXBackBuf;
    uint8_t *front = FastGFXFrontBuf;
    int buf_idx = 0;  // ping-pong index

    // Ensure map[] is populated using same encoding as copyframetoscreen
    if (map[15] == 0) {
        for (int i = 0; i < 16; i++) {
            uint8_t col0 = ((RGB121map[i] >> 16) & 0xF8) | ((RGB121map[i] >> 13) & 0x07);
            uint8_t col1 = ((RGB121map[i] >>  5) & 0xE0) | ((RGB121map[i] >>  3) & 0x1F);
            map[i] = col0 | (col1 << 8);
        }
    }

    // Configure DMA channel for SPI writes
    dma_channel_config dma_cfg = dma_channel_get_default_config(fastgfx_dma_chan);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, spi_get_dreq(spi, true));  // TX DREQ

    // Tear sync
    if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP ||
        Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B ||
        Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P) {
        while (GetLineILI9341() != 0) {}
    }

    // Scan for dirty regions: batch consecutive dirty scanlines
    int y = 0;
    while (y < VRes) {
        // Skip clean scanlines
        if (memcmp(back + y * stride, front + y * stride, stride) == 0) {
            y++;
            continue;
        }

        // Found a dirty scanline — find the extent of the dirty region
        int y_start = y;
        int x_min = stride;  // in bytes
        int x_max = 0;

        while (y < VRes && memcmp(back + y * stride, front + y * stride, stride) != 0) {
            uint8_t *b = back + y * stride;
            uint8_t *f = front + y * stride;
            // Find leftmost and rightmost dirty byte in this scanline
            for (int x = 0; x < stride; x++) {
                if (b[x] != f[x]) {
                    if (x < x_min) x_min = x;
                    if (x > x_max) x_max = x;
                }
            }
            y++;
        }
        int y_end = y;  // exclusive

        // Convert byte range to pixel range (each byte = 2 pixels)
        int px_start = x_min * 2;
        int px_end = x_max * 2 + 1;  // inclusive

        // Set display window for this dirty rectangle
        DefineRegionSPI(px_start, y_start, px_end, y_end - 1, 1);

        // Send each scanline in the dirty region
        for (int sy = y_start; sy < y_end; sy++) {
            uint16_t *lb = fastgfx_linebuf[buf_idx];
            int pixel_count = px_end - px_start + 1;

            // Convert 4-bit indexed -> RGB565 into line buffer
            int out = 0;
            for (int bx = x_min; bx <= x_max; bx++) {
                uint8_t byte = back[sy * stride + bx];
                lb[out++] = (uint16_t)map[byte & 0x0F];
                lb[out++] = (uint16_t)map[(byte >> 4) & 0x0F];
            }

            // Wait for previous DMA to finish before reusing this buffer
            dma_channel_wait_for_finish_blocking(fastgfx_dma_chan);

            // DMA the line buffer to SPI
            dma_channel_configure(
                fastgfx_dma_chan,
                &dma_cfg,
                &spi_get_hw(spi)->dr,    // write to SPI data register
                (uint8_t *)lb,            // read from line buffer
                pixel_count * 2,          // byte count (RGB565 = 2 bytes/pixel)
                true                      // start immediately
            );

            // Copy this scanline's dirty bytes from back to front
            memcpy(front + sy * stride + x_min, back + sy * stride + x_min, x_max - x_min + 1);

            // Alternate ping-pong buffer
            buf_idx ^= 1;
        }

        // Wait for last DMA in this region to finish before next DefineRegionSPI
        dma_channel_wait_for_finish_blocking(fastgfx_dma_chan);
        spi_finish(spi);
        ClearCS(Option.LCD_CS);
    }

    __dmb();
    fastgfx_done = true;
}

/*
 * merge_optimized — FRAMEBUFFER MERGE with scanline diff + DMA.
 *
 * Composite layer onto frame into a line buffer, diff against ShadowBuf,
 * and DMA only changed regions. Used when ShadowBuf != NULL (FRAMEBUFFER CREATE FAST).
 */
void merge_optimized(uint8_t colour) {
    if (LayerBuf == NULL || FrameBuf == NULL || ShadowBuf == NULL) return;

    int stride = HRes / 2;
    uint8_t highcolour = colour << 4;
    uint8_t *s = LayerBuf;
    uint8_t *d = FrameBuf;
    uint8_t *shadow = ShadowBuf;

    mutex_enter_blocking(&frameBufferMutex);

    // Tear sync
    if (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP ||
        Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B ||
        Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ILI9488P) {
        while (GetLineILI9341() != 0) {}
    }

    // Single-pass: composite, diff against shadow, batch+send dirty regions
    // Same flow as fastgfx_swap_core1 but with frame+layer compositing
    spi_inst_t *spi = fastgfx_get_spi();
    int dma_chan = fb_dma_chan;
    int buf_idx = 0;
    uint8_t LineBuf[stride];

    // Ensure map[] is populated
    if (map[15] == 0) {
        for (int i = 0; i < 16; i++) {
            uint8_t col0 = ((RGB121map[i] >> 16) & 0xF8) | ((RGB121map[i] >> 13) & 0x07);
            uint8_t col1 = ((RGB121map[i] >>  5) & 0xE0) | ((RGB121map[i] >>  3) & 0x1F);
            map[i] = col0 | (col1 << 8);
        }
    }

    dma_channel_config dma_cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, spi_get_dreq(spi, true));

    int y = 0;
    while (y < VRes) {
        // Composite this scanline
        memcpy(LineBuf, d + y * stride, stride);
        uint8_t *ss = s + y * stride;
        for (int x = 0; x < stride; x++) {
            uint8_t top = *ss & 0xF0;
            uint8_t bottom = *ss++ & 0x0f;
            if (top == highcolour && bottom == colour) continue;
            if (top != highcolour && bottom != colour) LineBuf[x] = (top | bottom);
            else if (top != highcolour) {
                LineBuf[x] &= 0x0F;
                LineBuf[x] |= top;
            } else {
                LineBuf[x] &= 0xF0;
                LineBuf[x] |= bottom;
            }
        }

        // Skip clean scanlines
        if (memcmp(LineBuf, shadow + y * stride, stride) == 0) {
            y++;
            continue;
        }

        // Found a dirty scanline — scan ahead to find the batch extent
        int y_start = y;
        int x_min = stride;
        int x_max = 0;

        // Process this and consecutive dirty scanlines
        while (y < VRes) {
            // Composite (already done for first line of batch)
            if (y != y_start) {
                memcpy(LineBuf, d + y * stride, stride);
                uint8_t *ss2 = s + y * stride;
                for (int x = 0; x < stride; x++) {
                    uint8_t top = *ss2 & 0xF0;
                    uint8_t bottom = *ss2++ & 0x0f;
                    if (top == highcolour && bottom == colour) continue;
                    if (top != highcolour && bottom != colour) LineBuf[x] = (top | bottom);
                    else if (top != highcolour) {
                        LineBuf[x] &= 0x0F;
                        LineBuf[x] |= top;
                    } else {
                        LineBuf[x] &= 0xF0;
                        LineBuf[x] |= bottom;
                    }
                }
                if (memcmp(LineBuf, shadow + y * stride, stride) == 0) break;
            }

            // Track dirty byte range, update only changed bytes in shadow
            uint8_t *sh = shadow + y * stride;
            for (int x = 0; x < stride; x++) {
                if (LineBuf[x] != sh[x]) {
                    if (x < x_min) x_min = x;
                    if (x > x_max) x_max = x;
                    sh[x] = LineBuf[x];
                }
            }
            y++;
        }
        int y_end = y;

        // Send dirty region — same as fastgfx_swap_core1
        int px_start = x_min * 2;
        int px_end = x_max * 2 + 1;
        int pixel_count = px_end - px_start + 1;

        DefineRegionSPI(px_start, y_start, px_end, y_end - 1, 1);

        for (int sy = y_start; sy < y_end; sy++) {
            uint16_t *lb = fastgfx_linebuf[buf_idx];
            int out = 0;
            for (int bx = x_min; bx <= x_max; bx++) {
                uint8_t byte = shadow[sy * stride + bx];
                lb[out++] = (uint16_t)map[byte & 0x0F];
                lb[out++] = (uint16_t)map[(byte >> 4) & 0x0F];
            }

            dma_channel_wait_for_finish_blocking(dma_chan);
            dma_channel_configure(
                dma_chan,
                &dma_cfg,
                &spi_get_hw(spi)->dr,
                (uint8_t *)lb,
                pixel_count * 2,
                true
            );
            buf_idx ^= 1;
        }

        dma_channel_wait_for_finish_blocking(dma_chan);
        spi_finish(spi);
        ClearCS(Option.LCD_CS);
    }

    mutex_exit(&frameBufferMutex);
    mergedone = true;
    __dmb();
    low_x = 0; low_y = 0; high_x = HRes - 1; high_y = VRes - 1;
}

void bc_fastgfx_swap(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    while (!fastgfx_done) { __dmb(); }
    if (fastgfx_frame_us > 0) {
        while (hal_time_us_64() - fastgfx_last_swap_us < fastgfx_frame_us) {
            tight_loop_contents();
        }
    }
    fastgfx_last_swap_us = hal_time_us_64();
    fastgfx_done = false;
    __dmb();
    multicore_fifo_push_blocking(8);
}

void bc_fastgfx_sync(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    while (!fastgfx_done) { __dmb(); }
}

void bc_fastgfx_create(void) {
    if (fastgfx_active) {
        while (!fastgfx_done) { __dmb(); }
        if (fastgfx_dma_chan >= 0) {
            dma_channel_unclaim(fastgfx_dma_chan);
            fastgfx_dma_chan = -1;
        }
        BC_FREE(FastGFXBackBuf);
        BC_FREE(FastGFXFrontBuf);
        FastGFXBackBuf = NULL;
        FastGFXFrontBuf = NULL;
        fastgfx_active = false;
        restorepanel();
    }
    if (FrameBuf || LayerBuf) error("FRAMEBUFFER is active");
    FastGFXBackBuf = BC_ALLOC(HRes * VRes / 2);
    FastGFXFrontBuf = BC_ALLOC(HRes * VRes / 2);
    if (!FastGFXBackBuf || !FastGFXFrontBuf) {
        unsigned int _u=0,_f=0,_r=0,_t=0;
        heap_scan_stats(&_u,&_f,&_r,&_t);
        error("NEM[fastgfx:bufs] want=%x2 used=%/% free=% run=%",
              (int)(HRes*VRes/2),
              (int)_u, (int)_t, (int)_f, (int)_r);
    }
    memset(FastGFXBackBuf, 0, HRes * VRes / 2);
    memset(FastGFXFrontBuf, 0, HRes * VRes / 2);
    fastgfx_dma_chan = dma_claim_unused_channel(true);
    WriteBuf = FastGFXBackBuf;
    setframebuffer();
    fastgfx_active = true;
    fastgfx_done = true;
    fastgfx_frame_us = 0;
    fastgfx_last_swap_us = 0;
}

void bc_fastgfx_close(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    while (!fastgfx_done) { __dmb(); }
    if (fastgfx_dma_chan >= 0) {
        dma_channel_unclaim(fastgfx_dma_chan);
        fastgfx_dma_chan = -1;
    }
    BC_FREE(FastGFXBackBuf);
    BC_FREE(FastGFXFrontBuf);
    FastGFXBackBuf = NULL;
    FastGFXFrontBuf = NULL;
    fastgfx_active = false;
    restorepanel();
}

void bc_fastgfx_reset(void) {
    if (!fastgfx_active) return;
    while (!fastgfx_done) { __dmb(); }
    if (fastgfx_dma_chan >= 0) {
        dma_channel_unclaim(fastgfx_dma_chan);
        fastgfx_dma_chan = -1;
    }
    BC_FREE(FastGFXBackBuf);
    BC_FREE(FastGFXFrontBuf);
    FastGFXBackBuf = NULL;
    FastGFXFrontBuf = NULL;
    fastgfx_active = false;
    restorepanel();
}

void bc_fastgfx_set_fps(int fps) {
    if (fps < 0 || fps > 1000) error("Number out of bounds");
    if (fps == 0)
        fastgfx_frame_us = 0;
    else
        fastgfx_frame_us = 1000000 / fps;
}

void cmd_fastgfx(void) {
    unsigned char *p = NULL;
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
