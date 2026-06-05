/*
 * drivers/web_console/web_console_display.h
 *
 * Target-clean virtual display backing for the browser web console.
 */

#ifndef WEB_CONSOLE_DISPLAY_H
#define WEB_CONSOLE_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct web_console_display {
    int width;
    int height;
    uint32_t * pixels;
    size_t pixel_count;
    unsigned generation;
    int dirty;
    int dirty_x1;
    int dirty_y1;
    int dirty_x2;
    int dirty_y2;
    int needs_resync;
} web_console_display_t;

int web_console_display_init(web_console_display_t * display,
                             int width, int height,
                             uint32_t * pixels, size_t pixel_count,
                             int bg);
const uint32_t * web_console_display_pixels(const web_console_display_t * display,
                                            size_t * pixel_count);
void web_console_display_clear(web_console_display_t * display, int colour);
void web_console_display_pixel(web_console_display_t * display,
                               int x, int y, int colour);
void web_console_display_rect(web_console_display_t * display,
                              int x1, int y1, int x2, int y2, int colour);
void web_console_display_bitmap(web_console_display_t * display,
                                int x, int y, int width, int height,
                                int scale, int fc, int bc,
                                const unsigned char * bitmap);
void web_console_display_draw_buffer(web_console_display_t * display,
                                     int x1, int y1, int x2, int y2,
                                     const unsigned char * bgr);
void web_console_display_read_buffer(const web_console_display_t * display,
                                     int x1, int y1, int x2, int y2,
                                     unsigned char * bgr);
void web_console_display_scroll(web_console_display_t * display,
                                int lines, int bg);
int web_console_display_take_resync(web_console_display_t * display);
void web_console_display_request_resync(web_console_display_t * display);
int web_console_display_dirty_bounds(const web_console_display_t * display,
                                     int * x1, int * y1, int * x2, int * y2);
void web_console_display_clear_dirty(web_console_display_t * display);
size_t web_console_display_pack_dirty_blit(const web_console_display_t * display,
                                           uint8_t * payload,
                                           size_t payload_cap,
                                           int x1, int y1, int x2, int y2);

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_DISPLAY_H */
