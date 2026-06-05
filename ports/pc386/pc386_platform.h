/*
 * ports/pc386/pc386_platform.h — force-included before every TU on
 * the pc386 build.
 *
 * Mirror of ports/host_native/host_platform.h. Defines the build
 * target, no-ops the Pico SDK section/attribute macros, and pulls in
 * the freestanding-libc primitive types that MMBasic core depends on.
 */
#ifndef _PC386_PLATFORM_H
#define _PC386_PLATFORM_H

#define PICOMITE 1
#define MMBASIC_HOST 1

/* Pico SDK section/attribute macros — no-op on pc386. */
#define __not_in_flash_func(x) x
#define __not_in_flash(x)
#define __uninitialized_ram(x) x
#define __in_flash(x)
#define __scratch_x(x)
#define __scratch_y(x)

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> /* lfs_util.h calls malloc without including <stdlib.h> */
#include <setjmp.h> /* MMBasic.h uses jmp_buf without including <setjmp.h> */
#include <time.h>   /* MM_Misc.h uses time_t without an explicit include */

#ifndef uint
typedef unsigned int uint;
#endif

#endif /* __ASSEMBLER__ */

#ifndef M_TWOPI
#define M_TWOPI (2.0 * 3.14159265358979323846)
#endif

/* Referenced by some board-config code paths even when no PSRAM is
 * present. host_platform.h does the same. */
#define PSRAMpin 0

#endif
