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
#include <sys/stat.h>

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

/* Strip "A:" / "B:" drive prefix before dispatching. Host FatFS is
 * mounted with the default empty drive name (= drive 0), so a literal
 * "B:..." path confuses FatFS's drive-letter parser. The single-backend
 * host build treats both MMBasic drives as the same FatFS / POSIX tree
 * (the test harness RAM disk, or the user's real cwd under host_sd_root). */
static const char *host_path_after_drive(const char *p)
{
    if (p && p[0] && p[1] == ':') return p + 2;
    return p;
}

int hal_fs_unlink(const char *path)           { return fatfs_rc_to_errno(host_f_unlink(host_path_after_drive(path))); }
int hal_fs_rename(const char *from, const char *to) { return fatfs_rc_to_errno(host_f_rename(host_path_after_drive(from), host_path_after_drive(to))); }
int hal_fs_mkdir (const char *path)           { return fatfs_rc_to_errno(host_f_mkdir(host_path_after_drive(path))); }
int hal_fs_rmdir (const char *path)           { return fatfs_rc_to_errno(host_f_unlink(host_path_after_drive(path))); }
int hal_fs_chdir (const char *path)
{
    const char *pp = host_path_after_drive(path);
    if (!pp || !*pp) pp = "/";
    return fatfs_rc_to_errno(host_f_chdir(pp));
}

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
    if (fno.fattrib & (AM_HID | AM_SYS)) out->mode |= HAL_FS_S_IFHIDDEN;
    out->mtime_us = 0;
    return 0;
}

/* -------------------------------------------------------------------- */
/* File-table ops — host slot table. Dispatches to POSIX fopen/fread/...
 * when host_sd_root is configured (REPL / --sim mode), and to the
 * vendored FatFS running against vm_host_fat's RAM disk otherwise
 * (test-harness mode). Self-contained: no dependence on FileIO.c's
 * FileTable[] or on host_fs_posix_*. Legacy FileIO.c callers keep
 * their existing paths until they're migrated to hal_fs_*. */
/* -------------------------------------------------------------------- */

extern const char *host_sd_root;

#define HAL_FS_MAX_OPEN 16

typedef struct {
    int is_posix;
    FILE *fp;
    FIL  *fatfs;
    size_t posix_size;   /* cached at open for lof() */
} host_fs_slot_t;

static host_fs_slot_t host_fs_slots[HAL_FS_MAX_OPEN];

static int host_alloc_slot(void)
{
    for (int i = 0; i < HAL_FS_MAX_OPEN; ++i) {
        if (!host_fs_slots[i].fp && !host_fs_slots[i].fatfs) return i;
    }
    return -1;
}

static host_fs_slot_t *host_slot_from_fd(hal_fs_fd_t fd)
{
    if (fd < 1 || fd > HAL_FS_MAX_OPEN) return NULL;
    host_fs_slot_t *s = &host_fs_slots[fd - 1];
    if (!s->fp && !s->fatfs) return NULL;
    return s;
}

/* Host mounts FatFS with the default empty drive (see vm_host_fat.c).
 * Strip MMBasic's "A:"/"B:" prefix before dispatch. */
static const char *host_hal_path(const char *p)
{
    if (p && p[0] && p[1] == ':') return p + 2;
    return p;
}

static const char *hal_flags_to_fopen_mode(int flags)
{
    int rw = flags & HAL_FS_O_RDWR;
    if (flags & HAL_FS_O_APPEND) {
        if (rw == HAL_FS_O_RDWR) return "a+b";
        return "ab";
    }
    if (flags & HAL_FS_O_TRUNC) {
        if (rw == HAL_FS_O_RDWR) return "w+b";
        return "wb";
    }
    if (rw == HAL_FS_O_RDWR) return (flags & HAL_FS_O_CREAT) ? "a+b" : "r+b";
    if (flags & HAL_FS_O_WRONLY) return (flags & HAL_FS_O_CREAT) ? "wb" : "r+b";
    return "rb";
}

static BYTE hal_flags_to_fatfs_host(int flags)
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

int hal_fs_open(const char *path, int flags, hal_fs_fd_t *out)
{
    if (!path || !out) return -EINVAL;
    int idx = host_alloc_slot();
    if (idx < 0) return -EMFILE;
    host_fs_slot_t *s = &host_fs_slots[idx];
    memset(s, 0, sizeof(*s));

    if (host_sd_root) {
        /* POSIX-backed. MMBasic "A:/foo" or "/foo" are both drive-rooted
         * paths that map to $host_sd_root/foo, NOT real POSIX /foo.
         * Strip the drive prefix and the leading '/', then join. */
        char rp[FF_MAX_LFN];
        const char *bare = host_hal_path(path);
        while (bare[0] == '/') bare++;
        size_t rl = strlen(host_sd_root);
        int need_sep = (rl > 0 && host_sd_root[rl - 1] != '/');
        snprintf(rp, sizeof(rp), "%s%s%s", host_sd_root, need_sep ? "/" : "", bare);
        struct stat st;
        if (stat(rp, &st) == 0) s->posix_size = (size_t)st.st_size;
        const char *m = hal_flags_to_fopen_mode(flags);
        FILE *fp = fopen(rp, m);
        if (!fp) return -errno;
        s->is_posix = 1;
        s->fp = fp;
        *out = idx + 1;
        return 0;
    }

    /* FatFS RAM-disk. */
    FIL *fp = (FIL *)calloc(1, sizeof(FIL));
    if (!fp) return -ENOMEM;
    FRESULT r = f_open(fp, host_hal_path(path), hal_flags_to_fatfs_host(flags));
    if (r != FR_OK) { free(fp); return fatfs_rc_to_errno(r); }
    s->fatfs = fp;
    *out = idx + 1;
    return 0;
}

int hal_fs_close(hal_fs_fd_t fd)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    int rc = 0;
    if (s->is_posix) {
        if (fclose(s->fp) != 0) rc = -EIO;
    } else {
        FRESULT r = f_close(s->fatfs);
        rc = fatfs_rc_to_errno(r);
        free(s->fatfs);
    }
    memset(s, 0, sizeof(*s));
    return rc;
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void *buf, size_t n)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s || !buf) return -EBADF;
    if (s->is_posix) {
        size_t got = fread(buf, 1, n, s->fp);
        if (got == 0 && ferror(s->fp)) return -EIO;
        return (ssize_t)got;
    }
    UINT bw = 0;
    FRESULT r = f_read(s->fatfs, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s || !buf) return -EBADF;
    if (s->is_posix) {
        size_t w = fwrite(buf, 1, n, s->fp);
        if (w != n) return -EIO;
        return (ssize_t)w;
    }
    UINT bw = 0;
    FRESULT r = f_write(s->fatfs, buf, (UINT)n, &bw);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (ssize_t)bw;
}

off_t hal_fs_seek(hal_fs_fd_t fd, off_t off, int whence)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->is_posix) {
        int w = (whence == HAL_FS_SEEK_CUR) ? SEEK_CUR
              : (whence == HAL_FS_SEEK_END) ? SEEK_END : SEEK_SET;
        if (fseek(s->fp, (long)off, w) != 0) return -EIO;
        return (off_t)ftell(s->fp);
    }
    FRESULT r;
    if (whence == HAL_FS_SEEK_SET)      r = f_lseek(s->fatfs, off);
    else if (whence == HAL_FS_SEEK_CUR) r = f_lseek(s->fatfs, f_tell(s->fatfs) + off);
    else                                r = f_lseek(s->fatfs, f_size(s->fatfs) + off);
    if (r != FR_OK) return fatfs_rc_to_errno(r);
    return (off_t)f_tell(s->fatfs);
}

off_t hal_fs_tell(hal_fs_fd_t fd)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->is_posix) return (off_t)ftell(s->fp);
    return (off_t)f_tell(s->fatfs);
}

int hal_fs_eof(hal_fs_fd_t fd)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->is_posix) {
        int c = fgetc(s->fp);
        if (c == EOF) return 1;
        ungetc(c, s->fp);
        return 0;
    }
    return f_eof(s->fatfs) ? 1 : 0;
}

int hal_fs_sync(hal_fs_fd_t fd)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->is_posix) { fflush(s->fp); return 0; }
    return fatfs_rc_to_errno(f_sync(s->fatfs));
}

off_t hal_fs_size(hal_fs_fd_t fd)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) return -EBADF;
    if (s->is_posix) return (off_t)s->posix_size;
    return (off_t)f_size(s->fatfs);
}

/* Migration helper — see ports/pico_sdk_common/hal_filesystem_pico.c.
 * For POSIX-backed fds the host adapter returns the cached FILE*; the
 * stub FIL* that external callers want on FileTable[].fptr is owned by
 * BasicFileOpen (it pre-allocates a FIL and seeds obj.objsize from
 * hal_fs_size). is_lfs_out encodes: 0 = FatFS, 1 = LFS (device only),
 * 2 = POSIX FILE* (host REPL/--sim). */
void *hal_fs_peek_handle(hal_fs_fd_t fd, int *is_lfs_out)
{
    host_fs_slot_t *s = host_slot_from_fd(fd);
    if (!s) { if (is_lfs_out) *is_lfs_out = 0; return NULL; }
    if (s->is_posix) { if (is_lfs_out) *is_lfs_out = 2; return s->fp; }
    if (is_lfs_out) *is_lfs_out = 0;
    return s->fatfs;
}

/* Directory iteration — host uses FatFS f_opendir/readdir/closedir
 * which route through host_fs_shims.c to either real POSIX (when
 * host_sd_root is set) or vm_host_fat's RAM disk (test-harness mode).
 * Avoids pulling <dirent.h> which collides with ff.h's `DIR` typedef. */
struct hal_fs_dir {
    DIR fatfs_dir;
};

int hal_fs_dir_open(const char *path, hal_fs_dir_t **out)
{
    if (!path || !out) return -EINVAL;
    hal_fs_dir_t *d = (hal_fs_dir_t *)calloc(1, sizeof(*d));
    if (!d) return -ENOMEM;
    FRESULT r = f_opendir(&d->fatfs_dir, path);
    if (r != FR_OK) { free(d); return fatfs_rc_to_errno(r); }
    *out = d;
    return 0;
}

int hal_fs_dir_next(hal_fs_dir_t *dir, struct hal_dirent *out)
{
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

int hal_fs_dir_close(hal_fs_dir_t *dir)
{
    if (!dir) return -EINVAL;
    FRESULT r = f_closedir(&dir->fatfs_dir);
    free(dir);
    return fatfs_rc_to_errno(r);
}
