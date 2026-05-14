/*
 * hal_vm_framebuffer_esp32_stub.c — ESP32 web-console virtual display.
 *
 * FRAMEBUFFER commands remain no-op for now, but the ordinary display
 * dispatch table is backed by a 320x240 RGB24 framebuffer. Draw calls
 * update pixels and dirty bounds; the web console transport flushes FRMB
 * snapshots or framebuffer-derived BLIT deltas.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "drivers/web_console/web_console_display.h"
#include "hal/hal_vm_framebuffer.h"

#define ESP32_WEB_DISPLAY_WIDTH  320
#define ESP32_WEB_DISPLAY_HEIGHT 240

static web_console_display_t s_web_display;
static uint32_t *s_web_pixels;
extern volatile int DISPLAY_TYPE;
extern void ProcessWeb(int mode);

web_console_display_t *esp32_web_console_display(void) {
    return s_web_pixels ? &s_web_display : NULL;
}

static void *web_display_alloc(size_t bytes) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(bytes);
    return p;
}

static void esp32_web_draw_pixel(int x, int y, int c) {
    web_console_display_pixel(&s_web_display, x, y, c);
}

static void esp32_web_draw_rectangle(int x1, int y1, int x2, int y2, int c) {
    web_console_display_rect(&s_web_display, x1, y1, x2, y2, c);
}

static void esp32_web_draw_bitmap(int x, int y, int width, int height,
                                  int scale, int fc, int bc,
                                  unsigned char *bitmap) {
    web_console_display_bitmap(&s_web_display, x, y, width, height, scale,
                               fc, bc, bitmap);
}

static void esp32_web_scroll_lcd(int lines) {
    web_console_display_scroll(&s_web_display, lines, gui_bcolour);
}

static void esp32_web_draw_buffer(int x1, int y1, int x2, int y2,
                                  unsigned char *p) {
    web_console_display_draw_buffer(&s_web_display, x1, y1, x2, y2, p);
}

static void esp32_web_draw_buffer_fast(int x1, int y1, int x2, int y2,
                                       int blank, unsigned char *p) {
    (void)blank;
    web_console_display_draw_buffer(&s_web_display, x1, y1, x2, y2, p);
}

static void esp32_web_read_buffer(int x1, int y1, int x2, int y2,
                                  unsigned char *p) {
    web_console_display_read_buffer(&s_web_display, x1, y1, x2, y2, p);
}

int esp32_web_console_display_init(void) {
    /* When the web console is disabled keep the framebuffer / display
     * dispatch entirely uninitialised. The USB serial console then takes
     * the fast path with OptionConsole=1 and no per-character framebuffer
     * cost. Also clear any DISPLAY_CONSOLE state left over from a previous
     * session when WebConsole was on -- otherwise `OPTION DISPLAY rows,cols`
     * keeps erroring "Cannot change LCD console" until OPTION RESET. */
    if (!Option.WebConsole) {
        int changed = Option.DISPLAY_CONSOLE != 0 || Option.DISPLAY_TYPE != 0;
        Option.DISPLAY_CONSOLE = 0;
        Option.DISPLAY_TYPE = 0;
        return changed;
    }

    int options_changed = Option.DISPLAY_TYPE != DISP_USER ||
                          Option.DISPLAY_CONSOLE != 1 ||
                          Option.DefaultFont != 0x01 ||
                          Option.ColourCode != 1 ||
                          Option.DefaultFC == 0;
    size_t pixels = (size_t)ESP32_WEB_DISPLAY_WIDTH *
                    (size_t)ESP32_WEB_DISPLAY_HEIGHT;
    if (!s_web_pixels) {
        s_web_pixels = (uint32_t *)web_display_alloc(pixels * sizeof(*s_web_pixels));
        if (!s_web_pixels) return 0;
    }
    if (!web_console_display_init(&s_web_display, ESP32_WEB_DISPLAY_WIDTH,
                                  ESP32_WEB_DISPLAY_HEIGHT, s_web_pixels,
                                  pixels, Option.DefaultBC)) {
        return 0;
    }

    Option.DISPLAY_TYPE = DISP_USER;
    DISPLAY_TYPE = DISP_USER;
    Option.DISPLAY_CONSOLE = 1;
    OptionConsole = 3;
    HRes = DisplayHRes = ESP32_WEB_DISPLAY_WIDTH;
    VRes = DisplayVRes = ESP32_WEB_DISPLAY_HEIGHT;
    DrawPixel = esp32_web_draw_pixel;
    DrawRectangle = esp32_web_draw_rectangle;
    DrawBitmap = esp32_web_draw_bitmap;
    ScrollLCD = esp32_web_scroll_lcd;
    DrawBuffer = esp32_web_draw_buffer;
    DrawBLITBuffer = esp32_web_draw_buffer;
    DrawBufferFast = esp32_web_draw_buffer_fast;
    ReadBuffer = esp32_web_read_buffer;
    ReadBufferFast = esp32_web_read_buffer;

    Option.DefaultFont = 0x01;
    Option.ColourCode = 1;
    if (Option.DefaultFC == 0 && Option.DefaultBC == 0) {
        Option.DefaultFC = 0x00ff00;
        Option.DefaultBC = 0x000000;
    }
    ApplyDefaultConsoleColours();
    CurrentX = 0;
    CurrentY = 0;
    ClearScreen(gui_bcolour);
    return options_changed;
}

void hal_vm_framebuffer_shutdown_runtime(void) {}
void hal_vm_framebuffer_service(void) {}
void hal_vm_framebuffer_create(int fast) { (void)fast; }
void hal_vm_framebuffer_layer(int hc, int c) { (void)hc; (void)c; }
void hal_vm_framebuffer_write(char w) { (void)w; }
void hal_vm_framebuffer_close(char w) { (void)w; }
void hal_vm_framebuffer_merge(int hc, int c, int m, int hr, int rms) {
    (void)hc; (void)c; (void)m; (void)hr; (void)rms;
}
void hal_vm_framebuffer_sync(void) {}
void hal_vm_framebuffer_wait(void) {}
void hal_vm_framebuffer_copy(char from, char to, int bg) { (void)from; (void)to; (void)bg; }

/* No physical display flush is required, but graphics-heavy BASIC code can
 * spend a long time between normal runtime polls. Use refresh as a capped
 * opportunity to drain framebuffer deltas without making draw primitives
 * transport-aware. */
void Display_Refresh(void) {
    static int in_refresh;
    static int64_t next_refresh_us;
    if (!Option.WebConsole) return;
    int64_t now = esp_timer_get_time();

    if (in_refresh || now < next_refresh_us) return;
    next_refresh_us = now + 33333;
    in_refresh = 1;
    ProcessWeb(0);
    in_refresh = 0;
}

/* Display-related globals + functions referenced by core code. */
volatile int DISPLAY_TYPE = 0;
void ScrollLCDSPISCR(int lines) { (void)lines; }
void setterminal(int height, int width) {
    if (height > 0) Option.Height = height;
    if (width > 0) Option.Width = width;
}

/* PicoCalc 320x320-screen flag, dispatcher state, and direct-buffer
 * primitives not used by the web display MVP. */
bool screen320 = 0;
void DisplayNotSet(void) {}
void setframebuffer(void) {}
void closeframebuffer(char layer) { (void)layer; }
void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    (void)x0; (void)y0; (void)w; (void)h; (void)colour;
}
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd) {
    (void)s; (void)xstart; (void)xend; (void)ystart; (void)yend; (void)odd;
}
