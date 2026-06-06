/*
 * drivers/vga_lcdcam_s3/vga_lcdcam_s3.c — ESP32-S3 VGA scanout via LCD_CAM.
 *
 * Drives an external resistor-ladder DAC (VGA666-style, wired for RGB332)
 * with a standard 640x480@60 VGA signal using the ESP32-S3 LCD_CAM RGB
 * panel peripheral. The peripheral allocates a single RGB332 frame buffer
 * in PSRAM and feeds the LCD DMA through internal-RAM bounce buffers.
 */

#include "vga_lcdcam_s3.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_cache.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

static const char *TAG = "vga_lcdcam";

static esp_lcd_panel_handle_t s_panel = NULL;
static uint8_t *s_fb = NULL;
static bool s_cpu_bounce_scanout = false;

/*
 * Standard VESA 640x480@60 timing. Pixel clock is nominally 25.175 MHz;
 * the LCD_CAM clock divider lands on the nearest achievable frequency,
 * which every VGA monitor tested tolerates. Both sync signals are
 * negative polarity (idle high, pulse low), so hsync/vsync_idle_low stay
 * 0. de is unused — a resistor DAC has no data-enable input.
 */
static uint32_t vga_pclk_hz(uint8_t clock_mode) {
    switch (clock_mode) {
        case VGA_LCDCAM_CLOCK_25MHZ:
        case VGA_LCDCAM_CLOCK_25MHZ240:
            return 25000000;
        case VGA_LCDCAM_CLOCK_STANDARD:
        case VGA_LCDCAM_CLOCK_PLL240:
        default:
            return 25175000;
    }
}

static lcd_clock_source_t vga_clock_source(uint8_t clock_mode) {
    switch (clock_mode) {
        case VGA_LCDCAM_CLOCK_PLL240:
        case VGA_LCDCAM_CLOCK_25MHZ240:
            return LCD_CLK_SRC_PLL240M;
        case VGA_LCDCAM_CLOCK_STANDARD:
        case VGA_LCDCAM_CLOCK_25MHZ:
        default:
            return LCD_CLK_SRC_DEFAULT;
    }
}

static const char *vga_clock_name(uint8_t clock_mode) {
    switch (clock_mode) {
        case VGA_LCDCAM_CLOCK_PLL240: return "PLL240";
        case VGA_LCDCAM_CLOCK_25MHZ: return "25MHZ";
        case VGA_LCDCAM_CLOCK_25MHZ240: return "25MHZ240";
        case VGA_LCDCAM_CLOCK_STANDARD:
        default:
            return "STANDARD";
    }
}

static void vga_apply_drive(const vga_lcdcam_pins_t *pins) {
    gpio_drive_cap_t drive = (pins->drive_cap <= GPIO_DRIVE_CAP_3)
                                 ? (gpio_drive_cap_t)pins->drive_cap
                                 : GPIO_DRIVE_CAP_DEFAULT;
    for (int i = 0; i < 8; i++) {
        if (pins->data_gpio[i] >= 0) gpio_set_drive_capability((gpio_num_t)pins->data_gpio[i], drive);
    }
    if (pins->hsync_gpio >= 0) gpio_set_drive_capability((gpio_num_t)pins->hsync_gpio, drive);
    if (pins->vsync_gpio >= 0) gpio_set_drive_capability((gpio_num_t)pins->vsync_gpio, drive);
    if (pins->pclk_gpio >= 0) gpio_set_drive_capability((gpio_num_t)pins->pclk_gpio, drive);
}

static esp_lcd_rgb_timing_t vga_640x480_60(uint8_t sync_flags, uint8_t clock_mode) {
    esp_lcd_rgb_timing_t t = {
        .pclk_hz = vga_pclk_hz(clock_mode),
        .h_res = VGA_LCDCAM_HRES,
        .v_res = VGA_LCDCAM_VRES,
        .hsync_pulse_width = 96,
        .hsync_back_porch = 48,
        .hsync_front_porch = 16,
        .vsync_pulse_width = 2,
        .vsync_back_porch = 33,
        .vsync_front_porch = 10,
        .flags = {
            .hsync_idle_low = (sync_flags & VGA_LCDCAM_SYNC_HSYNC_IDLE_LOW) != 0,
            .vsync_idle_low = (sync_flags & VGA_LCDCAM_SYNC_VSYNC_IDLE_LOW) != 0,
            .de_idle_high = 0,
            .pclk_active_neg = 0,
            .pclk_idle_high = 0,
        },
    };
    return t;
}

bool vga_lcdcam_s3_init(const vga_lcdcam_pins_t *pins, uint8_t **fb_out) {
    if (s_panel) {
        if (fb_out) *fb_out = s_fb;
        return s_fb != NULL;
    }
    if (!pins) return false;

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = vga_clock_source(pins->clock_mode),
        .timings = vga_640x480_60(pins->sync_flags, pins->clock_mode),
        .data_width = 8,      /* RGB332 — one byte per pixel out an 8-bit bus */
        .bits_per_pixel = 8,
        .num_fbs = 1,
        /* Feed the LCD peripheral from internal RAM bounce buffers. Direct
         * PSRAM scanout can lose horizontal phase when flash/cache or drawing
         * traffic briefly starves EDMA. 16 lines divides the 480-line frame
         * exactly and keeps ISR cadence low enough for the S3. */
        .bounce_buffer_size_px = 16 * VGA_LCDCAM_HRES,
        .hsync_gpio_num = pins->hsync_gpio,
        .vsync_gpio_num = pins->vsync_gpio,
        .de_gpio_num = -1,    /* VGA has no data-enable line */
        .pclk_gpio_num = pins->pclk_gpio,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            pins->data_gpio[0], pins->data_gpio[1], pins->data_gpio[2],
            pins->data_gpio[3], pins->data_gpio[4], pins->data_gpio[5],
            pins->data_gpio[6], pins->data_gpio[7],
            -1, -1, -1, -1, -1, -1, -1, -1,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        s_panel = NULL;
        return false;
    }
    vga_apply_drive(pins);

    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
        return false;
    }

    void *fb = NULL;
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb);
    if (err != ESP_OK || fb == NULL) {
        ESP_LOGE(TAG, "get_frame_buffer failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
        return false;
    }

    s_fb = (uint8_t *)fb;
    s_cpu_bounce_scanout = cfg.bounce_buffer_size_px != 0;
    if (fb_out) *fb_out = s_fb;
    ESP_LOGI(TAG, "VGA %dx%d RGB332 up; fb=%p bounce_lines=%d clock=%s req_pclk=%" PRIu32 " drive=%u",
             VGA_LCDCAM_HRES, VGA_LCDCAM_VRES, s_fb,
             s_cpu_bounce_scanout ? 16 : 0,
             vga_clock_name(pins->clock_mode), vga_pclk_hz(pins->clock_mode),
             pins->drive_cap);
    return true;
}

uint8_t *vga_lcdcam_s3_framebuffer(void) { return s_fb; }

bool vga_lcdcam_s3_active(void) { return s_panel != NULL && s_fb != NULL; }

void vga_lcdcam_s3_flush_region(int x1, int y1, int x2, int y2) {
    if (!s_fb) return;
    if (s_cpu_bounce_scanout) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= VGA_LCDCAM_HRES) x2 = VGA_LCDCAM_HRES - 1;
    if (y2 >= VGA_LCDCAM_VRES) y2 = VGA_LCDCAM_VRES - 1;
    if (x1 > x2 || y1 > y2) return;

    const size_t width = (size_t)(x2 - x1 + 1);
    if (x1 == 0 && width == VGA_LCDCAM_HRES) {
        const size_t offset = (size_t)y1 * VGA_LCDCAM_HRES;
        const size_t bytes = (size_t)(y2 - y1 + 1) * VGA_LCDCAM_HRES;
        esp_cache_msync(s_fb + offset, bytes,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    } else {
        for (int y = y1; y <= y2; y++) {
            const size_t offset = (size_t)y * VGA_LCDCAM_HRES + (size_t)x1;
            esp_cache_msync(s_fb + offset, width,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        }
    }
}

void vga_lcdcam_s3_flush_all(void) {
    vga_lcdcam_s3_flush_region(0, 0, VGA_LCDCAM_HRES - 1, VGA_LCDCAM_VRES - 1);
}

void vga_lcdcam_s3_clear(uint8_t colour) {
    if (!s_fb) return;
    memset(s_fb, colour, VGA_LCDCAM_HRES * VGA_LCDCAM_VRES);
    vga_lcdcam_s3_flush_all();
}

void vga_lcdcam_s3_present_rgb332_2x(const uint8_t *src, int src_w, int src_h,
                                     int src_stride, int x1, int y1, int x2, int y2) {
    if (!s_fb || !src || src_w <= 0 || src_h <= 0 || src_stride < src_w) return;
    const int out_w = src_w * 2;
    const int out_h = src_h * 2;
    if (out_w > VGA_LCDCAM_HRES || out_h > VGA_LCDCAM_VRES) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= src_w) x2 = src_w - 1;
    if (y2 >= src_h) y2 = src_h - 1;
    if (x1 > x2 || y1 > y2) return;

    const int xoff = (VGA_LCDCAM_HRES - out_w) / 2;
    const int yoff = (VGA_LCDCAM_VRES - out_h) / 2;
    for (int y = y1; y <= y2; y++) {
        const uint8_t *s = src + (size_t)y * src_stride + x1;
        uint8_t *d0 = s_fb + (size_t)(yoff + y * 2) * VGA_LCDCAM_HRES + xoff + x1 * 2;
        uint8_t *d1 = d0 + VGA_LCDCAM_HRES;
        for (int x = x1; x <= x2; x++) {
            uint8_t c = *s++;
            *d0++ = c;
            *d0++ = c;
            *d1++ = c;
            *d1++ = c;
        }
    }
    vga_lcdcam_s3_flush_region(xoff + x1 * 2, yoff + y1 * 2,
                               xoff + x2 * 2 + 1, yoff + y2 * 2 + 1);
}

static uint8_t rgb332_dither3(uint8_t c, int x, int y) {
    static const uint8_t bayer4[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    const uint8_t threshold = bayer4[y & 3][x & 3];
    const uint8_t r = (c >> 5) & 0x07;
    const uint8_t g = (c >> 2) & 0x07;
    const uint8_t b = c & 0x03;
    const uint8_t rv = r == 7 ? 16 : (uint8_t)(r * 2);
    const uint8_t gv = g == 7 ? 16 : (uint8_t)(g * 2);
    const uint8_t bv = b == 3 ? 16 : (uint8_t)(b * 4);
    uint8_t out = 0;
    if (rv > threshold) out |= 0x80;
    if (gv > threshold) out |= 0x10;
    if (bv > threshold) out |= 0x02;
    return out;
}

void vga_lcdcam_s3_present_rgb332_2x_dither3(const uint8_t *src, int src_w, int src_h,
                                             int src_stride, int x1, int y1, int x2, int y2) {
    if (!s_fb || !src || src_w <= 0 || src_h <= 0 || src_stride < src_w) return;
    const int out_w = src_w * 2;
    const int out_h = src_h * 2;
    if (out_w > VGA_LCDCAM_HRES || out_h > VGA_LCDCAM_VRES) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= src_w) x2 = src_w - 1;
    if (y2 >= src_h) y2 = src_h - 1;
    if (x1 > x2 || y1 > y2) return;

    const int xoff = (VGA_LCDCAM_HRES - out_w) / 2;
    const int yoff = (VGA_LCDCAM_VRES - out_h) / 2;
    for (int y = y1; y <= y2; y++) {
        const uint8_t *s = src + (size_t)y * src_stride + x1;
        for (int x = x1; x <= x2; x++) {
            const uint8_t c = *s++;
            const int px = xoff + x * 2;
            const int py = yoff + y * 2;
            uint8_t *d0 = s_fb + (size_t)py * VGA_LCDCAM_HRES + px;
            uint8_t *d1 = d0 + VGA_LCDCAM_HRES;
            d0[0] = rgb332_dither3(c, px, py);
            d0[1] = rgb332_dither3(c, px + 1, py);
            d1[0] = rgb332_dither3(c, px, py + 1);
            d1[1] = rgb332_dither3(c, px + 1, py + 1);
        }
    }
    vga_lcdcam_s3_flush_region(xoff + x1 * 2, yoff + y1 * 2,
                               xoff + x2 * 2 + 1, yoff + y2 * 2 + 1);
}
