/* ports/pc386/include_shim/time.h — freestanding shim.
 *
 * MMBasic uses time_t + struct tm for DATE$/TIME$/DATETIME$. The
 * actual conversion functions are routed through hal_time + a
 * pc386_time_pieces.c that implements the calendar math.
 */
#ifndef _PC386_TIME_H
#define _PC386_TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t      time     (time_t *t);
clock_t     clock    (void);
struct tm  *localtime(const time_t *t);
struct tm  *gmtime   (const time_t *t);
time_t      mktime   (struct tm *tm);
/* GPS.h declares timegm with const struct tm *; keep ours matching so
 * the two declarations don't conflict. */
time_t      timegm   (const struct tm *tm);
size_t      strftime (char *s, size_t n, const char *fmt, const struct tm *tm);
double      difftime (time_t a, time_t b);
char       *asctime  (const struct tm *tm);
char       *ctime    (const time_t *t);

#endif
