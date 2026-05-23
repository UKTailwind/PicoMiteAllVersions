#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H

/*
 * Small portability shims for POSIX functions that aren't uniformly
 * available across the host's three targets (Linux, macOS, Windows
 * via mingw-w64). Each shim picks the right backing call at compile
 * time so callers don't carry #ifdef branches.
 */

#include <time.h>

/* localtime_r: POSIX-reentrant local-time conversion. Returns the
 * filled tm pointer on success, NULL on failure. Windows MSVC has
 * localtime_s with swapped argument order. */
static inline struct tm *host_localtime_r(const time_t *t, struct tm *out) {
#ifdef _WIN32
    return localtime_s(out, t) == 0 ? out : NULL;
#else
    return localtime_r(t, out);
#endif
}

#endif
