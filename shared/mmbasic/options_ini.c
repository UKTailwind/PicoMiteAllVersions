#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "options_ini.h"

typedef enum {
    MM_OPT_INI_U8,
    MM_OPT_INI_I8,
    MM_OPT_INI_U16,
    MM_OPT_INI_I16,
    MM_OPT_INI_U32,
    MM_OPT_INI_I32,
    MM_OPT_INI_STR,
    MM_OPT_INI_HEX,
} mm_options_ini_type_t;

typedef struct {
    const char * name;
    uint16_t off;
    uint16_t len;
    mm_options_ini_type_t type;
} mm_options_ini_field_t;

#define OPT_FIELD(n, t) {#n, (uint16_t)offsetof(struct option_s, n), (uint16_t)sizeof(((struct option_s *)0)->n), t}

static const mm_options_ini_field_t option_fields[] = {
    OPT_FIELD(Magic, MM_OPT_INI_I32),
    OPT_FIELD(Autorun, MM_OPT_INI_I8),
    OPT_FIELD(Tab, MM_OPT_INI_I8),
    OPT_FIELD(Invert, MM_OPT_INI_I8),
    OPT_FIELD(Listcase, MM_OPT_INI_I8),
    OPT_FIELD(PROG_FLASH_SIZE, MM_OPT_INI_U32),
    OPT_FIELD(HEAP_SIZE, MM_OPT_INI_U32),
    OPT_FIELD(Height, MM_OPT_INI_I16),
    OPT_FIELD(Width, MM_OPT_INI_I16),
    OPT_FIELD(DISPLAY_TYPE, MM_OPT_INI_U8),
    OPT_FIELD(DISPLAY_ORIENTATION, MM_OPT_INI_I8),
    OPT_FIELD(PIN, MM_OPT_INI_I32),
    OPT_FIELD(Baudrate, MM_OPT_INI_I32),
    OPT_FIELD(ColourCode, MM_OPT_INI_I8),
    OPT_FIELD(MOUSE_CLOCK, MM_OPT_INI_U8),
    OPT_FIELD(MOUSE_DATA, MM_OPT_INI_U8),
    OPT_FIELD(spare, MM_OPT_INI_I8),
    OPT_FIELD(CPU_Speed, MM_OPT_INI_I32),
    OPT_FIELD(Telnet, MM_OPT_INI_U32),
    OPT_FIELD(DefaultFC, MM_OPT_INI_I32),
    OPT_FIELD(DefaultBC, MM_OPT_INI_I32),
    OPT_FIELD(D3, MM_OPT_INI_I16),
    OPT_FIELD(KEYBOARD_CLOCK, MM_OPT_INI_U8),
    OPT_FIELD(KEYBOARD_DATA, MM_OPT_INI_U8),
    OPT_FIELD(continuation, MM_OPT_INI_U8),
    OPT_FIELD(LOCAL_KEYBOARD, MM_OPT_INI_U8),
    OPT_FIELD(KeyboardBrightness, MM_OPT_INI_U8),
    OPT_FIELD(D2, MM_OPT_INI_U8),
    OPT_FIELD(DefaultFont, MM_OPT_INI_U8),
    OPT_FIELD(KeyboardConfig, MM_OPT_INI_U8),
    OPT_FIELD(RTC_Clock, MM_OPT_INI_U8),
    OPT_FIELD(RTC_Data, MM_OPT_INI_U8),
    OPT_FIELD(KEYBOARDBL, MM_OPT_INI_U8),
    OPT_FIELD(LCD_CLK, MM_OPT_INI_U8),
    OPT_FIELD(LCD_MOSI, MM_OPT_INI_U8),
    OPT_FIELD(LCD_MISO, MM_OPT_INI_U8),
    OPT_FIELD(TCP_PORT, MM_OPT_INI_U16),
    OPT_FIELD(ServerResponceTime, MM_OPT_INI_U16),
    OPT_FIELD(X_TILE, MM_OPT_INI_I16),
    OPT_FIELD(Y_TILE, MM_OPT_INI_I16),
    OPT_FIELD(LCD_CD, MM_OPT_INI_U8),
    OPT_FIELD(LCD_CS, MM_OPT_INI_U8),
    OPT_FIELD(LCD_Reset, MM_OPT_INI_U8),
    OPT_FIELD(TOUCH_CS, MM_OPT_INI_U8),
    OPT_FIELD(TOUCH_IRQ, MM_OPT_INI_U8),
    OPT_FIELD(TOUCH_SWAPXY, MM_OPT_INI_I8),
    OPT_FIELD(repeat, MM_OPT_INI_U8),
    OPT_FIELD(disabletftp, MM_OPT_INI_I8),
    OPT_FIELD(TOUCH_XZERO, MM_OPT_INI_I32),
    OPT_FIELD(TOUCH_YZERO, MM_OPT_INI_I32),
    OPT_FIELD(TOUCH_XSCALE, MM_OPT_INI_U32),
    OPT_FIELD(TOUCH_YSCALE, MM_OPT_INI_U32),
    OPT_FIELD(MaxCtrls, MM_OPT_INI_U8),
    OPT_FIELD(HDMIclock, MM_OPT_INI_U8),
    OPT_FIELD(HDMId0, MM_OPT_INI_U8),
    OPT_FIELD(HDMId1, MM_OPT_INI_U8),
    OPT_FIELD(HDMId2, MM_OPT_INI_U8),
    OPT_FIELD(spare3, MM_OPT_INI_HEX),
    OPT_FIELD(FlashSize, MM_OPT_INI_U32),
    OPT_FIELD(SD_CS, MM_OPT_INI_U8),
    OPT_FIELD(SYSTEM_MOSI, MM_OPT_INI_U8),
    OPT_FIELD(SYSTEM_MISO, MM_OPT_INI_U8),
    OPT_FIELD(SYSTEM_CLK, MM_OPT_INI_U8),
    OPT_FIELD(DISPLAY_BL, MM_OPT_INI_U8),
    OPT_FIELD(DISPLAY_CONSOLE, MM_OPT_INI_U8),
    OPT_FIELD(TOUCH_Click, MM_OPT_INI_U8),
    OPT_FIELD(LCD_RD, MM_OPT_INI_I8),
    OPT_FIELD(AUDIO_L, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_R, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_SLICE, MM_OPT_INI_U8),
    OPT_FIELD(SDspeed, MM_OPT_INI_U8),
    OPT_FIELD(pins, MM_OPT_INI_HEX),
    OPT_FIELD(TOUCH_CAP, MM_OPT_INI_U8),
    OPT_FIELD(SSD_DATA, MM_OPT_INI_U8),
    OPT_FIELD(THRESHOLD_CAP, MM_OPT_INI_U8),
    OPT_FIELD(audio_i2s_data, MM_OPT_INI_U8),
    OPT_FIELD(audio_i2s_bclk, MM_OPT_INI_U8),
    OPT_FIELD(LCDVOP, MM_OPT_INI_I8),
    OPT_FIELD(I2Coffset, MM_OPT_INI_I8),
    OPT_FIELD(NoHeartbeat, MM_OPT_INI_U8),
    OPT_FIELD(Refresh, MM_OPT_INI_I8),
    OPT_FIELD(SYSTEM_I2C_SDA, MM_OPT_INI_U8),
    OPT_FIELD(SYSTEM_I2C_SCL, MM_OPT_INI_U8),
    OPT_FIELD(RTC, MM_OPT_INI_U8),
    OPT_FIELD(PWM, MM_OPT_INI_I8),
    OPT_FIELD(INT1pin, MM_OPT_INI_U8),
    OPT_FIELD(INT2pin, MM_OPT_INI_U8),
    OPT_FIELD(INT3pin, MM_OPT_INI_U8),
    OPT_FIELD(INT4pin, MM_OPT_INI_U8),
    OPT_FIELD(SD_CLK_PIN, MM_OPT_INI_U8),
    OPT_FIELD(SD_MOSI_PIN, MM_OPT_INI_U8),
    OPT_FIELD(SD_MISO_PIN, MM_OPT_INI_U8),
    OPT_FIELD(SerialConsole, MM_OPT_INI_U8),
    OPT_FIELD(SerialTX, MM_OPT_INI_U8),
    OPT_FIELD(SerialRX, MM_OPT_INI_U8),
    OPT_FIELD(numlock, MM_OPT_INI_U8),
    OPT_FIELD(capslock, MM_OPT_INI_U8),
    OPT_FIELD(LIBRARY_FLASH_SIZE, MM_OPT_INI_U32),
    OPT_FIELD(AUDIO_CLK_PIN, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_MOSI_PIN, MM_OPT_INI_U8),
    OPT_FIELD(SYSTEM_I2C_SLOW, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_CS_PIN, MM_OPT_INI_U8),
    OPT_FIELD(UDP_PORT, MM_OPT_INI_U16),
    OPT_FIELD(UDPServerResponceTime, MM_OPT_INI_U16),
    OPT_FIELD(hostname, MM_OPT_INI_STR),
    OPT_FIELD(ipaddress, MM_OPT_INI_STR),
    OPT_FIELD(mask, MM_OPT_INI_STR),
    OPT_FIELD(gateway, MM_OPT_INI_STR),
    OPT_FIELD(heartbeatpin, MM_OPT_INI_U8),
    OPT_FIELD(PSRAM_CS_PIN, MM_OPT_INI_U8),
    OPT_FIELD(BGR, MM_OPT_INI_U8),
    OPT_FIELD(NoScroll, MM_OPT_INI_U8),
    OPT_FIELD(CombinedCS, MM_OPT_INI_U8),
    OPT_FIELD(USBKeyboard, MM_OPT_INI_U8),
    OPT_FIELD(VGA_HSYNC, MM_OPT_INI_U8),
    OPT_FIELD(VGA_BLUE, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_MISO_PIN, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_DCS_PIN, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_DREQ_PIN, MM_OPT_INI_U8),
    OPT_FIELD(AUDIO_RESET_PIN, MM_OPT_INI_U8),
    OPT_FIELD(SSD_DC, MM_OPT_INI_U8),
    OPT_FIELD(SSD_WR, MM_OPT_INI_U8),
    OPT_FIELD(SSD_RD, MM_OPT_INI_U8),
    OPT_FIELD(SSD_RESET, MM_OPT_INI_I8),
    OPT_FIELD(BackLightLevel, MM_OPT_INI_U8),
    OPT_FIELD(NoReset, MM_OPT_INI_U8),
    OPT_FIELD(AllPins, MM_OPT_INI_U8),
    OPT_FIELD(modbuff, MM_OPT_INI_U8),
    OPT_FIELD(RepeatStart, MM_OPT_INI_I16),
    OPT_FIELD(RepeatRate, MM_OPT_INI_I16),
    OPT_FIELD(modbuffsize, MM_OPT_INI_I32),
    OPT_FIELD(F1key, MM_OPT_INI_STR),
    OPT_FIELD(F5key, MM_OPT_INI_STR),
    OPT_FIELD(F6key, MM_OPT_INI_STR),
    OPT_FIELD(F7key, MM_OPT_INI_STR),
    OPT_FIELD(F8key, MM_OPT_INI_STR),
    OPT_FIELD(F9key, MM_OPT_INI_STR),
    OPT_FIELD(SSID, MM_OPT_INI_STR),
    OPT_FIELD(PASSWORD, MM_OPT_INI_STR),
    OPT_FIELD(platform, MM_OPT_INI_STR),
    OPT_FIELD(extensions, MM_OPT_INI_HEX),
    OPT_FIELD(pc386_sb_base, MM_OPT_INI_U16),
    OPT_FIELD(pc386_sb_irq, MM_OPT_INI_U8),
    OPT_FIELD(pc386_sb_dma, MM_OPT_INI_U8),
    OPT_FIELD(pc386_sb_dma16, MM_OPT_INI_U8),
    OPT_FIELD(WebConsole, MM_OPT_INI_U8),
};

static char * trim(char * s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char * e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static const mm_options_ini_field_t * find_field(const char * name) {
    for (unsigned i = 0; i < sizeof(option_fields) / sizeof(option_fields[0]); i++) {
        if (strcasecmp(name, option_fields[i].name) == 0) return &option_fields[i];
    }
    return NULL;
}

static unsigned long parse_number(const char * s) {
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '&' && (s[1] == 'H' || s[1] == 'h')) return strtoul(s + 2, NULL, 16);
    return strtoul(s, NULL, 0);
}

static int hex_nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void set_scalar(uint8_t * dst, uint16_t len, unsigned long v) {
    for (uint16_t i = 0; i < len; i++) dst[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
}

static unsigned long get_scalar(const uint8_t * src, uint16_t len) {
    unsigned long v = 0;
    for (uint16_t i = 0; i < len && i < sizeof(v); i++) v |= ((unsigned long)src[i]) << (8u * i);
    return v;
}

static void parse_string(char * value, uint8_t * dst, uint16_t len) {
    memset(dst, 0, len);
    value = trim(value);
    if (*value == '"') value++;
    uint16_t n = 0;
    while (*value && *value != '"' && n + 1 < len) {
        if (*value == '\\') {
            value++;
            if (*value == 'n') {
                dst[n++] = '\n';
                value++;
                continue;
            }
            if (*value == 'r') {
                dst[n++] = '\r';
                value++;
                continue;
            }
            if (*value == 't') {
                dst[n++] = '\t';
                value++;
                continue;
            }
            if (*value == 'x' && hex_nibble(value[1]) >= 0 && hex_nibble(value[2]) >= 0) {
                dst[n++] = (uint8_t)((hex_nibble(value[1]) << 4) | hex_nibble(value[2]));
                value += 3;
                continue;
            }
            if (*value == 0) break;
        }
        dst[n++] = (uint8_t)*value++;
    }
}

static void parse_hex_bytes(char * value, uint8_t * dst, uint16_t len) {
    memset(dst, 0, len);
    uint16_t n = 0;
    while (*value && n < len) {
        int hi = hex_nibble(*value++);
        if (hi < 0) continue;
        int lo = hex_nibble(*value++);
        if (lo < 0) break;
        dst[n++] = (uint8_t)((hi << 4) | lo);
    }
}

static void append_escaped(char * line, size_t line_len, const uint8_t * src, uint16_t len) {
    size_t out = strlen(line);
    for (uint16_t i = 0; i < len && src[i]; i++) {
        unsigned char c = src[i];
        if (out + 5 >= line_len) break;
        if (c == '\\' || c == '"') {
            line[out++] = '\\';
            line[out++] = (char)c;
        } else if (c == '\n') {
            line[out++] = '\\';
            line[out++] = 'n';
        } else if (c == '\r') {
            line[out++] = '\\';
            line[out++] = 'r';
        } else if (c == '\t') {
            line[out++] = '\\';
            line[out++] = 't';
        } else if (c >= 32 && c < 127) {
            line[out++] = (char)c;
        } else {
            snprintf(line + out, line_len - out, "\\x%02X", c);
            out += strlen(line + out);
        }
    }
    line[out] = 0;
}

int mm_options_ini_is_sparse(const char * buf) {
    return buf && strstr(buf, "Only values that differ") != NULL;
}

void mm_options_ini_parse(char * buf, uint8_t * option_buf, int sparse_ini,
                          mm_options_ini_name_predicate skip_legacy,
                          void * skip_legacy_ctx) {
    char * line = buf;
    while (line && *line) {
        char * next = strchr(line, '\n');
        if (next) *next++ = 0;
        char * s = trim(line);
        if (*s && *s != '#' && *s != ';' && *s != '[') {
            char * eq = strchr(s, '=');
            if (eq) {
                *eq++ = 0;
                char * name = trim(s);
                char * value = trim(eq);
                const mm_options_ini_field_t * f = find_field(name);
                if (f) {
                    if (!sparse_ini && skip_legacy && skip_legacy(name, skip_legacy_ctx)) goto next_line;
                    uint8_t * dst = option_buf + f->off;
                    if (f->type == MM_OPT_INI_STR)
                        parse_string(value, dst, f->len);
                    else if (f->type == MM_OPT_INI_HEX)
                        parse_hex_bytes(value, dst, f->len);
                    else
                        set_scalar(dst, f->len, parse_number(value));
                }
            }
        }
    next_line:
        if (!next) break;
        line = next;
    }
}

int mm_options_ini_has_changes(const uint8_t * option_buf,
                               const uint8_t * default_option_buf,
                               mm_options_ini_name_predicate skip_write,
                               void * skip_write_ctx) {
    for (unsigned i = 0; i < sizeof(option_fields) / sizeof(option_fields[0]); i++) {
        const mm_options_ini_field_t * f = &option_fields[i];
        if (skip_write && skip_write(f->name, skip_write_ctx)) continue;
        if (memcmp(option_buf + f->off, default_option_buf + f->off, f->len) != 0) return 1;
    }
    return 0;
}

int mm_options_ini_write_changed(const uint8_t * option_buf,
                                 const uint8_t * default_option_buf,
                                 mm_options_ini_name_predicate skip_write,
                                 void * skip_write_ctx,
                                 mm_options_ini_write_line write_line,
                                 void * write_ctx) {
    for (unsigned i = 0; i < sizeof(option_fields) / sizeof(option_fields[0]); i++) {
        const mm_options_ini_field_t * f = &option_fields[i];
        if (skip_write && skip_write(f->name, skip_write_ctx)) continue;
        const uint8_t * src = option_buf + f->off;
        const uint8_t * def = default_option_buf + f->off;
        if (memcmp(src, def, f->len) == 0) continue;

        char line[512];
        if (f->type == MM_OPT_INI_STR) {
            snprintf(line, sizeof(line), "%s=\"", f->name);
            append_escaped(line, sizeof(line), src, f->len);
            strncat(line, "\"\r\n", sizeof(line) - strlen(line) - 1);
        } else if (f->type == MM_OPT_INI_HEX) {
            snprintf(line, sizeof(line), "%s=", f->name);
            for (uint16_t j = 0; j < f->len && strlen(line) + 4 < sizeof(line); j++) {
                char b[4];
                snprintf(b, sizeof(b), "%02X", src[j]);
                strcat(line, b);
            }
            strcat(line, "\r\n");
        } else if (strcasecmp(f->name, "pc386_sb_base") == 0) {
            snprintf(line, sizeof(line), "%s=&H%lX\r\n", f->name, get_scalar(src, f->len));
        } else {
            snprintf(line, sizeof(line), "%s=%lu\r\n", f->name, get_scalar(src, f->len));
        }
        if (write_line(write_ctx, line) != 0) return -1;
    }
    return 0;
}
