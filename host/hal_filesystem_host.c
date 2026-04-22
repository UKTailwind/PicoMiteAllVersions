/*
 * host/hal_filesystem_host.c — hal_filesystem over libc / POSIX for the
 * native + WASM hosts.
 *
 * No callers yet — this TU exists so the HAL headers compile and link
 * on host as the Phase 4 foundation. FileIO.c callers migrate in a
 * follow-up commit; until then this implementation is dead weight, but
 * it proves the contract maps cleanly to libc (the shape the plan
 * committed to in "hal_fs.h deliberately mirrors POSIX").
 *
 * Note: we undef any mmbasic_* POSIX renames introduced by
 * host_platform.h (timegm/chdir etc.) so we reach real libc here.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

/* host_platform.h renames libc chdir to mmbasic_chdir; undo locally
 * so hal_fs_chdir reaches real libc. */
#ifdef chdir
#undef chdir
#endif
#ifdef rename
#undef rename
#endif

#include "hal/hal_filesystem.h"

/* A small fd table. The HAL's hal_fs_fd_t is an index into this array;
 * slot 0 is reserved (unused) so 0 is never a valid fd. */
#define HAL_FS_HOST_MAX 32
static FILE *host_fs_slots[HAL_FS_HOST_MAX] = { NULL };

static int host_fs_alloc_slot(FILE *fp)
{
    for (int i = 1; i < HAL_FS_HOST_MAX; i++) {
        if (host_fs_slots[i] == NULL) {
            host_fs_slots[i] = fp;
            return i;
        }
    }
    return -1;
}

static FILE *host_fs_lookup(hal_fs_fd_t fd)
{
    if (fd < 1 || fd >= HAL_FS_HOST_MAX) return NULL;
    return host_fs_slots[fd];
}

static const char *mode_from_flags(int flags)
{
    bool rd = (flags & HAL_FS_O_RDONLY) || (flags & HAL_FS_O_RDWR);
    bool wr = (flags & HAL_FS_O_WRONLY) || (flags & HAL_FS_O_RDWR);
    bool trunc  = flags & HAL_FS_O_TRUNC;
    bool append = flags & HAL_FS_O_APPEND;
    bool creat  = flags & HAL_FS_O_CREAT;

    if (append && wr)          return rd ? "a+b" : "ab";
    if (trunc  && wr)          return rd ? "w+b" : "wb";
    if (creat  && wr && !rd)   return "wb";
    if (wr && !rd)             return "r+b";   /* open-existing-for-write: best libc match */
    if (wr && rd)              return "r+b";
    return "rb";
}

int hal_fs_open(const char *path, int flags, hal_fs_fd_t *out)
{
    if (!path || !out) return -EINVAL;
    FILE *fp = fopen(path, mode_from_flags(flags));
    if (!fp) return -errno;
    int fd = host_fs_alloc_slot(fp);
    if (fd < 0) { fclose(fp); return -EMFILE; }
    *out = fd;
    return 0;
}

int hal_fs_close(hal_fs_fd_t fd)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    host_fs_slots[fd] = NULL;
    return fclose(fp) == 0 ? 0 : -errno;
}

ssize_t hal_fs_read(hal_fs_fd_t fd, void *buf, size_t n)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    size_t got = fread(buf, 1, n, fp);
    if (got == 0 && ferror(fp)) return -EIO;
    return (ssize_t)got;
}

ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    size_t put = fwrite(buf, 1, n, fp);
    if (put != n) return -EIO;
    return (ssize_t)put;
}

off_t hal_fs_seek(hal_fs_fd_t fd, off_t off, int whence)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    int w = (whence == HAL_FS_SEEK_SET) ? SEEK_SET
          : (whence == HAL_FS_SEEK_CUR) ? SEEK_CUR
          : SEEK_END;
    if (fseek(fp, off, w) != 0) return -errno;
    return ftell(fp);
}

off_t hal_fs_tell(hal_fs_fd_t fd)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    return ftell(fp);
}

int hal_fs_eof(hal_fs_fd_t fd)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    return feof(fp) ? 1 : 0;
}

int hal_fs_sync(hal_fs_fd_t fd)
{
    FILE *fp = host_fs_lookup(fd);
    if (!fp) return -EBADF;
    return fflush(fp) == 0 ? 0 : -errno;
}

int hal_fs_unlink(const char *path) { return unlink(path) == 0 ? 0 : -errno; }
int hal_fs_rename(const char *from, const char *to) { return rename(from, to) == 0 ? 0 : -errno; }
int hal_fs_mkdir (const char *path) { return mkdir (path, 0777) == 0 ? 0 : -errno; }
int hal_fs_rmdir (const char *path) { return rmdir (path) == 0 ? 0 : -errno; }
int hal_fs_chdir (const char *path) { return chdir (path) == 0 ? 0 : -errno; }

char *hal_fs_getcwd(char *buf, size_t n) { return getcwd(buf, n); }

int hal_fs_stat(const char *path, struct hal_stat *out)
{
    struct stat st;
    if (stat(path, &st) != 0) return -errno;
    out->size     = st.st_size;
    out->mode     = S_ISDIR(st.st_mode) ? HAL_FS_S_IFDIR
                  : S_ISREG(st.st_mode) ? HAL_FS_S_IFREG : 0;
    out->mtime_us = (uint64_t)st.st_mtime * 1000000ULL;
    return 0;
}

/* Directory iteration. Hide DIR* + base path behind the opaque
 * hal_fs_dir_t shape. */
struct hal_fs_dir {
    DIR  *dp;
    char  path[512];
};

int hal_fs_dir_open(const char *path, hal_fs_dir_t **out)
{
    if (!path || !out) return -EINVAL;
    hal_fs_dir_t *d = (hal_fs_dir_t *)calloc(1, sizeof(*d));
    if (!d) return -ENOMEM;
    d->dp = opendir(path);
    if (!d->dp) { int e = errno; free(d); return -e; }
    strncpy(d->path, path, sizeof(d->path) - 1);
    *out = d;
    return 0;
}

int hal_fs_dir_next(hal_fs_dir_t *dir, struct hal_dirent *out)
{
    if (!dir || !dir->dp || !out) return -EINVAL;
    struct dirent *e;
    errno = 0;
    while ((e = readdir(dir->dp)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        strncpy(out->name, e->d_name, sizeof(out->name) - 1);
        out->name[sizeof(out->name) - 1] = '\0';

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir->path, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            out->mode = S_ISDIR(st.st_mode) ? HAL_FS_S_IFDIR
                      : S_ISREG(st.st_mode) ? HAL_FS_S_IFREG : 0;
            out->size = st.st_size;
        } else {
            out->mode = 0;
            out->size = 0;
        }
        return 1;
    }
    return errno ? -errno : 0;
}

int hal_fs_dir_close(hal_fs_dir_t *dir)
{
    if (!dir) return -EINVAL;
    int r = dir->dp ? closedir(dir->dp) : 0;
    free(dir);
    return r == 0 ? 0 : -errno;
}
