/*
 * esp32_vga.c — ESP32-S3 VGA (RGB332 over LCD_CAM) option setter + display
 * bring-up.
 *
 * OPTION VGA r2,r1,r0,g2,g1,g0,b1,b0,hsync,vsync,pclk
 *   Stores the eleven GPIOs that drive an external resistor-ladder DAC
 *   (VGA666-style, wired RGB332). The eight data pins are listed
 *   most-significant first per channel; pclk is required by LCD_CAM but
 *   is not wired to the DAC board (use any spare GPIO).
 * OPTION VGA DISABLE
 *   Clears the configuration.
 *
 * esp32_vga_display_init() brings the panel up at boot when configured,
 * binds the RGB332 draw primitives, and points the framebuffer globals
 * at the continuously-scanned LCD_CAM buffer. The REPL stays on the USB
 * serial console; VGA is a graphics surface that BASIC draws into.
 */

#include <stdint.h>
#include <string.h>

#include "esp_system.h" /* esp_restart */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"

#include "drivers/draw_rgb332/draw_rgb332.h"
#include "vga_lcdcam_s3.h"

/* Core framebuffer + draw-pointer globals (defined in Draw.c / state). */
extern short HRes, VRes;
extern int ScreenSize;
extern unsigned char *WriteBuf, *DisplayBuf, *FrameBuf, *LayerBuf, *SecondFrame, *SecondLayer;
extern volatile int DISPLAY_TYPE;
extern void (*DrawRectangle)(int x1, int y1, int x2, int y2, int c);
extern void (*DrawBitmap)(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void (*ScrollLCD)(int lines);
extern void (*DrawBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*DrawBufferFast)(int x1, int y1, int x2, int y2, int blank, unsigned char *c);
extern void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*DrawPixel)(int x1, int y1, int c);

/* True once OPTION VGA has supplied a full pin map. */
static int vga_configured(void) {
    for (int i = 0; i < 8; i++)
        if (Option.VGA_DATA[i] == 0) return 0;
    return Option.VGA_HSYNC && Option.VGA_VSYNC && Option.VGA_PCLK;
}

/* Translate a stored pin number to its chip GPIO via the Metro pin map. */
static int pin_to_gpio(int pin) {
    if (pin <= 0 || pin > NBRPINS) return -1;
    return PinDef[pin].GPno;
}

void esp32_vga_display_init(void) {
    if (!vga_configured()) return;
    if (vga_lcdcam_s3_active()) return;

    vga_lcdcam_pins_t pins;
    for (int i = 0; i < 8; i++) pins.data_gpio[i] = pin_to_gpio(Option.VGA_DATA[i]);
    pins.hsync_gpio = pin_to_gpio(Option.VGA_HSYNC);
    pins.vsync_gpio = pin_to_gpio(Option.VGA_VSYNC);
    pins.pclk_gpio = pin_to_gpio(Option.VGA_PCLK);

    uint8_t *fb = NULL;
    if (!vga_lcdcam_s3_init(&pins, &fb) || fb == NULL) return;

    HRes = VGA_LCDCAM_HRES;
    VRes = VGA_LCDCAM_VRES;
    ScreenSize = VGA_LCDCAM_HRES * VGA_LCDCAM_VRES;
    WriteBuf = DisplayBuf = FrameBuf = LayerBuf = SecondFrame = SecondLayer = fb;
    DISPLAY_TYPE = SCREENMODE5; /* 8-bit RGB332 surface */

    DrawRectangle = DrawRectangle256;
    DrawBitmap = DrawBitmap256;
    ScrollLCD = ScrollLCD256;
    DrawBuffer = DrawBuffer256;
    ReadBuffer = ReadBuffer256;
    DrawBufferFast = DrawBuffer256Fast;
    ReadBufferFast = ReadBuffer256Fast;
    DrawPixel = DrawPixel256;

    memset(fb, 0, ScreenSize);
}

/* Parse one GPIO token (plain chip GPIO number) into a stored pin index. */
static int parse_vga_gpio(unsigned char *arg) {
    int gpio = getint(arg, 0, HAL_PORT_GPIO_COUNT - 1);
    int pin = codemap(gpio);
    if (pin <= 0 || pin > NBRPINS) error("Invalid pin");
    return pin;
}

int esp32_vga_option_setter(unsigned char *line) {
    unsigned char *tp = checkstring(line, (unsigned char *)"VGA");
    if (!tp) return 0;
    if (CurrentLinePtr) error("Invalid in a program");

    if (checkstring(tp, (unsigned char *)"DISABLE")) {
        memset(Option.VGA_DATA, 0, sizeof(Option.VGA_DATA));
        Option.VGA_VSYNC = 0;
        Option.VGA_PCLK = 0;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        esp_restart();
        return 1;
    }

    getargs(&tp, 21, (unsigned char *)","); /* 11 pins -> up to 21 tokens */
    if (argc != 21) error("Syntax: OPTION VGA r2,r1,r0,g2,g1,g0,b1,b0,hsync,vsync,pclk");

    /* argv order is MSB-first per channel; store so VGA_DATA[bit] holds
     * the pin driving bus bit `bit` (bit 7 = red MSB .. bit 0 = blue LSB). */
    for (int i = 0; i < 8; i++)
        Option.VGA_DATA[7 - i] = parse_vga_gpio(argv[i * 2]);
    Option.VGA_HSYNC = parse_vga_gpio(argv[16]);
    Option.VGA_VSYNC = parse_vga_gpio(argv[18]);
    Option.VGA_PCLK = parse_vga_gpio(argv[20]);

    SaveOptions();
    _excep_code = RESET_COMMAND;
    esp_restart();
    return 1;
}
