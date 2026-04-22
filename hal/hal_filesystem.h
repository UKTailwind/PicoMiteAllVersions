/*
 * hal/hal_filesystem.h — POSIX-style file + directory surface for the
 * MMBasic FILES / LOAD / SAVE / OPEN / SEEK / KILL / RENAME / MKDIR /
 * RMDIR / CHDIR / CWD$() commands.
 *
 * The signatures deliberately mirror POSIX so a host port can route
 * straight to libc. Device ports dispatch by path prefix (A:, B:) to
 * FatFS (SD) and littlefs (internal flash).
 *
 * File descriptors are opaque small ints. The HAL does not alias them
 * to POSIX fds — callers never pass a hal_fs_fd_t to libc or vice versa.
 *
 * Return codes: >= 0 success (byte counts / fd / 1), < 0 errno-style error.
 *
 * Global HAL conventions apply (see hal/CONTRACT.md).
 */

#ifndef HAL_FILESYSTEM_H
#define HAL_FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>  /* off_t, ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Open flags (bitmask). */
#define HAL_FS_O_RDONLY   0x0001
#define HAL_FS_O_WRONLY   0x0002
#define HAL_FS_O_RDWR     0x0003
#define HAL_FS_O_CREAT    0x0010
#define HAL_FS_O_TRUNC    0x0020
#define HAL_FS_O_APPEND   0x0040
#define HAL_FS_O_EXCL     0x0080

#define HAL_FS_SEEK_SET   0
#define HAL_FS_SEEK_CUR   1
#define HAL_FS_SEEK_END   2

#define HAL_FS_S_IFDIR    0x4000
#define HAL_FS_S_IFREG    0x8000
#define HAL_FS_S_IFHIDDEN 0x0001

struct hal_stat {
    off_t    size;
    uint32_t mode;
    uint64_t mtime_us;
};

struct hal_dirent {
    char     name[256];
    uint32_t mode;
    off_t    size;
};

typedef int hal_fs_fd_t;
typedef struct hal_fs_dir hal_fs_dir_t;  /* opaque */

/* File ops */
int     hal_fs_open (const char *path, int flags, hal_fs_fd_t *out);
int     hal_fs_close(hal_fs_fd_t fd);
ssize_t hal_fs_read (hal_fs_fd_t fd,       void *buf, size_t n);
ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n);
off_t   hal_fs_seek (hal_fs_fd_t fd, off_t off, int whence);
off_t   hal_fs_tell (hal_fs_fd_t fd);
int     hal_fs_eof  (hal_fs_fd_t fd);
int     hal_fs_sync (hal_fs_fd_t fd);
off_t   hal_fs_size (hal_fs_fd_t fd);

/* Migration-period helper: return the underlying backend handle for an
 * open fd, with *is_lfs_out set to 0=FatFS FIL*, 1=LFS lfs_file_t*,
 * 2=POSIX FILE*. Used by FileIO.c to keep FileTable[].fptr / .lfsptr
 * populated for legacy callers during the incremental migration. Retire
 * once every direct FileTable.fptr / .lfsptr reader is HAL-routed. */
void   *hal_fs_peek_handle(hal_fs_fd_t fd, int *is_lfs_out);

/* Path ops */
int     hal_fs_unlink(const char *path);
int     hal_fs_rename(const char *from, const char *to);
int     hal_fs_mkdir (const char *path);
int     hal_fs_rmdir (const char *path);
int     hal_fs_chdir (const char *path);
char   *hal_fs_getcwd(char *buf, size_t n);
int     hal_fs_stat  (const char *path, struct hal_stat *out);

/* Directory iteration */
int hal_fs_dir_open (const char *path, hal_fs_dir_t **out);
int hal_fs_dir_next (hal_fs_dir_t *dir, struct hal_dirent *out);
int hal_fs_dir_close(hal_fs_dir_t *dir);

#ifdef __cplusplus
}
#endif

#endif  /* HAL_FILESYSTEM_H */
