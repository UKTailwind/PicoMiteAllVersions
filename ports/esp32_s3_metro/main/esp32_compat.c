/*
 * esp32_compat.c — small porting bits with no obvious category:
 *
 *   - host_time_us_64 / host_sleep_us: aliases the host_runtime layer
 *     expects from a sibling time TU.
 *   - timegm: GNU/BSD extension that newlib on Xtensa doesn't expose.
 *     GPS.h calls into it via the host_platform.h rename trick.
 *   - flash_prog_buf: RAM-backed mirror of the program-memory region.
 *     Replaced by an esp_partition-backed impl in a later phase.
 *   - host_framebuffer_* / host_fb_* / MX470Display / DisplayPutS /
 *     host_runtime_get_pixel / load_basic_source: no-op stubs for
 *     symbols only meaningful on ports that have a display or REPL
 *     loader of their own.
 *   - cmd_framebuffer / cmd_fastgfx: error stubs for BASIC commands
 *     that need a framebuffer this port doesn't have.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"

/* ---- time aliases ---- */

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

/* ---- timegm: defined as the underlying libc symbol after host_platform.h
 * has macro-renamed user calls to mmbasic_timegm. host_runtime.c #undef's
 * timegm in its own scope and calls it; this provides the body. */

#undef timegm
#undef gmtime
time_t timegm(struct tm *tm) {
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    if (m <= 2) { y -= 1; m += 12; }
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m - 3) + 2) / 5 + tm->tm_mday - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400L + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}

/* ---- runtime backing storage ---- */

/* MAX_PROG_SIZE for the program + a small "erased flash" tail. Both
 * regions are 0xff-filled to mirror XIP-mapped flash semantics:
 * PrepareProgramExt walks past the program terminator looking for
 * 0xff as "end of program / start of CFunction area", and the
 * CFunction-walk loop expects 0xffffffff-aligned terminator. Stale
 * bytes here corrupt the walk and crash with LoadProhibited. */
#define FLASH_PROG_TRAILER  4096
unsigned char flash_prog_buf[MAX_PROG_SIZE + FLASH_PROG_TRAILER];

__attribute__((constructor))
static void flash_prog_buf_init(void) {
    memset(flash_prog_buf, 0xff, sizeof flash_prog_buf);
}

/* ---- no-op stubs for symbols only meaningful on ports with a display ---- */

void host_framebuffer_service(void) {}
void host_framebuffer_close(int which) { (void)which; }
int  host_fb_write_screenshot(const char *path) { (void)path; return -1; }
uint32_t host_runtime_get_pixel(int x, int y) { (void)x; (void)y; return 0; }
void MX470Display(unsigned char c) { (void)c; }
void DisplayPutS(char *s) { (void)s; }

/* load_basic_source — tokenize a .bas text buffer into ProgMemory.
 * SaveProgramToFlash calls this with the freshly-read file contents from
 * FileLoadProgram so subsequent LIST / RUN see a valid program in memory.
 * Mirrors ports/mmbasic_ansi/ansi_main.c::load_basic_source. */
extern unsigned char *ProgMemory;
extern unsigned char tknbuf[];
extern unsigned char inpbuf[];
extern int PSize;
extern void tokenise(int console);
extern void flash_range_erase(uint32_t off, uint32_t count);

int load_basic_source(const char *source) {
    flash_range_erase(0, MAX_PROG_SIZE);
    unsigned char *pm = ProgMemory;
    const char *line = source;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            if (len >= STRINGSIZE) len = STRINGSIZE - 1;
            memcpy(inpbuf, line, len);
            inpbuf[len] = '\0';
            tokenise(0);
            unsigned char *tp = tknbuf;
            while (!(tp[0] == 0 && tp[1] == 0)) *pm++ = *tp++;
            *pm++ = 0;
        }
        line = eol ? eol + 1 : line + strlen(line);
    }
    *pm++ = 0;
    *pm++ = 0;
    PSize = (int)(pm - ProgMemory);
    return 0;
}

/* ---- BASIC commands that require a framebuffer this port doesn't have ---- */

void cmd_framebuffer(void) { error("FRAMEBUFFER not supported on this port"); }
void cmd_fastgfx(void)     { error("FASTGFX not supported on this port"); }
