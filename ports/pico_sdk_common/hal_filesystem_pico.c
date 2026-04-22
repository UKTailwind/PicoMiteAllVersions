/*
 * ports/pico_sdk_common/hal_filesystem_pico.c — hal_filesystem dispatcher
 * for device builds.
 *
 * The PicoMite device exposes two filesystems:
 *   A:   SD card via FatFS (f_* ops, backed by disk_* in mmc_stm32.c)
 *   B:   internal flash via littlefs (lfs_* ops, backed by fs_flash_* in
 *        FileIO.c, cfg in pico_lfs_cfg)
 *
 * This adapter inspects the path prefix and routes each POSIX-style op
 * to the correct library. Paths without an explicit drive prefix fall
 * through to the legacy logic in FileIO.c via the existing FatFS default
 * — callers that need implicit-drive semantics still use cmd_chdir /
 * BasicFileOpen, which keep their own filepath[] state machine.
 *
 * The open/read/write/seek/close file-table ops are NOT implemented in
 * this TU yet — they go through FileIO.c's FileTable[] state machine
 * (BasicFileOpen, FileGetChar, ...). Migrating those is a separate
 * step; until then hal_fs_{open,close,read,write,seek,tell,eof,sync,
 * dir_*} return -ENOSYS on device. None are called on device today.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "hal/hal_filesystem.h"
#include "ff.h"
#include "lfs.h"

extern lfs_t lfs;

/* FatFS rc -> HAL errno (negative). */
static int fatfs_rc_to_errno(FRESULT r)
{
    switch (r) {
    case FR_OK:                  return 0;
    case FR_NO_FILE:             return -ENOENT;
    case FR_NO_PATH:             return -ENOENT;
    case FR_DENIED:              return -EACCES;
    case FR_EXIST:               return -EEXIST;
    case FR_WRITE_PROTECTED:     return -EROFS;
    case FR_INVALID_NAME:        return -EINVAL;
    case FR_INVALID_DRIVE:       return -ENODEV;
    case FR_NOT_READY:           return -EIO;
    case FR_DISK_ERR:            return -EIO;
    case FR_INT_ERR:             return -EIO;
    case FR_NOT_ENABLED:         return -ENODEV;
    case FR_NO_FILESYSTEM:       return -ENOENT;
    case FR_TIMEOUT:             return -ETIMEDOUT;
    case FR_LOCKED:              return -EACCES;
    default:                     return -EIO;
    }
}

/* LFS error -> HAL errno (negative). LFS already returns negative
 * errno-style codes (LFS_ERR_*), but the numeric values don't always
 * match POSIX errno — translate explicitly. */
static int lfs_rc_to_errno(int r)
{
    if (r >= 0) return r;
    switch (r) {
    case LFS_ERR_OK:       return 0;
    case LFS_ERR_IO:       return -EIO;
    case LFS_ERR_CORRUPT:  return -EIO;
    case LFS_ERR_NOENT:    return -ENOENT;
    case LFS_ERR_EXIST:    return -EEXIST;
    case LFS_ERR_NOTDIR:   return -ENOTDIR;
    case LFS_ERR_ISDIR:    return -EISDIR;
    case LFS_ERR_NOTEMPTY: return -ENOTEMPTY;
    case LFS_ERR_BADF:     return -EBADF;
    case LFS_ERR_FBIG:     return -EFBIG;
    case LFS_ERR_INVAL:    return -EINVAL;
    case LFS_ERR_NOSPC:    return -ENOSPC;
    case LFS_ERR_NOMEM:    return -ENOMEM;
    case LFS_ERR_NOATTR:   return -ENODATA;
    case LFS_ERR_NAMETOOLONG: return -ENAMETOOLONG;
    default:               return -EIO;
    }
}

/* Strip the explicit drive prefix ("A:", "B:") and return the rest of
 * the path. Callers that want FatFS-usable paths leave A: in place (FatFS
 * parses drive letters natively); LFS doesn't understand them and must
 * see just the path component. */
static const char *path_after_drive(const char *path)
{
    if (path && path[0] && path[1] == ':') return path + 2;
    return path;
}

/* Choose which filesystem owns a given path. Only an explicit "A:" or
 * "B:" prefix selects; unprefixed paths default to FatFS (historical
 * behaviour — the boot drive on device). */
typedef enum { FS_FATFS, FS_LFS } fs_kind_t;

static fs_kind_t path_fs(const char *path)
{
    if (path && (path[0] == 'B' || path[0] == 'b') && path[1] == ':') return FS_LFS;
    return FS_FATFS;
}

/* -- Path ops ---------------------------------------------------------- */

int hal_fs_mkdir(const char *path)
{
    if (!path) return -EINVAL;
    if (path_fs(path) == FS_LFS) {
        return lfs_rc_to_errno(lfs_mkdir(&lfs, path_after_drive(path)));
    }
    return fatfs_rc_to_errno(f_mkdir(path));
}

int hal_fs_rmdir(const char *path)
{
    if (!path) return -EINVAL;
    if (path_fs(path) == FS_LFS) {
        return lfs_rc_to_errno(lfs_remove(&lfs, path_after_drive(path)));
    }
    return fatfs_rc_to_errno(f_unlink(path));
}

int hal_fs_unlink(const char *path)
{
    if (!path) return -EINVAL;
    if (path_fs(path) == FS_LFS) {
        return lfs_rc_to_errno(lfs_remove(&lfs, path_after_drive(path)));
    }
    return fatfs_rc_to_errno(f_unlink(path));
}

int hal_fs_rename(const char *from, const char *to)
{
    if (!from || !to) return -EINVAL;
    fs_kind_t f = path_fs(from), t = path_fs(to);
    if (f != t) return -EXDEV;  /* cross-device rename not supported */
    if (f == FS_LFS) {
        return lfs_rc_to_errno(lfs_rename(&lfs, path_after_drive(from), path_after_drive(to)));
    }
    return fatfs_rc_to_errno(f_rename(from, to));
}

int hal_fs_chdir(const char *path)
{
    if (!path) return -EINVAL;
    /* cmd_chdir has its own path-resolution logic (mmbasic_chdir) — it
     * still drives f_chdir / lfs_dir_open directly. This entry is here
     * for completeness; if it ever gets called on device, route to the
     * simpler of the two. */
    if (path_fs(path) == FS_LFS) {
        lfs_dir_t d;
        int r = lfs_dir_open(&lfs, &d, path_after_drive(path));
        if (r == 0) lfs_dir_close(&lfs, &d);
        return lfs_rc_to_errno(r);
    }
    return fatfs_rc_to_errno(f_chdir(path));
}

char *hal_fs_getcwd(char *buf, size_t n)
{
    if (!buf || n == 0) return NULL;
    FRESULT r = f_getcwd(buf, (UINT)n);
    if (r != FR_OK) return NULL;
    return buf;
}

int hal_fs_stat(const char *path, struct hal_stat *out)
{
    if (!path || !out) return -EINVAL;
    if (path_fs(path) == FS_LFS) {
        struct lfs_info info;
        int r = lfs_stat(&lfs, path_after_drive(path), &info);
        if (r < 0) return lfs_rc_to_errno(r);
        out->size = info.size;
        out->mode = (info.type == LFS_TYPE_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
        out->mtime_us = 0;
        return 0;
    }
    FILINFO fno;
    FRESULT r = f_stat(path, &fno);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    out->size = fno.fsize;
    out->mode = (fno.fattrib & AM_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
    if (fno.fattrib & AM_HID) out->mode |= HAL_FS_S_IFHIDDEN;
    out->mtime_us = 0;
    return 0;
}

/* -- File-table ops: not yet migrated on device. ---------------------- */

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
