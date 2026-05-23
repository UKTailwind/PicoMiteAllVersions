#ifndef HOST_COMPAT_H
#define HOST_COMPAT_H

/*
 * Small portability shims for POSIX functions that aren't uniformly
 * available across the host's three targets (Linux, macOS, Windows
 * via mingw-w64). Each shim picks the right backing call at compile
 * time so callers don't carry #ifdef branches.
 */

#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
/* Windows has no POSIX symbolic-link mode bit, so define S_ISLNK
 * as always-false. Callers that ask "is this a symlink?" get the
 * right answer on a filesystem that doesn't have them. */
#ifndef S_ISLNK
#define S_ISLNK(m) 0
#endif
#endif

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

/* mkdir: POSIX takes a permission-bits arg; the Windows CRT _mkdir
 * is one-arg. Returns 0 on success, -1 on failure. */
static inline int host_mkdir(const char *path) {
#ifdef _WIN32
    return _mkdir(path) == 0 ? 0 : -1;
#else
    return mkdir(path, 0755) == 0 ? 0 : -1;
#endif
}

#endif
