/*
 * esp32_fastgfx.c - FASTGFX for ESP32-S3 scanout and local SPI LCD.
 *
 * VGA keeps the generic scanout behavior: draw into a back buffer and copy
 * it to FRAMEBUFFER on SWAP. The Freenove ILI9341 path uses the same shared
 * RGB121 WriteBuf drawing primitives as PicoMite SPI-LCD FASTGFX, then
 * presents only dirty packed scanlines to the ESP-IDF LCD driver.
 */

#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bc_alloc.h"
#include "hal/hal_time.h"
#include "hal/hal_vga_ops.h"

extern int esp32_ili9341_lcd_ready(void);
extern void esp32_ili9341_lcd_snapshot_rgb121(uint8_t * out);
extern void esp32_ili9341_lcd_present_rgb121_diff(uint8_t * back, uint8_t * front);

typedef enum {
    ESP32_FASTGFX_NONE = 0,
    ESP32_FASTGFX_SCANOUT,
    ESP32_FASTGFX_LCD
} esp32_fastgfx_mode_t;

static esp32_fastgfx_mode_t s_mode;
static uint8_t * s_back;
static uint8_t * s_front;
static unsigned char * s_saved_writebuf;
static uint32_t s_frame_us;
static uint64_t s_last_swap_us;

int esp32_fastgfx_active(void) {
    return s_mode != ESP32_FASTGFX_NONE;
}

static size_t lcd_buf_size(void) {
    if (HRes <= 0 || VRes <= 0) return 0;
    return (size_t)HRes * (size_t)VRes / 2u;
}

static void fastgfx_sleep_to_fps(void) {
    if (s_frame_us == 0) return;
    uint64_t now = hal_time_us_64();
    uint64_t deadline = s_last_swap_us + s_frame_us;
    while (now < deadline) {
        now = hal_time_us_64();
    }
}

static void fastgfx_free_lcd_buffers(void) {
    if (s_back) {
        heap_caps_free(s_back);
        s_back = NULL;
    }
    if (s_front) {
        heap_caps_free(s_front);
        s_front = NULL;
    }
}

static void fastgfx_reset_state(int clear_fps) {
    if (s_mode == ESP32_FASTGFX_SCANOUT && s_saved_writebuf) {
        WriteBuf = s_saved_writebuf;
        s_saved_writebuf = NULL;
    } else if (s_mode == ESP32_FASTGFX_LCD) {
        restorepanel();
    }
    if (s_mode == ESP32_FASTGFX_SCANOUT && s_back) {
        bc_free(s_back);
        s_back = NULL;
    }
    fastgfx_free_lcd_buffers();
    s_mode = ESP32_FASTGFX_NONE;
    if (clear_fps) s_frame_us = 0;
    s_last_swap_us = 0;
}

static void fastgfx_scanout_present(void) {
    if (!s_back || FRAMEBUFFER == NULL || framebuffersize == 0) return;
    memcpy((void *)FRAMEBUFFER, s_back, framebuffersize);
    hal_vga_ops_fastgfx_present();
}

static void fastgfx_create_scanout(void) {
    s_back = (uint8_t *)bc_alloc((size_t)framebuffersize);
    if (!s_back) error("FASTGFX: out of memory");
    memcpy(s_back, (const void *)FRAMEBUFFER, framebuffersize);
    s_saved_writebuf = WriteBuf;
    WriteBuf = (unsigned char *)s_back;
    s_mode = ESP32_FASTGFX_SCANOUT;
}

static void fastgfx_create_lcd(void) {
    size_t bytes = lcd_buf_size();
    if (bytes == 0) error("Display not configured");
    s_back = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_front = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_back || !s_front) {
        fastgfx_free_lcd_buffers();
        error("NEM[fastgfx:bufs] want=%x2", (int)bytes);
    }
    esp32_ili9341_lcd_snapshot_rgb121(s_front);
    WriteBuf = s_back;
    setframebuffer();
    s_mode = ESP32_FASTGFX_LCD;
}

void bc_fastgfx_create(void) {
    if (s_mode != ESP32_FASTGFX_NONE) fastgfx_reset_state(0);
    if (FrameBuf || LayerBuf) error("FRAMEBUFFER is active");

    if (esp32_ili9341_lcd_ready()) {
        fastgfx_create_lcd();
    } else if (FRAMEBUFFER != NULL && framebuffersize != 0) {
        fastgfx_create_scanout();
    } else {
        error("FASTGFX requires an active display buffer");
    }
    s_last_swap_us = hal_time_us_64();
}

void bc_fastgfx_close(void) {
    if (s_mode == ESP32_FASTGFX_NONE) error("FASTGFX not active");
    if (s_mode == ESP32_FASTGFX_SCANOUT) {
        fastgfx_scanout_present();
    } else if (s_mode == ESP32_FASTGFX_LCD) {
        esp32_ili9341_lcd_present_rgb121_diff(s_back, s_front);
    }
    fastgfx_reset_state(0);
}

void bc_fastgfx_swap(void) {
    if (s_mode == ESP32_FASTGFX_NONE) error("FASTGFX not active");
    fastgfx_sleep_to_fps();
    if (s_mode == ESP32_FASTGFX_SCANOUT) {
        hal_vga_ops_wait_scanline_zero();
        fastgfx_scanout_present();
    } else if (s_mode == ESP32_FASTGFX_LCD) {
        esp32_ili9341_lcd_present_rgb121_diff(s_back, s_front);
    }
    s_last_swap_us = hal_time_us_64();
}

void bc_fastgfx_sync(void) {
    if (s_mode == ESP32_FASTGFX_NONE) error("FASTGFX not active");
}

void bc_fastgfx_reset(void) {
    fastgfx_reset_state(1);
}

void bc_fastgfx_set_fps(int fps) {
    if (fps < 0 || fps > 1000) error("Number out of bounds");
    s_frame_us = fps == 0 ? 0u : 1000000u / (uint32_t)fps;
}

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
