#ifndef HOST_FS_H
#define HOST_FS_H

/*
 * POSIX-backed filesystem helpers for the host REPL / simulator.
 * Kept isolated from MMBasic_Includes.h so POSIX's DIR / dirent / setmode
 * symbols don't collide with FatFS's and MMBasic's own declarations.
 *
 * All paths passed in are expected to be already resolved (absolute or
 * relative to cwd) — the REPL's path-resolve step lives in the stubs.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*host_fs_emit_line)(const char *name);

/* List entries of `dir`, filtering by glob `pattern`. For each match,
 * call `emit(name)`. Returns 0 on success, -1 on error. */
int host_fs_list_dir(const char *dir, const char *pattern, host_fs_emit_line emit);

/* Stateful directory walker — the iteration primitives FileIO.c's cmd_files
 * / cmd_copy / cmd_kill / fun_dir need. Opaque because POSIX DIR clashes
 * with FatFS's DIR typedef; opening an opaque handle lets this interface
 * live in the same translation unit as any caller that includes ff.h.
 *
 * host_fs_walk_open:  returns a walker or NULL on opendir() failure.
 * host_fs_walk_next:  fills caller-provided scalars, returns 1 if an
 *                     entry was produced, 0 at end of dir. Filters by
 *                     the same case-insensitive glob as host_fs_list_dir.
 * host_fs_walk_close: releases the walker. Safe on NULL. */
typedef struct host_fs_walker host_fs_walker_t;
host_fs_walker_t *host_fs_walk_open(const char *dir, const char *pattern);
int host_fs_walk_next(host_fs_walker_t *w,
                      char *name_out, int name_cap,
                      int *is_dir_out,
                      unsigned long long *size_out,
                      long long *mtime_epoch_out);
void host_fs_walk_close(host_fs_walker_t *w);

/* Whole-path operations. Each returns 0 on success, -1 on error. Used by
 * the host f_unlink / f_rename / f_mkdir / f_chdir / f_getcwd wrappers
 * in host_stubs_legacy.c when host_sd_root is set. Paths are absolute
 * or relative-to-cwd — the caller has already joined host_sd_root. */
int host_fs_unlink(const char *path);
int host_fs_rename(const char *from, const char *to);
int host_fs_mkdir(const char *path);
int host_fs_rmdir(const char *path);
int host_fs_chdir(const char *path);
int host_fs_getcwd(char *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif
