/*
 * drivers/vga_mode13h/cirrus_gd542x.h - Cirrus Logic CL-GD542x helpers.
 */

#ifndef DRIVERS_VGA_MODE13H_CIRRUS_GD542X_H
#define DRIVERS_VGA_MODE13H_CIRRUS_GD542X_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;
    const char * chip_name;
    uint8_t raw_cr27;
    uint8_t device_id;
    uint16_t memory_kb;
    uint8_t sr6;
    uint8_t sr0a;
    uint8_t sr0f;
    uint8_t sr7;
    uint8_t gr9;
    uint8_t gra;
    uint8_t grb;
} CirrusGd542xState;

typedef struct {
    uint16_t vesa_mode;
    uint8_t cirrus_mode;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t bpp;
    bool planar;
    /* True for modes the stock Cirrus BIOS doesn't know (e.g. panel-native
     * 800x480). cirrus_set_mode must skip the int 10h native call and
     * program the chip directly via write_svga_regs. */
    bool skip_bios_set;
} CirrusGd542xModeInfo;

bool cirrus_gd542x_probe(void);
CirrusGd542xState cirrus_gd542x_read_state(void);
const CirrusGd542xModeInfo * cirrus_gd542x_mode_info(uint16_t vesa_mode);
bool cirrus_gd542x_mode_supported(uint16_t vesa_mode);
bool cirrus_gd542x_set_mode(uint16_t vesa_mode);
void cirrus_gd542x_select_single_window(void);
void cirrus_gd542x_set_bank_4k(uint8_t bank);
void cirrus_gd542x_prepare_cpu_access(bool planar);
void cirrus_gd542x_reset_vga_compat(void);

/* Read the live scanline pitch (bytes per row) from CR13 + CR1B[4].
 * Pass planar=true for 4bpp planar modes, false for 8bpp packed. */
uint16_t cirrus_gd542x_read_line_bytes(bool planar);

#endif /* DRIVERS_VGA_MODE13H_CIRRUS_GD542X_H */
