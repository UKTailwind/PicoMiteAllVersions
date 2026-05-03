/*
 * drivers/display_merge/display_merge_pico.c — real merge-pipeline
 * control for the PicoMite (SPI-LCD) device family.
 *
 * Linked only into PICOMITE variants (rp2040 + rp2350). The pipeline
 * is driven through the rp2040 inter-core FIFO; core1 runs UpdateCore
 * (defined at the bottom of this file), which receives the command
 * words posted by the hooks below. See hal/hal_display_merge.h for the
 * contract.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/dma.h"

extern bool mergerunning;
extern volatile bool mergedone;
extern uint32_t mergetimer;
extern uint32_t _excep_code;
extern mutex_t frameBufferMutex;
extern unsigned char *ShadowBuf;
extern int fb_dma_chan;
extern void fastgfx_swap_core1(void);

void hal_display_merge_abort(void) {
    if (!mergerunning) return;
    multicore_fifo_push_blocking(0xFF);
    busy_wait_ms(mergetimer + 200);
    if (mergerunning) {
        /* Core1 failed to ack the stop within mergetimer+200 ms. Hard
         * reset — the BASIC program is about to run with a half-dead
         * display pipeline otherwise. Matches the legacy inline
         * pattern that lived in Draw.c before the HAL extraction. */
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
}

void hal_display_merge_check_busy(void) {
    if (mergerunning) error("Display in use for merged operation");
}

void hal_display_merge_lock_fb(void) {
    mutex_enter_blocking(&frameBufferMutex);
}

void hal_display_merge_unlock_fb(void) {
    mutex_exit(&frameBufferMutex);
}

void hal_display_merge_mark_done(void) {
    mergedone = true;
    __dmb();
}

void hal_display_fast_dma_alloc(unsigned bytes) {
    ShadowBuf = GetMemory(bytes);
    memset(ShadowBuf, 0, bytes);
    fb_dma_chan = dma_claim_unused_channel(true);
}

void hal_display_fast_dma_free(void) {
    if (ShadowBuf) { FreeMemory(ShadowBuf); ShadowBuf = NULL; }
    if (fb_dma_chan >= 0) { dma_channel_unclaim(fb_dma_chan); fb_dma_chan = -1; }
}

void hal_display_nextgen_refresh_rect(int x_lo, int y_lo, int x_hi, int y_hi) {
    multicore_fifo_push_blocking(6);
    multicore_fifo_push_blocking((uint32_t)x_lo | ((uint32_t)x_hi << 16));
    multicore_fifo_push_blocking((uint32_t)y_lo | ((uint32_t)y_hi << 16));
}

void hal_display_nextgen_scroll_reset(void) {
    multicore_fifo_push_blocking(7);
    multicore_fifo_push_blocking((uint32_t)0);
}

int hal_display_merge_has_pipeline(void) { return 1; }

void hal_display_merge_sync_wait(void) {
    mergedone = false;
    __dmb();
    while (mergedone == false) CheckAbort();
}

void hal_display_merge_post_fill(unsigned colour) {
    multicore_fifo_push_blocking(2);
    multicore_fifo_push_blocking((uint32_t)colour);
}

void hal_display_merge_post_bg(unsigned colour, unsigned timer_us) {
    multicore_fifo_push_blocking(3);
    multicore_fifo_push_blocking((uint32_t)colour);
    multicore_fifo_push_blocking((uint32_t)timer_us);
}

void hal_display_merge_post_copy(const void *src) {
    multicore_fifo_push_blocking(1);
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)src);
}

void hal_display_merge_post_blit_fill(int x, int y, int w, int h, unsigned colour) {
    multicore_fifo_push_blocking(4);
    multicore_fifo_push_blocking((uint32_t)x);
    multicore_fifo_push_blocking((uint32_t)y);
    multicore_fifo_push_blocking((uint32_t)w);
    multicore_fifo_push_blocking((uint32_t)h);
    multicore_fifo_push_blocking((uint32_t)colour);
}

void hal_display_merge_post_blit_bg(int x, int y, int w, int h,
                                    unsigned colour, unsigned timer_us) {
    multicore_fifo_push_blocking(5);
    multicore_fifo_push_blocking((uint32_t)x);
    multicore_fifo_push_blocking((uint32_t)y);
    multicore_fifo_push_blocking((uint32_t)w);
    multicore_fifo_push_blocking((uint32_t)h);
    multicore_fifo_push_blocking((uint32_t)colour);
    multicore_fifo_push_blocking((uint32_t)timer_us);
}

/* UpdateCore is the receiver counterpart to the post_* functions above:
 * it spins on the inter-core FIFO, decoding the command word + payloads
 * posted by core0 and driving the SPI-LCD merge path on core1.
 *
 * core1stack[] is owned by ports/pico_sdk_common/core1_runtime.c — sized
 * per port via HAL_PORT_CORE1_STACK_WORDS. */
void __not_in_flash_func(UpdateCore)(void) {
    while (true) {
        __dmb();
        if (!multicore_fifo_rvalid()) continue;
        int command = multicore_fifo_pop_blocking();
        if (command == 3) {
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            uint32_t timer = (uint32_t)multicore_fifo_pop_blocking();
            uint64_t delaytime = 0;
            if (timer) delaytime = time_us_64() + timer;
            mergerunning = true;
            while (1) {
                if (multicore_fifo_rvalid()) {
                    int a = multicore_fifo_pop_blocking();
                    if (a == 0xff) { mergerunning = false; break; }
                }
                if (timer) {
                    busy_wait_until(delaytime);
                    delaytime = time_us_64() + timer;
                }
                if (ShadowBuf) merge_optimized(colour);
                else            merge(colour);
            }
        } else if (command == 2) {
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            if (ShadowBuf) merge_optimized(colour);
            else           merge(colour);
        } else if (command == 4) {
            int x1 = multicore_fifo_pop_blocking();
            int y1 = multicore_fifo_pop_blocking();
            int w  = multicore_fifo_pop_blocking();
            int h  = multicore_fifo_pop_blocking();
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            blitmerge(x1, y1, w, h, colour);
        } else if (command == 5) {
            int x1 = multicore_fifo_pop_blocking();
            int y1 = multicore_fifo_pop_blocking();
            int w  = multicore_fifo_pop_blocking();
            int h  = multicore_fifo_pop_blocking();
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            uint32_t timer = (uint32_t)multicore_fifo_pop_blocking();
            uint64_t delaytime = 0;
            if (timer) delaytime = time_us_64() + timer;
            mergerunning = true;
            while (1) {
                if (multicore_fifo_rvalid()) {
                    int a = multicore_fifo_pop_blocking();
                    if (a == 0xff) { mergerunning = false; break; }
                }
                if (timer) {
                    busy_wait_until(delaytime);
                    delaytime = time_us_64() + timer;
                }
                blitmerge(x1, y1, w, h, colour);
            }
#if HAL_PORT_HAS_PICOMITE && defined(rp2350)
        } else if (command == 6) {
            int x_low = (int)multicore_fifo_pop_blocking();
            int y_low = (int)multicore_fifo_pop_blocking();
            int x_high = x_low >> 16; x_low &= 0xFFFF;
            int y_high = y_low >> 16; y_low &= 0xFFFF;
            mutex_enter_blocking(&frameBufferMutex);
            copybuffertoscreen((uint8_t *)ScreenBuffer, x_low, y_low, x_high, y_high);
            mutex_exit(&frameBufferMutex);
        } else if (command == 7) {
            int t = (int)multicore_fifo_pop_blocking();
            spi_write_command(CMD_SET_SCROLL_START);
            spi_write_data(t >> 8);
            spi_write_data(t);
#endif
        } else if (command == 1) {
            uint8_t *s = (uint8_t *)multicore_fifo_pop_blocking();
            mutex_enter_blocking(&frameBufferMutex);
            copyframetoscreen(s, 0, HRes - 1, 0, VRes - 1, 0);
            mutex_exit(&frameBufferMutex);
        } else if (command == 8) {
            fastgfx_swap_core1();
        }
    }
}
