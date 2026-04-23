/*
 * hal/hal_fatfs_dispatch.h — FatFS-shaped directory + path ops with
 * backend-routed dispatch.
 *
 * MMBasic's FILES / COPY / KILL / NAME / CHDIR / CWD$() implementations
 * walk directories using the FatFS f_findfirst / f_findnext / f_closedir
 * / f_unlink / f_chdir / f_getcwd surface (DIR + FILINFO). On device the
 * call lands directly on the vendored ff.c. On host it has to route to
 * either the vm_host_fat in-memory disk (test harness) or a real POSIX
 * directory walker (REPL / --sim, when host_sd_root is set).
 *
 * Core (FileIO.c) calls hal_ff_* exclusively. Device impls in
 * ports/pico_sdk_common/cmd_files_hooks.c forward to FatFS f_*; host
 * impls in host/host_fs_shims.c (host_f_*) wrap the dispatch.
 *
 * No #ifdefs in this header — the dispatch happens at link time.
 */

#ifndef HAL_FATFS_DISPATCH_H
#define HAL_FATFS_DISPATCH_H

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

FRESULT hal_ff_findfirst(DIR *dp, FILINFO *fi, const TCHAR *path,
                         const TCHAR *pattern);
FRESULT hal_ff_findnext (DIR *dp, FILINFO *fi);
FRESULT hal_ff_closedir (DIR *dp);
FRESULT hal_ff_unlink   (const TCHAR *path);
FRESULT hal_ff_chdir    (const TCHAR *path);
FRESULT hal_ff_getcwd   (TCHAR *buf, UINT len);

#ifdef __cplusplus
}
#endif

#endif  /* HAL_FATFS_DISPATCH_H */
