/*
 * hal_filesystem_esp32.c — hal_filesystem dispatcher for the
 * ESP32-S3 port. Models ports/pico_sdk_common/hal_filesystem_pico.c.
 *
 * Drive layout:
 *   A:   LittleFS on the "lfsdata" flash partition.
 *   B:   reserved for FatFS-over-SD; not present on this board, so
 *        port_drive_check rejects B: at the BASIC layer before any
 *        hal_fs_* call lands here.
 *
 * Paths arrive WITH the "A:" / "B:" prefix from BasicFileOpen + cmd_files;
 * path_after_drive() strips it before handing off to lfs_*.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "lfs.h"
#include "hal/hal_filesystem.h"

extern lfs_t lfs;
extern int esp32_lfs_mount(void);

static int lfs_rc_to_errno(int r) {
    if (r >= 0) return r;
    switch (r) {
    case LFS_ERR_OK:
        return 0;
    case LFS_ERR_IO:
        return -EIO;
    case LFS_ERR_CORRUPT:
        return -EIO;
    case LFS_ERR_NOENT:
        return -ENOENT;
    case LFS_ERR_EXIST:
        return -EEXIST;
    case LFS_ERR_NOTDIR:
        return -ENOTDIR;
    case LFS_ERR_ISDIR:
        return -EISDIR;
    case LFS_ERR_NOTEMPTY:
        return -ENOTEMPTY;
    case LFS_ERR_BADF:
        return -EBADF;
    case LFS_ERR_FBIG:
        return -EFBIG;
    case LFS_ERR_INVAL:
        return -EINVAL;
    case LFS_ERR_NOSPC:
        return -ENOSPC;
    case LFS_ERR_NOMEM:
        return -ENOMEM;
    default:
        return -EIO;
    }
}

/* Strip "A:" / "B:" so lfs_* sees path-only. LFS accepts a leading "/". */
static const char * path_after_drive(const char * path) {
    if (path && path[0] && path[1] == ':') return path + 2;
    return path;
}

/* Open-file slot table. hal_fs_fd_t is `int`; we hand out 1-based indices.
 * MAXOPENFILES is MMBasic's per-fnbr cap; size to match so SaveContext +
 * MAXOPENFILES concurrent opens fits. */
#ifndef HAL_FS_ESP32_MAX_OPEN
#define HAL_FS_ESP32_MAX_OPEN 8
#endif

typedef struct {
    int in_use;
    lfs_file_t file;
    int writable;
    char * path; /* malloc'd; non-NULL on writable opens */
} esp_fs_slot_t;

static esp_fs_slot_t s_slots[HAL_FS_ESP32_MAX_OPEN];

static int alloc_slot(void) {
    for (int i = 0; i < HAL_FS_ESP32_MAX_OPEN; i++) {
        if (!s_slots[i].in_use) return i;
    }
    return -1;
}

static esp_fs_slot_t * slot_from_fd(hal_fs_fd_t fd) {
    if (fd < 1 || fd > HAL_FS_ESP32_MAX_OPEN) return NULL;
    esp_fs_slot_t * s = &s_slots[fd - 1];
    return s->in_use ? s : NULL;
}

static int hal_flags_to_lfs(int flags) {
    int m = 0;
    if ((flags & HAL_FS_O_RDWR) == HAL_FS_O_RDWR)
        m |= LFS_O_RDWR;
    else if (flags & HAL_FS_O_WRONLY)
        m |= LFS_O_WRONLY;
    else
        m |= LFS_O_RDONLY;
    if (flags & HAL_FS_O_CREAT) m |= LFS_O_CREAT;
    if (flags & HAL_FS_O_TRUNC) m |= LFS_O_TRUNC;
    if (flags & HAL_FS_O_EXCL) m |= LFS_O_EXCL;
    /* R+W+APPEND: BASIC wants "open at EOF, then random-access R/W".
     * LFS_O_APPEND would force every write to EOF, which breaks that.
     * For W-only+APPEND we can still use LFS_O_APPEND directly. */
    if ((flags & HAL_FS_O_APPEND) && !(m & LFS_O_RDWR))
        m |= LFS_O_APPEND;
    return m;
}

/* ---- file ops ---- */

int hal_fs_open(const char * path, int flags, hal_fs_fd_t * out) {
    if (!path || !out) return -EINVAL;
    if (esp32_lfs_mount() != 0) return -ENODEV;

    int idx = alloc_slot();
    if (idx < 0) return -EMFILE;
    esp_fs_slot_t * s = &s_slots[idx];
    memset(s, 0, sizeof(*s));

    const char * lfs_path = path_after_drive(path);
    if (!*lfs_path) lfs_path = "/";

    int lfsmode = hal_flags_to_lfs(flags);
    s->writable = (flags & (HAL_FS_O_WRONLY | HAL_FS_O_RDWR)) != 0;

    int r = lfs_file_open(&lfs, &s->file, lfs_path, lfsmode);
    if (r < 0) return lfs_rc_to_errno(r);

    if (s->writable) {
        size_t plen = strlen(lfs_path) + 1;
        s->path = malloc(plen);
        if (s->path) memcpy(s->path, lfs_path, plen);
        /* R+W+APPEND: LFS_O_APPEND was suppressed; position at EOF
         * manually so the first write goes to the tail and subsequent
         * seeks go where the caller wants. */
        if ((flags & HAL_FS_O_APPEND) && (lfsmode & LFS_O_RDWR))
            lfs_file_seek(&lfs, &s->file, lfs_file_size(&lfs, &s->file), LFS_SEEK_SET);
        lfs_file_sync(&lfs, &s->file);
    }

    s->in_use = 1;
    *out = idx + 1;
    return 0;
}

int hal_fs_close(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    int r = lfs_file_close(&lfs, &s->file);
    if (s->path) {
        free(s->path);
        s->path = NULL;
    }
    s->in_use = 0;
    return lfs_rc_to_errno(r);
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void * buf, size_t n) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (!buf) return -EINVAL;
    int r = lfs_file_read(&lfs, &s->file, buf, n);
    if (r < 0) return lfs_rc_to_errno(r);
    return r;
}

ssize_t hal_fs_write(hal_fs_fd_t fd, const void * buf, size_t n) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    if (!buf) return -EINVAL;
    int r = lfs_file_write(&lfs, &s->file, buf, n);
    if (r < 0) return lfs_rc_to_errno(r);
    return r;
}

int hal_fs_getc(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    unsigned char c;
    int r = lfs_file_read(&lfs, &s->file, &c, 1);
    if (r < 0) return lfs_rc_to_errno(r);
    if (r == 0) return -ENODATA;
    return c;
}

int hal_fs_putc(hal_fs_fd_t fd, char c) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    int r = lfs_file_write(&lfs, &s->file, &c, 1);
    if (r < 0) return lfs_rc_to_errno(r);
    if (r != 1) return -EIO;
    return 0;
}

off_t hal_fs_seek(hal_fs_fd_t fd, off_t off, int whence) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    int lw = (whence == HAL_FS_SEEK_CUR)   ? LFS_SEEK_CUR
             : (whence == HAL_FS_SEEK_END) ? LFS_SEEK_END
                                           : LFS_SEEK_SET;
    int r = lfs_file_seek(&lfs, &s->file, off, lw);
    if (r < 0) return lfs_rc_to_errno(r);
    return r;
}

off_t hal_fs_tell(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    return (off_t)lfs_file_tell(&lfs, &s->file);
}

int hal_fs_eof(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    return lfs_file_tell(&lfs, &s->file) == lfs_file_size(&lfs, &s->file) ? 1 : 0;
}

int hal_fs_sync(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    return lfs_rc_to_errno(lfs_file_sync(&lfs, &s->file));
}

off_t hal_fs_size(hal_fs_fd_t fd) {
    esp_fs_slot_t * s = slot_from_fd(fd);
    if (!s) return -EBADF;
    return (off_t)lfs_file_size(&lfs, &s->file);
}

/* ---- path ops via lfs_* ---- */

int hal_fs_unlink(const char * path) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    return lfs_rc_to_errno(lfs_remove(&lfs, path_after_drive(path)));
}

int hal_fs_rename(const char * from, const char * to) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    return lfs_rc_to_errno(lfs_rename(&lfs, path_after_drive(from), path_after_drive(to)));
}

int hal_fs_mkdir(const char * path) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    return lfs_rc_to_errno(lfs_mkdir(&lfs, path_after_drive(path)));
}

int hal_fs_rmdir(const char * path) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    return lfs_rc_to_errno(lfs_remove(&lfs, path_after_drive(path)));
}

int hal_fs_chdir(const char * path) {
    (void)path;
    return 0;
}

char * hal_fs_getcwd(char * buf, size_t n) {
    if (n) buf[0] = '/';
    if (n > 1) buf[1] = 0;
    return buf;
}

int hal_fs_stat(const char * path, struct hal_stat * out) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    struct lfs_info info;
    int r = lfs_stat(&lfs, path_after_drive(path), &info);
    if (r < 0) return lfs_rc_to_errno(r);
    if (out) {
        out->size = info.size;
        out->mode = (info.type == LFS_TYPE_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
        out->mtime_us = 0;
    }
    return 0;
}

/* ---- directory iteration ---- */

struct hal_fs_dir {
    lfs_dir_t dir;
};

int hal_fs_dir_open(const char * path, hal_fs_dir_t ** out) {
    if (esp32_lfs_mount() != 0) return -ENODEV;
    hal_fs_dir_t * h = malloc(sizeof *h);
    if (!h) return -ENOMEM;
    int r = lfs_dir_open(&lfs, &h->dir, path_after_drive(path));
    if (r < 0) {
        free(h);
        return lfs_rc_to_errno(r);
    }
    *out = h;
    return 0;
}

int hal_fs_dir_next(hal_fs_dir_t * dir, struct hal_dirent * out) {
    if (!dir || !out) return -EINVAL;
    for (;;) {
        struct lfs_info info;
        int r = lfs_dir_read(&lfs, &dir->dir, &info);
        if (r < 0) return lfs_rc_to_errno(r);
        if (r == 0) return 0; /* end of stream */
        /* Skip "." and ".." — the BASIC FILES contract is "real entries". */
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
            continue;
        snprintf(out->name, sizeof out->name, "%s", info.name);
        out->mode = (info.type == LFS_TYPE_DIR) ? HAL_FS_S_IFDIR : HAL_FS_S_IFREG;
        out->size = info.size;
        return 1;
    }
}

int hal_fs_dir_close(hal_fs_dir_t * dir) {
    if (!dir) return 0;
    lfs_dir_close(&lfs, &dir->dir);
    free(dir);
    return 0;
}
