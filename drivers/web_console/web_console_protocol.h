/*
 * drivers/web_console/web_console_protocol.h
 *
 * Target-clean wire-format helpers for the browser web console.
 */

#ifndef WEB_CONSOLE_PROTOCOL_H
#define WEB_CONSOLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_CONSOLE_FRAME_HEADER_LEN 8u

typedef enum {
    WEB_CONSOLE_OP_CLS    = 0x01,
    WEB_CONSOLE_OP_RECT   = 0x02,
    WEB_CONSOLE_OP_PIXEL  = 0x03,
    WEB_CONSOLE_OP_SCROLL = 0x04,
    WEB_CONSOLE_OP_BLIT   = 0x05,
    WEB_CONSOLE_OP_BLIT_RGB332_RLE = 0x06,
} web_console_cmd_opcode_t;

size_t web_console_frmb_len(int width, int height);
size_t web_console_cmds_len(size_t command_len);

size_t web_console_pack_frmb(uint8_t *dst, size_t dst_len,
                             int width, int height,
                             const uint32_t *rgb24_pixels,
                             size_t pixel_count);

size_t web_console_pack_cmds(uint8_t *dst, size_t dst_len,
                             int width, int height,
                             const uint8_t *commands,
                             size_t command_len);

size_t web_console_pack_cmd_cls(uint8_t *dst, size_t dst_len, int colour);
size_t web_console_pack_cmd_rect(uint8_t *dst, size_t dst_len,
                                 int x1, int y1, int x2, int y2,
                                 int colour);
size_t web_console_pack_cmd_pixel(uint8_t *dst, size_t dst_len,
                                  int x, int y, int colour);
size_t web_console_pack_cmd_scroll(uint8_t *dst, size_t dst_len,
                                   int lines, int bg);
size_t web_console_pack_cmd_blit(uint8_t *dst, size_t dst_len,
                                 int x, int y, int width, int height,
                                 const uint32_t *rgb24_pixels);

int web_console_parse_key_json(const char *text, size_t len, int *out_code);

int web_console_audio_build_tone(char *dst, size_t dst_len,
                                 double left_hz, double right_hz,
                                 int has_duration,
                                 long long duration_ms);
int web_console_audio_build_stop(char *dst, size_t dst_len);
int web_console_audio_build_sound(char *dst, size_t dst_len,
                                  int slot, const char *ch,
                                  const char *type, double freq_hz,
                                  int volume);
int web_console_audio_build_volume(char *dst, size_t dst_len,
                                   int left, int right);
int web_console_audio_build_pause(char *dst, size_t dst_len);
int web_console_audio_build_resume(char *dst, size_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_PROTOCOL_H */
