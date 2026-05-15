/*
 * hal/hal_calendar.h — wall-clock calendar conversion HAL.
 *
 * Two functions, one purpose: convert between Unix epoch seconds and
 * broken-down UTC time. Used by BASIC's DATE$ / TIME$ / EPOCH /
 * DATETIME$ / DAY$ commands plus the GPS NMEA decoder and the NTP
 * client.
 *
 * Why a HAL at all? The naive approach is to call libc `timegm` and
 * `gmtime` directly. Two problems:
 *   1. GPS.h historically declared both with `const struct tm *`
 *      arguments, but POSIX `timegm`/`gmtime` decls in <time.h> are
 *      `struct tm *` (no const) because they may mutate. Including
 *      both headers in the same TU is a signature conflict.
 *   2. Some target libcs lack `timegm` entirely (newlib on Xtensa),
 *      and others provide it but with a 32-bit `long` internal type
 *      that overflows past 2038.
 *
 * The HAL gives every caller a single contract with const-correct
 * inputs and reentrant outputs. Per-port drivers under
 * drivers/calendar/ resolve the symbols — calendar_bare.c uses
 * int64-clean math and works on every target.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md):
 *   caller owns all buffers; HAL impl never calls MMBasic's error().
 */

#ifndef HAL_CALENDAR_H
#define HAL_CALENDAR_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Convert a broken-down UTC time (`*tm`) to seconds since the Unix
 * epoch (1970-01-01 00:00:00 UTC). The input is treated as UTC; no
 * timezone or DST adjustment is applied. The implementation is
 * required to be tolerant of unnormalised `tm` fields in the same way
 * POSIX `timegm` is — extra days roll into months, etc.
 *
 * The argument is `const`; implementations must not mutate it (callers
 * working with a `struct tm *` from `gmtime` family should still copy
 * if they want to pass the same buffer to `_epoch_to_tm` afterwards).
 */
time_t hal_calendar_tm_to_epoch(const struct tm *tm);

/* Convert Unix epoch seconds to broken-down UTC time. Writes through
 * `*out` (caller-owned buffer) — reentrant; no internal static state.
 * Fills tm_sec/min/hour/mday/mon/year/wday/yday; tm_isdst is set to 0
 * (UTC has no DST).
 */
void hal_calendar_epoch_to_tm(time_t epoch, struct tm *out);

#ifdef __cplusplus
}
#endif

#endif  /* HAL_CALENDAR_H */
