/*
 * ports/pico_sdk_common/hal_filesystem_pico.c — hal_filesystem dispatcher
 * for device builds.
 *
 * The PicoMite device exposes two filesystems:
 *   A:   internal flash via littlefs (lfs_* ops, backed by fs_flash_*
 *        in FileIO.c, cfg in pico_lfs_cfg)
 *   B:   SD card via FatFS (f_* ops, backed by disk_* in mmc_stm32.c)
 *
 * (The naming is a historical PicoMite convention — A: is the on-chip
 * flash, B: is the removable SD card. Confirmed by the filepath[]
 * initialiser in FileIO.c: FatFSFileSystem==0 → A: → LFS,
 *                         FatFSFileSystem==1 → B: → FatFS.)
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

/* Choose which filesystem owns a given path. "A:" -> LFS, "B:" -> FatFS.
 * Unprefixed paths default to LFS (the historical boot drive). */
typedef enum { FS_FATFS, FS_LFS } fs_kind_t;

static fs_kind_t path_fs(const char *path)
{
    if (path && (path[0] == 'B' || path[0] == 'b') && path[1] == ':') return FS_FATFS;
    return FS_LFS;
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
    /* LFS has no chdir — probe existence via lfs_dir_open and caller
     * (mmbasic_chdir) keeps track of the cwd in filepath[]. FatFS's
     * f_chdir wants a path without the MMBasic drive prefix (FatFS on
     * both host and device is mounted with the default drive "" = 0:,
     * so a literal "B:" or "A:" confuses it). Strip the drive letter
     * before dispatching to either backend. */
    if (path_fs(path) == FS_LFS) {
        const char *pp = path_after_drive(path);
        if (!*pp) pp = "/";
        lfs_dir_t d;
        int r = lfs_dir_open(&lfs, &d, pp);
        if (r == 0) lfs_dir_close(&lfs, &d);
        return lfs_rc_to_errno(r);
    }
    const char *pp = path_after_drive(path);
    if (!*pp) pp = "/";
    return fatfs_rc_to_errno(f_chdir(pp));
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
    if (fno.fattrib & (AM_HID | AM_SYS)) out->mode |= HAL_FS_S_IFHIDDEN;
    out->mtime_us = 0;
    return 0;
}

/* -- File-table ops --------------------------------------------------- */

#include <stdlib.h>
extern DWORD get_fattime(void);
extern void *GetMemory(int msize);
extern void FreeMemory(unsigned char *addr);

/* HAL_FS_MAX_OPEN slots. Sized a few above MAXOPENFILES (=10 on device)
 * so callers that open transient HAL-only fds (no FileTable slot) don't
 * exhaust the table before MMBasic's own file-number capacity. */
#define HAL_FS_MAX_OPEN 16

typedef struct {
    fs_kind_t kind;
    union {
        FIL        *fatfs;
        lfs_file_t *lfs;
    } h;
    int writable;
    char *lfs_path;  /* non-NULL for writable LFS slots — used to re-set 'A' xattr on sync/close */
} pico_fs_slot_t;

static pico_fs_slot_t pico_fs_slots[HAL_FS_MAX_OPEN];

static int alloc_slot(void)
{
    for (int i = 0; i < HAL_FS_MAX_OPEN; ++i) {
        if (pico_fs_slots[i].h.fatfs == NULL && pico_fs_slots[i].h.lfs == NULL) return i;
    }
    return -1;
}

static pico_fs_slot_t *slot_from_fd(hal_fs_fd_t fd)
{
    if (fd < 1 || fd > HAL_FS_MAX_OPEN) return NULL;
    pico_fs_slot_t *s = &pico_fs_slots[fd - 1];
    if (s->h.fatfs == NULL && s->h.lfs == NULL) return NULL;
    return s;
}

static BYTE hal_flags_to_fatfs(int flags)
{
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

static int hal_flags_to_lfs(int flags)
{
    int m = 0;
    if ((flags & HAL_FS_O_RDWR) == HAL_FS_O_RDWR) m |= LFS_O_RDWR;
    else if (flags & HAL_FS_O_WRONLY)             m |= LFS_O_WRONLY;
    else                                          m |= LFS_O_RDONLY;
    if (flags & HAL_FS_O_CREAT) m |= LFS_O_CREAT;
    if (flags & HAL_FS_O_TRUNC) m |= LFS_O_TRUNC;
    if (flags & HAL_FS_O_EXCL)  m |= LFS_O_EXCL;
    /* LFS_O_APPEND forces every write to the tail; MMBasic's R+W+APPEND
     * combo wants "open at EOF, random-access R/W after" so we emulate
     * that via explicit lfs_file_seek. Wonly+APPEND can use LFS_O_APPEND
     * directly — every write goes to EOF. */
    if ((flags & HAL_FS_O_APPEND) && !(m & LFS_O_RDWR))
        m |= LFS_O_APPEND;
    return m;
}

/* Write the MMBasic 'A' (modification-time) xattr on LFS. MMBasic uses
 * lfs xattrs to track mtime because LFS has no native mtime. */
static void lfs_stamp_atime(const char *path)
{
    DWORD dt = get_fattime();
    (void)lfs_setattr(&lfs, path, 'A', &dt, 4);
}

int hal_fs_open(const char *path, int flags, hal_fs_fd_t *out)
{
    if (!path || !out) return -EINVAL;
    int idx = alloc_slot();
    if (idx < 0) return -EMFILE;
    pico_fs_slot_t *s = &pico_fs_slots[idx];
    memset(s, 0, sizeof(*s));
    s->kind = path_fs(path);
    const int want_write = (flags & (HAL_FS_O_WRONLY | HAL_FS_O_RDWR)) != 0;
    s->writable = want_write;

    if (s->kind == FS_LFS) {
        const char *lfs_path = path_after_drive(path);
        if (!*lfs_path) lfs_path = "/";
        lfs_file_t *fh = (lfs_file_t *)GetMemory(sizeof(lfs_file_t));
        if (!fh) return -ENOMEM;
        int lfsmode = hal_flags_to_lfs(flags);
        /* Original FileIO.c cleared the stale 'A' xattr before re-opening
         * for write. Preserve that. */
        if (want_write) {
            struct lfs_info probe;
            if (lfs_stat(&lfs, lfs_path, &probe) >= 0)
                (void)lfs_removeattr(&lfs, lfs_path, 'A');
        }
        int r = lfs_file_open(&lfs, fh, lfs_path, lfsmode);
        if (r < 0) { FreeMemory((unsigned char *)fh); return lfs_rc_to_errno(r); }
        s->h.lfs = fh;
        if (want_write) {
            size_t plen = strlen(lfs_path) + 1;
            s->lfs_path = (char *)GetMemory(plen);
            if (s->lfs_path) memcpy(s->lfs_path, lfs_path, plen);
            lfs_stamp_atime(lfs_path);
            /* R+W+APPEND: position at EOF, then allow random access. */
            if ((flags & HAL_FS_O_APPEND) && (lfsmode & LFS_O_RDWR))
                lfs_file_seek(&lfs, fh, lfs_file_size(&lfs, fh), LFS_SEEK_SET);
            lfs_file_sync(&lfs, fh);
        }
        *out = idx + 1;
        return 0;
    }

    const char *ff_path = path_after_drive(path);
    if (!*ff_path) ff_path = "/";
    FIL *fp = (FIL *)GetMemory(sizeof(FIL));
    if (!fp) return -ENOMEM;
    BYTE ffmode = hal_flags_to_fatfs(flags);
    FRESULT r = f_open(fp, ff_path, ffmode);
    if (r != FR_OK) { FreeMemory((unsigned char *)fp); return fatfs_rc_to_errno(r); }
    s->h.fatfs = fp;
    *out = idx + 1;
    return 0;
}

int hal_fs_close(hal_fs_fd_t fd)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    int rc = 0;
    if (s->kind == FS_LFS) {
        if (s->writable && s->lfs_path) lfs_stamp_atime(s->lfs_path);
        int r = lfs_file_close(&lfs, s->h.lfs);
        rc = lfs_rc_to_errno(r);
        FreeMemory((unsigned char *)s->h.lfs);
        if (s->lfs_path) FreeMemory((unsigned char *)s->lfs_path);
    } else {
        FRESULT r = f_close(s->h.fatfs);
        rc = fatfs_rc_to_errno(r);
        FreeMemory((unsigned char *)s->h.fatfs);
    }
    memset(s, 0, sizeof(*s));
    return rc;
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void *buf, size_t n)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (!buf) return -EINVAL;
    if (s->kind == FS_LFS) {
        int r = lfs_file_read(&lfs, s->h.lfs, buf, n);
        if (r < 0) return lfs_rc_to_errno(r);
        return r;
    }
    UINT bw = 0;
    FRESULT r = f_read(s->h.fatfs, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (!buf) return -EINVAL;
    if (s->kind == FS_LFS) {
        int r = lfs_file_write(&lfs, s->h.lfs, buf, n);
        if (r < 0) return lfs_rc_to_errno(r);
        return r;
    }
    UINT bw = 0;
    FRESULT r = f_write(s->h.fatfs, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

off_t hal_fs_seek(hal_fs_fd_t fd, off_t off, int whence)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->kind == FS_LFS) {
        int lw = (whence == HAL_FS_SEEK_CUR) ? LFS_SEEK_CUR
                : (whence == HAL_FS_SEEK_END) ? LFS_SEEK_END
                :                               LFS_SEEK_SET;
        int r = lfs_file_seek(&lfs, s->h.lfs, off, lw);
        if (r < 0) return lfs_rc_to_errno(r);
        return r;
    }
    FRESULT r;
    if (whence == HAL_FS_SEEK_SET)      r = f_lseek(s->h.fatfs, off);
    else if (whence == HAL_FS_SEEK_CUR) r = f_lseek(s->h.fatfs, f_tell(s->h.fatfs) + off);
    else                                r = f_lseek(s->h.fatfs, f_size(s->h.fatfs) + off);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (off_t)f_tell(s->h.fatfs);
}

off_t hal_fs_tell(hal_fs_fd_t fd)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->kind == FS_LFS) return (off_t)lfs_file_tell(&lfs, s->h.lfs);
    return (off_t)f_tell(s->h.fatfs);
}

int hal_fs_eof(hal_fs_fd_t fd)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->kind == FS_LFS) {
        return lfs_file_tell(&lfs, s->h.lfs) == lfs_file_size(&lfs, s->h.lfs) ? 1 : 0;
    }
    return f_eof(s->h.fatfs) ? 1 : 0;
}

int hal_fs_sync(hal_fs_fd_t fd)
{
    pico_fs_slot_t *s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->kind == FS_LFS) {
        if (s->writable && s->lfs_path) lfs_stamp_atime(s->lfs_path);
        return lfs_rc_to_errno(lfs_file_sync(&lfs, s->h.lfs));
    }
    return fatfs_rc_to_errno(f_sync(s->h.fatfs));
}


/* Directory iteration — holds either a FatFS DIR* (SD) or an LFS
 * lfs_dir_t* (internal flash) plus the base path so hal_fs_dir_next
 * can reconstruct full paths for stat purposes. */
#include <stdlib.h>
struct hal_fs_dir {
    fs_kind_t kind;
    union {
        DIR      fatfs;
        lfs_dir_t lfs;
    } h;
    char path[256];
};

int hal_fs_dir_open(const char *path, hal_fs_dir_t **out)
{
    if (!path || !out) return -EINVAL;
    hal_fs_dir_t *d = (hal_fs_dir_t *)calloc(1, sizeof(*d));
    if (!d) return -ENOMEM;
    d->kind = path_fs(path);
    strncpy(d->path, path, sizeof(d->path) - 1);
    int r;
    if (d->kind == FS_LFS) {
        r = lfs_rc_to_errno(lfs_dir_open(&lfs, &d->h.lfs, path_after_drive(path)));
    } else {
        r = fatfs_rc_to_errno(f_opendir(&d->h.fatfs, path));
    }
    if (r < 0) { free(d); return r; }
    *out = d;
    return 0;
}

int hal_fs_dir_next(hal_fs_dir_t *dir, struct hal_dirent *out)
{
    if (!dir || !out) return -EINVAL;
    if (dir->kind == FS_LFS) {
        struct lfs_info info;
        int r = lfs_dir_read(&lfs, &dir->h.lfs, &info);
        if (r == 0) return 0;  /* end */
        if (r < 0) return lfs_rc_to_errno(r);
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            return hal_fs_dir_next(dir, out);
        }
        strncpy(out->name, info.name, sizeof(out->name) - 1);
        out->name[sizeof(out->name) - 1] = '\0';
        out->size = info.size;
        out->mode = (info.type == LFS_TYPE_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
        return 1;
    }
    FILINFO fno;
    FRESULT r = f_readdir(&dir->h.fatfs, &fno);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    if (fno.fname[0] == 0) return 0;
    strncpy(out->name, fno.fname, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->size = fno.fsize;
    out->mode = (fno.fattrib & AM_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
    if (fno.fattrib & (AM_HID | AM_SYS)) out->mode |= HAL_FS_S_IFHIDDEN;
    return 1;
}

int hal_fs_dir_close(hal_fs_dir_t *dir)
{
    if (!dir) return -EINVAL;
    int r;
    if (dir->kind == FS_LFS) {
        r = lfs_rc_to_errno(lfs_dir_close(&lfs, &dir->h.lfs));
    } else {
        r = fatfs_rc_to_errno(f_closedir(&dir->h.fatfs));
    }
    free(dir);
    return r;
}
