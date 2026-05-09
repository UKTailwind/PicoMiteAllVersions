/*
 * esp32_glue.c — Phase B link glue.
 *
 * Provides the platform-glue symbols host_runtime.c / host_fs_shims.c
 * expect from a sibling host TU (host_terminal.c / host_time.c /
 * host_fb.c / host_main.c) on the native build. ESP32 doesn't carry
 * those files directly because they're either too POSIX-y (terminal,
 * pthread audio) or display-sim only.
 *
 * Almost everything here is a stub. Real impls land in Phase C/D as
 * the relevant subsystems get wired (USB Serial/JTAG console, FATFS,
 * etc.).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------- host_time.c equivalents (POSIX→ESP-IDF) ----------- */

uint64_t host_time_us_64(void) {
    return (uint64_t)esp_timer_get_time();
}

void host_sleep_us(uint64_t us) {
    if (us < 1000) {
        esp_rom_delay_us((uint32_t)us);
    } else {
        TickType_t t = pdMS_TO_TICKS((us + 999) / 1000);
        if (!t) t = 1;
        vTaskDelay(t);
    }
}

/* ---------- host_terminal.c equivalents ---------- */

/* host_runtime.c routes ALL output through host_output_hook. Signature
 * is (const char *text, int len) — host_runtime.c calls it with both
 * single-byte chunks (SerialConsolePutC) and multi-byte chunks
 * (host_print). On ESP32 we forward straight to fwrite(stdout); IDF
 * has stdio wired to USB Serial/JTAG when
 * CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y. */
static void esp32_console_write(const char *text, int len) {
    fwrite(text, 1, len, stdout);
}
void (*host_output_hook)(const char *text, int len) = esp32_console_write;

/* The chip's USB Serial/JTAG console is byte-level raw — no terminal
 * driver doing line discipline, no readline. Tell host_runtime.c so
 * MMInkey routes through host_read_byte_nonblock + the ESC decoder
 * (cursor keys, etc) instead of the line-buffered fgetc fallback. */
int  host_raw_mode_is_active(void) { return 1; }

/* ---- USB Serial/JTAG console: stdin wiring ---------------------------
 * IDF wires stdio to USB Serial/JTAG when CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y,
 * but by default the VFS uses a polling driver, blocking reads, and CRLF
 * translation. The MMBasic line editor wants raw bytes with non-blocking
 * polling. esp32_console_init() does the 5-step IDF dance to switch the
 * VFS over to the interrupt-driven driver and disable line-ending
 * translation, then sets stdin O_NONBLOCK so read(2) returns -1/EAGAIN
 * when no byte is available.
 */

void esp32_console_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;

    /* Install + activate the interrupt-driven USB Serial/JTAG driver
     * AND switch IDF's stdio VFS to use it. With CONFIG_ESP_CONSOLE_
     * USB_SERIAL_JTAG=y, IDF wires stdio to USB Serial/JTAG via a
     * default polling reader that doesn't actually pull bytes off
     * the hardware FIFO unless someone calls read() in the right
     * way. The interrupt-driven driver fills its own ringbuffer
     * automatically, and the VFS layered on top exposes that buffer
     * via read(STDIN_FILENO, ...). */
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {
            .tx_buffer_size = 256,
            .rx_buffer_size = 256,
        };
        usb_serial_jtag_driver_install(&cfg);
    }
    usb_serial_jtag_vfs_use_driver();

    /* No CRLF translation — MMBasic owns its own line endings. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Non-blocking stdin so MMInkey's poll returns -1 instead of hanging. */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

/* Single-byte push-back for the ANSI escape decoder (host_runtime.c). */
static int s_pushback = -1;

int host_read_byte_nonblock(void) {
    if (s_pushback >= 0) { int c = s_pushback; s_pushback = -1; return c; }
    unsigned char c;
    int n = usb_serial_jtag_read_bytes(&c, 1, 0);
    return (n == 1) ? (int)c : -1;
}

int host_read_byte_blocking_ms(int ms) {
    if (s_pushback >= 0) { int c = s_pushback; s_pushback = -1; return c; }
    /* < 0 means wait forever; usb_serial_jtag_read_bytes accepts
     * portMAX_DELAY for that. */
    TickType_t ticks = (ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    if (ms > 0 && ticks == 0) ticks = 1;
    unsigned char c;
    int n = usb_serial_jtag_read_bytes(&c, 1, ticks);
    return (n == 1) ? (int)c : -1;
}

void host_push_back_byte(int c) { s_pushback = c; }

/* host_fb.c equivalents — display sim. On a port without a
 * framebuffer, all framebuffer ops are no-ops. */
void host_framebuffer_service(void) {}
void host_framebuffer_close(int which) { (void)which; }
int  host_fb_write_screenshot(const char *path) { (void)path; return -1; }

/* host_main.c equivalent — load_basic_source is the host REPL
 * loader. Not used at Phase B since app_main loads inline. */
void load_basic_source(const char *src) { (void)src; }

/* Editor.c globals that MMBasic.c::error references when emitting
 * error messages. Real Editor.c isn't linked; provide the symbols
 * as zeros so the error path doesn't crash. */
int StartEditPoint = 0;
int StartEditChar  = 0;
int editactive     = 0;

/* timegm — not in newlib's POSIX subset on Xtensa. Provide a real
 * impl. esp32_platform.h aliases user calls of `timegm` to
 * `mmbasic_timegm` so GPS.h's const-vs-non-const signature matches
 * newlib without colliding. host_runtime.c's mmbasic_timegm
 * internally calls libc timegm via #undef-ed scope, which is what
 * we resolve here. */
#undef timegm
#undef gmtime
time_t timegm(struct tm *tm) {
    /* Days-from-epoch via the algorithm from RFC-3339 / Howard Hinnant. */
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    if (m <= 2) { y -= 1; m += 12; }
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153*(m - 3) + 2)/5 + tm->tm_mday - 1;
    unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;
    long days = era*146097 + (long)doe - 719468;
    return (time_t)(days*86400L + tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec);
}

/* ---------- BASIC commands not present on stdio-style ports ---------- */

#include "MMBasic_Includes.h"

void cmd_framebuffer(void) { error("FRAMEBUFFER not supported on this port"); }
void cmd_fastgfx(void)     { error("FASTGFX not supported on this port"); }
void cmd_edit(void)        { error("EDIT not supported on this port"); }
void cmd_editfile(void)    { error("EDITFILE not supported on this port"); }

/* Display routines MMBasic_Prompt.c calls when the LCD console is active.
 * No display on this port → no-ops. */
void MX470Display(unsigned char c) { (void)c; }
void DisplayPutS(char *s)          { (void)s; }

/* ---------- runtime backing storage ---------- */

/* flash_prog_buf — RAM mirror of device flash. Allocated 2× MAX_PROG_SIZE
 * (program + scratch). Phase E will move this onto a real flash partition
 * via esp_partition_*. */
unsigned char flash_prog_buf[MAX_PROG_SIZE * 2];

/* host_runtime_get_pixel — stub for display_pixel_host.c. Real impl
 * arrives when a framebuffer driver lands. */
uint32_t host_runtime_get_pixel(int x, int y) { (void)x; (void)y; return 0; }

/* (timegm impl is above, before the BASIC-command stubs.) */
