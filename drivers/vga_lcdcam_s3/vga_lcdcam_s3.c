/*
 * drivers/vga_lcdcam_s3/vga_lcdcam_s3.c — ESP32-S3 VGA scanout via LCD_CAM.
 *
 * Drives an external resistor-ladder DAC (VGA666-style, wired for RGB332)
 * with a standard 640x480@60 VGA signal using the ESP32-S3 LCD_CAM RGB
 * panel peripheral. The peripheral allocates a single RGB332 frame buffer
 * in PSRAM and refreshes it continuously by DMA; a small DRAM bounce
 * buffer keeps the DMA fed without stalling on PSRAM latency.
 */

#include "vga_lcdcam_s3.h"

#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "vga_lcdcam";

static esp_lcd_panel_handle_t s_panel = NULL;
static uint8_t *s_fb = NULL;

/*
 * Standard VESA 640x480@60 timing. Pixel clock is nominally 25.175 MHz;
 * the LCD_CAM clock divider lands on the nearest achievable frequency,
 * which every VGA monitor tested tolerates. Both sync signals are
 * negative polarity (idle high, pulse low), so hsync/vsync_idle_low stay
 * 0. de is unused — a resistor DAC has no data-enable input.
 */
static esp_lcd_rgb_timing_t vga_640x480_60(void) {
    esp_lcd_rgb_timing_t t = {
        .pclk_hz = 25175000,
        .h_res = VGA_LCDCAM_HRES,
        .v_res = VGA_LCDCAM_VRES,
        .hsync_pulse_width = 96,
        .hsync_back_porch = 48,
        .hsync_front_porch = 16,
        .vsync_pulse_width = 2,
        .vsync_back_porch = 33,
        .vsync_front_porch = 10,
        .flags = {
            .hsync_idle_low = 0,
            .vsync_idle_low = 0,
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
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = vga_640x480_60(),
        .data_width = 8,      /* RGB332 — one byte per pixel out an 8-bit bus */
        .bits_per_pixel = 8,
        .num_fbs = 1,
        /* DRAM bounce buffer (10 lines). DMA pulls from here far faster
         * than from the PSRAM framebuffer, preventing scanline tearing. */
        .bounce_buffer_size_px = VGA_LCDCAM_HRES * 10,
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
    if (fb_out) *fb_out = s_fb;
    ESP_LOGI(TAG, "VGA %dx%d RGB332 up; fb=%p", VGA_LCDCAM_HRES, VGA_LCDCAM_VRES, s_fb);
    return true;
}

uint8_t *vga_lcdcam_s3_framebuffer(void) { return s_fb; }

bool vga_lcdcam_s3_active(void) { return s_panel != NULL && s_fb != NULL; }
