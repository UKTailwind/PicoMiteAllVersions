/* ports/pc386/include_shim/limits.h — freestanding shim.
 *
 * Wraps gcc's freestanding <limits.h> (which provides INT_MAX etc.)
 * and adds the POSIX-defined limits MMBasic core uses: PATH_MAX.
 *
 * The #include_next pulls in gcc's own header; our additions sit on
 * top. Without it, our shim would shadow the integer-limit macros
 * MMBasic relies on (CHAR_BIT, INT_MAX, LONG_MIN, etc.).
 */
#ifndef _PC386_LIMITS_H
#define _PC386_LIMITS_H

#include_next <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 256 /* FAT max-name path; MMBasic never goes deep */
#endif

#endif
