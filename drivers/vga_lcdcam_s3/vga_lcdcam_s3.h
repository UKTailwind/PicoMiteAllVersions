/*
 * drivers/vga_lcdcam_s3/vga_lcdcam_s3.h — ESP32-S3 VGA scanout via LCD_CAM.
 *
 * Generates a 640x480@60 VGA signal out of the ESP32-S3 LCD_CAM (RGB
 * panel) peripheral into an external resistor-ladder DAC (e.g. a VGA666
 * board wired for RGB332). The peripheral owns a single PSRAM frame
 * buffer of HRes*VRes bytes (one RGB332 byte per pixel), then feeds LCD
 * DMA through small internal-RAM bounce buffers. MMBasic draws straight
 * into the PSRAM framebuffer.
 *
 * Pixel format is RGB332: bits 7..5 red, 4..2 green, 1..0 blue — matching
 * the RGB332() packer and the *256 draw routines in core/drivers.
 */

#ifndef VGA_LCDCAM_S3_H
#define VGA_LCDCAM_S3_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Active VGA resolution. The framebuffer is one byte per pixel. */
#define VGA_LCDCAM_HRES 640
#define VGA_LCDCAM_VRES 480

/* Raw chip GPIO numbers for the eight RGB332 data lines plus the sync
 * pins. data_gpio[0] is the least-significant bus bit (blue LSB) and
 * data_gpio[7] the most-significant (red MSB), matching how the RGB332
 * byte is laid out (bit 0 -> data_gpio[0] ... bit 7 -> data_gpio[7]).
 * Unused data lines (the five not wired in 3-bit mode) and pclk_gpio may
 * be set to -1: LCD_CAM skips unrouted data lines and paces its DMA from
 * pclk_hz without a clock pin, which a resistor-DAC VGA does not need. */
typedef struct {
    int data_gpio[8];
    int hsync_gpio;
    int vsync_gpio;
    int pclk_gpio;
    uint8_t sync_flags;
    uint8_t clock_mode;
    uint8_t drive_cap;
} vga_lcdcam_pins_t;

#define VGA_LCDCAM_SYNC_HSYNC_IDLE_LOW 0x01u
#define VGA_LCDCAM_SYNC_VSYNC_IDLE_LOW 0x02u

#define VGA_LCDCAM_CLOCK_STANDARD 0u
#define VGA_LCDCAM_CLOCK_PLL240   1u
#define VGA_LCDCAM_CLOCK_25MHZ    2u
#define VGA_LCDCAM_CLOCK_25MHZ240 3u

/*
 * Bring up the LCD_CAM RGB panel with VGA 640x480@60 timing and the
 * supplied pin map. On success the continuously-scanned PSRAM frame
 * buffer is returned via *fb_out and the peripheral starts refreshing
 * immediately. Returns true on success, false if the peripheral could
 * not be created (the caller should leave the display disabled).
 *
 * Safe to call once; a second call returns the existing framebuffer
 * without re-initialising.
 */
bool vga_lcdcam_s3_init(const vga_lcdcam_pins_t *pins, uint8_t **fb_out);

/* The live framebuffer pointer, or NULL if init has not succeeded. */
uint8_t *vga_lcdcam_s3_framebuffer(void);

/* True once the panel is running. */
bool vga_lcdcam_s3_active(void);

void vga_lcdcam_s3_flush_region(int x1, int y1, int x2, int y2);
void vga_lcdcam_s3_flush_all(void);
void vga_lcdcam_s3_clear(uint8_t colour);
void vga_lcdcam_s3_present_rgb332_2x(const uint8_t *src, int src_w, int src_h,
                                     int src_stride, int x1, int y1, int x2, int y2);
void vga_lcdcam_s3_present_rgb332_2x_dither3(const uint8_t *src, int src_w, int src_h,
                                             int src_stride, int x1, int y1, int x2, int y2);

#ifdef __cplusplus
}
#endif

#endif /* VGA_LCDCAM_S3_H */
