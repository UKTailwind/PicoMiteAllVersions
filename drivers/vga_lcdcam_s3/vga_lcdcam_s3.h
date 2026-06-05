/*
 * drivers/vga_lcdcam_s3/vga_lcdcam_s3.h — ESP32-S3 VGA scanout via LCD_CAM.
 *
 * Generates a 640x480@60 VGA signal out of the ESP32-S3 LCD_CAM (RGB
 * panel) peripheral into an external resistor-ladder DAC (e.g. a VGA666
 * board wired for RGB332). The peripheral owns a single PSRAM frame
 * buffer of HRes*VRes bytes (one RGB332 byte per pixel) and refreshes it
 * continuously by DMA; MMBasic draws straight into that buffer.
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
 * pclk_gpio is required by the peripheral but is not wired to the DAC
 * board; pass any spare GPIO. */
typedef struct {
    int data_gpio[8];
    int hsync_gpio;
    int vsync_gpio;
    int pclk_gpio;
} vga_lcdcam_pins_t;

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

#ifdef __cplusplus
}
#endif

#endif /* VGA_LCDCAM_S3_H */
