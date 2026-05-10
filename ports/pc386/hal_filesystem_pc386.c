/*
 * ports/pc386/hal_filesystem_pc386.c — POSIX-style FS HAL over FatFs.
 *
 * Stage 3 maps the HAL surface onto FatFs's f_open/f_read/f_close
 * family. The Stage 2 mounts of A: and C: stand; this layer is the
 * adapter that turns hal_fs_open("A:\PROG.BAS", HAL_FS_O_RDONLY,...)
 * into f_open(&FIL, "A:\PROG.BAS", FA_READ).
 *
 * Placeholder for sub-stage 3a — every entry panics. Real impl in 3f.
 */

#include "hal/hal_filesystem.h"
#include "pc386_panic.h"

int     hal_fs_open (const char *path, int flags, hal_fs_fd_t *out)
{ (void)path; (void)flags; (void)out; pc386_panic("hal_fs_open not yet implemented (3f)"); }

int     hal_fs_close(hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_close not yet implemented (3f)"); }

ssize_t hal_fs_read (hal_fs_fd_t fd, void *buf, size_t n)
{ (void)fd; (void)buf; (void)n; pc386_panic("hal_fs_read not yet implemented (3f)"); }

ssize_t hal_fs_write(hal_fs_fd_t fd, const void *buf, size_t n)
{ (void)fd; (void)buf; (void)n; pc386_panic("hal_fs_write not yet implemented (3f)"); }

int     hal_fs_getc (hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_getc not yet implemented (3f)"); }

int     hal_fs_putc (hal_fs_fd_t fd, char c)
{ (void)fd; (void)c; pc386_panic("hal_fs_putc not yet implemented (3f)"); }

off_t   hal_fs_seek (hal_fs_fd_t fd, off_t off, int whence)
{ (void)fd; (void)off; (void)whence; pc386_panic("hal_fs_seek not yet implemented (3f)"); }

off_t   hal_fs_tell (hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_tell not yet implemented (3f)"); }

int     hal_fs_eof  (hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_eof not yet implemented (3f)"); }

int     hal_fs_sync (hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_sync not yet implemented (3f)"); }

off_t   hal_fs_size (hal_fs_fd_t fd)
{ (void)fd; pc386_panic("hal_fs_size not yet implemented (3f)"); }

int     hal_fs_unlink(const char *path)
{ (void)path; pc386_panic("hal_fs_unlink not yet implemented (3f)"); }

int     hal_fs_rename(const char *from, const char *to)
{ (void)from; (void)to; pc386_panic("hal_fs_rename not yet implemented (3f)"); }

int     hal_fs_mkdir (const char *path)
{ (void)path; pc386_panic("hal_fs_mkdir not yet implemented (3f)"); }

int     hal_fs_rmdir (const char *path)
{ (void)path; pc386_panic("hal_fs_rmdir not yet implemented (3f)"); }

int     hal_fs_chdir (const char *path)
{ (void)path; pc386_panic("hal_fs_chdir not yet implemented (3f)"); }

char   *hal_fs_getcwd(char *buf, size_t n)
{ (void)buf; (void)n; pc386_panic("hal_fs_getcwd not yet implemented (3f)"); }

int     hal_fs_stat  (const char *path, struct hal_stat *out)
{ (void)path; (void)out; pc386_panic("hal_fs_stat not yet implemented (3f)"); }

int hal_fs_dir_open (const char *path, hal_fs_dir_t **out)
{ (void)path; (void)out; pc386_panic("hal_fs_dir_open not yet implemented (3f)"); }

int hal_fs_dir_next (hal_fs_dir_t *dir, struct hal_dirent *out)
{ (void)dir; (void)out; pc386_panic("hal_fs_dir_next not yet implemented (3f)"); }

int hal_fs_dir_close(hal_fs_dir_t *dir)
{ (void)dir; pc386_panic("hal_fs_dir_close not yet implemented (3f)"); }
