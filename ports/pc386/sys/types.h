/*
 * ports/pc386/sys/types.h — freestanding shim.
 *
 * i686-elf-gcc has no <sys/types.h>. Hosted ports get off_t/ssize_t
 * from libc; pc386 provides them itself. Definitions match Linux/glibc
 * on ILP32 — the two consumers (hal_filesystem.h and FatFs callers in
 * hal_filesystem_pc386.c) only care about size and signedness.
 */

#ifndef PC386_SYS_TYPES_H
#define PC386_SYS_TYPES_H

typedef long          off_t;     /* 32-bit signed on i686-elf */
typedef int           ssize_t;   /* 32-bit signed on i686-elf */

#endif
