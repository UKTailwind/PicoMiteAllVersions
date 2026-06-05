/*
 * drivers/calendar/calendar_bare.c — port-agnostic calendar HAL impl.
 *
 * Implements hal_calendar_tm_to_epoch / hal_calendar_epoch_to_tm
 * using Howard Hinnant's civil_from_days / days_from_civil algorithms
 * (http://howardhinnant.github.io/date_algorithms.html). The body has
 * no system dependencies — pure integer math over int64_t — and works
 * identically on every port from RP2040 (32-bit ARM, optional 64-bit
 * time_t) to Xtensa ESP32-S3 (32-bit long, 64-bit time_t under IDF)
 * to macOS / glibc / emscripten / pc386 (32-bit i386, 64-bit time_t).
 *
 * Lineage: the gmtime_r body started life in GPS.c (lines 145-197 in
 * commit a6279a7) following the same algorithm; the timegm body was
 * derived from the same algorithm in reverse. The original GPS.c
 * timegm body computed days/hours/minutes/seconds in the platform's
 * time_t type, which was 64-bit on RP2350 (PicoCalc passes) but
 * meaningfully different on ESP32 where the equivalent body in
 * esp32_compat.c used `long` (32-bit) and overflowed past year 2038.
 * The int64_t-everywhere math here removes that distinction.
 *
 * Implements hal/hal_calendar.h. Linked by every port that ships
 * BASIC's wall-clock surface.
 */

#include "hal/hal_calendar.h"

#include <stdint.h>

#define SECS_PER_DAY 86400
#define SECS_PER_HOUR 3600
#define SECS_PER_MIN 60
#define DAYS_PER_WEEK 7
#define YEAR_BASE 1900

/* Constants from Howard Hinnant's algorithm, anchored on 0000-03-01
 * (a Wednesday) which simplifies the leap-year arithmetic. */
#define EPOCH_ADJUSTMENT_DAYS 719468
#define ADJUSTED_EPOCH_WDAY 3 /* 0000-03-01 was a Wednesday */
#define DAYS_PER_ERA 146097L
#define DAYS_PER_CENTURY 36524L
#define DAYS_PER_4_YEARS (3 * 365 + 366)
#define DAYS_PER_YEAR 365
#define DAYS_IN_JANUARY 31
#define DAYS_IN_FEBRUARY 28
#define YEARS_PER_ERA 400

static int is_leap_year(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
}

void hal_calendar_epoch_to_tm(time_t epoch, struct tm * out) {
    /* All math in int64 so we are safe past 2038 on ports with 64-bit
     * time_t and a 32-bit `long`. */
    int64_t lcltime = (int64_t)epoch;
    int64_t days = lcltime / SECS_PER_DAY + EPOCH_ADJUSTMENT_DAYS;
    int64_t rem = lcltime % SECS_PER_DAY;
    if (rem < 0) {
        rem += SECS_PER_DAY;
        --days;
    }

    out->tm_hour = (int)(rem / SECS_PER_HOUR);
    rem %= SECS_PER_HOUR;
    out->tm_min = (int)(rem / SECS_PER_MIN);
    out->tm_sec = (int)(rem % SECS_PER_MIN);

    int weekday = (int)((ADJUSTED_EPOCH_WDAY + days) % DAYS_PER_WEEK);
    if (weekday < 0) weekday += DAYS_PER_WEEK;
    out->tm_wday = weekday;

    /* civil_from_days */
    int64_t era = (days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
    uint64_t eraday = (uint64_t)(days - era * DAYS_PER_ERA);                                                                                   /* [0, 146096] */
    uint64_t erayear = (uint64_t)((eraday - eraday / (DAYS_PER_4_YEARS - 1) + eraday / DAYS_PER_CENTURY - eraday / (DAYS_PER_ERA - 1)) / 365); /* [0, 399]   */
    uint64_t yearday = eraday - (DAYS_PER_YEAR * erayear + erayear / 4 - erayear / 100);                                                       /* [0, 365]   */
    uint64_t month = (5 * yearday + 2) / 153;                                                                                                  /* [0, 11]    */
    uint64_t day = yearday - (153 * month + 2) / 5 + 1;                                                                                        /* [1, 31]    */
    /* Shift Mar/Apr/.../Feb (anchored at March = 0) back to Jan/Feb/.../Dec
     * (Jan = 0). Months 0..9 become 2..11; months 10..11 become 0..1, with a
     * +1 year roll. */
    if (month < 10)
        month += 2;
    else
        month -= 10;
    int64_t year = (int64_t)erayear + era * YEARS_PER_ERA + ((month <= 1) ? 1 : 0);

    out->tm_yday = (int)(yearday >= DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY
                             ? yearday - (DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY)
                             : yearday + DAYS_IN_JANUARY + DAYS_IN_FEBRUARY + is_leap_year((int)erayear));
    out->tm_year = (int)(year - YEAR_BASE);
    out->tm_mon = (int)month;
    out->tm_mday = (int)day;
    out->tm_isdst = 0;
}

time_t hal_calendar_tm_to_epoch(const struct tm * tm) {
    /* days_from_civil — inverse of civil_from_days above. Shift Mar/Apr/.../Feb
     * so the leap day is at end-of-year, then compute the day count via the
     * era/era-year/year-day decomposition. All math in int64. */
    int64_t y = tm->tm_year + YEAR_BASE;
    int64_t m = tm->tm_mon + 1;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint64_t yoe = (uint64_t)(y - era * 400);
    uint64_t doy = (uint64_t)((153 * (m - 3) + 2) / 5 + tm->tm_mday - 1);
    uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * DAYS_PER_ERA + (int64_t)doe - EPOCH_ADJUSTMENT_DAYS;

    return (time_t)(days * SECS_PER_DAY + (int64_t)tm->tm_hour * SECS_PER_HOUR + (int64_t)tm->tm_min * SECS_PER_MIN + (int64_t)tm->tm_sec);
}
