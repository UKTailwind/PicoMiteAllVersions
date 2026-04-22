/*
 * host/hal_filesystem_host.c — hal_filesystem on host.
 *
 * The host port already has a dispatch layer (host/host_fs_shims.c)
 * that routes FatFS-style f_* and host_f_* calls to either real POSIX
 * (when host_sd_root is set for REPL / --sim) or to the vendored
 * FatFS running on vm_host_fat's RAM disk (test-harness mode).
 *
 * Rather than re-implementing that dispatch here, hal_fs_* forwards to
 * the host_f_* wrappers where they exist, and to the vendored f_* /
 * host_fs_posix_* otherwise. Errors convert to negative POSIX errno;
 * FatFS FRESULT values translate via fatfs_rc_to_errno below.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ff.h"
#include "diskio.h"

/* host_platform.h's rename-to-mmbasic_chdir applies to any code that
 * includes it; hal_filesystem_host.c does not include host_platform.h,
 * so chdir/rename here reach libc's real symbols. Explicit paranoia: */
#ifdef chdir
#undef chdir
#endif

#include "hal/hal_filesystem.h"

/* Existing host FatFS adapters (host/host_fs_shims.c). */
extern FRESULT host_f_unlink (const TCHAR *path);
extern FRESULT host_f_rename (const TCHAR *from, const TCHAR *to);
extern FRESULT host_f_mkdir  (const TCHAR *path);
extern FRESULT host_f_chdir  (const TCHAR *path);
extern FRESULT host_f_getcwd (TCHAR *buf, UINT len);

static int fatfs_rc_to_errno(FRESULT r)
{
    switch (r) {
    case FR_OK:              return 0;
    case FR_NO_FILE:         return -ENOENT;
    case FR_NO_PATH:         return -ENOENT;
    case FR_DENIED:          return -EACCES;
    case FR_EXIST:           return -EEXIST;
    case FR_WRITE_PROTECTED: return -EROFS;
    case FR_INVALID_NAME:    return -EINVAL;
    case FR_INVALID_DRIVE:   return -ENODEV;
    case FR_NOT_READY:       return -EIO;
    case FR_DISK_ERR:        return -EIO;
    case FR_INT_ERR:         return -EIO;
    case FR_NOT_ENABLED:     return -ENODEV;
    case FR_NO_FILESYSTEM:   return -ENOENT;
    case FR_TIMEOUT:         return -ETIMEDOUT;
    case FR_LOCKED:          return -EACCES;
    default:                 return -EIO;
    }
}

/* -------------------------------------------------------------------- */
/* Path ops — route through host_f_* which knows about host_sd_root vs
 * vm_host_fat RAM disk. */
/* -------------------------------------------------------------------- */

int hal_fs_unlink(const char *path)           { return fatfs_rc_to_errno(host_f_unlink(path)); }
int hal_fs_rename(const char *from, const char *to) { return fatfs_rc_to_errno(host_f_rename(from, to)); }
int hal_fs_mkdir (const char *path)           { return fatfs_rc_to_errno(host_f_mkdir(path)); }
int hal_fs_rmdir (const char *path)           { return fatfs_rc_to_errno(host_f_unlink(path)); }  /* FatFS f_unlink handles dirs */
int hal_fs_chdir (const char *path)           { return fatfs_rc_to_errno(host_f_chdir(path)); }

char *hal_fs_getcwd(char *buf, size_t n)
{
    if (!buf || n == 0) return NULL;
    if (host_f_getcwd(buf, (UINT)n) != FR_OK) return NULL;
    return buf;
}

int hal_fs_stat(const char *path, struct hal_stat *out)
{
    if (!path || !out) return -EINVAL;
    FILINFO fno;
    FRESULT r = f_stat(path, &fno);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    out->size = fno.fsize;
    out->mode = (fno.fattrib & AM_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
    if (fno.fattrib & AM_HID) out->mode |= HAL_FS_S_IFHIDDEN;
    out->mtime_us = 0;
    return 0;
}

/* -------------------------------------------------------------------- */
/* File-table ops are not yet migrated on host — they still go through
 * FileIO.c's BasicFileOpen + FileTable[] machinery (which itself
 * dispatches to host_fs_posix_* or FatFS). These entries return ENOSYS
 * so the symbols resolve; no caller invokes them today. */
/* -------------------------------------------------------------------- */

int     hal_fs_open (const char *path, int flags, hal_fs_fd_t *out) { (void)path; (void)flags; (void)out; return -ENOSYS; }
int     hal_fs_close(hal_fs_fd_t fd)                                 { (void)fd; return -ENOSYS; }
ssize_t hal_fs_read (hal_fs_fd_t fd,       void *buf, size_t n)      { (void)fd; (void)buf; (void)n; return -ENOSYS; }
ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n)      { (void)fd; (void)buf; (void)n; return -ENOSYS; }
off_t   hal_fs_seek (hal_fs_fd_t fd, off_t off, int whence)          { (void)fd; (void)off; (void)whence; return -ENOSYS; }
off_t   hal_fs_tell (hal_fs_fd_t fd)                                 { (void)fd; return -ENOSYS; }
int     hal_fs_eof  (hal_fs_fd_t fd)                                 { (void)fd; return -ENOSYS; }
int     hal_fs_sync (hal_fs_fd_t fd)                                 { (void)fd; return -ENOSYS; }

int hal_fs_dir_open (const char *path, hal_fs_dir_t **out)      { (void)path; (void)out; return -ENOSYS; }
int hal_fs_dir_next (hal_fs_dir_t *dir, struct hal_dirent *out) { (void)dir; (void)out; return -ENOSYS; }
int hal_fs_dir_close(hal_fs_dir_t *dir)                          { (void)dir; return -ENOSYS; }
