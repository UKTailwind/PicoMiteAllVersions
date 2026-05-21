/*
 * ports/pc386/pc386_rtc.c — CMOS RTC driver for MMBasic.
 *
 *   RTC SET y, m, d, h, mm, ss   — write the date/time to CMOS.
 *
 * Pairs with the CMOS reader in pc386_libc.c (which feeds time() /
 * localtime_r() / DATE$ / TIME$). After SET we invalidate the boot
 * epoch cache so the next time() call re-reads the new clock.
 *
 * The CMOS RTC is at I/O ports 0x70 (index) / 0x71 (data); standard
 * PC/AT since 1984.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int    pc386_boot_epoch_inited;
extern time_t pc386_boot_epoch;

static void pc386_cmos_write(uint8_t reg, uint8_t val) {
    __asm__ volatile("outb %0, $0x70" :: "a"(reg));
    __asm__ volatile("outb %0, $0x71" :: "a"(val));
}

static uint8_t pc386_cmos_rd(uint8_t reg) {
    __asm__ volatile("outb %0, $0x70" :: "a"(reg));
    uint8_t v;
    __asm__ volatile("inb $0x71, %0" : "=a"(v));
    return v;
}

static uint8_t pc386_bin_to_bcd(uint8_t v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

void cmd_rtc(void) {
    unsigned char *p;
    if ((p = checkstring(cmdline, (unsigned char *)"SET"))) {
        getargs(&p, 11, (unsigned char *)",");
        if (argc != 11) error("Argument count");
        int year = getint(argv[0],  1980, 2099);
        int mon  = getint(argv[2],  1, 12);
        int day  = getint(argv[4],  1, 31);
        int hour = getint(argv[6],  0, 23);
        int min  = getint(argv[8],  0, 59);
        int sec  = getint(argv[10], 0, 59);
        uint8_t statusB = pc386_cmos_rd(0x0B);
        uint8_t cent = (uint8_t)(year / 100);
        uint8_t yy   = (uint8_t)(year % 100);
        if (!(statusB & 0x04)) {
            sec  = pc386_bin_to_bcd((uint8_t)sec);
            min  = pc386_bin_to_bcd((uint8_t)min);
            hour = pc386_bin_to_bcd((uint8_t)hour);
            day  = pc386_bin_to_bcd((uint8_t)day);
            mon  = pc386_bin_to_bcd((uint8_t)mon);
            yy   = pc386_bin_to_bcd(yy);
            cent = pc386_bin_to_bcd(cent);
        }
        pc386_cmos_write(0x00, (uint8_t)sec);
        pc386_cmos_write(0x02, (uint8_t)min);
        pc386_cmos_write(0x04, (uint8_t)hour);
        pc386_cmos_write(0x07, (uint8_t)day);
        pc386_cmos_write(0x08, (uint8_t)mon);
        pc386_cmos_write(0x09, yy);
        pc386_cmos_write(0x32, cent);
        /* Force time() to re-read the RTC on its next call. */
        pc386_boot_epoch_inited = 0;
        return;
    }
    error("Syntax");
}
