/*
 * drivers/vga_mode13h/vga_mode13h.c - PC386 linear framebuffer graphics.
 *
 * Mode 1 uses classic VGA mode 13h. Modes 2..6 are routed through a
 * selected SVGA backend: VBE for emulators, Cirrus GD542x for Pocket386.
 */

#include "vga_mode13h.h"
#include "cirrus_gd542x.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "ports/pc386/multiboot1.h"

extern const int CMM1map[16];
extern const mb1_info_t * pc386_multiboot_info;
extern uint16_t pc386_bios_video_int10(uint16_t ax, uint16_t bx, uint16_t cx,
                                       uint16_t dx, uint16_t es, uint16_t di);

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MAX_WIDTH 1024
#define VGA_MAX_HEIGHT 768
#define VGA_MAX_PIXELS (VGA_MAX_WIDTH * VGA_MAX_HEIGHT)
#define VBE_MODE_INFO_ADDR 0x5400u

#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA 0x3C9
#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT 0x01CF
#define BGA_INDEX_ID 0
#define BGA_INDEX_XRES 1
#define BGA_INDEX_YRES 2
#define BGA_INDEX_BPP 3
#define BGA_INDEX_ENABLE 4
#define BGA_INDEX_BANK 5
#define BGA_INDEX_VIRT_WIDTH 6
#define BGA_INDEX_VIRT_HEIGHT 7
#define BGA_INDEX_X_OFFSET 8
#define BGA_INDEX_Y_OFFSET 9
#define BGA_ID_MIN 0xB0C0
#define BGA_ID_MAX 0xB0C5
#define BGA_ENABLED 0x0001
#define BGA_LFB_ENABLED 0x0040

static volatile uint8_t * const vga_fb = (volatile uint8_t *)0xA0000u;
static volatile uint8_t * fb;
static uint8_t shadow[VGA_MAX_PIXELS];
static uint8_t fastgfx_back[VGA_MAX_PIXELS];
static bool linear_fb_available;
static bool fastgfx_active;
static int fastgfx_fps;
static uint64_t fastgfx_next_sync_us;
static int vga_width = VGA_WIDTH;
static int vga_height = VGA_HEIGHT;
static int fb_width = VGA_WIDTH;
static int fb_height = VGA_HEIGHT;
static int fb_pitch = VGA_WIDTH;
static int fb_bpp = 8;
static bool banked_fb_available;
static bool planar_fb_available;
static uint32_t bank_granularity_bytes = 65536;
static uint32_t bank_window_bytes = 65536;
static uint32_t bank_current = UINT32_MAX;
/* Hardware scroll: chip Start Address in bytes from VRAM offset 0.
 * Logical fb offsets are translated to physical VRAM via this value so
 * scrolling becomes a CRTC register update instead of a full screen blit. */
static uint32_t fb_start_byte = 0;
static uint32_t fb_vram_size = 524288u; /* GD5420 = 512KB */
static bool fb_hw_scroll_capable = false;
static int vga_x_offset;
static int vga_y_offset;
static int vga_scale = 1;
static int vga_current_mode = 1;
static uint8_t fb_red_pos, fb_red_size;
static uint8_t fb_green_pos, fb_green_size;
static uint8_t fb_blue_pos, fb_blue_size;

typedef enum {
    PC386_VIDEO_VGA13H,
    PC386_VIDEO_SVGA,
} Pc386VideoBackend;

typedef enum {
    PC386_SVGA_VESA,
    PC386_SVGA_CIRRUS,
} Pc386SvgaBackend;

typedef struct {
    bool valid;
    uint16_t bios_mode;
    uint16_t probe_status;
    uint16_t attributes;
    uint32_t framebuffer;
    uint8_t win_a_attributes;
    uint16_t win_granularity_kb;
    uint16_t win_size_kb;
    uint16_t win_a_segment;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    bool linear_supported;
    bool banked_supported;
    uint8_t red_pos, red_size;
    uint8_t green_pos, green_size;
    uint8_t blue_pos, blue_size;
} VesaModeInfo;

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
    uint16_t fallback_bios_mode;
    const char * name;
} Pc386VideoMode;

static VesaModeInfo vesa_mode_cache[4];
static Pc386SvgaBackend svga_backend = PC386_SVGA_VESA;

static const Pc386VideoMode pc386_modes[] = {
    {1, 320, 200, 320, 200, 0, 0, 1, PC386_VIDEO_VGA13H, 0x0013, 0x0000, "320x200"},
    {2, 640, 480, 640, 480, 0, 0, 1, PC386_VIDEO_SVGA, 0x0101, 0x0000, "640x480"},
    {3, 720, 400, 720, 400, 0, 0, 1, PC386_VIDEO_SVGA, 0x0103, 0x0000, "720x400"},
    {4, 320, 320, 720, 400, 200, 40, 1, PC386_VIDEO_SVGA, 0x0103, 0x0000, "320x320"},
    {5, 480, 480, 640, 480, 80, 0, 1, PC386_VIDEO_SVGA, 0x0101, 0x0000, "480x480"},
    {6, 320, 320, 1024, 768, 192, 64, 2, PC386_VIDEO_SVGA, 0x0104, 0x0000, "320x320x2"},
};

static uint16_t vbe_last_mode;
static uint16_t vbe_last_lfb_status;
static uint16_t vbe_last_banked_status;
static uint16_t vbe_last_bank_status;
static const char * vbe_last_failure = "none";
static uint16_t cirrus_last_native_set_ax;
static uint16_t cirrus_last_native_get_ax;
static bool cirrus_last_native_used;

static uint8_t rgb_to_332(uint32_t rgb);
static uint32_t rgb_from_332(uint8_t idx);
static int rgb_to_cmm1_index(uint32_t rgb);

static uint32_t fit_to_mask(uint8_t value, uint8_t bits) {
    if (bits == 0) return 0;
    if (bits >= 8) return value;
    return (uint32_t)((value * ((1u << bits) - 1u) + 127u) / 255u);
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void vga_write_regs(const uint8_t * regs) {
    outb(0x3C2, *regs++);

    for (uint8_t i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, *regs++);
    }

    outb(0x3D4, 0x03);
    outb(0x3D5, (uint8_t)(inb(0x3D5) | 0x80));
    outb(0x3D4, 0x11);
    outb(0x3D5, (uint8_t)(inb(0x3D5) & ~0x80));

    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, *regs++);
    }

    for (uint8_t i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, *regs++);
    }

    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, *regs++);
    }

    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

static void vga_program_mode13h_registers(void) {
    static const uint8_t mode13h_regs[] = {
        0x63,
        0x03,
        0x01,
        0x0F,
        0x00,
        0x0E,
        0x5F,
        0x4F,
        0x50,
        0x82,
        0x54,
        0x80,
        0xBF,
        0x1F,
        0x00,
        0x41,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x9C,
        0x0E,
        0x8F,
        0x28,
        0x40,
        0x96,
        0xB9,
        0xA3,
        0xFF,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x40,
        0x05,
        0x0F,
        0xFF,
        0x00,
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B,
        0x0C,
        0x0D,
        0x0E,
        0x0F,
        0x41,
        0x00,
        0x0F,
        0x00,
        0x00,
    };
    vga_write_regs(mode13h_regs);
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
    outb(VGA_DAC_WRITE_INDEX, 0);
    for (unsigned i = 0; i < 16; i++) {
        uint32_t rgb = (uint32_t)CMM1map[i] & 0x00FFFFFFu;
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)((rgb >> 16) & 0xFFu)));
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)((rgb >> 8) & 0xFFu)));
        outb(VGA_DAC_DATA, scale_to_dac((uint8_t)(rgb & 0xFFu)));
    }
}

static uint16_t vbe_read16(int off) {
    volatile uint8_t * p = (volatile uint8_t *)(uintptr_t)(VBE_MODE_INFO_ADDR + (uint32_t)off);
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t vbe_read32(int off) {
    volatile uint8_t * p = (volatile uint8_t *)(uintptr_t)(VBE_MODE_INFO_ADDR + (uint32_t)off);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool vesa_probe_mode_info(uint16_t bios_mode, VesaModeInfo * out) {
    volatile uint8_t * info = (volatile uint8_t *)(uintptr_t)VBE_MODE_INFO_ADDR;
    for (int i = 0; i < 256; i++) info[i] = 0;

    uint16_t status = pc386_bios_video_int10(0x4F01, 0, bios_mode, 0, 0, VBE_MODE_INFO_ADDR);

    uint16_t attributes = vbe_read16(0);
    uint8_t win_a_attributes = info[2];
    uint8_t bpp = info[25];
    bool linear_supported = bpp != 4 && (attributes & 0x0080u) != 0 && vbe_read32(40) != 0;
    bool banked_supported = (win_a_attributes & 0x05u) == 0x05u && vbe_read16(8) != 0;
    if ((attributes & 0x0001u) == 0) return false;
    if (bpp != 4 && bpp != 8 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return false;
    if (!linear_supported && !banked_supported) return false;
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->bios_mode = bios_mode;
    out->probe_status = status;
    out->attributes = attributes;
    out->framebuffer = vbe_read32(40);
    out->win_a_attributes = win_a_attributes;
    out->win_granularity_kb = vbe_read16(4);
    out->win_size_kb = vbe_read16(6);
    out->win_a_segment = vbe_read16(8);
    out->pitch = vbe_read16(16);
    out->width = vbe_read16(18);
    out->height = vbe_read16(20);
    out->bpp = bpp;
    out->linear_supported = linear_supported;
    out->banked_supported = banked_supported;
    out->red_pos = info[32];
    out->red_size = info[31];
    out->green_pos = info[34];
    out->green_size = info[33];
    out->blue_pos = info[36];
    out->blue_size = info[35];
    return true;
}

static const VesaModeInfo * vesa_get_mode_info(uint16_t bios_mode) {
    VesaModeInfo * free_slot = NULL;
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

static void vesa_apply_mode_info(const VesaModeInfo * info) {
    fb = (volatile uint8_t *)(uintptr_t)info->framebuffer;
    fb_width = (int)info->width;
    fb_height = (int)info->height;
    fb_pitch = (int)info->pitch;
    fb_bpp = (int)info->bpp;
    fb_red_pos = info->red_pos;
    fb_red_size = info->red_size;
    fb_green_pos = info->green_pos;
    fb_green_size = info->green_size;
    fb_blue_pos = info->blue_pos;
    fb_blue_size = info->blue_size;
}

static bool vesa_set_bank(uint32_t bank) {
    if (bank == bank_current) return true;
    vbe_last_bank_status = pc386_bios_video_int10(0x4F05, 0, 0, (uint16_t)bank, 0, 0);
    if (vbe_last_bank_status != 0x004Fu) {
        vbe_last_failure = "4F05 bank";
        return false;
    }
    bank_current = bank;
    return true;
}

static uint32_t banked_window_base(uint32_t offset) {
    uint32_t win = bank_window_bytes ? bank_window_bytes : 65536u;
    return (offset / win) * win;
}

static uint32_t banked_bank_for_offset(uint32_t offset) {
    uint32_t gran = bank_granularity_bytes ? bank_granularity_bytes : 65536u;
    return banked_window_base(offset) / gran;
}

static uint32_t banked_window_offset(uint32_t offset) {
    return offset - banked_window_base(offset);
}

static void vram_write_bytes(volatile uint8_t * dst, const uint8_t * src, uint32_t len) {
    uint32_t dwords = len >> 2;
    uint32_t tail = len & 3u;
    if (dwords) {
        __asm__ volatile("rep movsl"
                         : "+D"(dst), "+S"(src), "+c"(dwords)
                         : : "memory");
    }
    if (tail) {
        __asm__ volatile("rep movsb"
                         : "+D"(dst), "+S"(src), "+c"(tail)
                         : : "memory");
    }
}

static void vram_fill_bytes(volatile uint8_t * dst, uint8_t value, uint32_t len) {
    uint32_t pattern = (uint32_t)value * 0x01010101u;
    uint32_t dwords = len >> 2;
    uint32_t tail = len & 3u;
    if (dwords) {
        __asm__ volatile("rep stosl"
                         : "+D"(dst), "+c"(dwords)
                         : "a"(pattern)
                         : "memory");
    }
    if (tail) {
        __asm__ volatile("rep stosb"
                         : "+D"(dst), "+c"(tail)
                         : "a"((uint32_t)value)
                         : "memory");
    }
}

static inline uint32_t fb_offset_logical_to_phys(uint32_t logical) {
    if (!fb_hw_scroll_capable || fb_vram_size == 0) return logical;
    uint32_t phys = logical + fb_start_byte;
    if (phys >= fb_vram_size) phys -= fb_vram_size;
    return phys;
}

static void cirrus_set_start_address_bytes(uint32_t byte_offset) {
    /* CR12 (high) / CR13 (low) hold a 16-bit Start Address in dwords for
     * chain-4 8bpp packed mode. 16 bits = 65535 dwords = 262140 bytes,
     * enough for the no-wrap scroll range of modes 2 (max 217 KB) and 3
     * (max 236 KB). Cirrus CR1B[0,2,3] extensions are not needed here. */
    uint32_t addr_dwords = byte_offset >> 2;
    outb(0x3D4, 0x0D);
    outb(0x3D5, (uint8_t)(addr_dwords & 0xFFu));
    outb(0x3D4, 0x0C);
    outb(0x3D5, (uint8_t)((addr_dwords >> 8) & 0xFFu));
}

static volatile uint8_t * banked_ptr(uint32_t offset) {
    offset = fb_offset_logical_to_phys(offset);
    uint32_t bank = banked_bank_for_offset(offset);
    uint32_t window_off = banked_window_offset(offset);
    if (window_off >= bank_window_bytes) return NULL;
    if (svga_backend == PC386_SVGA_CIRRUS) {
        if (bank != bank_current) {
            cirrus_gd542x_set_bank_4k((uint8_t)bank);
            bank_current = bank;
        }
    } else if (!vesa_set_bank(bank)) {
        return NULL;
    }
    return fb + window_off;
}

static bool banked_write_bytes(uint32_t offset, const uint8_t * src, uint32_t len) {
    while (len != 0) {
        uint32_t window_off = banked_window_offset(offset);
        uint32_t chunk = bank_window_bytes - window_off;
        if (chunk > len) chunk = len;
        volatile uint8_t * dst = banked_ptr(offset);
        if (dst == NULL) return false;
        vram_write_bytes(dst, src, chunk);
        offset += chunk;
        src += chunk;
        len -= chunk;
    }
    return true;
}

static bool banked_fill_bytes(uint32_t offset, uint8_t value, uint32_t len) {
    while (len != 0) {
        uint32_t window_off = banked_window_offset(offset);
        uint32_t chunk = bank_window_bytes - window_off;
        if (chunk > len) chunk = len;
        volatile uint8_t * dst = banked_ptr(offset);
        if (dst == NULL) return false;
        vram_fill_bytes(dst, value, chunk);
        offset += chunk;
        len -= chunk;
    }
    return true;
}

static uint16_t bga_read(uint16_t index) {
    outw(BGA_INDEX_PORT, index);
    return inw(BGA_DATA_PORT);
}

static void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_INDEX_PORT, index);
    outw(BGA_DATA_PORT, value);
}

static bool bga_available(void) {
    uint16_t id = bga_read(BGA_INDEX_ID);
    return id >= BGA_ID_MIN && id <= BGA_ID_MAX;
}

static void print_hex16(uint16_t value) {
    MMPrintString("0x");
    PIntH(value);
}

static void print_hex32(uint32_t value) {
    MMPrintString("0x");
    PIntH(value);
}

static void vesa_print_mode_debug(uint16_t bios_mode) {
    const VesaModeInfo * info = vesa_get_mode_info(bios_mode);
    MMPrintString("VBE ");
    print_hex16(bios_mode);
    if (info == NULL) {
        MMPrintString(": not probed\r\n");
        return;
    }
    MMPrintString(": probe=");
    print_hex16(info->probe_status);
    MMPrintString(" attr=");
    print_hex16(info->attributes);
    MMPrintString(" winAattr=");
    print_hex16(info->win_a_attributes);
    MMPrintString(" ");
    PInt(info->width);
    MMPrintString("x");
    PInt(info->height);
    MMPrintString("x");
    PInt(info->bpp);
    MMPrintString(" pitch=");
    PInt(info->pitch);
    MMPrintString(" lfb=");
    PInt(info->linear_supported ? 1 : 0);
    MMPrintString(" bank=");
    PInt(info->banked_supported ? 1 : 0);
    MMPrintString(" winA=");
    print_hex16(info->win_a_segment);
    MMPrintString(" granKB=");
    PInt(info->win_granularity_kb);
    MMPrintString(" winKB=");
    PInt(info->win_size_kb);
    MMPrintString(" fb=");
    print_hex32(info->framebuffer);
    MMPrintString("\r\n");
}

static void vesa_print_last_failure(void) {
    MMPrintString("VBE set failed: ");
    MMPrintString((char *)vbe_last_failure);
    MMPrintString(" mode=");
    print_hex16(vbe_last_mode);
    MMPrintString(" lfbAX=");
    print_hex16(vbe_last_lfb_status);
    MMPrintString(" bankAX=");
    print_hex16(vbe_last_banked_status);
    MMPrintString(" bankSelAX=");
    print_hex16(vbe_last_bank_status);
    MMPrintString("\r\n");
    vesa_print_mode_debug(vbe_last_mode);
}

static void cirrus_print_debug(void) {
    CirrusGd542xState state = cirrus_gd542x_read_state();
    MMPrintString("Cirrus present=");
    PInt(state.present ? 1 : 0);
    MMPrintString(" chip=");
    MMPrintString((char *)state.chip_name);
    MMPrintString(" memKB=");
    PInt(state.memory_kb);
    MMPrintString(" cr27=");
    print_hex16(state.raw_cr27);
    MMPrintString(" id=");
    print_hex16(state.device_id);
    MMPrintString(" sr6=");
    print_hex16(state.sr6);
    MMPrintString(" sr0a=");
    print_hex16(state.sr0a);
    MMPrintString(" sr0f=");
    print_hex16(state.sr0f);
    MMPrintString(" sr7=");
    print_hex16(state.sr7);
    MMPrintString(" gr9=");
    print_hex16(state.gr9);
    MMPrintString(" gra=");
    print_hex16(state.gra);
    MMPrintString(" grb=");
    print_hex16(state.grb);
    MMPrintString(" nativeSet=");
    print_hex16(cirrus_last_native_set_ax);
    MMPrintString(" nativeGet=");
    print_hex16(cirrus_last_native_get_ax);
    MMPrintString(" nativeUsed=");
    PInt(cirrus_last_native_used ? 1 : 0);
    MMPrintString("\r\n");
}

static bool bga_set_mode(const VesaModeInfo * info) {
    if (!bga_available()) return false;
    if (info->width == 0 || info->height == 0 || info->bpp == 0) return false;
    bga_write(BGA_INDEX_ENABLE, 0);
    bga_write(BGA_INDEX_BPP, info->bpp);
    bga_write(BGA_INDEX_XRES, info->width);
    bga_write(BGA_INDEX_YRES, info->height);
    bga_write(BGA_INDEX_BANK, 0);
    bga_write(BGA_INDEX_VIRT_WIDTH, info->width);
    bga_write(BGA_INDEX_VIRT_HEIGHT, info->height);
    bga_write(BGA_INDEX_X_OFFSET, 0);
    bga_write(BGA_INDEX_Y_OFFSET, 0);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);
    return bga_read(BGA_INDEX_XRES) == info->width &&
           bga_read(BGA_INDEX_YRES) == info->height &&
           bga_read(BGA_INDEX_BPP) == info->bpp;
}

static bool vesa_set_bios_mode(uint16_t bios_mode) {
    vbe_last_mode = bios_mode;
    vbe_last_lfb_status = 0;
    vbe_last_banked_status = 0;
    vbe_last_bank_status = 0;
    vbe_last_failure = "none";
    const VesaModeInfo * info = vesa_get_mode_info(bios_mode);
    if (info == NULL) {
        vbe_last_failure = "4F01 probe";
        return false;
    }
    if (bga_available() && info->linear_supported) {
        if (bga_set_mode(info)) {
            vesa_apply_mode_info(info);
            linear_fb_available = true;
            banked_fb_available = false;
            planar_fb_available = false;
            if (fb_bpp == 8) vga_load_332_palette();
            return true;
        }
    }
    {
        bool use_linear = false;
        if (info->linear_supported) {
            vbe_last_lfb_status = pc386_bios_video_int10(0x4F02, (uint16_t)(bios_mode | 0x4000u),
                                                         0, 0, 0, 0);
            use_linear = vbe_last_lfb_status == 0x004Fu;
        }
        if (!use_linear) {
            if (!info->banked_supported) {
                vbe_last_failure = "no banked";
                return false;
            }
            vbe_last_banked_status = pc386_bios_video_int10(0x4F02, bios_mode, 0, 0, 0, 0);
            if (vbe_last_banked_status != 0x004Fu) {
                vbe_last_failure = "4F02 banked";
                return false;
            }
        }
        if (use_linear) {
            vesa_apply_mode_info(info);
            linear_fb_available = true;
            banked_fb_available = false;
            planar_fb_available = false;
        } else {
            fb = (volatile uint8_t *)(uintptr_t)((uint32_t)info->win_a_segment << 4);
            fb_width = (int)info->width;
            fb_height = (int)info->height;
            fb_pitch = (int)info->pitch;
            fb_bpp = (int)info->bpp;
            bank_granularity_bytes = (uint32_t)(info->win_granularity_kb ? info->win_granularity_kb : 64u) * 1024u;
            bank_window_bytes = (uint32_t)(info->win_size_kb ? info->win_size_kb : 64u) * 1024u;
            bank_current = UINT32_MAX;
            linear_fb_available = false;
            banked_fb_available = true;
            planar_fb_available = info->bpp == 4;
            if (!vesa_set_bank(0)) return false;
        }
    }
    if (fb_bpp == 8) vga_load_332_palette();
    if (fb_bpp == 4) vga_load_cmm1_palette();
    return true;
}

static bool cirrus_mode_supported(uint16_t bios_mode) {
    return cirrus_gd542x_mode_supported(bios_mode);
}

static bool cirrus_set_mode(uint16_t bios_mode) {
    const CirrusGd542xModeInfo * info = cirrus_gd542x_mode_info(bios_mode);
    if (info == NULL) return false;
    cirrus_last_native_used = false;
    cirrus_last_native_set_ax = 0;
    cirrus_last_native_get_ax = 0;
    if (!info->skip_bios_set) {
        cirrus_last_native_set_ax = pc386_bios_video_int10((uint16_t)info->cirrus_mode, 0, 0, 0, 0, 0);
        cirrus_last_native_get_ax = pc386_bios_video_int10(0x0F00, 0, 0, 0, 0, 0);
        if ((cirrus_last_native_get_ax & 0x00FFu) == info->cirrus_mode) {
            if (info->planar) {
                cirrus_gd542x_prepare_cpu_access(true);
            } else {
                cirrus_gd542x_select_single_window();
                cirrus_gd542x_set_bank_4k(0);
            }
            cirrus_last_native_used = true;
        }
    }
    if (!cirrus_last_native_used && !cirrus_gd542x_set_mode(bios_mode)) {
        return false;
    }
    fb = vga_fb;
    fb_width = info->width;
    fb_height = info->height;
    fb_pitch = info->pitch;
    /* The Cirrus BIOS is free to pick a different stride than our mode table
     * (some BIOSes pad to a power of two). Trust the chip over the table when
     * CR13 reads back as a sane value. */
    uint16_t programmed_pitch = cirrus_gd542x_read_line_bytes(info->planar);
    if (programmed_pitch >= info->pitch && programmed_pitch <= 4096u) {
        fb_pitch = programmed_pitch;
    }
    fb_bpp = info->bpp;
    fb_red_pos = fb_red_size = 0;
    fb_green_pos = fb_green_size = 0;
    fb_blue_pos = fb_blue_size = 0;
    bank_granularity_bytes = 4096;
    bank_window_bytes = 65536;
    bank_current = UINT32_MAX;
    linear_fb_available = false;
    banked_fb_available = true;
    planar_fb_available = info->planar;
    if (fb_bpp == 8) vga_load_332_palette();
    if (fb_bpp == 4) vga_load_cmm1_palette();
    return true;
}

static uint8_t rgb_to_332(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb & 0xFFu);
    return (uint8_t)((r & 0xE0u) | ((g & 0xE0u) >> 3) | (b >> 6));
}

static uint32_t rgb_from_332(uint8_t idx) {
    uint32_t r = expand3((uint8_t)((idx >> 5) & 0x07u));
    uint32_t g = expand3((uint8_t)((idx >> 2) & 0x07u));
    uint32_t b = expand2((uint8_t)(idx & 0x03u));
    return (r << 16) | (g << 8) | b;
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

static uint32_t pack_linear_pixel(uint32_t rgb) {
    if (fb_bpp == 8) return rgb_to_332(rgb);
    if (fb_bpp == 4) return (uint32_t)rgb_to_cmm1_index(rgb) & 0x0Fu;
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb & 0xFFu);
    return (fit_to_mask(r, fb_red_size) << fb_red_pos) |
           (fit_to_mask(g, fb_green_size) << fb_green_pos) |
           (fit_to_mask(b, fb_blue_size) << fb_blue_pos);
}

static void vga_write_planar_pixel(int x, int y, uint8_t colour) {
    uint32_t off = (uint32_t)((size_t)y * (size_t)fb_pitch + (size_t)(x >> 3));
    volatile uint8_t * p = banked_fb_available ? banked_ptr(off) : fb + off;
    if (p == NULL) return;
    uint8_t bit = (uint8_t)(0x80u >> (x & 7));
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F);
    outb(0x3CE, 0x00);
    outb(0x3CF, (uint8_t)(colour & 0x0F));
    outb(0x3CE, 0x01);
    outb(0x3CF, 0x0F);
    outb(0x3CE, 0x03);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x08);
    outb(0x3CF, bit);
    (void)*p;
    *p = 0xFF;
}

static void hw_put_pixel_xy(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= fb_width || y >= fb_height) return;
    if (!linear_fb_available && !banked_fb_available) {
        if (x < VGA_WIDTH && y < VGA_HEIGHT) {
            vga_fb[(size_t)y * VGA_WIDTH + (size_t)x] = rgb_to_332(rgb);
        }
        return;
    }

    if (planar_fb_available && fb_bpp == 4) {
        vga_write_planar_pixel(x, y, (uint8_t)pack_linear_pixel(rgb));
        return;
    }

    uint32_t off = (uint32_t)((size_t)y * (size_t)fb_pitch + (size_t)x * (size_t)((fb_bpp + 7) / 8));
    volatile uint8_t * p = banked_fb_available ? banked_ptr(off) : fb + off;
    if (p == NULL) return;
    uint32_t px = pack_linear_pixel(rgb);
    if (fb_bpp == 8) {
        *p = (uint8_t)px;
    } else if (fb_bpp == 32) {
        *(volatile uint32_t *)p = px;
    } else if (fb_bpp == 24) {
        p[0] = (uint8_t)(px & 0xFFu);
        p[1] = (uint8_t)((px >> 8) & 0xFFu);
        p[2] = (uint8_t)((px >> 16) & 0xFFu);
    } else if (fb_bpp == 15 || fb_bpp == 16) {
        *(volatile uint16_t *)p = (uint16_t)px;
    }
}

static void hw_put_pixel_index_xy(int x, int y, uint8_t idx) {
    if (x < 0 || y < 0 || x >= fb_width || y >= fb_height) return;
    if (!linear_fb_available && !banked_fb_available) {
        if (x < VGA_WIDTH && y < VGA_HEIGHT) {
            vga_fb[(size_t)y * VGA_WIDTH + (size_t)x] = idx;
        }
        return;
    }
    if (fb_bpp == 8) {
        uint32_t off = (uint32_t)((size_t)y * (size_t)fb_pitch + (size_t)x);
        volatile uint8_t * p = banked_fb_available ? banked_ptr(off) : fb + off;
        if (p != NULL) *p = idx;
        return;
    }
    if (planar_fb_available && fb_bpp == 4) {
        vga_write_planar_pixel(x, y, (uint8_t)rgb_to_cmm1_index(rgb_from_332(idx)));
        return;
    }
    hw_put_pixel_xy(x, y, rgb_from_332(idx));
}

static inline size_t visible_pixels(void) {
    return (size_t)vga_width * (size_t)vga_height;
}

static inline uint8_t * draw_target(void) {
    return fastgfx_active ? fastgfx_back : shadow;
}

static inline bool pure_vga13h_frontbuffer(void) {
    return !linear_fb_available && !banked_fb_available && !fastgfx_active && vga_scale == 1 &&
           vga_x_offset == 0 && vga_y_offset == 0 &&
           vga_width == VGA_WIDTH && vga_height == VGA_HEIGHT;
}

static void hw_put_logical_pixel_index(int x, int y, uint8_t idx) {
    int sx = vga_x_offset + x * vga_scale;
    int sy = vga_y_offset + y * vga_scale;
    for (int dy = 0; dy < vga_scale; dy++) {
        for (int dx = 0; dx < vga_scale; dx++) hw_put_pixel_index_xy(sx + dx, sy + dy, idx);
    }
}

static void store_draw_pixel(size_t off, uint32_t rgb) {
    uint8_t idx = rgb_to_332(rgb & 0x00FFFFFFu);
    if (fastgfx_active) {
        fastgfx_back[off] = idx;
        return;
    }
    shadow[off] = idx;
    int y = (int)(off / (size_t)vga_width);
    int x = (int)(off - (size_t)y * (size_t)vga_width);
    hw_put_logical_pixel_index(x, y, idx);
}

static void present_shadow(void) {
    if (!linear_fb_available && !banked_fb_available && vga_scale == 1 && vga_x_offset == 0 &&
        vga_y_offset == 0 && vga_width == VGA_WIDTH && vga_height == VGA_HEIGHT) {
        memcpy((void *)vga_fb, shadow, visible_pixels());
        return;
    }
    if (banked_fb_available && !planar_fb_available && fb_bpp == 8 && vga_scale == 1) {
        for (int y = 0; y < vga_height; y++) {
            uint32_t dst = (uint32_t)((size_t)(vga_y_offset + y) * (size_t)fb_pitch + (size_t)vga_x_offset);
            const uint8_t * src = shadow + (size_t)y * (size_t)vga_width;
            if (!banked_write_bytes(dst, src, (uint32_t)vga_width)) break;
        }
        return;
    }
    for (int y = 0; y < vga_height; y++) {
        size_t src = (size_t)y * (size_t)vga_width;
        for (int x = 0; x < vga_width; x++) {
            hw_put_logical_pixel_index(x, y, shadow[src + (size_t)x]);
        }
    }
}

static void clear_physical_framebuffer(uint32_t rgb) {
    if (!linear_fb_available && !banked_fb_available && fb_width == VGA_WIDTH && fb_height == VGA_HEIGHT) {
        memset((void *)vga_fb, rgb_to_332(rgb), (size_t)VGA_WIDTH * (size_t)VGA_HEIGHT);
        return;
    }
    if (banked_fb_available && planar_fb_available && fb_bpp == 4 && (rgb & 0x00FFFFFFu) == 0) {
        outb(0x3C4, 0x02);
        outb(0x3C5, 0x0F);
        outb(0x3CE, 0x01);
        outb(0x3CF, 0x00);
        outb(0x3CE, 0x03);
        outb(0x3CF, 0x00);
        outb(0x3CE, 0x05);
        outb(0x3CF, 0x00);
        outb(0x3CE, 0x08);
        outb(0x3CF, 0xFF);
        for (int y = 0; y < fb_height; y++) {
            uint32_t off = (uint32_t)((size_t)y * (size_t)fb_pitch);
            if (!banked_fill_bytes(off, 0, (uint32_t)fb_pitch)) break;
        }
        return;
    }
    if (banked_fb_available && !planar_fb_available && fb_bpp == 8) {
        uint8_t idx = rgb_to_332(rgb);
        for (int y = 0; y < fb_height; y++) {
            uint32_t off = (uint32_t)((size_t)y * (size_t)fb_pitch);
            if (!banked_fill_bytes(off, idx, (uint32_t)fb_width)) break;
        }
        return;
    }
    for (int y = 0; y < fb_height; y++) {
        for (int x = 0; x < fb_width; x++) hw_put_pixel_xy(x, y, rgb);
    }
}

static void normalize_rect(int * x1, int * y1, int * x2, int * y2) {
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

static bool clip_rect(int * x1, int * y1, int * x2, int * y2) {
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
    /* Outer guard only requires unscaled (vga_scale == 1). The banked-fill
     * inner branch handles letterbox offsets via vga_x_offset/vga_y_offset,
     * so modes 4/5 don't need the byte-at-a-time fallback. */
    if (vga_scale == 1) {
        uint8_t idx = rgb_to_332(rgb);
        uint8_t * target = draw_target();
        for (int y = y1; y <= y2; y++) {
            size_t off = (size_t)y * (size_t)vga_width + (size_t)x1;
            size_t len = (size_t)(x2 - x1 + 1);
            memset(target + off, idx, len);
            if (!linear_fb_available && !banked_fb_available && !fastgfx_active &&
                vga_width == VGA_WIDTH && vga_height == VGA_HEIGHT) {
                memset((void *)(vga_fb + off), idx, len);
            } else if (banked_fb_available && !planar_fb_available && fb_bpp == 8 && !fastgfx_active &&
                       vga_scale == 1) {
                uint32_t dst = (uint32_t)((size_t)(vga_y_offset + y) * (size_t)fb_pitch +
                                          (size_t)vga_x_offset + (size_t)x1);
                (void)banked_fill_bytes(dst, idx, (uint32_t)len);
            } else if (!fastgfx_active) {
                for (int x = x1; x <= x2; x++) hw_put_logical_pixel_index(x, y, idx);
            }
        }
        return;
    }
    for (int y = y1; y <= y2; y++) {
        size_t off = (size_t)y * (size_t)vga_width + (size_t)x1;
        for (int x = x1; x <= x2; x++, off++) store_draw_pixel(off, rgb);
    }
}

static void mode13h_draw_bitmap(int x1, int y1, int width, int height,
                                int scale, int fc, int bc,
                                unsigned char * bitmap) {
    if (x1 >= vga_width || y1 >= vga_height ||
        x1 + width * scale < 0 || y1 + height * scale < 0) {
        return;
    }
    if (banked_fb_available && !planar_fb_available && fb_bpp == 8 && !fastgfx_active &&
        vga_scale == 1 && scale > 0 && width > 0 && width * scale <= VGA_MAX_WIDTH) {
        static uint8_t row[VGA_MAX_WIDTH];
        uint8_t fc_idx = rgb_to_332((uint32_t)fc & 0x00FFFFFFu);
        uint8_t bc_idx = rgb_to_332((uint32_t)bc & 0x00FFFFFFu);
        int row_width = width * scale;
        for (int i = 0; i < height; i++) {
            for (int j = 0; j < scale; j++) {
                int dst_y = y1 + i * scale + j;
                if (dst_y < 0 || dst_y >= vga_height) continue;
                int clip_x0 = x1 < 0 ? -x1 : 0;
                int clip_x1 = x1 + row_width > vga_width ? vga_width - x1 : row_width;
                if (clip_x0 >= clip_x1) continue;
                uint8_t * shadow_row = shadow + (size_t)dst_y * (size_t)vga_width;
                for (int sx = clip_x0; sx < clip_x1; sx++) {
                    int k = sx / scale;
                    int bit = (bitmap[((i * width) + k) / 8] >>
                               (((height * width) - ((i * width) + k) - 1) % 8)) &
                              1;
                    int colour = bit ? fc : bc;
                    if (colour < 0) {
                        row[sx - clip_x0] = shadow_row[(size_t)(x1 + sx)];
                    } else {
                        row[sx - clip_x0] = bit ? fc_idx : bc_idx;
                    }
                }
                size_t shadow_off = (size_t)dst_y * (size_t)vga_width + (size_t)(x1 + clip_x0);
                uint32_t len = (uint32_t)(clip_x1 - clip_x0);
                memcpy(shadow + shadow_off, row, len);
                uint32_t dst = (uint32_t)((size_t)(vga_y_offset + dst_y) * (size_t)fb_pitch +
                                          (size_t)vga_x_offset + (size_t)(x1 + clip_x0));
                (void)banked_write_bytes(dst, row, len);
            }
        }
        return;
    }
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < scale; j++) {
            for (int k = 0; k < width; k++) {
                int bit = (bitmap[((i * width) + k) / 8] >>
                           (((height * width) - ((i * width) + k) - 1) % 8)) &
                          1;
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

    uint8_t blank = rgb_to_332((uint32_t)PromptBC & 0x00FFFFFFu);
    if (lines > 0) {
        size_t keep = (size_t)(vga_height - lines) * (size_t)vga_width;
        uint8_t * target = draw_target();
        memmove(target, target + (size_t)lines * (size_t)vga_width, keep);
        memset(target + keep, blank, (size_t)lines * (size_t)vga_width);
        if (pure_vga13h_frontbuffer()) {
            memmove((void *)vga_fb,
                    (const void *)(vga_fb + (size_t)lines * (size_t)vga_width),
                    keep);
            memset((void *)(vga_fb + keep), blank, (size_t)lines * (size_t)vga_width);
        } else if (fb_hw_scroll_capable && !fastgfx_active) {
            /* Hardware scroll: shift the CRTC Start Address forward and only
             * blank the newly-exposed bottom rows. When the scroll would push
             * the visible region past VRAM end, blit the shadow back to byte
             * 0 and reset Start to 0 (one full blit per ~hundreds of scrolls). */
            uint32_t scroll_bytes = (uint32_t)lines * (uint32_t)fb_pitch;
            uint32_t frame_size = (uint32_t)fb_height * (uint32_t)fb_pitch;
            uint32_t new_start = fb_start_byte + scroll_bytes;
            if (new_start + frame_size > fb_vram_size) {
                fb_start_byte = 0;
                for (int y = 0; y < fb_height; y++) {
                    uint32_t dst = (uint32_t)y * (uint32_t)fb_pitch;
                    const uint8_t * src = shadow + (size_t)y * (size_t)vga_width;
                    (void)banked_write_bytes(dst, src, (uint32_t)vga_width);
                }
                cirrus_set_start_address_bytes(0);
            } else {
                fb_start_byte = new_start;
                for (int i = 0; i < lines; i++) {
                    uint32_t row = (uint32_t)(fb_height - lines + i) * (uint32_t)fb_pitch;
                    (void)banked_fill_bytes(row, blank, (uint32_t)fb_width);
                }
                cirrus_set_start_address_bytes(new_start);
            }
        } else if (!fastgfx_active) {
            present_shadow();
        }
    } else {
        lines = -lines;
        size_t keep = (size_t)(vga_height - lines) * (size_t)vga_width;
        uint8_t * target = draw_target();
        memmove(target + (size_t)lines * (size_t)vga_width, target, keep);
        memset(target, blank, (size_t)lines * (size_t)vga_width);
        if (pure_vga13h_frontbuffer()) {
            memmove((void *)(vga_fb + (size_t)lines * (size_t)vga_width),
                    (const void *)vga_fb,
                    keep);
            memset((void *)vga_fb, blank, (size_t)lines * (size_t)vga_width);
        } else if (!fastgfx_active) {
            present_shadow();
        }
    }
}

static void mode13h_draw_buffer(int x1, int y1, int x2, int y2, unsigned char * p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    if (banked_fb_available && !planar_fb_available && fb_bpp == 8 && !fastgfx_active &&
        vga_scale == 1) {
        static uint8_t row[VGA_MAX_WIDTH];
        int rect_width = x2 - x1 + 1;
        for (int y = y1; y <= y2; y++) {
            int clip_x0 = x1 < 0 ? -x1 : 0;
            int clip_x1 = rect_width;
            if (x1 + clip_x1 > vga_width) clip_x1 = vga_width - x1;
            for (int dx = 0; dx < rect_width; dx++) {
                uint32_t b = *p++;
                uint32_t g = *p++;
                uint32_t r = *p++;
                if (y >= 0 && y < vga_height && dx >= clip_x0 && dx < clip_x1) {
                    uint32_t rgb = (r << 16) | (g << 8) | b;
                    row[dx - clip_x0] = rgb_to_332(rgb);
                }
            }
            if (y < 0 || y >= vga_height || clip_x0 >= clip_x1) continue;
            int x_start = x1 + clip_x0;
            uint32_t len = (uint32_t)(clip_x1 - clip_x0);
            size_t shadow_off = (size_t)y * (size_t)vga_width + (size_t)x_start;
            memcpy(shadow + shadow_off, row, len);
            uint32_t dst = (uint32_t)((size_t)(vga_y_offset + y) * (size_t)fb_pitch +
                                      (size_t)vga_x_offset + (size_t)x_start);
            (void)banked_write_bytes(dst, row, len);
        }
        return;
    }
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

static void mode13h_read_buffer(int x1, int y1, int x2, int y2, unsigned char * p) {
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

static void mode13h_draw_buffer_fast(int x1, int y1, int x2, int y2,
                                     int blank, unsigned char * p) {
    normalize_rect(&x1, &y1, &x2, &y2);
    int toggle = 0;
    if (banked_fb_available && !planar_fb_available && fb_bpp == 8 && !fastgfx_active &&
        vga_scale == 1) {
        static uint8_t row[VGA_MAX_WIDTH];
        int rect_width = x2 - x1 + 1;
        for (int y = y1; y <= y2; y++) {
            int clip_x0 = x1 < 0 ? -x1 : 0;
            int clip_x1 = rect_width;
            if (x1 + clip_x1 > vga_width) clip_x1 = vga_width - x1;
            bool row_visible = (y >= 0 && y < vga_height && clip_x0 < clip_x1);
            uint8_t * shadow_row = row_visible ? shadow + (size_t)y * (size_t)vga_width : NULL;
            for (int dx = 0; dx < rect_width; dx++) {
                uint8_t packed = *p;
                uint8_t nibble = toggle ? (uint8_t)(packed >> 4) : (uint8_t)(packed & 0x0F);
                if (toggle) p++;
                toggle = !toggle;
                if (!row_visible || dx < clip_x0 || dx >= clip_x1) continue;
                uint8_t idx_out;
                if (blank != -1 && nibble == sprite_transparent) {
                    idx_out = shadow_row[(size_t)(x1 + dx)];
                } else {
                    idx_out = rgb_to_332((uint32_t)CMM1map[nibble & 0x0F]);
                }
                row[dx - clip_x0] = idx_out;
            }
            if (!row_visible) continue;
            int x_start = x1 + clip_x0;
            uint32_t len = (uint32_t)(clip_x1 - clip_x0);
            size_t shadow_off = (size_t)y * (size_t)vga_width + (size_t)x_start;
            memcpy(shadow + shadow_off, row, len);
            uint32_t dst = (uint32_t)((size_t)(vga_y_offset + y) * (size_t)fb_pitch +
                                      (size_t)vga_x_offset + (size_t)x_start);
            (void)banked_write_bytes(dst, row, len);
        }
        return;
    }
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

static void mode13h_read_buffer_fast(int x1, int y1, int x2, int y2, unsigned char * p) {
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
    uint8_t * target = draw_target();
    return rgb_from_332(target[(size_t)y * (size_t)vga_width + (size_t)x]);
}

static const Pc386VideoMode * find_mode(int mode) {
    for (size_t i = 0; i < sizeof(pc386_modes) / sizeof(pc386_modes[0]); i++) {
        if (pc386_modes[i].mode == mode) return &pc386_modes[i];
    }
    return NULL;
}

static bool mode_fits(const Pc386VideoMode * m) {
    if (m->backend == PC386_VIDEO_VGA13H) return true;
#ifdef PC386_NO_BIOS_VIDEO
    return false;
#else
    if (svga_backend == PC386_SVGA_CIRRUS) {
        return cirrus_mode_supported(m->bios_mode) ||
               (m->fallback_bios_mode != 0 && cirrus_mode_supported(m->fallback_bios_mode));
    }
    return vesa_mode_supported(m->bios_mode) ||
           (m->fallback_bios_mode != 0 && vesa_mode_supported(m->fallback_bios_mode));
#endif
}

static uint16_t select_mode_bios_mode(const Pc386VideoMode * m) {
    if (m->backend == PC386_VIDEO_VGA13H) return m->bios_mode;
    if (svga_backend == PC386_SVGA_CIRRUS) {
        if (cirrus_mode_supported(m->bios_mode)) return m->bios_mode;
        if (m->fallback_bios_mode != 0 && cirrus_mode_supported(m->fallback_bios_mode)) {
            return m->fallback_bios_mode;
        }
        return m->bios_mode;
    }
    if (vesa_mode_supported(m->bios_mode)) return m->bios_mode;
    if (m->fallback_bios_mode != 0 && vesa_mode_supported(m->fallback_bios_mode)) {
        return m->fallback_bios_mode;
    }
    return m->bios_mode;
}

void vga_mode13h_set_mode(int mode, int clear) {
    const Pc386VideoMode * m = find_mode(mode);
    if (m == NULL) error("Invalid mode");
    if (!mode_fits(m)) error("Mode not available");

    int next_scale = m->scale;
    int next_x_offset = m->x_offset;
    int next_y_offset = m->y_offset;
    vga_mode13h_fastgfx_reset();
    if (m->backend == PC386_VIDEO_VGA13H) {
        bool using_vbe_compat = false;
#ifndef PC386_NO_BIOS_VIDEO
        if (svga_backend == PC386_SVGA_VESA && linear_fb_available && vesa_mode_supported(0x0101)) {
            using_vbe_compat = vesa_set_bios_mode(0x0101);
            next_scale = 2;
            next_x_offset = 0;
            next_y_offset = 0;
        }
#endif
        if (!using_vbe_compat) {
            (void)m->bios_mode;
#ifndef PC386_NO_BIOS_VIDEO
            if (svga_backend == PC386_SVGA_CIRRUS) {
                (void)pc386_bios_video_int10(0x0013, 0, 0, 0, 0, 0);
            }
#endif
            vga_program_mode13h_registers();
            if (svga_backend == PC386_SVGA_CIRRUS) cirrus_gd542x_reset_vga_compat();
            fb = vga_fb;
            fb_width = VGA_WIDTH;
            fb_height = VGA_HEIGHT;
            fb_pitch = VGA_WIDTH;
            fb_bpp = 8;
            linear_fb_available = false;
            banked_fb_available = false;
            planar_fb_available = false;
            bank_current = UINT32_MAX;
            vga_load_332_palette();
        }
    } else {
        if (svga_backend == PC386_SVGA_CIRRUS) {
            if (!cirrus_set_mode(select_mode_bios_mode(m))) {
                cirrus_print_debug();
                error("Mode not available");
            }
        } else if (!vesa_set_bios_mode(select_mode_bios_mode(m))) {
            vesa_print_last_failure();
            error("Mode requires VBE");
        }
        if (m->hw_width > fb_width || m->hw_height > fb_height) error("Mode not available");
    }
    vga_width = m->width;
    vga_height = m->height;
    vga_scale = next_scale;
    vga_x_offset = next_x_offset;
    vga_y_offset = next_y_offset;
    vga_current_mode = mode;
    DisplayHRes = HRes = vga_width;
    DisplayVRes = VRes = vga_height;
    if (Option.DISPLAY_CONSOLE) {
        SetFont(Option.DefaultFont);
        Option.Width = HRes / gui_font_width;
        Option.Height = VRes / gui_font_height;
    }
    /* Hardware scroll is only safe for full-screen banked 8bpp packed modes
     * (no letterbox, no pixel-doubling) where the chip's display window
     * matches the logical surface 1:1. Mode 2 (640x480) and mode 3 (720x400)
     * qualify; mode 4/5 letterbox and mode 1 VGA13h do not. */
    fb_start_byte = 0;
    fb_hw_scroll_capable = banked_fb_available && !planar_fb_available && fb_bpp == 8 &&
                           vga_scale == 1 && vga_x_offset == 0 && vga_y_offset == 0 &&
                           vga_width == fb_width && vga_height == fb_height;
    if (fb_hw_scroll_capable) cirrus_set_start_address_bytes(0);
    if (clear) {
        memset(shadow, 0, visible_pixels());
        clear_physical_framebuffer(0);
        CurrentX = CurrentY = 0;
    }
}

void vga_mode13h_fastgfx_create(void) {
    size_t pixels = visible_pixels();
    memcpy(fastgfx_back, shadow, pixels);
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
    memcpy(shadow, fastgfx_back, pixels);
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
        MMPrintString("1:320x200");
#ifndef PC386_NO_BIOS_VIDEO
        bool mode101 = svga_backend == PC386_SVGA_CIRRUS ? cirrus_mode_supported(0x0101) : vesa_mode_supported(0x0101);
        bool mode103 = svga_backend == PC386_SVGA_CIRRUS ? cirrus_mode_supported(0x0103) : vesa_mode_supported(0x0103);
        bool mode105 = svga_backend == PC386_SVGA_CIRRUS ? cirrus_mode_supported(0x0105) : vesa_mode_supported(0x0105);
        bool mode104 = svga_backend == PC386_SVGA_CIRRUS ? cirrus_mode_supported(0x0104) : vesa_mode_supported(0x0104);
        if (mode101) MMPrintString(" 2:640x480 5:480x480");
        if (mode103) MMPrintString(" 3:720x400 4:320x320");
        if (mode105 || mode104) {
            MMPrintString(" 6:320x320x2");
        }
#endif
        MMPrintString("\r\n");
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"DEBUG")) {
#ifdef PC386_NO_BIOS_VIDEO
        MMPrintString("PC386_NO_BIOS_VIDEO=1\r\n");
#else
        MMPrintString("backend=");
        MMPrintString(svga_backend == PC386_SVGA_CIRRUS ? "cirrus\r\n" : "vesa\r\n");
        cirrus_print_debug();
        vesa_print_mode_debug(0x0101);
        vesa_print_mode_debug(0x0103);
        vesa_print_mode_debug(0x0105);
        vesa_print_mode_debug(0x0104);
        MMPrintString("BGA id=");
        print_hex16(bga_read(BGA_INDEX_ID));
        MMPrintString("\r\n");
        vesa_print_last_failure();
#endif
        return;
    }
    int mode = getint(cmdline, 1, 6);
#ifdef PC386_NO_BIOS_VIDEO
    if (mode != 1) {
        MMPrintString("Mode not available\r\n");
        return;
    }
#endif
    vga_mode13h_set_mode(mode, true);
}

void vga_mode13h_init(void) {
    (void)pc386_multiboot_info;
#if defined(PC386_VIDEO_CIRRUS)
    svga_backend = PC386_SVGA_CIRRUS;
#elif defined(PC386_VIDEO_VESA)
    svga_backend = PC386_SVGA_VESA;
#else
    svga_backend = cirrus_gd542x_probe() ? PC386_SVGA_CIRRUS : PC386_SVGA_VESA;
#endif
    fb = vga_fb;
    fb_width = VGA_WIDTH;
    fb_height = VGA_HEIGHT;
    fb_pitch = VGA_WIDTH;
    fb_bpp = 8;
    linear_fb_available = false;
    banked_fb_available = false;
    planar_fb_available = false;
    bank_current = UINT32_MAX;

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
