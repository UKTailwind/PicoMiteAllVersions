/* ports/pc386/include_shim/errno.h — freestanding shim.
 *
 * Errno values mirror Linux/glibc on i386 — the MMBasic core only
 * checks a small subset (EINVAL, ENOENT, ENOMEM, EPERM, EROFS) and
 * doesn't care about exact numeric agreement with any standard,
 * just that the symbols resolve and round-trip through errno. */
#ifndef _PC386_ERRNO_H
#define _PC386_ERRNO_H

extern int errno;

#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define EBADF           9
#define ENOMEM         12
#define EACCES         13
#define EFAULT         14
#define EBUSY          16
#define EEXIST         17
#define EXDEV          18
#define ENODEV         19
#define ENOTDIR        20
#define EISDIR         21
#define EINVAL         22
#define ENFILE         23
#define EMFILE         24
#define ENOTTY         25
#define EFBIG          27
#define ENOSPC         28
#define ESPIPE         29
#define EROFS          30
#define ERANGE         34
#define ENAMETOOLONG   36
#define ENOSYS         38
#define ENODATA        61
#define EOVERFLOW      75
#define EAGAIN         11

#endif
