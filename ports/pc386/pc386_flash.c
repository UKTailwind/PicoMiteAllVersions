/*
 * ports/pc386/pc386_flash.c — RAM-backed flash buffers + Pico-SDK
 * compatible flash_range_erase/program shims.
 *
 * The PC has no flash chip. ProgMemory lives in BSS; the Option block
 * is cached in BSS but backed by C:/OPTIONS.INI, a human-readable
 * key/value file on the boot FAT volume.
 *
 * Layout (offsets in the flash address space):
 *   0..MAX_PROG_SIZE-1            — program area (tokenised BASIC)
 *   FLASH_TARGET_OFFSET .. +sz    — Option block
 *
 * Mirrors host_native's pattern (flash_prog_buf + host_flash_option_buf
 * in host_fs_shims.c) but with one program-area buffer instead of the
 * multi-slot region.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ff.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Program area: 1 MB on pc386 (MAX_PROG_SIZE = HEAP_MEMORY_SIZE). */
uint8_t pc386_flash_prog_buf[MAX_PROG_SIZE];

/* Option block: just the option_s struct, padded for sector alignment. */
static uint8_t pc386_flash_option_buf[sizeof(struct option_s)];
static uint8_t pc386_default_option_buf[sizeof(struct option_s)];
static int pc386_options_capturing_defaults;
static int pc386_options_sparse_ini;

/* Read-only handles consumed by Memory.c's scan loops + LoadOptions's
 * memcpy-from-flash path. */
const uint8_t *flash_option_contents = pc386_flash_option_buf;
const uint8_t *flash_target_contents = pc386_flash_prog_buf;

/* flash_progmemory is the runtime ProgMemory base; bc_alloc reads from
 * it. Initialised here so kmain doesn't have to. */
extern const uint8_t *flash_progmemory;
extern void port_apply_load_overrides(void);
extern void pc386_apply_runtime_option_defaults(void);

typedef enum {
    PC386_OPT_U8,
    PC386_OPT_I8,
    PC386_OPT_U16,
    PC386_OPT_I16,
    PC386_OPT_U32,
    PC386_OPT_I32,
    PC386_OPT_STR,
    PC386_OPT_HEX,
} pc386_opt_type_t;

typedef struct {
    const char *name;
    uint16_t off;
    uint16_t len;
    pc386_opt_type_t type;
} pc386_opt_field_t;

#define OPT_FIELD(n, t) { #n, (uint16_t)offsetof(struct option_s, n), (uint16_t)sizeof(((struct option_s *)0)->n), t }

static const pc386_opt_field_t pc386_option_fields[] = {
    OPT_FIELD(Magic, PC386_OPT_I32),
    OPT_FIELD(Autorun, PC386_OPT_I8),
    OPT_FIELD(Tab, PC386_OPT_I8),
    OPT_FIELD(Invert, PC386_OPT_I8),
    OPT_FIELD(Listcase, PC386_OPT_I8),
    OPT_FIELD(PROG_FLASH_SIZE, PC386_OPT_U32),
    OPT_FIELD(HEAP_SIZE, PC386_OPT_U32),
    OPT_FIELD(Height, PC386_OPT_I16),
    OPT_FIELD(Width, PC386_OPT_I16),
    OPT_FIELD(DISPLAY_TYPE, PC386_OPT_U8),
    OPT_FIELD(DISPLAY_ORIENTATION, PC386_OPT_I8),
    OPT_FIELD(PIN, PC386_OPT_I32),
    OPT_FIELD(Baudrate, PC386_OPT_I32),
    OPT_FIELD(ColourCode, PC386_OPT_I8),
    OPT_FIELD(MOUSE_CLOCK, PC386_OPT_U8),
    OPT_FIELD(MOUSE_DATA, PC386_OPT_U8),
    OPT_FIELD(spare, PC386_OPT_I8),
    OPT_FIELD(CPU_Speed, PC386_OPT_I32),
    OPT_FIELD(Telnet, PC386_OPT_U32),
    OPT_FIELD(DefaultFC, PC386_OPT_I32),
    OPT_FIELD(DefaultBC, PC386_OPT_I32),
    OPT_FIELD(D3, PC386_OPT_I16),
    OPT_FIELD(KEYBOARD_CLOCK, PC386_OPT_U8),
    OPT_FIELD(KEYBOARD_DATA, PC386_OPT_U8),
    OPT_FIELD(continuation, PC386_OPT_U8),
    OPT_FIELD(LOCAL_KEYBOARD, PC386_OPT_U8),
    OPT_FIELD(KeyboardBrightness, PC386_OPT_U8),
    OPT_FIELD(D2, PC386_OPT_U8),
    OPT_FIELD(DefaultFont, PC386_OPT_U8),
    OPT_FIELD(KeyboardConfig, PC386_OPT_U8),
    OPT_FIELD(RTC_Clock, PC386_OPT_U8),
    OPT_FIELD(RTC_Data, PC386_OPT_U8),
    OPT_FIELD(KEYBOARDBL, PC386_OPT_U8),
    OPT_FIELD(LCD_CLK, PC386_OPT_U8),
    OPT_FIELD(LCD_MOSI, PC386_OPT_U8),
    OPT_FIELD(LCD_MISO, PC386_OPT_U8),
    OPT_FIELD(TCP_PORT, PC386_OPT_U16),
    OPT_FIELD(ServerResponceTime, PC386_OPT_U16),
    OPT_FIELD(X_TILE, PC386_OPT_I16),
    OPT_FIELD(Y_TILE, PC386_OPT_I16),
    OPT_FIELD(LCD_CD, PC386_OPT_U8),
    OPT_FIELD(LCD_CS, PC386_OPT_U8),
    OPT_FIELD(LCD_Reset, PC386_OPT_U8),
    OPT_FIELD(TOUCH_CS, PC386_OPT_U8),
    OPT_FIELD(TOUCH_IRQ, PC386_OPT_U8),
    OPT_FIELD(TOUCH_SWAPXY, PC386_OPT_I8),
    OPT_FIELD(repeat, PC386_OPT_U8),
    OPT_FIELD(disabletftp, PC386_OPT_I8),
    OPT_FIELD(TOUCH_XZERO, PC386_OPT_I32),
    OPT_FIELD(TOUCH_YZERO, PC386_OPT_I32),
    OPT_FIELD(TOUCH_XSCALE, PC386_OPT_U32),
    OPT_FIELD(TOUCH_YSCALE, PC386_OPT_U32),
    OPT_FIELD(MaxCtrls, PC386_OPT_U8),
    OPT_FIELD(HDMIclock, PC386_OPT_U8),
    OPT_FIELD(HDMId0, PC386_OPT_U8),
    OPT_FIELD(HDMId1, PC386_OPT_U8),
    OPT_FIELD(HDMId2, PC386_OPT_U8),
    OPT_FIELD(spare3, PC386_OPT_HEX),
    OPT_FIELD(FlashSize, PC386_OPT_U32),
    OPT_FIELD(SD_CS, PC386_OPT_U8),
    OPT_FIELD(SYSTEM_MOSI, PC386_OPT_U8),
    OPT_FIELD(SYSTEM_MISO, PC386_OPT_U8),
    OPT_FIELD(SYSTEM_CLK, PC386_OPT_U8),
    OPT_FIELD(DISPLAY_BL, PC386_OPT_U8),
    OPT_FIELD(DISPLAY_CONSOLE, PC386_OPT_U8),
    OPT_FIELD(TOUCH_Click, PC386_OPT_U8),
    OPT_FIELD(LCD_RD, PC386_OPT_I8),
    OPT_FIELD(AUDIO_L, PC386_OPT_U8),
    OPT_FIELD(AUDIO_R, PC386_OPT_U8),
    OPT_FIELD(AUDIO_SLICE, PC386_OPT_U8),
    OPT_FIELD(SDspeed, PC386_OPT_U8),
    OPT_FIELD(pins, PC386_OPT_HEX),
    OPT_FIELD(TOUCH_CAP, PC386_OPT_U8),
    OPT_FIELD(SSD_DATA, PC386_OPT_U8),
    OPT_FIELD(THRESHOLD_CAP, PC386_OPT_U8),
    OPT_FIELD(audio_i2s_data, PC386_OPT_U8),
    OPT_FIELD(audio_i2s_bclk, PC386_OPT_U8),
    OPT_FIELD(LCDVOP, PC386_OPT_I8),
    OPT_FIELD(I2Coffset, PC386_OPT_I8),
    OPT_FIELD(NoHeartbeat, PC386_OPT_U8),
    OPT_FIELD(Refresh, PC386_OPT_I8),
    OPT_FIELD(SYSTEM_I2C_SDA, PC386_OPT_U8),
    OPT_FIELD(SYSTEM_I2C_SCL, PC386_OPT_U8),
    OPT_FIELD(RTC, PC386_OPT_U8),
    OPT_FIELD(PWM, PC386_OPT_I8),
    OPT_FIELD(INT1pin, PC386_OPT_U8),
    OPT_FIELD(INT2pin, PC386_OPT_U8),
    OPT_FIELD(INT3pin, PC386_OPT_U8),
    OPT_FIELD(INT4pin, PC386_OPT_U8),
    OPT_FIELD(SD_CLK_PIN, PC386_OPT_U8),
    OPT_FIELD(SD_MOSI_PIN, PC386_OPT_U8),
    OPT_FIELD(SD_MISO_PIN, PC386_OPT_U8),
    OPT_FIELD(SerialConsole, PC386_OPT_U8),
    OPT_FIELD(SerialTX, PC386_OPT_U8),
    OPT_FIELD(SerialRX, PC386_OPT_U8),
    OPT_FIELD(numlock, PC386_OPT_U8),
    OPT_FIELD(capslock, PC386_OPT_U8),
    OPT_FIELD(LIBRARY_FLASH_SIZE, PC386_OPT_U32),
    OPT_FIELD(AUDIO_CLK_PIN, PC386_OPT_U8),
    OPT_FIELD(AUDIO_MOSI_PIN, PC386_OPT_U8),
    OPT_FIELD(SYSTEM_I2C_SLOW, PC386_OPT_U8),
    OPT_FIELD(AUDIO_CS_PIN, PC386_OPT_U8),
    OPT_FIELD(UDP_PORT, PC386_OPT_U16),
    OPT_FIELD(UDPServerResponceTime, PC386_OPT_U16),
    OPT_FIELD(hostname, PC386_OPT_STR),
    OPT_FIELD(ipaddress, PC386_OPT_STR),
    OPT_FIELD(mask, PC386_OPT_STR),
    OPT_FIELD(gateway, PC386_OPT_STR),
    OPT_FIELD(heartbeatpin, PC386_OPT_U8),
    OPT_FIELD(PSRAM_CS_PIN, PC386_OPT_U8),
    OPT_FIELD(BGR, PC386_OPT_U8),
    OPT_FIELD(NoScroll, PC386_OPT_U8),
    OPT_FIELD(CombinedCS, PC386_OPT_U8),
    OPT_FIELD(USBKeyboard, PC386_OPT_U8),
    OPT_FIELD(VGA_HSYNC, PC386_OPT_U8),
    OPT_FIELD(VGA_BLUE, PC386_OPT_U8),
    OPT_FIELD(AUDIO_MISO_PIN, PC386_OPT_U8),
    OPT_FIELD(AUDIO_DCS_PIN, PC386_OPT_U8),
    OPT_FIELD(AUDIO_DREQ_PIN, PC386_OPT_U8),
    OPT_FIELD(AUDIO_RESET_PIN, PC386_OPT_U8),
    OPT_FIELD(SSD_DC, PC386_OPT_U8),
    OPT_FIELD(SSD_WR, PC386_OPT_U8),
    OPT_FIELD(SSD_RD, PC386_OPT_U8),
    OPT_FIELD(SSD_RESET, PC386_OPT_I8),
    OPT_FIELD(BackLightLevel, PC386_OPT_U8),
    OPT_FIELD(NoReset, PC386_OPT_U8),
    OPT_FIELD(AllPins, PC386_OPT_U8),
    OPT_FIELD(modbuff, PC386_OPT_U8),
    OPT_FIELD(RepeatStart, PC386_OPT_I16),
    OPT_FIELD(RepeatRate, PC386_OPT_I16),
    OPT_FIELD(modbuffsize, PC386_OPT_I32),
    OPT_FIELD(F1key, PC386_OPT_STR),
    OPT_FIELD(F5key, PC386_OPT_STR),
    OPT_FIELD(F6key, PC386_OPT_STR),
    OPT_FIELD(F7key, PC386_OPT_STR),
    OPT_FIELD(F8key, PC386_OPT_STR),
    OPT_FIELD(F9key, PC386_OPT_STR),
    OPT_FIELD(SSID, PC386_OPT_STR),
    OPT_FIELD(PASSWORD, PC386_OPT_STR),
    OPT_FIELD(platform, PC386_OPT_STR),
    OPT_FIELD(extensions, PC386_OPT_HEX),
    OPT_FIELD(pc386_sb_base, PC386_OPT_U16),
    OPT_FIELD(pc386_sb_irq, PC386_OPT_U8),
    OPT_FIELD(pc386_sb_dma, PC386_OPT_U8),
    OPT_FIELD(pc386_sb_dma16, PC386_OPT_U8),
};

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static const pc386_opt_field_t *find_field(const char *name)
{
    for (unsigned i = 0; i < sizeof(pc386_option_fields) / sizeof(pc386_option_fields[0]); i++) {
        if (strcasecmp(name, pc386_option_fields[i].name) == 0) return &pc386_option_fields[i];
    }
    return NULL;
}

static int is_legacy_runtime_field(const char *name)
{
    return strcasecmp(name, "DefaultFont") == 0 ||
           strcasecmp(name, "Height") == 0 ||
           strcasecmp(name, "Width") == 0 ||
           strcasecmp(name, "DISPLAY_TYPE") == 0 ||
           strcasecmp(name, "DISPLAY_CONSOLE") == 0;
}

static int is_runtime_derived_field(const char *name)
{
    return strcasecmp(name, "Height") == 0 ||
           strcasecmp(name, "Width") == 0 ||
           strcasecmp(name, "DISPLAY_TYPE") == 0 ||
           strcasecmp(name, "DISPLAY_CONSOLE") == 0;
}

static unsigned long parse_number(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '&' && (s[1] == 'H' || s[1] == 'h')) return strtoul(s + 2, NULL, 16);
    return strtoul(s, NULL, 0);
}

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void set_scalar(uint8_t *dst, uint16_t len, unsigned long v)
{
    for (uint16_t i = 0; i < len; i++) dst[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
}

static unsigned long get_scalar(const uint8_t *src, uint16_t len)
{
    unsigned long v = 0;
    for (uint16_t i = 0; i < len && i < sizeof(v); i++) v |= ((unsigned long)src[i]) << (8u * i);
    return v;
}

static void parse_string(char *value, uint8_t *dst, uint16_t len)
{
    memset(dst, 0, len);
    value = trim(value);
    if (*value == '"') value++;
    uint16_t n = 0;
    while (*value && *value != '"' && n + 1 < len) {
        if (*value == '\\') {
            value++;
            if (*value == 'n') { dst[n++] = '\n'; value++; continue; }
            if (*value == 'r') { dst[n++] = '\r'; value++; continue; }
            if (*value == 't') { dst[n++] = '\t'; value++; continue; }
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

static void parse_hex_bytes(char *value, uint8_t *dst, uint16_t len)
{
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

static void pc386_options_parse_ini(char *buf)
{
    char *line = buf;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = trim(line);
        if (*s && *s != '#' && *s != ';' && *s != '[') {
            char *eq = strchr(s, '=');
            if (eq) {
                *eq++ = 0;
                char *name = trim(s);
                char *value = trim(eq);
                const pc386_opt_field_t *f = find_field(name);
                if (f) {
                    if (!pc386_options_sparse_ini && is_legacy_runtime_field(name)) {
                        goto next_line;
                    }
                    uint8_t *dst = pc386_flash_option_buf + f->off;
                    if (f->type == PC386_OPT_STR) parse_string(value, dst, f->len);
                    else if (f->type == PC386_OPT_HEX) parse_hex_bytes(value, dst, f->len);
                    else set_scalar(dst, f->len, parse_number(value));
                }
            }
        }
next_line:
        if (!next) break;
        line = next;
    }
}

static void pc386_options_load_ini(void)
{
    FIL fp;
    if (f_open(&fp, "C:/OPTIONS.INI", FA_READ) != FR_OK) return;
    static char buf[16384];
    UINT got = 0;
    if (f_read(&fp, buf, sizeof(buf) - 1, &got) == FR_OK) {
        buf[got] = 0;
        pc386_options_sparse_ini = strstr(buf, "Only values that differ from pc386 defaults") != NULL;
        pc386_options_parse_ini(buf);
    }
    f_close(&fp);
}

static void append_escaped(char *line, size_t line_len, const uint8_t *src, uint16_t len)
{
    size_t out = strlen(line);
    for (uint16_t i = 0; i < len && src[i]; i++) {
        unsigned char c = src[i];
        if (out + 5 >= line_len) break;
        if (c == '\\' || c == '"') {
            line[out++] = '\\';
            line[out++] = (char)c;
        } else if (c == '\n') {
            line[out++] = '\\'; line[out++] = 'n';
        } else if (c == '\r') {
            line[out++] = '\\'; line[out++] = 'r';
        } else if (c == '\t') {
            line[out++] = '\\'; line[out++] = 't';
        } else if (c >= 32 && c < 127) {
            line[out++] = (char)c;
        } else {
            snprintf(line + out, line_len - out, "\\x%02X", c);
            out += strlen(line + out);
        }
    }
    line[out] = 0;
}

static FRESULT write_line(FIL *fp, const char *line)
{
    UINT wrote = 0;
    return f_write(fp, line, (UINT)strlen(line), &wrote);
}

static void pc386_options_capture_defaults(void)
{
    struct option_s saved;
    memcpy(&saved, &Option, sizeof(saved));
    memset(&Option, 0, sizeof(Option));
    port_apply_load_overrides();
    pc386_apply_runtime_option_defaults();
    memcpy(pc386_default_option_buf, &Option, sizeof(Option));
    memcpy(&Option, &saved, sizeof(Option));
}

static void pc386_options_write_ini(void)
{
    struct option_s zero;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(pc386_default_option_buf, &zero, sizeof(zero)) == 0) {
        pc386_options_capture_defaults();
    }

    int changed = 0;
    for (unsigned i = 0; i < sizeof(pc386_option_fields) / sizeof(pc386_option_fields[0]); i++) {
        const pc386_opt_field_t *f = &pc386_option_fields[i];
        if (is_runtime_derived_field(f->name)) continue;
        if (memcmp(pc386_flash_option_buf + f->off, pc386_default_option_buf + f->off, f->len) != 0) {
            changed = 1;
            break;
        }
    }
    if (!changed) {
        f_unlink("C:/OPTIONS.INI");
        return;
    }

    FIL fp;
    if (f_open(&fp, "C:/OPTIONS.INI", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
    write_line(&fp, "# PicoMite PC386 options\r\n");
    write_line(&fp, "# Only values that differ from pc386 defaults are written.\r\n");
    write_line(&fp, "# Edit as key=value. Hex values may use 0xNN or &HNN.\r\n\r\n");
    for (unsigned i = 0; i < sizeof(pc386_option_fields) / sizeof(pc386_option_fields[0]); i++) {
        const pc386_opt_field_t *f = &pc386_option_fields[i];
        if (is_runtime_derived_field(f->name)) continue;
        const uint8_t *src = pc386_flash_option_buf + f->off;
        const uint8_t *def = pc386_default_option_buf + f->off;
        if (memcmp(src, def, f->len) == 0) continue;
        char line[512];
        if (f->type == PC386_OPT_STR) {
            snprintf(line, sizeof(line), "%s=\"", f->name);
            append_escaped(line, sizeof(line), src, f->len);
            strncat(line, "\"\r\n", sizeof(line) - strlen(line) - 1);
        } else if (f->type == PC386_OPT_HEX) {
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
        write_line(&fp, line);
    }
    f_close(&fp);
}

void pc386_flash_init(void) {
    /* Program area starts erased (0xFF). Option block reads as zero on
     * first boot — matches the freshness contract in hal_flash.h, which
     * is *different* from raw flash 0xFF state. */
    memset(pc386_flash_prog_buf,   0xFF, sizeof(pc386_flash_prog_buf));
    memset(pc386_flash_option_buf, 0x00, sizeof(pc386_flash_option_buf));
    memset(pc386_default_option_buf, 0x00, sizeof(pc386_default_option_buf));
    pc386_options_capturing_defaults = 0;
    pc386_options_sparse_ini = 0;
    flash_progmemory = pc386_flash_prog_buf;
    pc386_options_load_ini();
}

void pc386_options_defaults_ready(void)
{
    pc386_options_capture_defaults();
}

/* ===== Pico-SDK shape ====================================================
 * MMBasic core still calls flash_range_erase/flash_range_program in a
 * few spots (Memory.c, FileIO.c). Route by offset: anything inside the
 * program area writes to pc386_flash_prog_buf; anything inside the Option
 * region writes to pc386_flash_option_buf; anything else is a no-op
 * (other ports' flash slots that pc386 doesn't carry).
 */

void flash_range_erase(uint32_t off, uint32_t count) {
    if (off + count <= sizeof(pc386_flash_prog_buf)) {
        memset(pc386_flash_prog_buf + off, 0xFF, count);
        return;
    }
    if (off >= FLASH_TARGET_OFFSET &&
        off + count <= FLASH_TARGET_OFFSET + sizeof(pc386_flash_option_buf)) {
        memset(pc386_flash_option_buf + (off - FLASH_TARGET_OFFSET), 0xFF, count);
        return;
    }
    /* Out-of-range write — silently ignored (matches host posture). */
}

void flash_range_program(uint32_t off, const uint8_t *data, uint32_t count) {
    if (off + count <= sizeof(pc386_flash_prog_buf)) {
        memcpy(pc386_flash_prog_buf + off, data, count);
        return;
    }
    if (off >= FLASH_TARGET_OFFSET &&
        off + count <= FLASH_TARGET_OFFSET + sizeof(pc386_flash_option_buf)) {
        memcpy(pc386_flash_option_buf + (off - FLASH_TARGET_OFFSET), data, count);
        return;
    }
}

/* SaveOptions calls this after mutating Option in RAM to refresh the
 * flash-backing snapshot — same pattern as host_options_snapshot. */
void pc386_options_snapshot(void) {
    if (pc386_options_capturing_defaults) return;
    memcpy(pc386_flash_option_buf, &Option, sizeof(Option));
    pc386_options_write_ini();
}
