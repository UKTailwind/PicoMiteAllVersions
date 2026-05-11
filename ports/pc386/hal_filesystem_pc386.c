/*
 * ports/pc386/hal_filesystem_pc386.c — hal_filesystem over FatFs.
 *
 * Pc386 has real FatFs mounted on A: (FAT12, 8 MB) and C: (FAT16,
 * 32 MB) over the ATA-PIO disk driver brought up in stage 2. Every
 * hal_fs_X / hal_ff_X entry is a direct forward to the matching f_X.
 *
 * Slot table: HAL_FS_MAX_OPEN FIL handles, allocated in pc386's bump
 * allocator. Slot 0 is reserved (hal_fs_fd_t == 0 is "closed"); fds
 * are slot index + 1.
 *
 * Patterned after host_native's hal_filesystem_host.c, minus the
 * POSIX/host_sd_root dispatch (we have only FatFs) and the
 * vm_host_fat detour.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "ff.h"
#include "hal/hal_filesystem.h"
#include "hal/hal_fatfs_dispatch.h"

/* -------------------------------------------------------------------- */
/* FRESULT → -errno                                                     */
/* -------------------------------------------------------------------- */
static int fatfs_rc_to_errno(FRESULT r) {
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
    case FR_TIMEOUT:         return -EIO;
    case FR_LOCKED:          return -EACCES;
    default:                 return -EIO;
    }
}

static BYTE hal_flags_to_fatfs(int flags) {
    BYTE m = 0;
    if ((flags & HAL_FS_O_RDWR) == HAL_FS_O_RDWR) m |= FA_READ | FA_WRITE;
    else if (flags & HAL_FS_O_WRONLY)             m |= FA_WRITE;
    else                                          m |= FA_READ;
    if (flags & HAL_FS_O_CREAT) {
        if      (flags & HAL_FS_O_TRUNC) m |= FA_CREATE_ALWAYS;
        else if (flags & HAL_FS_O_EXCL)  m |= FA_CREATE_NEW;
        else                             m |= FA_OPEN_ALWAYS;
    }
    if (flags & HAL_FS_O_APPEND) m |= FA_OPEN_APPEND;
    return m;
}

/* -------------------------------------------------------------------- */
/* Slot table                                                           */
/* -------------------------------------------------------------------- */
#define HAL_FS_MAX_OPEN 16

static FIL pc386_fs_slots[HAL_FS_MAX_OPEN];
static char pc386_fs_used[HAL_FS_MAX_OPEN];

static int pc386_fs_alloc(void) {
    for (int i = 0; i < HAL_FS_MAX_OPEN; i++) {
        if (!pc386_fs_used[i]) { pc386_fs_used[i] = 1; return i; }
    }
    return -1;
}

static FIL *pc386_fs_get(hal_fs_fd_t fd) {
    if (fd < 1 || fd > HAL_FS_MAX_OPEN) return NULL;
    if (!pc386_fs_used[fd - 1]) return NULL;
    return &pc386_fs_slots[fd - 1];
}

static void pc386_fs_release(hal_fs_fd_t fd) {
    if (fd >= 1 && fd <= HAL_FS_MAX_OPEN) pc386_fs_used[fd - 1] = 0;
}

/* -------------------------------------------------------------------- */
/* File ops                                                             */
/* -------------------------------------------------------------------- */
int hal_fs_open(const char *path, int flags, hal_fs_fd_t *out) {
    if (!path || !out) return -EINVAL;
    int idx = pc386_fs_alloc();
    if (idx < 0) return -EMFILE;
    FRESULT r = f_open(&pc386_fs_slots[idx], path, hal_flags_to_fatfs(flags));
    if (r != FR_OK) { pc386_fs_release(idx + 1); return fatfs_rc_to_errno(r); }
    *out = idx + 1;
    return 0;
}

int hal_fs_close(hal_fs_fd_t fd) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    FRESULT r = f_close(fp);
    pc386_fs_release(fd);
    return fatfs_rc_to_errno(r);
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void *buf, size_t n) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp || !buf) return -EBADF;
    UINT bw = 0;
    FRESULT r = f_read(fp, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp || !buf) return -EBADF;
    UINT bw = 0;
    FRESULT r = f_write(fp, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

int hal_fs_getc(hal_fs_fd_t fd) {
    unsigned char c;
    ssize_t r = hal_fs_read(fd, &c, 1);
    if (r < 0) return (int)r;
    if (r == 0) return -ENODATA;
    return (int)c;
}

int hal_fs_putc(hal_fs_fd_t fd, char c) {
    ssize_t w = hal_fs_write(fd, &c, 1);
    if (w < 0) return (int)w;
    if (w != 1) return -EIO;
    return 0;
}

off_t hal_fs_seek(hal_fs_fd_t fd, off_t off, int whence) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    FRESULT r;
    if (whence == HAL_FS_SEEK_SET)      r = f_lseek(fp, off);
    else if (whence == HAL_FS_SEEK_CUR) r = f_lseek(fp, f_tell(fp) + off);
    else                                r = f_lseek(fp, f_size(fp) + off);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (off_t)f_tell(fp);
}

off_t hal_fs_tell(hal_fs_fd_t fd) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    return (off_t)f_tell(fp);
}

int hal_fs_eof(hal_fs_fd_t fd) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    return f_eof(fp) ? 1 : 0;
}

int hal_fs_sync(hal_fs_fd_t fd) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    return fatfs_rc_to_errno(f_sync(fp));
}

off_t hal_fs_size(hal_fs_fd_t fd) {
    FIL *fp = pc386_fs_get(fd);
    if (!fp) return -EBADF;
    return (off_t)f_size(fp);
}

/* -------------------------------------------------------------------- */
/* Path ops — direct FatFs forwarders.                                  */
/* -------------------------------------------------------------------- */
int hal_fs_unlink(const char *path) { return fatfs_rc_to_errno(f_unlink(path)); }
int hal_fs_rename(const char *from, const char *to) { return fatfs_rc_to_errno(f_rename(from, to)); }
int hal_fs_mkdir (const char *path) { return fatfs_rc_to_errno(f_mkdir(path)); }
int hal_fs_rmdir (const char *path) { return fatfs_rc_to_errno(f_unlink(path)); }
int hal_fs_chdir (const char *path) { return fatfs_rc_to_errno(f_chdir(path)); }

char *hal_fs_getcwd(char *buf, size_t n) {
    if (!buf || n == 0) return NULL;
    if (f_getcwd(buf, (UINT)n) != FR_OK) return NULL;
    return buf;
}

int hal_fs_stat(const char *path, struct hal_stat *out) {
    if (!path || !out) return -EINVAL;
    FILINFO fno;
    FRESULT r = f_stat(path, &fno);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    out->size = fno.fsize;
    out->mode = (fno.fattrib & AM_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
    if (fno.fattrib & (AM_HID | AM_SYS)) out->mode |= HAL_FS_S_IFHIDDEN;
    out->mtime_us = 0;
    return 0;
}

/* -------------------------------------------------------------------- */
/* Directory iteration                                                   */
/* -------------------------------------------------------------------- */
struct hal_fs_dir {
    DIR fatfs_dir;
};

int hal_fs_dir_open(const char *path, hal_fs_dir_t **out) {
    if (!path || !out) return -EINVAL;
    hal_fs_dir_t *d = (hal_fs_dir_t *)calloc(1, sizeof(*d));
    if (!d) return -ENOMEM;
    FRESULT r = f_opendir(&d->fatfs_dir, path);
    if (r != FR_OK) { free(d); return fatfs_rc_to_errno(r); }
    *out = d;
    return 0;
}

int hal_fs_dir_next(hal_fs_dir_t *dir, struct hal_dirent *out) {
    if (!dir || !out) return -EINVAL;
    FILINFO fno;
    FRESULT r = f_readdir(&dir->fatfs_dir, &fno);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    if (fno.fname[0] == 0) return 0;
    strncpy(out->name, fno.fname, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->size = fno.fsize;
    out->mode = (fno.fattrib & AM_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
    if (fno.fattrib & (AM_HID | AM_SYS)) out->mode |= HAL_FS_S_IFHIDDEN;
    return 1;
}

int hal_fs_dir_close(hal_fs_dir_t *dir) {
    if (!dir) return -EINVAL;
    FRESULT r = f_closedir(&dir->fatfs_dir);
    free(dir);
    return fatfs_rc_to_errno(r);
}

/* -------------------------------------------------------------------- */
/* hal_ff_* — direct FatFs forwarders for FILES / COPY / KILL / NAME    */
/* / CHDIR / CWD$().                                                     */
/* -------------------------------------------------------------------- */
FRESULT hal_ff_findfirst(DIR *dp, FILINFO *fi, const TCHAR *path,
                         const TCHAR *pattern) {
    return f_findfirst(dp, fi, path, pattern);
}

FRESULT hal_ff_findnext(DIR *dp, FILINFO *fi) { return f_findnext(dp, fi); }
FRESULT hal_ff_closedir(DIR *dp)              { return f_closedir(dp); }
FRESULT hal_ff_unlink  (const TCHAR *path)    { return f_unlink(path); }
FRESULT hal_ff_chdir   (const TCHAR *path)    { return f_chdir(path); }
FRESULT hal_ff_getcwd  (TCHAR *buf, UINT len) { return f_getcwd(buf, len); }
