/*
 * hal_vm_framebuffer_esp32.c — ESP32 web-console virtual display and
 * local SPI-LCD FRAMEBUFFER backend.
 *
 * The web-console display path is backed by a 320x240 RGB24 framebuffer.
 * The Freenove ILI9341 path implements MMBasic's 4-bit FRAMEBUFFER command
 * surface over PSRAM buffers and presents RGB121 rectangles through the
 * ESP-IDF ILI9341 transport.
 */

#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "drivers/web_console/web_console_display.h"
#include "hal/hal_time.h"
#include "hal/hal_vm_framebuffer.h"

#define ESP32_WEB_DISPLAY_WIDTH 320
#define ESP32_WEB_DISPLAY_HEIGHT 240

static web_console_display_t s_web_display;
static uint32_t * s_web_pixels;
extern volatile int DISPLAY_TYPE;
extern void ProcessWeb(int mode);
extern const int colours[16];
extern unsigned char * WriteBuf;
extern bool mergerunning;
extern volatile bool mergedone;
extern uint32_t mergetimer;

extern int esp32_ili9341_lcd_restore_panel(void);
extern int esp32_ili9341_lcd_ready(void);
extern void esp32_ili9341_lcd_flush_pending(void);
extern int esp32_fastgfx_active(void);
extern void esp32_ili9341_lcd_present_rgb121_rect(const uint8_t * src,
                                                  int xstart, int xend,
                                                  int ystart, int yend,
                                                  int odd);

static int s_fb_merge_running;
static uint8_t s_fb_merge_colour;
static uint32_t s_fb_merge_interval_us;
static int64_t s_fb_merge_next_us;
static uint8_t * s_fb_copy_src;
static int s_fb_copy_pending;
static int s_fb_in_service;

web_console_display_t * esp32_web_console_display(void) {
    return s_web_pixels ? &s_web_display : NULL;
}

void esp32_web_console_display_deinit(void) {
    if (s_web_pixels) {
        heap_caps_free(s_web_pixels);
        s_web_pixels = NULL;
    }
    memset(&s_web_display, 0, sizeof(s_web_display));
}

static void * web_display_alloc(size_t bytes) {
    void * p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
                                  unsigned char * bitmap) {
    web_console_display_bitmap(&s_web_display, x, y, width, height, scale,
                               fc, bc, bitmap);
}

static void esp32_web_scroll_lcd(int lines) {
    web_console_display_scroll(&s_web_display, lines, gui_bcolour);
}

static void esp32_web_draw_buffer(int x1, int y1, int x2, int y2,
                                  unsigned char * p) {
    web_console_display_draw_buffer(&s_web_display, x1, y1, x2, y2, p);
}

static uint8_t web_packed_rgb121_get(const uint8_t * p, int index) {
    uint8_t b = p[(size_t)index >> 1];
    return (index & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0fu);
}

static void web_packed_rgb121_set(uint8_t * p, int index, uint8_t v) {
    uint8_t * b = &p[(size_t)index >> 1];
    if (index & 1)
        *b = (uint8_t)((*b & 0x0fu) | ((v & 0x0fu) << 4));
    else
        *b = (uint8_t)((*b & 0xf0u) | (v & 0x0fu));
}

static void esp32_web_draw_buffer_fast(int x1, int y1, int x2, int y2,
                                       int blank, unsigned char * p) {
    if (!p) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    int src_w = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint8_t nibble = web_packed_rgb121_get(
                p, (y - y1) * src_w + (x - x1));
            if (blank != -1 && nibble == sprite_transparent) continue;
            web_console_display_pixel(&s_web_display, x, y, colours[nibble & 0x0f]);
        }
    }
}

static void esp32_web_read_buffer(int x1, int y1, int x2, int y2,
                                  unsigned char * p) {
    web_console_display_read_buffer(&s_web_display, x1, y1, x2, y2, p);
}

static void esp32_web_read_buffer_fast(int x1, int y1, int x2, int y2,
                                       unsigned char * p) {
    if (!p) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    int index = 0;
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            uint8_t nibble = 0;
            if (x >= 0 && y >= 0 && x < s_web_display.width &&
                y < s_web_display.height && s_web_pixels) {
                uint32_t c = s_web_pixels[(size_t)y * (size_t)s_web_display.width +
                                          (size_t)x];
                nibble = RGB121(c);
            }
            web_packed_rgb121_set(p, index++, nibble);
        }
    }
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
        esp32_web_console_display_deinit();
        Option.DISPLAY_CONSOLE = 0;
        Option.DISPLAY_TYPE = 0;
        DISPLAY_TYPE = 0;
        OptionConsole = 1;
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
    ReadBLITBuffer = esp32_web_read_buffer;
    ReadBufferFast = esp32_web_read_buffer_fast;

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

static size_t esp32_fb_bytes(void) {
    if (HRes <= 0 || VRes <= 0) return 0;
    return (size_t)HRes * (size_t)VRes / 2u;
}

static uint8_t esp32_fb_rgb121(uint32_t c) {
    return (uint8_t)(((c & 0x800000u) >> 20) |
                     ((c & 0x00C000u) >> 13) |
                     ((c & 0x000080u) >> 7));
}

static unsigned char * esp32_fb_alloc(size_t bytes, const char * tag) {
    unsigned char * p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    if (!p) error("NEM[%s] want=%", tag, (int)bytes);
    return p;
}

static unsigned char * esp32_fb_try_alloc(size_t bytes) {
    unsigned char * p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    return p;
}

static void esp32_fb_free(unsigned char ** p) {
    if (*p) {
        heap_caps_free(*p);
        *p = NULL;
    }
}

static void esp32_fb_merge_region(int x0, int y0, int w, int h, uint8_t transparent) {
    if (!FrameBuf || !LayerBuf) return;
    if (w <= 0 || h <= 0) return;
    int x1 = x0 < 0 ? 0 : x0;
    int y1 = y0 < 0 ? 0 : y0;
    int x2 = x0 + w - 1;
    int y2 = y0 + h - 1;
    if (x2 >= HRes) x2 = HRes - 1;
    if (y2 >= VRes) y2 = VRes - 1;
    if (x1 > x2 || y1 > y2) return;

    size_t row_bytes = (size_t)HRes / 2u;
    uint8_t high = (uint8_t)(transparent << 4);
    uint8_t * out = (uint8_t *)web_display_alloc(row_bytes);
    if (!out) error("NEM[gfx:merge] want=%", (int)row_bytes);
    for (int y = y1; y <= y2; y++) {
        uint8_t * frame = FrameBuf + (size_t)y * row_bytes;
        uint8_t * layer = LayerBuf + (size_t)y * row_bytes;
        uint8_t * shadow = ShadowBuf ? ShadowBuf + (size_t)y * row_bytes : NULL;
        memcpy(out, frame, row_bytes);
        for (int bx = x1 / 2; bx <= x2 / 2; bx++) {
            uint8_t l = layer[bx];
            uint8_t merged = out[bx];
            if (bx * 2 >= x1 && bx * 2 <= x2) {
                uint8_t bottom = l & 0x0fu;
                if (bottom != transparent)
                    merged = (uint8_t)((merged & 0xf0u) | bottom);
            }
            if (bx * 2 + 1 >= x1 && bx * 2 + 1 <= x2) {
                uint8_t top = l & 0xf0u;
                if (top != high)
                    merged = (uint8_t)((merged & 0x0fu) | top);
            }
            out[bx] = merged;
        }
        if (!shadow) {
            copyframetoscreen(out + x1 / 2, x1, x2, y, y, x1 & 1);
            continue;
        }
        if (x1 & 1) {
            int bx = x1 / 2;
            out[bx] = (uint8_t)((out[bx] & 0xf0u) | (shadow[bx] & 0x0fu));
        }
        if (!(x2 & 1)) {
            int bx = x2 / 2;
            out[bx] = (uint8_t)((out[bx] & 0x0fu) | (shadow[bx] & 0xf0u));
        }
        int bx = x1 / 2;
        int bx_end = x2 / 2;
        while (bx <= bx_end) {
            while (bx <= bx_end && out[bx] == shadow[bx]) bx++;
            if (bx > bx_end) break;
            int run_start = bx;
            while (bx <= bx_end && out[bx] != shadow[bx]) bx++;
            int run_end = bx - 1;
            memcpy(shadow + run_start, out + run_start,
                   (size_t)(run_end - run_start + 1));
            copyframetoscreen(out + run_start, run_start * 2,
                              run_end * 2 + 1, y, y, 0);
        }
    }
    heap_caps_free(out);
}

static void esp32_fb_stop_merge(void) {
    s_fb_merge_running = 0;
    s_fb_merge_interval_us = 0;
    s_fb_merge_next_us = 0;
    mergerunning = false;
    mergetimer = 0;
    mergedone = true;
}

static void esp32_fb_clear_pending_copy(void) {
    s_fb_copy_src = NULL;
    s_fb_copy_pending = 0;
}

static void esp32_fb_merge_now(uint8_t transparent) {
    mergedone = false;
    esp32_fb_merge_region(0, 0, HRes, VRes, transparent);
    mergedone = true;
}

static void esp32_fb_copy_to_screen(uint8_t * src) {
    if (!src) return;
    copyframetoscreen(src, 0, HRes - 1, 0, VRes - 1, 0);
    if (ShadowBuf) memcpy(ShadowBuf, src, esp32_fb_bytes());
}

static void esp32_fb_service_once(int force) {
    if (s_fb_in_service) return;
    s_fb_in_service = 1;
    if (s_fb_copy_pending && s_fb_copy_src) {
        esp32_fb_copy_to_screen(s_fb_copy_src);
        s_fb_copy_src = NULL;
        s_fb_copy_pending = 0;
    }
    if (s_fb_merge_running) {
        int64_t now = esp_timer_get_time();
        if (force && s_fb_merge_interval_us && s_fb_merge_next_us > now) {
            uint64_t wait_us = (uint64_t)(s_fb_merge_next_us - now);
            if (wait_us > 0) hal_time_sleep_us((uint32_t)wait_us);
            now = esp_timer_get_time();
        }
        if (force || s_fb_merge_interval_us == 0 || now >= s_fb_merge_next_us) {
            esp32_fb_merge_now(s_fb_merge_colour);
            if (s_fb_merge_interval_us)
                s_fb_merge_next_us = esp_timer_get_time() + s_fb_merge_interval_us;
            else
                s_fb_merge_next_us = 0;
        }
    }
    s_fb_in_service = 0;
}

void hal_vm_framebuffer_shutdown_runtime(void) {
    esp32_fb_stop_merge();
    esp32_fb_clear_pending_copy();
    if (WriteBuf == FrameBuf || WriteBuf == LayerBuf) restorepanel();
    esp32_fb_free(&FrameBuf);
    esp32_fb_free(&LayerBuf);
    esp32_fb_free(&ShadowBuf);
    fb_dma_chan = -1;
}

void hal_vm_framebuffer_service(void) {
    esp32_ili9341_lcd_flush_pending();
    esp32_fb_service_once(0);
}
void hal_vm_framebuffer_create(int fast) {
    size_t bytes = esp32_fb_bytes();
    unsigned char * frame = NULL;
    unsigned char * shadow = NULL;
    if (!esp32_ili9341_lcd_ready()) error("FRAMEBUFFER requires active ILI9341 display");
    if (esp32_fastgfx_active()) error("FASTGFX is active");
    if (FrameBuf) error("Framebuffer already exists");
    if (bytes == 0) error("Display not configured");
    frame = esp32_fb_try_alloc(bytes);
    if (!frame) error("NEM[gfx:fb] want=%", (int)bytes);
    if (fast) {
        shadow = esp32_fb_try_alloc(bytes);
        if (!shadow) {
            heap_caps_free(frame);
            error("NEM[gfx:shadow] want=%", (int)bytes);
        }
    }
    FrameBuf = frame;
    ShadowBuf = shadow;
    if (ShadowBuf) memset(ShadowBuf, 0xff, bytes);
}
void hal_vm_framebuffer_layer(int hc, int c) {
    size_t bytes = esp32_fb_bytes();
    uint8_t transparent = hc ? esp32_fb_rgb121((uint32_t)c) : 0;
    if (!esp32_ili9341_lcd_ready()) error("FRAMEBUFFER requires active ILI9341 display");
    if (esp32_fastgfx_active()) error("FASTGFX is active");
    if (LayerBuf) error("Layer already exists");
    if (bytes == 0) error("Display not configured");
    LayerBuf = esp32_fb_alloc(bytes, "gfx:layer");
    memset(LayerBuf, (int)(transparent | (transparent << 4)), bytes);
}
void hal_vm_framebuffer_write(char w) {
    switch (w) {
    case 'N':
        if (mergerunning) error("Display in use for merged operation");
        restorepanel();
        return;
    case 'F':
        if (!FrameBuf) error("Frame buffer not created");
        WriteBuf = FrameBuf;
        setframebuffer();
        return;
    case 'L':
        if (!LayerBuf) error("Layer buffer not created");
        WriteBuf = LayerBuf;
        setframebuffer();
        return;
    default:
        error("Syntax");
    }
}
void hal_vm_framebuffer_close(char w) {
    if (w == 0) w = 'A';
    esp32_fb_stop_merge();
    esp32_fb_clear_pending_copy();
    if ((w == 'A' || w == 'F') && FrameBuf) {
        if (WriteBuf == FrameBuf) restorepanel();
        esp32_fb_free(&FrameBuf);
        esp32_fb_free(&ShadowBuf);
    }
    if ((w == 'A' || w == 'L') && LayerBuf) {
        if (WriteBuf == LayerBuf) restorepanel();
        esp32_fb_free(&LayerBuf);
    }
    if (w != 'A' && w != 'F' && w != 'L') error("Syntax");
}
void hal_vm_framebuffer_merge(int hc, int c, int m, int hr, int rms) {
    uint8_t transparent = hc ? esp32_fb_rgb121((uint32_t)c) : 0;
    if (!LayerBuf) error("Layer not created");
    if (!FrameBuf) error("Framebuffer not created");
    if (hr && rms < 0) error("Number out of bounds");
    switch (m) {
    case BC_FB_MERGE_MODE_NOW:
        esp32_fb_stop_merge();
        esp32_fb_merge_now(transparent);
        return;
    case BC_FB_MERGE_MODE_B:
        esp32_fb_stop_merge();
        esp32_fb_merge_now(transparent);
        return;
    case BC_FB_MERGE_MODE_R:
        if (WriteBuf == NULL) {
            WriteBuf = FrameBuf;
            setframebuffer();
        }
        esp32_fb_stop_merge();
        s_fb_merge_running = 1;
        s_fb_merge_colour = transparent;
        s_fb_merge_interval_us = (uint32_t)(hr ? rms : 0) * 1000u;
        s_fb_merge_next_us = s_fb_merge_interval_us
                                 ? esp_timer_get_time() + s_fb_merge_interval_us
                                 : 0;
        mergerunning = true;
        mergetimer = (uint32_t)(hr ? rms : 0);
        mergedone = true;
        return;
    case BC_FB_MERGE_MODE_A:
        esp32_fb_stop_merge();
        return;
    default:
        error("Syntax");
    }
}
void hal_vm_framebuffer_sync(void) {
    esp32_ili9341_lcd_flush_pending();
    esp32_fb_service_once(1);
    esp32_ili9341_lcd_flush_pending();
}
void hal_vm_framebuffer_wait(void) {}
void hal_vm_framebuffer_copy(char from, char to, int bg) {
    unsigned char * saved = WriteBuf;
    uint8_t * s = NULL;
    uint8_t * d = NULL;
    from = (char)toupper((unsigned char)from);
    to = (char)toupper((unsigned char)to);
    if (from == to) return;

    if (from == 'N') {
        if ((void *)ReadBuffer == (void *)DisplayNotSet) error("Invalid on this display");
    } else if (from == 'F') {
        if (!FrameBuf) error("Frame buffer not created");
        s = FrameBuf;
    } else if (from == 'L') {
        if (!LayerBuf) error("Layer buffer not created");
        s = LayerBuf;
    } else {
        error("Syntax");
    }

    if (to == 'N') {
        if (!s) error("Syntax");
        if (bg) {
            s_fb_copy_src = s;
            s_fb_copy_pending = 1;
        } else {
            esp32_fb_copy_to_screen(s);
        }
    } else if (to == 'F') {
        if (!FrameBuf) error("Frame buffer not created");
        d = FrameBuf;
    } else if (to == 'L') {
        if (!LayerBuf) error("Layer buffer not created");
        d = LayerBuf;
    } else {
        error("Syntax");
    }

    if (d) {
        if (s) {
            memcpy(d, s, esp32_fb_bytes());
        } else {
            unsigned char * line = esp32_fb_alloc((size_t)HRes * 3u, "gfx:read");
            WriteBuf = d;
            setframebuffer();
            for (int y = 0; y < VRes; y++) {
                restorepanel();
                ReadBuffer(0, y, HRes - 1, y, line);
                WriteBuf = d;
                setframebuffer();
                DrawBuffer(0, y, HRes - 1, y, line);
            }
            heap_caps_free(line);
        }
    }

    WriteBuf = saved;
    if (WriteBuf == FrameBuf || WriteBuf == LayerBuf)
        setframebuffer();
    else
        restorepanel();
}

/* No physical display flush is required, but graphics-heavy BASIC code can
 * spend a long time between normal runtime polls. Use refresh as a capped
 * opportunity to drain framebuffer deltas without making draw primitives
 * transport-aware. */
void Display_Refresh(void) {
    static int in_refresh;
    static int64_t next_refresh_us;
    esp32_ili9341_lcd_flush_pending();
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
void ScrollLCDSPISCR(int lines) {
    (void)lines;
}
void setterminal(int height, int width) {
    if (height > 0) Option.Height = height;
    if (width > 0) Option.Width = width;
}

/* PicoCalc 320x320-screen flag, dispatcher state, and direct-buffer
 * primitives not used by the web display MVP. */
bool screen320 = 0;
void DisplayNotSet(void) {}
void setframebuffer(void) {
    if (Option.DISPLAY_TYPE == ILI9341 && HRes > 0 && VRes > 0) {
        DrawRectangle = DrawRectangle16;
        DrawBitmap = DrawBitmap16;
        ScrollLCD = ScrollLCD16;
        DrawBuffer = DrawBuffer16;
        ReadBuffer = ReadBuffer16;
        DrawBLITBuffer = DrawBuffer16;
        ReadBLITBuffer = ReadBuffer16;
        DrawBufferFast = DrawBuffer16Fast;
        ReadBufferFast = ReadBuffer16Fast;
        DrawPixel = DrawPixel16;
    }
}
void restorepanel(void) {
    WriteBuf = NULL;
    if (esp32_ili9341_lcd_restore_panel()) return;
    if (!s_web_pixels) return;
    DrawPixel = esp32_web_draw_pixel;
    DrawRectangle = esp32_web_draw_rectangle;
    DrawBitmap = esp32_web_draw_bitmap;
    ScrollLCD = esp32_web_scroll_lcd;
    DrawBuffer = esp32_web_draw_buffer;
    DrawBLITBuffer = esp32_web_draw_buffer;
    DrawBufferFast = esp32_web_draw_buffer_fast;
    ReadBuffer = esp32_web_read_buffer;
    ReadBLITBuffer = esp32_web_read_buffer;
    ReadBufferFast = esp32_web_read_buffer_fast;
}
void closeframebuffer(char layer) {
    hal_vm_framebuffer_close(layer ? layer : 'A');
}
void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    esp32_fb_merge_region(x0, y0, w, h, colour);
}
void copyframetoscreen(uint8_t * s, int xstart, int xend, int ystart, int yend, int odd) {
    esp32_ili9341_lcd_present_rgb121_rect(s, xstart, xend, ystart, yend, odd);
}
