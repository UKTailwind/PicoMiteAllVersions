/*
 * esp32_vga.c — ESP32-S3 VGA (RGB332 over LCD_CAM) option setter + display
 * bring-up.
 *
 * OPTION VGA r2,r1,r0,g2,g1,g0,b1,b0,hsync,vsync
 *   Stores the ten GPIOs that drive an external resistor-ladder DAC
 *   (VGA666-style, wired RGB332). The eight data pins are listed
 *   most-significant first per channel.
 * OPTION VGA 3BIT r,g,b,hsync,vsync
 *   8-colour mode for a 1-bit-per-channel DAC (Serial Wombat board):
 *   only the three channel MSBs are wired.
 * OPTION VGA SYNC hsync,vsync
 *   Diagnostic sync polarity override. Each argument is NEGATIVE
 *   (standard VGA, idle high / pulse low) or POSITIVE.
 * OPTION VGA DISABLE
 *   Clears the configuration.
 *
 * A pixel clock is not exposed: LCD_CAM paces its DMA from pclk_hz
 * internally and a resistor-DAC VGA has no PCLK input, so the panel is
 * created with pclk_gpio_num = -1 (no clock pin routed).
 *
 * esp32_vga_display_init() brings the panel up at boot when configured,
 * binds the RGB332 draw primitives, and points the framebuffer globals
 * at the continuously-scanned LCD_CAM buffer. The REPL is mirrored to
 * VGA and USB serial when VGA is configured.
 */

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_system.h" /* esp_restart */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"

#include "drivers/draw_rgb332/draw_rgb332.h"
#include "vga_lcdcam_s3.h"

/* Core framebuffer + draw-pointer globals (defined in Draw.c / state). */
extern short HRes, VRes, DisplayHRes, DisplayVRes;
extern short CurrentX, CurrentY;
extern int ScreenSize;
extern uint32_t framebuffersize;
extern unsigned char *WriteBuf, *DisplayBuf, *FrameBuf, *LayerBuf, *SecondFrame, *SecondLayer, *FRAMEBUFFER;
extern unsigned char OptionConsole;
extern volatile int DISPLAY_TYPE;
extern void (*DrawRectangle)(int x1, int y1, int x2, int y2, int c);
extern void (*DrawBitmap)(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char * bitmap);
extern void (*ScrollLCD)(int lines);
extern void (*DrawBuffer)(int x1, int y1, int x2, int y2, unsigned char * c);
extern void (*ReadBuffer)(int x1, int y1, int x2, int y2, unsigned char * c);
extern void (*DrawBufferFast)(int x1, int y1, int x2, int y2, int blank, unsigned char * c);
extern void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char * c);
extern void (*DrawPixel)(int x1, int y1, int c);
extern void ApplyDefaultConsoleColours(void);

#define ESP32_VGA_SYNC_MAGIC 0x80u
#define ESP32_VGA_SYNC_MASK (VGA_LCDCAM_SYNC_HSYNC_IDLE_LOW | VGA_LCDCAM_SYNC_VSYNC_IDLE_LOW)
#define ESP32_VGA_CLOCK_SHIFT 2u
#define ESP32_VGA_CLOCK_MASK 0x0cu
#define ESP32_VGA_DRIVE_SHIFT 4u
#define ESP32_VGA_DRIVE_MASK 0x30u
#define ESP32_VGA_DRIVE_MAGIC 0x40u
#define ESP32_VGA_DISABLED_MAGIC 0x7fu
#define ESP32_VGA_MODE_640X480 1
#define ESP32_VGA_MODE_320X240 2
#define ESP32_VGA_MODE_320X240_DITHER 3
#define ESP32_VGA_INTERNAL_MODE_640X480 5
#define ESP32_VGA_INTERNAL_MODE_320X240 6
#define ESP32_VGA_INTERNAL_MODE_320X240_DITHER 7
#define ESP32_VGA_MODE_320X240_W 320
#define ESP32_VGA_MODE_320X240_H 240

static uint8_t * s_vga_scanout_fb = NULL;
static uint8_t * s_vga_logical_fb = NULL;
static int s_vga_mode = ESP32_VGA_MODE_640X480;

static uint8_t vga_sync_flags(void) {
    return (Option.VGA_PCLK & ESP32_VGA_SYNC_MAGIC) ? (Option.VGA_PCLK & ESP32_VGA_SYNC_MASK) : 0;
}

static uint8_t vga_clock_mode(void) {
    return (Option.VGA_PCLK & ESP32_VGA_SYNC_MAGIC)
               ? ((Option.VGA_PCLK & ESP32_VGA_CLOCK_MASK) >> ESP32_VGA_CLOCK_SHIFT)
               : VGA_LCDCAM_CLOCK_STANDARD;
}

static uint8_t vga_drive_cap(void) {
    if (!(Option.VGA_PCLK & ESP32_VGA_SYNC_MAGIC)) return 2;
    if (!(Option.VGA_PCLK & ESP32_VGA_DRIVE_MAGIC)) return 2;
    return (Option.VGA_PCLK & ESP32_VGA_DRIVE_MASK) >> ESP32_VGA_DRIVE_SHIFT;
}

static uint8_t vga_pack_option_flags(uint8_t sync_flags, uint8_t clock_mode, uint8_t drive_cap) {
    return ESP32_VGA_SYNC_MAGIC |
           (sync_flags & ESP32_VGA_SYNC_MASK) |
           ((clock_mode << ESP32_VGA_CLOCK_SHIFT) & ESP32_VGA_CLOCK_MASK) |
           ESP32_VGA_DRIVE_MAGIC |
           ((drive_cap << ESP32_VGA_DRIVE_SHIFT) & ESP32_VGA_DRIVE_MASK);
}

static uint8_t vga_3bit_default_sync_flags(void) {
    return 0;
}

static uint8_t vga_3bit_default_clock_mode(void) {
    return VGA_LCDCAM_CLOCK_25MHZ;
}

static const char * vga_clock_name(uint8_t mode) {
    switch (mode) {
    case VGA_LCDCAM_CLOCK_PLL240:
        return "PLL240";
    case VGA_LCDCAM_CLOCK_25MHZ:
        return "25MHZ";
    case VGA_LCDCAM_CLOCK_25MHZ240:
        return "25MHZ240";
    case VGA_LCDCAM_CLOCK_STANDARD:
    default:
        return "STANDARD";
    }
}

static int parse_vga_clock_mode(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if (checkstring(p, (unsigned char *)"STANDARD")) return VGA_LCDCAM_CLOCK_STANDARD;
    if (checkstring(p, (unsigned char *)"PLL240")) return VGA_LCDCAM_CLOCK_PLL240;
    if (checkstring(p, (unsigned char *)"25MHZ240")) return VGA_LCDCAM_CLOCK_25MHZ240;
    if (checkstring(p, (unsigned char *)"25MHZ")) return VGA_LCDCAM_CLOCK_25MHZ;
    return (int)getint(p, 0, 3);
}

/* True once OPTION VGA has supplied a usable pin map. Needs at least the
 * per-channel MSBs (bus bits 7/4/1) plus sync: full RGB332 sets all eight
 * data pins, while 3-bit (8-colour) sets only these three and leaves the
 * rest unconnected (esp_lcd skips data lines configured as -1). */
static int vga_configured(void) {
    return Option.VGA_DATA[7] && Option.VGA_DATA[4] && Option.VGA_DATA[1] &&
           Option.VGA_HSYNC && Option.VGA_VSYNC;
}

static int vga_disabled_by_user(void) {
    return Option.VGA_PCLK == ESP32_VGA_DISABLED_MAGIC;
}

/* Translate a stored pin number to its chip GPIO via the Metro pin map. */
static int pin_to_gpio(int pin) {
    if (pin <= 0 || pin > NBRPINS) return -1;
    return PinDef[pin].GPno;
}

static void esp32_vga_bind_rgb332_draw(void) {
    DrawRectangle = DrawRectangle256;
    DrawBitmap = DrawBitmap256;
    ScrollLCD = ScrollLCD256;
    DrawBuffer = DrawBuffer256;
    ReadBuffer = ReadBuffer256;
    DrawBufferFast = DrawBuffer256Fast;
    ReadBufferFast = ReadBuffer256Fast;
    DrawPixel = DrawPixel256;
}

static uint8_t * esp32_vga_alloc_logical_fb(size_t bytes) {
    uint8_t * p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    return p;
}

static int esp32_vga_mode_from_internal(int mode) {
    if (mode == ESP32_VGA_INTERNAL_MODE_640X480) return ESP32_VGA_MODE_640X480;
    if (mode == ESP32_VGA_INTERNAL_MODE_320X240) return ESP32_VGA_MODE_320X240;
    if (mode == ESP32_VGA_INTERNAL_MODE_320X240_DITHER) return ESP32_VGA_MODE_320X240_DITHER;
    return mode;
}

static int esp32_vga_display_type_for_mode(int mode) {
    if (mode == ESP32_VGA_MODE_320X240) return SCREENMODE6;
    if (mode == ESP32_VGA_MODE_320X240_DITHER) return SCREENMODE7;
    return SCREENMODE5;
}

static int esp32_vga_apply_mode(int mode, bool clear) {
    mode = esp32_vga_mode_from_internal(mode);
    if (!vga_lcdcam_s3_active() || !s_vga_scanout_fb) return 0;
    if (mode != ESP32_VGA_MODE_640X480 &&
        mode != ESP32_VGA_MODE_320X240 &&
        mode != ESP32_VGA_MODE_320X240_DITHER) return 0;

    if (mode == ESP32_VGA_MODE_640X480) {
        if (s_vga_logical_fb) {
            heap_caps_free(s_vga_logical_fb);
            s_vga_logical_fb = NULL;
        }
        HRes = DisplayHRes = VGA_LCDCAM_HRES;
        VRes = DisplayVRes = VGA_LCDCAM_VRES;
        ScreenSize = VGA_LCDCAM_HRES * VGA_LCDCAM_VRES;
        framebuffersize = (uint32_t)ScreenSize;
        FRAMEBUFFER = WriteBuf = DisplayBuf = FrameBuf = LayerBuf = SecondFrame = SecondLayer = s_vga_scanout_fb;
    } else {
        const size_t bytes = ESP32_VGA_MODE_320X240_W * ESP32_VGA_MODE_320X240_H;
        if (!s_vga_logical_fb) {
            s_vga_logical_fb = esp32_vga_alloc_logical_fb(bytes);
            if (!s_vga_logical_fb) error("Not enough memory");
            clear = true;
        }
        HRes = DisplayHRes = ESP32_VGA_MODE_320X240_W;
        VRes = DisplayVRes = ESP32_VGA_MODE_320X240_H;
        ScreenSize = (int)bytes;
        framebuffersize = (uint32_t)ScreenSize;
        FRAMEBUFFER = WriteBuf = DisplayBuf = FrameBuf = LayerBuf = SecondFrame = SecondLayer = s_vga_logical_fb;
    }

    Option.DISPLAY_TYPE = esp32_vga_display_type_for_mode(mode);
    DISPLAY_TYPE = Option.DISPLAY_TYPE;
    Option.DISPLAY_CONSOLE = 1;
    OptionConsole = 3;
    s_vga_mode = mode;
    esp32_vga_bind_rgb332_draw();
    ApplyDefaultConsoleColours();
    if (clear) {
        memset(WriteBuf, 0, ScreenSize);
        vga_lcdcam_s3_clear(0);
        if (mode == ESP32_VGA_MODE_320X240)
            vga_lcdcam_s3_present_rgb332_2x(WriteBuf, HRes, VRes, HRes, 0, 0, HRes - 1, VRes - 1);
    } else if (mode == ESP32_VGA_MODE_320X240) {
        vga_lcdcam_s3_present_rgb332_2x(WriteBuf, HRes, VRes, HRes, 0, 0, HRes - 1, VRes - 1);
    } else if (mode == ESP32_VGA_MODE_320X240_DITHER) {
        vga_lcdcam_s3_present_rgb332_2x_dither3(WriteBuf, HRes, VRes, HRes, 0, 0, HRes - 1, VRes - 1);
    } else {
        vga_lcdcam_s3_flush_all();
    }
    CurrentX = 0;
    CurrentY = 0;
    return 1;
}

void esp32_vga_display_init(void) {
    if (!vga_configured()) return;
    if (vga_lcdcam_s3_active()) return;

    vga_lcdcam_pins_t pins;
    for (int i = 0; i < 8; i++) pins.data_gpio[i] = pin_to_gpio(Option.VGA_DATA[i]);
    pins.hsync_gpio = pin_to_gpio(Option.VGA_HSYNC);
    pins.vsync_gpio = pin_to_gpio(Option.VGA_VSYNC);
    pins.pclk_gpio = -1; /* no PCLK pin: LCD_CAM paces DMA from pclk_hz */
    pins.sync_flags = vga_sync_flags();
    pins.clock_mode = vga_clock_mode();
    pins.drive_cap = vga_drive_cap();
    if (pins.data_gpio[7] < 0 || pins.data_gpio[4] < 0 || pins.data_gpio[1] < 0 ||
        pins.hsync_gpio < 0 || pins.vsync_gpio < 0)
        return;

    uint8_t * fb = NULL;
    if (!vga_lcdcam_s3_init(&pins, &fb) || fb == NULL) return;

    s_vga_scanout_fb = fb;
    esp32_vga_apply_mode(ESP32_VGA_MODE_640X480, true);
}

int esp32_vga_apply_default_options_if_unset(void) {
    if (vga_configured() || vga_disabled_by_user()) return 0;
    int r = codemap(8);
    int g = codemap(9);
    int b = codemap(10);
    int hsync = codemap(11);
    int vsync = codemap(12);
    if (r <= 0 || g <= 0 || b <= 0 || hsync <= 0 || vsync <= 0) return 0;
    memset(Option.VGA_DATA, 0, sizeof(Option.VGA_DATA));
    Option.VGA_DATA[7] = r;
    Option.VGA_DATA[4] = g;
    Option.VGA_DATA[1] = b;
    Option.VGA_HSYNC = hsync;
    Option.VGA_VSYNC = vsync;
    Option.VGA_PCLK = vga_pack_option_flags(0, VGA_LCDCAM_CLOCK_25MHZ, 2);
    return 1;
}

void DrawRGB332FlushRegion(int x1, int y1, int x2, int y2) {
    if (s_vga_mode == ESP32_VGA_MODE_320X240 && WriteBuf == s_vga_logical_fb) {
        vga_lcdcam_s3_present_rgb332_2x(s_vga_logical_fb, ESP32_VGA_MODE_320X240_W,
                                        ESP32_VGA_MODE_320X240_H, ESP32_VGA_MODE_320X240_W,
                                        x1, y1, x2, y2);
    } else if (s_vga_mode == ESP32_VGA_MODE_320X240_DITHER && WriteBuf == s_vga_logical_fb) {
        vga_lcdcam_s3_present_rgb332_2x_dither3(s_vga_logical_fb, ESP32_VGA_MODE_320X240_W,
                                                ESP32_VGA_MODE_320X240_H, ESP32_VGA_MODE_320X240_W,
                                                x1, y1, x2, y2);
    } else if (s_vga_mode == ESP32_VGA_MODE_640X480 && WriteBuf == s_vga_scanout_fb) {
        vga_lcdcam_s3_flush_region(x1, y1, x2, y2);
    }
}

void fastgfx_present_hook(void) {
    if (s_vga_mode == ESP32_VGA_MODE_320X240 && s_vga_logical_fb == FRAMEBUFFER) {
        vga_lcdcam_s3_present_rgb332_2x(s_vga_logical_fb, ESP32_VGA_MODE_320X240_W,
                                        ESP32_VGA_MODE_320X240_H, ESP32_VGA_MODE_320X240_W,
                                        0, 0, ESP32_VGA_MODE_320X240_W - 1,
                                        ESP32_VGA_MODE_320X240_H - 1);
    } else if (s_vga_mode == ESP32_VGA_MODE_320X240_DITHER && s_vga_logical_fb == FRAMEBUFFER) {
        vga_lcdcam_s3_present_rgb332_2x_dither3(s_vga_logical_fb, ESP32_VGA_MODE_320X240_W,
                                                ESP32_VGA_MODE_320X240_H, ESP32_VGA_MODE_320X240_W,
                                                0, 0, ESP32_VGA_MODE_320X240_W - 1,
                                                ESP32_VGA_MODE_320X240_H - 1);
    } else if (s_vga_mode == ESP32_VGA_MODE_640X480 && s_vga_scanout_fb == FRAMEBUFFER) {
        vga_lcdcam_s3_flush_all();
    }
}

void setmode(int mode, bool clear) {
    if (!vga_lcdcam_s3_active()) return;
    if (!esp32_vga_apply_mode(mode, clear)) error("Invalid mode");
}

void cmd_mode(void) {
    int mode = getint(cmdline, ESP32_VGA_MODE_640X480, ESP32_VGA_MODE_320X240_DITHER);
    setmode(mode, true);
}

/* Parse one GPIO token (plain chip GPIO number) into a stored pin index. */
static int parse_vga_gpio(unsigned char * arg) {
    /* Accept either a "GPn" pin name or a raw chip GPIO number, matching
     * how pins are written elsewhere (e.g. OPTION AUDIO). A bare getint()
     * would treat "GP8" as an auto-created variable (= 0), silently
     * mis-storing the pin. */
    unsigned char * p = arg;
    skipspace(p);
    int gpio;
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p') && isdigit(p[2]))
        gpio = (int)getinteger(p + 2);
    else
        gpio = (int)getinteger(p);
    if (gpio < 0 || gpio >= HAL_PORT_GPIO_COUNT) error("Invalid pin");
    int pin = codemap(gpio);
    if (pin <= 0 || pin > NBRPINS) error("Invalid pin");
    return pin;
}

static int parse_vga_sync_polarity(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if (checkstring(p, (unsigned char *)"NEGATIVE")) return 0;
    if (checkstring(p, (unsigned char *)"POSITIVE")) return 1;
    return (int)getint(p, 0, 1);
}

int esp32_vga_option_setter(unsigned char * line) {
    unsigned char * tp = checkstring(line, (unsigned char *)"VGA");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");

    if (checkstring(tp, (unsigned char *)"DISABLE")) {
        memset(Option.VGA_DATA, 0, sizeof(Option.VGA_DATA));
        Option.VGA_HSYNC = 0;
        Option.VGA_VSYNC = 0;
        Option.VGA_PCLK = ESP32_VGA_DISABLED_MAGIC;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    unsigned char * sp = checkstring(tp, (unsigned char *)"SYNC");
    if (sp) {
        getargs(&sp, 3, (unsigned char *)","); /* 2 args -> 3 tokens */
        if (argc != 3) error("Syntax: OPTION VGA SYNC NEGATIVE|POSITIVE,NEGATIVE|POSITIVE");
        uint8_t flags = 0;
        if (parse_vga_sync_polarity(argv[0])) flags |= VGA_LCDCAM_SYNC_HSYNC_IDLE_LOW;
        if (parse_vga_sync_polarity(argv[2])) flags |= VGA_LCDCAM_SYNC_VSYNC_IDLE_LOW;
        Option.VGA_PCLK = vga_pack_option_flags(flags, vga_clock_mode(), vga_drive_cap());
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    sp = checkstring(tp, (unsigned char *)"CLOCK");
    if (sp) {
        getargs(&sp, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax: OPTION VGA CLOCK STANDARD|PLL240|25MHZ|25MHZ240");
        Option.VGA_PCLK = vga_pack_option_flags(vga_sync_flags(), parse_vga_clock_mode(argv[0]), vga_drive_cap());
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    sp = checkstring(tp, (unsigned char *)"DRIVE");
    if (sp) {
        getargs(&sp, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax: OPTION VGA DRIVE 0..3");
        Option.VGA_PCLK = vga_pack_option_flags(vga_sync_flags(), vga_clock_mode(), (uint8_t)getint(argv[0], 0, 3));
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    /* 3-bit (8-colour) mode for a 1-bit-per-channel DAC (e.g. the Serial
     * Wombat board). Only the three channel MSBs are wired; the remaining
     * five RGB332 bus bits stay unconnected (-1, skipped by esp_lcd). */
    unsigned char * bp = checkstring(tp, (unsigned char *)"3BIT");
    if (bp) {
        uint8_t sync_flags = vga_3bit_default_sync_flags();
        getargs(&bp, 9, (unsigned char *)","); /* 5 pins -> 9 tokens */
        if (argc != 9) error("Syntax: OPTION VGA 3BIT r,g,b,hsync,vsync");
        memset(Option.VGA_DATA, 0, sizeof(Option.VGA_DATA));
        Option.VGA_DATA[7] = parse_vga_gpio(argv[0]); /* red   -> bus bit 7 */
        Option.VGA_DATA[4] = parse_vga_gpio(argv[2]); /* green -> bus bit 4 */
        Option.VGA_DATA[1] = parse_vga_gpio(argv[4]); /* blue  -> bus bit 1 */
        Option.VGA_HSYNC = parse_vga_gpio(argv[6]);
        Option.VGA_VSYNC = parse_vga_gpio(argv[8]);
        Option.VGA_PCLK = vga_pack_option_flags(sync_flags, vga_3bit_default_clock_mode(), vga_drive_cap());
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    uint8_t sync_flags = vga_sync_flags();
    getargs(&tp, 19, (unsigned char *)","); /* 10 pins -> up to 19 tokens */
    if (argc != 19) error("Syntax: OPTION VGA r2,r1,r0,g2,g1,g0,b1,b0,hsync,vsync");

    /* argv order is MSB-first per channel; store so VGA_DATA[bit] holds
     * the pin driving bus bit `bit` (bit 7 = red MSB .. bit 0 = blue LSB). */
    for (int i = 0; i < 8; i++)
        Option.VGA_DATA[7 - i] = parse_vga_gpio(argv[i * 2]);
    Option.VGA_HSYNC = parse_vga_gpio(argv[16]);
    Option.VGA_VSYNC = parse_vga_gpio(argv[18]);
    Option.VGA_PCLK = vga_pack_option_flags(sync_flags, vga_clock_mode(), vga_drive_cap());

    SaveOptions();
    _excep_code = RESET_COMMAND;
    esp_restart();
    return 1;
}

/* LIST OPTIONS line. 3-bit mode wires only the three channel MSBs (bus
 * bits 7/4/1); full RGB332 also sets the lower bits, so a non-MSB data
 * pin distinguishes the two forms. */
void esp32_vga_print_options(void) {
    if (!vga_configured()) return;
    uint8_t sync_flags = vga_sync_flags();
    int full = Option.VGA_DATA[0] || Option.VGA_DATA[2];
    MMPrintString("OPTION VGA ");
    if (full) {
        for (int i = 7; i >= 0; i--) {
            MMPrintString((char *)PinDef[Option.VGA_DATA[i]].pinname);
            MMputchar(',', 1);
        }
    } else {
        MMPrintString("3BIT ");
        MMPrintString((char *)PinDef[Option.VGA_DATA[7]].pinname);
        MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.VGA_DATA[4]].pinname);
        MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.VGA_DATA[1]].pinname);
        MMputchar(',', 1);
    }
    MMPrintString((char *)PinDef[Option.VGA_HSYNC].pinname);
    MMputchar(',', 1);
    MMPrintString((char *)PinDef[Option.VGA_VSYNC].pinname);
    PRet();
    MMPrintString("OPTION VGA SYNC ");
    MMPrintString((sync_flags & VGA_LCDCAM_SYNC_HSYNC_IDLE_LOW) ? "POSITIVE" : "NEGATIVE");
    MMputchar(',', 1);
    MMPrintString((sync_flags & VGA_LCDCAM_SYNC_VSYNC_IDLE_LOW) ? "POSITIVE" : "NEGATIVE");
    PRet();
    MMPrintString("OPTION VGA CLOCK ");
    MMPrintString((char *)vga_clock_name(vga_clock_mode()));
    PRet();
    MMPrintString("OPTION VGA DRIVE ");
    char drive[4];
    IntToStr(drive, vga_drive_cap(), 10);
    MMPrintString(drive);
    PRet();
}
