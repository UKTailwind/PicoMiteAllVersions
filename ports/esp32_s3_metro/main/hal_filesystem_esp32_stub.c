/*
 * hal_filesystem_esp32_stub.c — Phase B stub for hal/hal_filesystem.h.
 * Every fs operation returns "not implemented" / EOF. Phase E replaces
 * with FATFS-via-VFS at /sd.
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "hal/hal_filesystem.h"

int     hal_fs_open(const char *p, int f, hal_fs_fd_t *out) { (void)p; (void)f; (void)out; return -1; }
int     hal_fs_close(hal_fs_fd_t fd) { (void)fd; return -1; }
ssize_t hal_fs_read(hal_fs_fd_t fd, void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }
ssize_t hal_fs_write(hal_fs_fd_t fd, const void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }
int     hal_fs_getc(hal_fs_fd_t fd) { (void)fd; return -1; }
int     hal_fs_putc(hal_fs_fd_t fd, char c) { (void)fd; (void)c; return -1; }
off_t   hal_fs_seek(hal_fs_fd_t fd, off_t off, int w) { (void)fd; (void)off; (void)w; return -1; }
off_t   hal_fs_tell(hal_fs_fd_t fd) { (void)fd; return -1; }
int     hal_fs_eof(hal_fs_fd_t fd) { (void)fd; return 1; }
int     hal_fs_sync(hal_fs_fd_t fd) { (void)fd; return -1; }
off_t   hal_fs_size(hal_fs_fd_t fd) { (void)fd; return 0; }
int     hal_fs_unlink(const char *p) { (void)p; return -1; }
int     hal_fs_rename(const char *f, const char *t) { (void)f; (void)t; return -1; }
int     hal_fs_mkdir(const char *p) { (void)p; return -1; }
int     hal_fs_rmdir(const char *p) { (void)p; return -1; }
int     hal_fs_chdir(const char *p) { (void)p; return -1; }
char   *hal_fs_getcwd(char *b, size_t n) { if (n > 0 && b) b[0] = 0; return b; }
int     hal_fs_stat(const char *p, struct hal_stat *out) { (void)p; (void)out; return -1; }
int     hal_fs_dir_open(const char *p, hal_fs_dir_t **out) { (void)p; (void)out; return -1; }
int     hal_fs_dir_next(hal_fs_dir_t *d, struct hal_dirent *o) { (void)d; (void)o; return -1; }
int     hal_fs_dir_close(hal_fs_dir_t *d) { (void)d; return -1; }
