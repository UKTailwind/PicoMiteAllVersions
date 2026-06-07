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
#include "hal/hal_pin.h"
#include "hal/hal_vga_ops.h"

#include "drivers/draw_rgb332/draw_rgb332.h"
#include "vga_lcdcam_s3.h"
#include "esp32_option_ext.h"

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
    return (ESP32_OPTION_VGA_PCLK & ESP32_VGA_SYNC_MAGIC) ? (ESP32_OPTION_VGA_PCLK & ESP32_VGA_SYNC_MASK) : 0;
}

static uint8_t vga_clock_mode(void) {
    return (ESP32_OPTION_VGA_PCLK & ESP32_VGA_SYNC_MAGIC)
               ? ((ESP32_OPTION_VGA_PCLK & ESP32_VGA_CLOCK_MASK) >> ESP32_VGA_CLOCK_SHIFT)
               : VGA_LCDCAM_CLOCK_STANDARD;
}

static uint8_t vga_drive_cap(void) {
    if (!(ESP32_OPTION_VGA_PCLK & ESP32_VGA_SYNC_MAGIC)) return 2;
    if (!(ESP32_OPTION_VGA_PCLK & ESP32_VGA_DRIVE_MAGIC)) return 2;
    return (ESP32_OPTION_VGA_PCLK & ESP32_VGA_DRIVE_MASK) >> ESP32_VGA_DRIVE_SHIFT;
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
    return ESP32_OPTION_VGA_DATA[7] && ESP32_OPTION_VGA_DATA[4] && ESP32_OPTION_VGA_DATA[1] &&
           Option.VGA_HSYNC && ESP32_OPTION_VGA_VSYNC;
}

static int vga_disabled_by_user(void) {
    return ESP32_OPTION_VGA_PCLK == ESP32_VGA_DISABLED_MAGIC;
}

/* Translate a stored pin number to its chip GPIO via the ESP32-S3 pin map. */
static int pin_to_gpio(int pin) {
    if (pin <= 0 || pin > NBRPINS) return -1;
    return PinDef[pin].GPno;
}

static int esp32_vga_pin_invalid(int pin) {
    return pin <= 0 || pin > NBRPINS || (PinDef[pin].mode & UNUSED);
}

static int esp32_vga_pin_reserved(int pin) {
    return !esp32_vga_pin_invalid(pin) && ExtCurrentConfig[pin] == EXT_BOOT_RESERVED;
}

static int esp32_vga_is_current_pin(int pin) {
    if (!esp32_vga_pin_reserved(pin))
        return 0;
    if (pin == Option.VGA_HSYNC || pin == ESP32_OPTION_VGA_VSYNC)
        return 1;
    for (int i = 0; i < ESP32_OPTION_VGA_DATA_COUNT; i++)
        if (ESP32_OPTION_VGA_DATA[i] == pin)
            return 1;
    return 0;
}

static void esp32_vga_release_pin(int pin) {
    if (!esp32_vga_pin_reserved(pin))
        return;
    hal_pin_deinit((uint32_t)PinDef[pin].GPno);
    ExtCurrentConfig[pin] = EXT_NOT_CONFIG;
}

static void esp32_vga_require_available_pin(int pin) {
    if (esp32_vga_pin_invalid(pin))
        error("Invalid pin");
    if (ExtCurrentConfig[pin] != EXT_NOT_CONFIG && !esp32_vga_is_current_pin(pin))
        error("Pin %/| is in use", pin, pin);
}

static void esp32_vga_require_distinct_pins(const uint8_t * pins, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!pins[i]) continue;
        for (size_t j = i + 1; j < count; j++) {
            if (pins[i] == pins[j])
                error("Pin %/| is in use", pins[j], pins[j]);
        }
    }
}

static void esp32_vga_reserve_pin(int pin) {
    if (!esp32_vga_pin_invalid(pin))
        ExtCurrentConfig[pin] = EXT_BOOT_RESERVED;
}

void esp32_vga_reserve_option_pins(void) {
    if (!vga_configured()) return;
    for (int i = 0; i < ESP32_OPTION_VGA_DATA_COUNT; i++)
        esp32_vga_reserve_pin(ESP32_OPTION_VGA_DATA[i]);
    esp32_vga_reserve_pin(Option.VGA_HSYNC);
    esp32_vga_reserve_pin(ESP32_OPTION_VGA_VSYNC);
}

static void esp32_vga_clear_options(void) {
    for (int i = 0; i < ESP32_OPTION_VGA_DATA_COUNT; i++)
        esp32_vga_release_pin(ESP32_OPTION_VGA_DATA[i]);
    esp32_vga_release_pin(Option.VGA_HSYNC);
    esp32_vga_release_pin(ESP32_OPTION_VGA_VSYNC);
    memset(ESP32_OPTION_VGA_DATA, 0, ESP32_OPTION_VGA_DATA_COUNT);
    Option.VGA_HSYNC = 0;
    ESP32_OPTION_VGA_VSYNC = 0;
}

static void esp32_vga_present_region(int x1, int y1, int x2, int y2) {
    int t;
    if (!vga_lcdcam_s3_active() || HRes <= 0 || VRes <= 0) return;
    if (x2 < x1) {
        t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y2 < y1) {
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= HRes) x2 = HRes - 1;
    if (y2 >= VRes) y2 = VRes - 1;
    if (x1 > x2 || y1 > y2) return;

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

static void esp32_vga_present_all(void) {
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

static void esp32_DrawRectangle256(int x1, int y1, int x2, int y2, int c) {
    DrawRectangle256(x1, y1, x2, y2, c);
    esp32_vga_present_region(x1, y1, x2, y2);
}

static void esp32_DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char * bitmap) {
    DrawBitmap256(x1, y1, width, height, scale, fc, bc, bitmap);
    esp32_vga_present_region(x1, y1, x1 + width * scale - 1, y1 + height * scale - 1);
}

static void esp32_DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char * p) {
    DrawBuffer256(x1, y1, x2, y2, p);
    esp32_vga_present_region(x1, y1, x2, y2);
}

static void esp32_DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char * p) {
    DrawBuffer256Fast(x1, y1, x2, y2, blank, p);
    esp32_vga_present_region(x1, y1, x2, y2);
}

static void esp32_DrawPixel256(int x, int y, int c) {
    DrawPixel256(x, y, c);
    esp32_vga_present_region(x, y, x, y);
}

static void esp32_ScrollLCD256(int lines) {
    ScrollLCD256(lines);
    esp32_vga_present_region(0, 0, HRes - 1, VRes - 1);
}

static void esp32_vga_bind_rgb332_draw(void) {
    DrawRectangle = esp32_DrawRectangle256;
    DrawBitmap = esp32_DrawBitmap256;
    ScrollLCD = esp32_ScrollLCD256;
    DrawBuffer = esp32_DrawBuffer256;
    ReadBuffer = ReadBuffer256;
    DrawBufferFast = esp32_DrawBuffer256Fast;
    ReadBufferFast = ReadBuffer256Fast;
    DrawPixel = esp32_DrawPixel256;
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
    for (int i = 0; i < 8; i++) pins.data_gpio[i] = pin_to_gpio(ESP32_OPTION_VGA_DATA[i]);
    pins.hsync_gpio = pin_to_gpio(Option.VGA_HSYNC);
    pins.vsync_gpio = pin_to_gpio(ESP32_OPTION_VGA_VSYNC);
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
    hal_vga_ops_set_fastgfx_present_callback(esp32_vga_present_all);
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
    memset(ESP32_OPTION_VGA_DATA, 0, ESP32_OPTION_VGA_DATA_COUNT);
    ESP32_OPTION_VGA_DATA[7] = r;
    ESP32_OPTION_VGA_DATA[4] = g;
    ESP32_OPTION_VGA_DATA[1] = b;
    Option.VGA_HSYNC = hsync;
    ESP32_OPTION_VGA_VSYNC = vsync;
    ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(0, VGA_LCDCAM_CLOCK_25MHZ, 2);
    return 1;
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
        esp32_vga_clear_options();
        ESP32_OPTION_VGA_PCLK = ESP32_VGA_DISABLED_MAGIC;
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
        ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(flags, vga_clock_mode(), vga_drive_cap());
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    sp = checkstring(tp, (unsigned char *)"CLOCK");
    if (sp) {
        getargs(&sp, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax: OPTION VGA CLOCK STANDARD|PLL240|25MHZ|25MHZ240");
        ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(vga_sync_flags(), parse_vga_clock_mode(argv[0]), vga_drive_cap());
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    sp = checkstring(tp, (unsigned char *)"DRIVE");
    if (sp) {
        getargs(&sp, 1, (unsigned char *)",");
        if (argc != 1) error("Syntax: OPTION VGA DRIVE 0..3");
        ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(vga_sync_flags(), vga_clock_mode(), (uint8_t)getint(argv[0], 0, 3));
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
        uint8_t pins[5];
        uint8_t data[ESP32_OPTION_VGA_DATA_COUNT] = {0};
        getargs(&bp, 9, (unsigned char *)","); /* 5 pins -> 9 tokens */
        if (argc != 9) error("Syntax: OPTION VGA 3BIT r,g,b,hsync,vsync");
        pins[0] = data[7] = parse_vga_gpio(argv[0]); /* red   -> bus bit 7 */
        pins[1] = data[4] = parse_vga_gpio(argv[2]); /* green -> bus bit 4 */
        pins[2] = data[1] = parse_vga_gpio(argv[4]); /* blue  -> bus bit 1 */
        pins[3] = parse_vga_gpio(argv[6]);
        pins[4] = parse_vga_gpio(argv[8]);
        for (int i = 0; i < 5; i++) esp32_vga_require_available_pin(pins[i]);
        esp32_vga_require_distinct_pins(pins, 5);
        esp32_vga_clear_options();
        memcpy(ESP32_OPTION_VGA_DATA, data, ESP32_OPTION_VGA_DATA_COUNT);
        Option.VGA_HSYNC = pins[3];
        ESP32_OPTION_VGA_VSYNC = pins[4];
        ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(sync_flags, vga_3bit_default_clock_mode(), vga_drive_cap());
        esp32_vga_reserve_option_pins();
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    uint8_t sync_flags = vga_sync_flags();
    uint8_t pins[10];
    uint8_t data[ESP32_OPTION_VGA_DATA_COUNT] = {0};
    getargs(&tp, 19, (unsigned char *)","); /* 10 pins -> up to 19 tokens */
    if (argc != 19) error("Syntax: OPTION VGA r2,r1,r0,g2,g1,g0,b1,b0,hsync,vsync");

    /* argv order is MSB-first per channel; store so VGA_DATA[bit] holds
     * the pin driving bus bit `bit` (bit 7 = red MSB .. bit 0 = blue LSB). */
    for (int i = 0; i < 8; i++)
        pins[i] = data[7 - i] = parse_vga_gpio(argv[i * 2]);
    pins[8] = parse_vga_gpio(argv[16]);
    pins[9] = parse_vga_gpio(argv[18]);
    for (int i = 0; i < 10; i++) esp32_vga_require_available_pin(pins[i]);
    esp32_vga_require_distinct_pins(pins, 10);
    esp32_vga_clear_options();
    memcpy(ESP32_OPTION_VGA_DATA, data, ESP32_OPTION_VGA_DATA_COUNT);
    Option.VGA_HSYNC = pins[8];
    ESP32_OPTION_VGA_VSYNC = pins[9];
    ESP32_OPTION_VGA_PCLK = vga_pack_option_flags(sync_flags, vga_clock_mode(), vga_drive_cap());
    esp32_vga_reserve_option_pins();

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
    int full = ESP32_OPTION_VGA_DATA[0] || ESP32_OPTION_VGA_DATA[2];
    MMPrintString("OPTION VGA ");
    if (full) {
        for (int i = 7; i >= 0; i--) {
            MMPrintString((char *)PinDef[ESP32_OPTION_VGA_DATA[i]].pinname);
            MMputchar(',', 1);
        }
    } else {
        MMPrintString("3BIT ");
        MMPrintString((char *)PinDef[ESP32_OPTION_VGA_DATA[7]].pinname);
        MMputchar(',', 1);
        MMPrintString((char *)PinDef[ESP32_OPTION_VGA_DATA[4]].pinname);
        MMputchar(',', 1);
        MMPrintString((char *)PinDef[ESP32_OPTION_VGA_DATA[1]].pinname);
        MMputchar(',', 1);
    }
    MMPrintString((char *)PinDef[Option.VGA_HSYNC].pinname);
    MMputchar(',', 1);
    MMPrintString((char *)PinDef[ESP32_OPTION_VGA_VSYNC].pinname);
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
