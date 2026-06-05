/*
 * drivers/vga_mode13h/cirrus_gd542x.c - Cirrus Logic CL-GD542x helpers.
 *
 * The CL-GD542x technical reference identifies the chip by unlocking
 * extension registers with SR6=0x12 and reading CR27[7:2].
 */

#include "cirrus_gd542x.h"

#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_MISC_OUTPUT 0x3C2
#define VGA_ATTR_INDEX 0x3C0
#define VGA_ATTR_DATA_R 0x3C1
#define VGA_INPUT_STATUS 0x3DA
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF

#define CIRRUS_SR_UNLOCK 0x06
#define CIRRUS_SR_MODE_EXT 0x07
#define CIRRUS_SR_MEMCFG 0x0A
#define CIRRUS_SR_DRAM_CONTROL 0x0F
#define CIRRUS_CR_ID 0x27
#define CIRRUS_GR_OFFSET0 0x09
#define CIRRUS_GR_OFFSET1 0x0A
#define CIRRUS_GR_BANKING 0x0B

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint8_t read_seq(uint8_t index) {
    outb(VGA_SEQ_INDEX, index);
    return inb(VGA_SEQ_DATA);
}

static void write_seq(uint8_t index, uint8_t value) {
    outb(VGA_SEQ_INDEX, index);
    outb(VGA_SEQ_DATA, value);
}

static uint8_t read_crtc(uint8_t index) {
    outb(VGA_CRTC_INDEX, index);
    return inb(VGA_CRTC_DATA);
}

static uint8_t read_gc(uint8_t index) {
    outb(VGA_GC_INDEX, index);
    return inb(VGA_GC_DATA);
}

static void write_gc(uint8_t index, uint8_t value) {
    outb(VGA_GC_INDEX, index);
    outb(VGA_GC_DATA, value);
}

static void write_indexed_table(uint16_t port, const uint16_t * regs) {
    for (const uint16_t * p = regs; *p != 0xFFFFu; p++) {
        uint16_t v = *p;
        __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
    }
}

static void write_attr(uint8_t index, uint8_t value) {
    (void)inb(VGA_INPUT_STATUS);
    outb(VGA_ATTR_INDEX, index);
    outb(VGA_ATTR_INDEX, value);
}

static void set_attribute_graphics_mode(bool packed_256) {
    for (uint8_t i = 0; i < 16; i++) write_attr(i, i);
    write_attr(0x10, packed_256 ? 0x41 : 0x01);
    write_attr(0x11, 0x00);
    write_attr(0x12, 0x0F);
    write_attr(0x13, 0x00);
    write_attr(0x14, 0x00);
    (void)inb(VGA_INPUT_STATUS);
    outb(VGA_ATTR_INDEX, 0x20);
}

static void unlock_extensions(void) {
    write_seq(CIRRUS_SR_UNLOCK, 0x12);
}

static uint8_t decode_device_id(uint8_t raw_cr27) {
    if (raw_cr27 == 0x8A) return 0x22;
    if (raw_cr27 == 0x8B) return 0x22;
    return (uint8_t)(raw_cr27 >> 2);
}

static bool device_id_is_gd542x(uint8_t id) {
    return id == 0x22 || id == 0x23 || id == 0x24 ||
           id == 0x25 || id == 0x26 || id == 0x27;
}

static const char * chip_name(uint8_t id) {
    switch (id) {
    case 0x22:
        return "GD5420";
    case 0x23:
        return "GD5422";
    case 0x24:
        return "GD5426";
    case 0x25:
        return "GD5424";
    case 0x26:
        return "GD5428";
    case 0x27:
        return "GD5429";
    default:
        return "unknown";
    }
}

static uint16_t detect_memory_kb(uint8_t id) {
    uint8_t sr0a = read_seq(CIRRUS_SR_MEMCFG);
    uint16_t kb = (uint16_t)(256u << ((sr0a >> 3) & 0x03u));

    if (id >= 0x28 || (kb <= 256 && id != 0x22)) {
        uint8_t sr0f = read_seq(CIRRUS_SR_DRAM_CONTROL);
        kb = 512;
        if (sr0f & 0x10) kb = (uint16_t)(kb * 2u);
        if ((sr0f & 0x18) == 0x18) kb = (uint16_t)(kb * 2u);
        if (id != 0x28 && (sr0f & 0x80)) kb = (uint16_t)(kb * 2u);
    }

    return kb;
}

bool cirrus_gd542x_probe(void) {
    unlock_extensions();
    uint8_t sr6 = read_seq(CIRRUS_SR_UNLOCK);
    uint8_t raw_cr27 = read_crtc(CIRRUS_CR_ID);
    uint8_t id = decode_device_id(raw_cr27);

    if (device_id_is_gd542x(id)) return true;

    /*
     * GD5429 does not implement SR6, but CR27 still carries the device ID.
     * For older chips, the unlock register should read back 0x12.
     */
    return sr6 == 0x12 && device_id_is_gd542x(id);
}

static const uint16_t graph_packed_256[] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x4005, 0x0506, 0x0F07,
    0xFF08, 0x0009, 0x000A, 0x000B, 0xFFFF};

static const uint16_t graph_planar_16[] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0506, 0x0F07,
    0xFF08, 0x0009, 0x000A, 0x000B, 0xFFFF};

static const uint16_t seq_640x480x8_legacy[] = {
    0x0300, 0x0101, 0x0F02, 0x0003, 0x0E04, 0x0107,
    0x580B, 0x580C, 0x580D, 0x580E,
    0x0412, 0x0013, 0x2017,
    0x331B, 0x331C, 0x331D, 0x331E,
    0xFFFF};

static const uint16_t crtc_640x480x8_legacy[] = {
    0x2C11,
    0x5F00, 0x4F01, 0x4F02, 0x8003, 0x5204, 0x1E05, 0x0B06, 0x3E07,
    0x4009, 0x000C, 0x000D,
    0xEA10, 0xDF12, 0x5013, 0x4014, 0xDF15, 0x0B16, 0xC317, 0xFF18,
    0x001A, 0x221B, 0x001D,
    0xFFFF};

/*
 * Register sets are derived from svgalib's Cirrus GD542x driver tables.
 * Layout: 24 CRTC, 21 attribute, 9 graphics, 5 sequencer, 1 misc,
 * 4 extended CRTC, 3 extended graphics, 27 extended sequencer, 1 DAC.
 */
#define SVGA_REG_CRTC 0
#define SVGA_REG_ATTR 24
#define SVGA_REG_GRAPH 45
#define SVGA_REG_SEQ 54
#define SVGA_REG_MISC 59
#define SVGA_REG_XCRTC 60
#define SVGA_REG_XGRAPH 64
#define SVGA_REG_XSEQ 67
#define SVGA_REG_DAC 94

/*
 * Each table layout, matching SVGA_REG_* offsets above:
 *   CRTC 0..23                                         (24 bytes)
 *   ATTR 0..20                                         (21 bytes)
 *   GRAPH 0..8                                         (9 bytes)
 *   SEQ 0..4                                           (5 bytes)
 *   MISC output                                        (1 byte)
 *   XCRTC: CR19, CR1A, CR1B, CR1D                      (4 bytes)
 *   XGRAPH: GR9, GRA, GRB                              (3 bytes)
 *   XSEQ: SR07, ..., SR0E (idx 7), SR0F (idx 8), ...,
 *         SR1E (idx 23)                                (27 bytes)
 *   Hidden DAC                                         (1 byte)
 *
 * Extended-register values follow svgalib's GD542x driver:
 *   SR07 = 0x01 for 8bpp packed, 0x00 for 4bpp planar
 *   SR0F = 0x10 (16-bit DRAM, no extended FIFO — bit 5 is 5422+ only)
 *   SR0E/SR1E hold the per-mode VCLK3 numerator/denominator
 *   CR1B = 0x22 for >256KB packed modes (extended address wrap)
 *   MISC bits[3:2]=11 select VCLK3 so the programmed clock takes effect.
 */
static const uint8_t regs_640x480x8[95] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xEA, 0x8C, 0xDF, 0x50, 0x60, 0xE7, 0x04, 0xAB,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x01, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0xEF,
    0x00, 0x00, 0x22, 0x00,
    0x00, 0x00, 0x00,
    0x01, 0x12, 0x01, 0x80, 0x01, 0x19, 0x4A, 0x4A, 0x10, 0x4A, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xD8, 0x00, 0x00, 0x01, 0x00, 0x2B, 0x2B,
    0x30, 0x2B, 0x1C,
    0x00};

/* 720x400 8bpp packed graphics, mode 1 (mode 13h) timing class.
 * The Pocket386 LCD scaler accepts this signal natively — mode 1
 * already outputs 720x400 70Hz timing for its 320x200 mode 13h
 * doubled scanout, and the scaler reports it correctly. Mode 3 reuses
 * that timing as a 720x400 linear graphics surface.
 *   Pixel clock 28.33 MHz (svgalib SR0E=0x5B, SR1E=0x2F).
 *   HTotal=896 (112 char), HActive=720, HFP=16, HSync=96, HBP=64.
 *   VTotal=449, VActive=400, VFP=12, VSync=2, VBP=35.
 *   HSync 31.62 kHz, VSync 70.4 Hz.
 *   HSync negative, VSync positive (MISC=0x6F).
 * skip_bios_set=true: stock Cirrus BIOS has no graphics mode at this
 * resolution; we program the chip directly via write_svga_regs. */
static const uint8_t regs_720x400x8[95] = {
    0x6B, 0x59, 0x5A, 0x90, 0x5C, 0x08, 0xBF, 0x1F, 0x00, 0x40, 0x00, 0x00,
    0x00, 0x00, 0x02, 0xBC, 0x9C, 0x8E, 0x8F, 0x5A, 0x00, 0x90, 0xBF, 0xE3,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x01, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x6F,
    0xFF, 0x00, 0x22, 0x00,
    0x00, 0x00, 0x00,
    0x01, 0x12, 0x01, 0x80, 0x05, 0x19, 0x4A, 0x5B, 0x10, 0x7E, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xD8, 0x00, 0x00, 0x01, 0x00, 0x2B, 0x2F,
    0x30, 0x33, 0x1C,
    0x00};

static const uint8_t regs_1024x768x4[95] = {
    0xA1, 0x7F, 0x80, 0x84, 0x84, 0x92, 0x2A, 0xFD, 0x00, 0x60, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x12, 0x89, 0xFF, 0x40, 0x00, 0x00, 0x2A, 0xE3,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x01, 0x00, 0x0F, 0x00, 0x00,
    0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF,
    0x00, 0x01, 0x0F, 0x00, 0x06,
    0xEF,
    0xFF, 0x4A, 0x22, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x12, 0x00, 0x80, 0x1C, 0x51, 0x4A, 0x76, 0x10, 0x61, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xDD, 0x00, 0x00, 0x01, 0x00, 0x2B, 0x34,
    0x30, 0x24, 0x1C,
    0x00};

typedef struct {
    CirrusGd542xModeInfo info;
    const uint8_t * regs;
    const uint16_t * legacy_seq;
    const uint16_t * legacy_crtc;
    uint16_t required_kb;
} CirrusModeTable;

static const CirrusModeTable mode_tables[] = {
    {{0x0101, 0x5F, 640, 480, 640, 8, false, false}, regs_640x480x8, seq_640x480x8_legacy, crtc_640x480x8_legacy, 512},
    /* mode 3: 720x400 70Hz graphics (mode 13h timing class). Stock Cirrus
     * BIOS has no graphics mode at this resolution; programmed directly. */
    {{0x0103, 0x00, 720, 400, 720, 8, false, true}, regs_720x400x8, 0, 0, 512},
    {{0x0104, 0x5D, 1024, 768, 128, 4, true, false}, regs_1024x768x4, 0, 0, 512},
};

static const CirrusModeTable * find_mode(uint16_t vesa_mode) {
    for (unsigned i = 0; i < sizeof(mode_tables) / sizeof(mode_tables[0]); i++) {
        if (mode_tables[i].info.vesa_mode == vesa_mode) return &mode_tables[i];
    }
    return 0;
}

const CirrusGd542xModeInfo * cirrus_gd542x_mode_info(uint16_t vesa_mode) {
    const CirrusModeTable * mode = find_mode(vesa_mode);
    return mode ? &mode->info : 0;
}

bool cirrus_gd542x_mode_supported(uint16_t vesa_mode) {
    const CirrusModeTable * mode = find_mode(vesa_mode);
    if (mode == 0 || !cirrus_gd542x_probe()) return false;
    CirrusGd542xState state = cirrus_gd542x_read_state();
    return state.memory_kb >= mode->required_kb;
}

static void write_svga_regs(const uint8_t * regs) {
    write_seq(0x00, 0x01);
    outb(VGA_MISC_OUTPUT, regs[SVGA_REG_MISC]);
    for (uint8_t i = 1; i < 5; i++) write_seq(i, regs[SVGA_REG_SEQ + i]);
    write_seq(0x00, 0x03);

    unlock_extensions();
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, (uint8_t)(regs[SVGA_REG_CRTC + 0x11] & 0x7F));
    for (uint8_t i = 0; i < 24; i++) {
        if (i == 0x11) continue;
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, regs[SVGA_REG_CRTC + i]);
    }
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, regs[SVGA_REG_CRTC + 0x11]);

    for (uint8_t i = 0; i < 9; i++) write_gc(i, regs[SVGA_REG_GRAPH + i]);

    for (uint8_t i = 0; i < 21; i++) write_attr(i, regs[SVGA_REG_ATTR + i]);
    (void)inb(VGA_INPUT_STATUS);
    outb(VGA_ATTR_INDEX, 0x20);

    outb(VGA_CRTC_INDEX, 0x19);
    outb(VGA_CRTC_DATA, regs[SVGA_REG_XCRTC + 0]);
    outb(VGA_CRTC_INDEX, 0x1A);
    outb(VGA_CRTC_DATA, regs[SVGA_REG_XCRTC + 1]);
    outb(VGA_CRTC_INDEX, 0x1B);
    outb(VGA_CRTC_DATA, regs[SVGA_REG_XCRTC + 2]);

    write_gc(0x09, regs[SVGA_REG_XGRAPH + 0]);
    write_gc(0x0A, regs[SVGA_REG_XGRAPH + 1]);
    write_gc(0x0B, regs[SVGA_REG_XGRAPH + 2]);

    write_seq(CIRRUS_SR_MODE_EXT, (uint8_t)((regs[SVGA_REG_XSEQ + 0] & 0xF0) | 0x01));
    write_seq(0x0E, regs[SVGA_REG_XSEQ + 7]);
    write_seq(0x1E, regs[SVGA_REG_XSEQ + 23]);
    write_seq(CIRRUS_SR_MODE_EXT, regs[SVGA_REG_XSEQ + 0]);
    write_seq(CIRRUS_SR_DRAM_CONTROL, regs[SVGA_REG_XSEQ + 8]);
    (void)regs[SVGA_REG_DAC];
}

bool cirrus_gd542x_set_mode(uint16_t vesa_mode) {
    const CirrusModeTable * mode = find_mode(vesa_mode);
    if (mode == 0 || !cirrus_gd542x_mode_supported(vesa_mode)) return false;

    unlock_extensions();
    if (mode->legacy_seq != 0 && mode->legacy_crtc != 0) {
        write_indexed_table(VGA_SEQ_INDEX, mode->legacy_seq);
        write_indexed_table(VGA_GC_INDEX, mode->info.planar ? graph_planar_16 : graph_packed_256);
        write_indexed_table(VGA_CRTC_INDEX, mode->legacy_crtc);
        set_attribute_graphics_mode(!mode->info.planar);
    } else {
        write_svga_regs(mode->regs);
        set_attribute_graphics_mode(!mode->info.planar);
    }
    cirrus_gd542x_select_single_window();
    cirrus_gd542x_set_bank_4k(0);
    return true;
}

void cirrus_gd542x_prepare_cpu_access(bool planar) {
    unlock_extensions();
    write_indexed_table(VGA_GC_INDEX, planar ? graph_planar_16 : graph_packed_256);
    set_attribute_graphics_mode(!planar);
    cirrus_gd542x_select_single_window();
    cirrus_gd542x_set_bank_4k(0);
}

void cirrus_gd542x_reset_vga_compat(void) {
    unlock_extensions();
    write_seq(CIRRUS_SR_MODE_EXT, 0x00);
    write_gc(CIRRUS_GR_OFFSET0, 0x00);
    write_gc(CIRRUS_GR_OFFSET1, 0x00);
    write_gc(CIRRUS_GR_BANKING, 0x00);
}

CirrusGd542xState cirrus_gd542x_read_state(void) {
    unlock_extensions();
    CirrusGd542xState state;
    state.raw_cr27 = read_crtc(CIRRUS_CR_ID);
    state.device_id = decode_device_id(state.raw_cr27);
    state.chip_name = chip_name(state.device_id);
    state.memory_kb = detect_memory_kb(state.device_id);
    state.sr6 = read_seq(CIRRUS_SR_UNLOCK);
    state.sr0a = read_seq(CIRRUS_SR_MEMCFG);
    state.sr0f = read_seq(CIRRUS_SR_DRAM_CONTROL);
    state.sr7 = read_seq(CIRRUS_SR_MODE_EXT);
    state.gr9 = read_gc(CIRRUS_GR_OFFSET0);
    state.gra = read_gc(CIRRUS_GR_OFFSET1);
    state.grb = read_gc(CIRRUS_GR_BANKING);
    state.present = device_id_is_gd542x(state.device_id);
    return state;
}

void cirrus_gd542x_select_single_window(void) {
    unlock_extensions();
    write_gc(CIRRUS_GR_BANKING, 0x00);
}

void cirrus_gd542x_set_bank_4k(uint8_t bank) {
    unlock_extensions();
    cirrus_gd542x_select_single_window();
    write_gc(CIRRUS_GR_OFFSET0, bank);
}

uint16_t cirrus_gd542x_read_line_bytes(bool planar) {
    unlock_extensions();
    uint16_t offset = read_crtc(0x13);
    uint8_t cr1b = read_crtc(0x1B);
    if (cr1b & 0x10) offset |= 0x100;
    /* Standard VGA offset units: 2 bytes per CR13 step in planar modes,
     * 8 bytes per step in 8bpp packed mode (chain-4 / dword addressing). */
    return planar ? (uint16_t)(offset * 2u) : (uint16_t)(offset * 8u);
}
