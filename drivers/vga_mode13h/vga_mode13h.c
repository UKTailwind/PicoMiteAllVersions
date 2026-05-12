/*
 * drivers/vga_mode13h/vga_mode13h.c — PC386 VGA/VBE graphics driver.
 */

#include "vga_mode13h.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "ports/pc386/multiboot1.h"

extern const int CMM1map[16];
extern const mb1_info_t *pc386_multiboot_info;
extern uint16_t pc386_bios_video_int10(uint16_t ax, uint16_t bx, uint16_t cx,
                                       uint16_t dx, uint16_t es, uint16_t di);

#define VGA_WIDTH          320
#define VGA_HEIGHT         200
#define VGA_MAX_WIDTH      1024
#define VGA_MAX_HEIGHT     768
#define VGA_MAX_PIXELS     (VGA_MAX_WIDTH * VGA_MAX_HEIGHT)

#define VGA_AC_INDEX       0x3C0
#define VGA_AC_WRITE       0x3C0
#define VGA_MISC_WRITE     0x3C2
#define VGA_SEQ_INDEX      0x3C4
#define VGA_SEQ_DATA       0x3C5
#define VGA_DAC_READ_INDEX 0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA       0x3C9
#define VGA_MISC_READ      0x3CC
#define VGA_GC_INDEX       0x3CE
#define VGA_GC_DATA        0x3CF
#define VGA_CRTC_INDEX     0x3D4
#define VGA_CRTC_DATA      0x3D5
#define VGA_INSTAT_READ    0x3DA
#define VBE_MODE_INFO_ADDR 0x5400u

static volatile uint8_t *const vga_fb = (volatile uint8_t *)0xA0000u;
static volatile uint8_t *vesa_fb;
static uint32_t shadow[VGA_MAX_PIXELS];
static uint32_t fastgfx_back[VGA_MAX_PIXELS];
static bool vesa_available;
static bool vesa_active;
static bool vga12_active;
static bool vbe_bios_available;
static bool fastgfx_active;
static int fastgfx_fps;
static uint64_t fastgfx_next_sync_us;
static int vga_width = VGA_WIDTH;
static int vga_height = VGA_HEIGHT;
static int vga_hw_width = VGA_WIDTH;
static int vga_hw_height = VGA_HEIGHT;
static int vga_x_offset;
static int vga_y_offset;
static int vga_scale = 1;
static int vga_current_mode = 1;
static int vesa_width;
static int vesa_height;
static int vesa_pitch;
static int vesa_bpp;
static uint8_t vesa_red_pos, vesa_red_size;
static uint8_t vesa_green_pos, vesa_green_size;
static uint8_t vesa_blue_pos, vesa_blue_size;
static uint16_t vesa_bios_mode;

typedef struct {
    bool valid;
    uint16_t bios_mode;
    uint32_t framebuffer;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t red_pos, red_size;
    uint8_t green_pos, green_size;
    uint8_t blue_pos, blue_size;
} VesaModeInfo;

static VesaModeInfo vesa_mode_cache[2];

typedef enum {
    PC386_VIDEO_VGA13H,
    PC386_VIDEO_VGA12H,
    PC386_VIDEO_VBE,
} Pc386VideoBackend;

typedef struct {
    int mode;
    int width;
    int height;
    int hw_width;
    int hw_height;
    int x_offset;
    int y_offset;
    int scale;
    Pc386VideoBackend backend;
    uint16_t bios_mode;
    const char *name;
} Pc386VideoMode;

static const Pc386VideoMode pc386_modes[] = {
    {1,  320, 200,  320, 200,   0,  0, 1, PC386_VIDEO_VGA13H, 0x0013, "320x200"},
    {2,  640, 480,  640, 480,   0,  0, 1, PC386_VIDEO_VGA12H, 0x0012, "640x480"},
    {3,  800, 600,  800, 600,   0,  0, 1, PC386_VIDEO_VBE,    0x4115, "800x600"},
    {4, 1024, 768, 1024, 768,   0,  0, 1, PC386_VIDEO_VBE,    0x4118, "1024x768"},
    {5,  480, 480,  640, 480,  80,  0, 1, PC386_VIDEO_VGA12H, 0x0012, "480x480"},
    {6,  320, 320, 1024, 768, 192, 64, 2, PC386_VIDEO_VBE,    0x4118, "320x320x2"},
};

static uint8_t rgb_to_332(uint32_t rgb);
static int rgb_to_cmm1_index(uint32_t rgb);
static void vga12_put_pixel_xy(int x, int y, uint32_t rgb);
static const VesaModeInfo *vesa_get_mode_info(uint16_t bios_mode);

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t fit_to_mask(uint8_t value, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits >= 8) return value;
    return (uint32_t)((value * ((1u << bits) - 1u) + 127u) / 255u);
}

static uint16_t vbe_read16(int off) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)(VBE_MODE_INFO_ADDR + (uint32_t)off);
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t vbe_read32(int off) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)(VBE_MODE_INFO_ADDR + (uint32_t)off);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool vesa_probe_mode_info(uint16_t bios_mode, VesaModeInfo *out) {
    volatile uint8_t *info = (volatile uint8_t *)(uintptr_t)VBE_MODE_INFO_ADDR;
    for (int i = 0; i < 256; i++) info[i] = 0;

    (void)pc386_bios_video_int10(0x4F01, 0, bios_mode, 0, 0, VBE_MODE_INFO_ADDR);

    uint16_t attributes = vbe_read16(0);
    uint8_t bpp = info[25];
    if ((attributes & 0x0001u) == 0 || (attributes & 0x0080u) == 0) return false;
    if (bpp != 16 && bpp != 24 && bpp != 32) return false;
    if (vbe_read32(40) == 0) return false;
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->bios_mode = bios_mode;
    out->framebuffer = vbe_read32(40);
    out->pitch = vbe_read16(16);
    out->width = vbe_read16(18);
    out->height = vbe_read16(20);
    out->bpp = bpp;
    out->red_pos = info[32];
    out->red_size = info[31];
    out->green_pos = info[34];
    out->green_size = info[33];
    out->blue_pos = info[36];
    out->blue_size = info[35];
    return true;
}

static const VesaModeInfo *vesa_get_mode_info(uint16_t bios_mode) {
    VesaModeInfo *free_slot = NULL;
    for (size_t i = 0; i < sizeof(vesa_mode_cache) / sizeof(vesa_mode_cache[0]); i++) {
        if (vesa_mode_cache[i].valid && vesa_mode_cache[i].bios_mode == bios_mode) {
            return &vesa_mode_cache[i];
        }
        if (!vesa_mode_cache[i].valid && free_slot == NULL) free_slot = &vesa_mode_cache[i];
    }
    if (free_slot == NULL) return NULL;
    if (!vesa_probe_mode_info(bios_mode, free_slot)) return NULL;
    return free_slot;
}

static bool vesa_mode_supported(uint16_t bios_mode) {
    return vesa_get_mode_info(bios_mode) != NULL;
}

static void vesa_apply_mode_info(const VesaModeInfo *info) {
    vesa_bios_mode = info->bios_mode;
    vesa_fb = (volatile uint8_t *)(uintptr_t)info->framebuffer;
    vesa_pitch = (int)info->pitch;
    vesa_width = (int)info->width;
    vesa_height = (int)info->height;
    vesa_bpp = (int)info->bpp;
    vesa_red_pos = info->red_pos;
    vesa_red_size = info->red_size;
    vesa_green_pos = info->green_pos;
    vesa_green_size = info->green_size;
    vesa_blue_pos = info->blue_pos;
    vesa_blue_size = info->blue_size;
}

static bool vesa_set_bios_mode(uint16_t bios_mode) {
    const VesaModeInfo *info = vesa_get_mode_info(bios_mode);
    if (info == NULL) return false;
    (void)pc386_bios_video_int10(0x4F02, bios_mode, 0, 0, 0, 0);
    vesa_apply_mode_info(info);
    vesa_available = true;
    vbe_bios_available = true;
    return true;
}

static void vesa_put_pixel_xy(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= vesa_width || y >= vesa_height) return;

    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb & 0xFFu);
    uint32_t px =
        (fit_to_mask(r, vesa_red_size) << vesa_red_pos) |
        (fit_to_mask(g, vesa_green_size) << vesa_green_pos) |
        (fit_to_mask(b, vesa_blue_size) << vesa_blue_pos);

    volatile uint8_t *p = vesa_fb + (size_t)y * (size_t)vesa_pitch + (size_t)x * (size_t)(vesa_bpp / 8);
    if (vesa_bpp == 32) {
        *(volatile uint32_t *)p = px;
    } else if (vesa_bpp == 24) {
        p[0] = (uint8_t)(px & 0xFFu);
        p[1] = (uint8_t)((px >> 8) & 0xFFu);
        p[2] = (uint8_t)((px >> 16) & 0xFFu);
    } else if (vesa_bpp == 16) {
        *(volatile uint16_t *)p = (uint16_t)px;
    }
}

static void hw_put_pixel_xy(int x, int y, uint32_t rgb) {
    if (vesa_active) {
        vesa_put_pixel_xy(x, y, rgb);
        return;
    }
    if (vga12_active) {
        vga12_put_pixel_xy(x, y, rgb);
        return;
    }
    if (x < 0 || y < 0 || x >= vga_hw_width || y >= vga_hw_height) return;
    vga_fb[(size_t)y * (size_t)vga_hw_width + (size_t)x] = rgb_to_332(rgb);
}

static inline size_t visible_pixels(void) {
    return (size_t)vga_width * (size_t)vga_height;
}

static inline uint32_t *draw_target(void) {
    return fastgfx_active ? fastgfx_back : shadow;
}

static void hw_put_logical_pixel(int x, int y, uint32_t rgb) {
    int sx = vga_x_offset + x * vga_scale;
    int sy = vga_y_offset + y * vga_scale;
    for (int dy = 0; dy < vga_scale; dy++) {
        for (int dx = 0; dx < vga_scale; dx++) hw_put_pixel_xy(sx + dx, sy + dy, rgb);
    }
}

static void store_draw_pixel(size_t off, uint32_t rgb) {
    rgb &= 0x00FFFFFFu;
    if (fastgfx_active) {
        fastgfx_back[off] = rgb;
        return;
    }
    shadow[off] = rgb;
    int y = (int)(off / (size_t)vga_width);
    int x = (int)(off - (size_t)y * (size_t)vga_width);
    hw_put_logical_pixel(x, y, rgb);
}

static void present_shadow(void) {
    for (int y = 0; y < vga_height; y++) {
        size_t src = (size_t)y * (size_t)vga_width;
        for (int x = 0; x < vga_width; x++) {
            hw_put_logical_pixel(x, y, shadow[src + (size_t)x]);
        }
    }
}

static void vga_write_regs(const uint8_t *regs) {
    outb(VGA_MISC_WRITE, *regs++);

    for (uint8_t i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }

    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & 0x7F);

    for (uint8_t i = 0; i < 25; i++) {
        uint8_t val = *regs++;
        if (i == 0x03) val |= 0x80;
        if (i == 0x11) val &= 0x7F;
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, val);
    }

    for (uint8_t i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, *regs++);
    }

    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

static uint8_t scale_to_dac(uint8_t value) {
    return (uint8_t)((value * 63u + 127u) / 255u);
}

static uint8_t expand3(uint8_t value) {
    return (uint8_t)((value * 255u + 3u) / 7u);
}

static uint8_t expand2(uint8_t value) {
    return (uint8_t)((value * 255u + 1u) / 3u);
}

static void vga_load_332_palette(void) {
    outb(VGA_DAC_WRITE_INDEX, 0);
    for (unsigned i = 0; i < 256; i++) {
        uint8_t r = expand3((uint8_t)((i >> 5) & 0x07));
        uint8_t g = expand3((uint8_t)((i >> 2) & 0x07));
        uint8_t b = expand2((uint8_t)(i & 0x03));
        outb(VGA_DAC_DATA, scale_to_dac(r));
        outb(VGA_DAC_DATA, scale_to_dac(g));
        outb(VGA_DAC_DATA, scale_to_dac(b));
    }
}

static void vga_load_cmm1_palette(void) {
    for (uint8_t i = 0; i < 16; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, i);
    }
    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);

    outb(VGA_DAC_WRITE_INDEX, 0);
    for (unsigned i = 0; i < 16; i++) {
        uint32_t c = (uint32_t)CMM1map[i];
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)((c >> 16) & 0xFFu)));
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)((c >> 8) & 0xFFu)));
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)(c & 0xFFu)));
    }
}

static uint8_t rgb_to_332(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb & 0xFFu);
    return (uint8_t)((r & 0xE0u) | ((g & 0xE0u) >> 3) | (b >> 6));
}

static void normalize_rect(int *x1, int *y1, int *x2, int *y2) {
    if (*x2 < *x1) {
        int t = *x1;
        *x1 = *x2;
        *x2 = t;
    }
    if (*y2 < *y1) {
        int t = *y1;
        *y1 = *y2;
        *y2 = t;
    }
}

static bool clip_rect(int *x1, int *y1, int *x2, int *y2) {
    normalize_rect(x1, y1, x2, y2);
    if (*x2 < 0 || *y2 < 0 || *x1 >= vga_width || *y1 >= vga_height) return false;
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 >= vga_width) *x2 = vga_width - 1;
    if (*y2 >= vga_height) *y2 = vga_height - 1;
    return true;
}

static void mode13h_draw_pixel(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= vga_width || y >= vga_height) return;
    size_t off = (size_t)y * (size_t)vga_width + (size_t)x;
    store_draw_pixel(off, (uint32_t)c);
}

static void mode13h_draw_rectangle(int x1, int y1, int x2, int y2, int c) {
    if (!clip_rect(&x1, &y1, &x2, &y2)) return;
    uint32_t rgb = (uint32_t)c & 0xFFFFFFu;
    for (int y = y1; y <= y2; y++) {
        size_t off = (size_t)y * (size_t)vga_width + (size_t)x1;
        for (int x = x1; x <= x2; x++, off++) {
            store_draw_pixel(off, rgb);
        }
    }
}

static void mode13h_draw_bitmap(int x1, int y1, int width, int height,
                                int scale, int fc, int bc,
                                unsigned char *bitmap) {
    if (x1 >= vga_width || y1 >= vga_height ||
        x1 + width * scale < 0 || y1 + height * scale < 0) {
        return;
    }
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < scale; j++) {
            for (int k = 0; k < width; k++) {
                int bit = (bitmap[((i * width) + k) / 8] >>
                           (((height * width) - ((i * width) + k) - 1) % 8)) & 1;
                int colour = bit ? fc : bc;
                if (colour < 0) continue;
                for (int m = 0; m < scale; m++) {
                    mode13h_draw_pixel(x1 + k * scale + m, y1 + i * scale + j, colour);
                }
            }
        }
    }
}

static void mode13h_scroll_lcd(int lines) {
    if (lines == 0) return;
    if (lines >= vga_height || lines <= -vga_height) {
        mode13h_draw_rectangle(0, 0, vga_width - 1, vga_height - 1, PromptBC);
        return;
    }

    if (lines > 0) {
        size_t keep = (size_t)(vga_height - lines) * (size_t)vga_width;
        uint32_t *target = draw_target();
        memmove(target, target + (size_t)lines * (size_t)vga_width, keep * sizeof(target[0]));
        if (!fastgfx_active) present_shadow();
        mode13h_draw_rectangle(0, vga_height - lines, vga_width - 1, vga_height - 1, PromptBC);
    } else {
        lines = -lines;
        size_t keep = (size_t)(vga_height - lines) * (size_t)vga_width;
        uint32_t *target = draw_target();
        memmove(target + (size_t)lines * (size_t)vga_width, target, keep * sizeof(target[0]));
        if (!fastgfx_active) present_shadow();
        mode13h_draw_rectangle(0, 0, vga_width - 1, lines - 1, PromptBC);
    }
}

static void mode13h_draw_buffer(int x1, int y1, int x2, int y2, unsigned char *p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint32_t b = *p++;
            uint32_t g = *p++;
            uint32_t r = *p++;
            if (x >= 0 && y >= 0 && x < vga_width && y < vga_height) {
                mode13h_draw_pixel(x, y, (int)((r << 16) | (g << 8) | b));
            }
        }
    }
}

static void mode13h_read_buffer(int x1, int y1, int x2, int y2, unsigned char *p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint32_t rgb = vga_mode13h_get_pixel(x, y);
            *p++ = (uint8_t)(rgb & 0xFFu);
            *p++ = (uint8_t)((rgb >> 8) & 0xFFu);
            *p++ = (uint8_t)((rgb >> 16) & 0xFFu);
        }
    }
}

static int rgb_to_cmm1_index(uint32_t rgb) {
    int best = 0;
    uint32_t best_dist = UINT32_MAX;
    int r = (int)((rgb >> 16) & 0xFFu);
    int g = (int)((rgb >> 8) & 0xFFu);
    int b = (int)(rgb & 0xFFu);
    for (int i = 0; i < 16; i++) {
        uint32_t c = (uint32_t)CMM1map[i];
        int dr = r - (int)((c >> 16) & 0xFFu);
        int dg = g - (int)((c >> 8) & 0xFFu);
        int db = b - (int)(c & 0xFFu);
        uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static void vga12_prepare_pixel_write(void) {
    outb(VGA_SEQ_INDEX, 0x02);
    outb(VGA_SEQ_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x01);
    outb(VGA_GC_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x03);
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, 0x00);
}

static void vga12_clear_black(void) {
    outb(VGA_SEQ_INDEX, 0x02);
    outb(VGA_SEQ_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x01);
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x03);
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, 0xFF);
    for (size_t i = 0; i < 80u * 480u; i++) vga_fb[i] = 0;
    vga12_prepare_pixel_write();
}

static void vga12_put_pixel_xy(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= vga_hw_width || y >= vga_hw_height) return;

    uint8_t colour = (uint8_t)(rgb_to_cmm1_index(rgb) & 0x0F);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    volatile uint8_t *p = vga_fb + (size_t)y * 80u + (size_t)(x >> 3);

    outb(VGA_GC_INDEX, 0x00);
    outb(VGA_GC_DATA, colour);
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, mask);
    (void)*p;
    *p = 0xFF;
}

static void mode13h_draw_buffer_fast(int x1, int y1, int x2, int y2,
                                     int blank, unsigned char *p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    int toggle = 0;
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint8_t packed = *p;
            uint8_t nibble = toggle ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0F);
            if (toggle) p++;
            toggle = !toggle;
            if (blank != -1 && nibble == sprite_transparent) continue;
            if (x >= 0 && y >= 0 && x < vga_width && y < vga_height) {
                mode13h_draw_pixel(x, y, CMM1map[nibble & 0x0F]);
            }
        }
    }
}

static void mode13h_read_buffer_fast(int x1, int y1, int x2, int y2, unsigned char *p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    int toggle = 0;
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint8_t nibble = (uint8_t)rgb_to_cmm1_index(vga_mode13h_get_pixel(x, y));
            if (toggle) {
                *p++ |= (uint8_t)(nibble << 4);
            } else {
                *p = nibble;
            }
            toggle = !toggle;
        }
    }
}

uint32_t vga_mode13h_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= vga_width || y >= vga_height) return 0;
    uint32_t *target = draw_target();
    return target[(size_t)y * (size_t)vga_width + (size_t)x] & 0xFFFFFFu;
}

static const Pc386VideoMode *find_mode(int mode) {
    for (size_t i = 0; i < sizeof(pc386_modes) / sizeof(pc386_modes[0]); i++) {
        if (pc386_modes[i].mode == mode) return &pc386_modes[i];
    }
    return NULL;
}

void vga_mode13h_set_mode(int mode, int clear) {
    const Pc386VideoMode *m = find_mode(mode);
    if (m == NULL) error("Invalid mode");
    vga_mode13h_fastgfx_reset();

    if (m->backend == PC386_VIDEO_VGA13H) {
        if (vga_current_mode != 1) pc386_bios_video_int10(m->bios_mode, 0, 0, 0, 0, 0);
        vesa_active = false;
        vga12_active = false;
        vga_load_332_palette();
    } else if (m->backend == PC386_VIDEO_VGA12H) {
        pc386_bios_video_int10(m->bios_mode, 0, 0, 0, 0, 0);
        vesa_active = false;
        vga12_active = true;
        vga_load_cmm1_palette();
        vga12_prepare_pixel_write();
    } else {
        if (!vesa_set_bios_mode(m->bios_mode)) error("Mode requires VBE");
        if (m->hw_width > vesa_width || m->hw_height > vesa_height) error("Mode not available");
        vesa_active = true;
        vga12_active = false;
    }

    vga_width = m->width;
    vga_height = m->height;
    vga_hw_width = m->hw_width;
    vga_hw_height = m->hw_height;
    vga_x_offset = m->x_offset;
    vga_y_offset = m->y_offset;
    vga_scale = m->scale;
    vga_current_mode = mode;
    DisplayHRes = HRes = vga_width;
    DisplayVRes = VRes = vga_height;
    if (Option.DISPLAY_CONSOLE) {
        SetFont(Option.DefaultFont);
        Option.Width = HRes / gui_font_width;
        Option.Height = VRes / gui_font_height;
    }
    if (clear) {
        memset(shadow, 0, sizeof(shadow));
        if (vga12_active) {
            vga12_clear_black();
        } else {
            int clear_width = vesa_active ? vesa_width : vga_hw_width;
            int clear_height = vesa_active ? vesa_height : vga_hw_height;
            for (int y = 0; y < clear_height; y++) {
                for (int x = 0; x < clear_width; x++) {
                    hw_put_pixel_xy(x, y, 0);
                }
            }
        }
        CurrentX = CurrentY = 0;
    }
}

void vga_mode13h_fastgfx_create(void) {
    size_t pixels = visible_pixels();
    memcpy(fastgfx_back, shadow, pixels * sizeof(fastgfx_back[0]));
    fastgfx_active = true;
    fastgfx_next_sync_us = 0;
}

void vga_mode13h_fastgfx_close(void) {
    if (!fastgfx_active) error("FASTGFX not active");
    vga_mode13h_fastgfx_swap();
    fastgfx_active = false;
    fastgfx_next_sync_us = 0;
}

void vga_mode13h_fastgfx_reset(void) {
    fastgfx_active = false;
    fastgfx_next_sync_us = 0;
}

void vga_mode13h_fastgfx_set_fps(int fps) {
    if (fps < 1 || fps > 1000) error("Number out of bounds");
    fastgfx_fps = fps;
    fastgfx_next_sync_us = 0;
}

void vga_mode13h_fastgfx_swap(void) {
    if (!fastgfx_active) return;
    size_t pixels = visible_pixels();
    memcpy(shadow, fastgfx_back, pixels * sizeof(shadow[0]));
    present_shadow();
}

void vga_mode13h_fastgfx_sync(void) {
    if (!fastgfx_active || fastgfx_fps <= 0) return;
    extern uint64_t hal_time_us_64(void);
    extern void hal_time_sleep_us(uint32_t us);
    uint64_t frame_us = 1000000ULL / (uint64_t)fastgfx_fps;
    uint64_t now = hal_time_us_64();
    if (fastgfx_next_sync_us == 0) fastgfx_next_sync_us = now + frame_us;
    if (now < fastgfx_next_sync_us) hal_time_sleep_us((uint32_t)(fastgfx_next_sync_us - now));
    fastgfx_next_sync_us += frame_us;
    now = hal_time_us_64();
    if (now > fastgfx_next_sync_us + frame_us * 2u) fastgfx_next_sync_us = now + frame_us;
}

void cmd_mode(void) {
    if (cmdline == NULL || *cmdline == 0) {
        MMPrintString("MODE ");
        PInt(vga_current_mode);
        MMPrintString(" ");
        MMPrintString((char *)find_mode(vga_current_mode)->name);
        MMPrintString("\r\n");
        MMPrintString("1:320x200 2:640x480 5:480x480");
        if (vesa_mode_supported(0x4115)) MMPrintString(" 3:800x600");
        if (vesa_mode_supported(0x4118)) MMPrintString(" 4:1024x768 6:320x320x2");
        MMPrintString("\r\n");
        return;
    }
    vga_mode13h_set_mode(getint(cmdline, 1, 6), true);
}

void vga_mode13h_init(void) {
    const mb1_info_t *mb = pc386_multiboot_info;
    if (mb && (mb->flags & MB1_INFO_FRAMEBUFFER) &&
        mb->framebuffer_type == 1 &&
        mb->framebuffer_addr != 0 &&
        mb->framebuffer_width <= VGA_MAX_WIDTH &&
        mb->framebuffer_height <= VGA_MAX_HEIGHT &&
        (mb->framebuffer_bpp == 16 || mb->framebuffer_bpp == 24 || mb->framebuffer_bpp == 32)) {
        vesa_fb = (volatile uint8_t *)(uintptr_t)mb->framebuffer_addr;
        vesa_width = (int)mb->framebuffer_width;
        vesa_height = (int)mb->framebuffer_height;
        vesa_pitch = (int)mb->framebuffer_pitch;
        vesa_bpp = (int)mb->framebuffer_bpp;
        vesa_red_pos = mb->color_info[0];
        vesa_red_size = mb->color_info[1];
        vesa_green_pos = mb->color_info[2];
        vesa_green_size = mb->color_info[3];
        vesa_blue_pos = mb->color_info[4];
        vesa_blue_size = mb->color_info[5];
        vesa_available = true;
        vbe_bios_available = true;
    }
    bool vbe800 = vesa_mode_supported(0x4115);
    bool vbe1024 = vesa_mode_supported(0x4118);
    vbe_bios_available = vbe_bios_available || vbe800 || vbe1024;

    Option.DISPLAY_TYPE = DISP_USER;
    DrawPixel = mode13h_draw_pixel;
    DrawRectangle = mode13h_draw_rectangle;
    DrawBitmap = mode13h_draw_bitmap;
    ScrollLCD = mode13h_scroll_lcd;
    DrawBuffer = mode13h_draw_buffer;
    ReadBuffer = mode13h_read_buffer;
    DrawBufferFast = mode13h_draw_buffer_fast;
    ReadBufferFast = mode13h_read_buffer_fast;
    DrawBLITBuffer = mode13h_draw_buffer;
    ReadBLITBuffer = mode13h_read_buffer;
    vga_mode13h_set_mode(1, true);
}
