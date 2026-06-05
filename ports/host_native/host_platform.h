/*
 * host_platform.h — Force-included before all sources for host build.
 *
 * Defines the compile target and stubs Pico SDK macros.
 */
#ifndef __HOST_PLATFORM_H
#define __HOST_PLATFORM_H

/* Build target */
#define PICOMITE 1
#define MMBASIC_HOST 1

/* Pico SDK attribute macros — no-op on host */
#define __not_in_flash_func(x) x
#define __not_in_flash(x)

/* Standard types needed before MMBasic headers */
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#ifdef _WIN32
/* Pull in <io.h> first so its POSIX-aliased setmode(int,int) is
 * declared under its original name. Then macro-rewrite every
 * subsequent `setmode` reference (including MMBasic's
 * declaration / definitions / call sites) to Display_SetMode, so
 * the two functions can coexist in any TU on Windows. <io.h>'s
 * declaration is unaffected because it was parsed before the
 * macro existed. */
#include <io.h>
#define setmode Display_SetMode
#endif
#ifndef uint
typedef unsigned int uint;
#endif

/* Pico SDK section/attribute macros */
#define __uninitialized_ram(x) x
#define __in_flash(x)
#define __scratch_x(x)
#define __scratch_y(x)

/* Math constants that may be missing */
#ifndef M_TWOPI
#define M_TWOPI (2.0 * 3.14159265358979323846)
#endif

/* PSRAMpin referenced in some configs */
#define PSRAMpin 0

#endif /* __HOST_PLATFORM_H */
