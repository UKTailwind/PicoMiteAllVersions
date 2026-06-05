/* ports/pc386/include_shim/sys/stat.h — freestanding shim.
 *
 * FileIO.c includes this for `struct stat` + `S_ISLNK`. The actual
 * stat() call is commented out; the symbol exists only to keep the
 * if-branch compiling. struct stat carries just `st_mode` (the only
 * field FileIO.c reads).
 */
#ifndef _PC386_SYS_STAT_H
#define _PC386_SYS_STAT_H

struct stat {
    unsigned int st_mode;
};

/* No symlinks on FAT — predicate always false. */
#define S_ISLNK(m) (0)
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISREG(m) (((m) & 0170000) == 0100000)

#endif
