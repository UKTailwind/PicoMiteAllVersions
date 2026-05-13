/*
 * host_fs_shims.c — host-side filesystem HAL.
 *
 * Holds the FatFS walker wrappers (host_f_findfirst/findnext/closedir/
 * unlink/rename/mkdir/chdir/getcwd) that let FileIO.c's cmd_files /
 * cmd_copy / cmd_kill / fun_dir / fun_cwd walk real POSIX directories
 * when host_sd_root is set; plus simulated flash / LFS / existence-check
 * stubs that let the rest of the interpreter link unchanged. All file
 * I/O now flows through hal/hal_filesystem.h — there is no separate
 * host_fs_posix_* table; BasicFileOpen hands the path to hal_fs_open
 * and the host adapter (hal_filesystem_host.c) owns the FILE* /
 * vm_host_fat FIL dispatch.
 *
 * flash_option_contents is RAM-backed; host_options_snapshot() syncs
 * the live Option back into it so LoadOptions inside error() restores
 * the correct host configuration rather than a zero-filled default.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "host_fs.h"
#include "runtime/runtime.h"

/* FatFS's FF_MAX_LFN is 63 — fine for the 8.3-style FAT world, but
 * POSIX paths on a real desktop regularly run several hundred chars
 * (macOS/Linux typically cap at PATH_MAX = 1024 or 4096). Any code that
 * joins host_sd_root + a filename and buffers the result in an
 * FF_MAX_LFN-sized array will truncate — or worse, throw "File name too
 * long" — on perfectly ordinary cwds. Size host-side path buffers to a
 * POSIX-friendly ceiling instead. */
#define HOST_PATH_MAX 4096

/* When set (REPL mode with --sd-root DIR, or cwd by default), file
 * commands operate on the real filesystem rooted here rather than on
 * the in-memory FAT disk. NULL for the test harness — commands that
 * require filesystem access error rather than scribble on the user's
 * real files. */
const char *host_sd_root = NULL;

static void host_resolve_sd_path(const char *fname, char *out, size_t out_cap) {
    if (!host_sd_root) { error("No SD root configured"); return; }
    /* Absolute paths pass through unchanged. Relative paths get joined to
     * host_sd_root with a single '/'. */
    if (fname[0] == '/') {
        if (strlen(fname) >= out_cap) error("File name too long");
        strcpy(out, fname);
        return;
    }
    size_t rl = strlen(host_sd_root);
    size_t fl = strlen(fname);
    int need_sep = (rl > 0 && host_sd_root[rl - 1] != '/');
    if (rl + (need_sep ? 1 : 0) + fl + 1 > out_cap) error("File name too long");
    memcpy(out, host_sd_root, rl);
    if (need_sep) out[rl++] = '/';
    memcpy(out + rl, fname, fl + 1);
}

/* =========================================================================
 * FatFS directory-walker wrappers.
 *
 * FileIO.c's cmd_files, cmd_copy, cmd_kill, fun_dir need f_findfirst /
 * f_findnext / f_closedir. In REPL / --sim mode (host_sd_root set) we
 * walk the user's real directory via host_fs_walk_* (POSIX) and populate
 * the FatFS FILINFO the caller expects. That way cmd_files' sort,
 * pagination, and formatting — all of which live in FileIO.c — run
 * unchanged on host-hosted file trees.
 *
 * Without host_sd_root the test harness wants the vm_host_fat RAM disk,
 * so we delegate straight to FatFS.
 *
 * FatFS date/time packing (ff.h: fdate = yr-1980<<9 | mon<<5 | day,
 * ftime = hr<<11 | min<<5 | sec/2). Zero-filled when mtime decode fails;
 * cmd_files prints "00:00 00-00-1980" but doesn't crash.
 * ======================================================================= */
static host_fs_walker_t *host_find_walker = NULL;

FRESULT host_f_findfirst(DIR *dp, FILINFO *fi, const TCHAR *path, const TCHAR *pattern);
FRESULT host_f_findnext(DIR *dp, FILINFO *fi);
FRESULT host_f_closedir(DIR *dp);
static void host_strip_fatfs_drive(const char *in, char *out, int out_cap);
void host_join_sd_root(const char *relpath, char *out, int out_cap);

static void host_fill_finfo_from_posix(FILINFO *fi, const char *name,
                                       int is_dir, unsigned long long size,
                                       long long mtime_epoch) {
    memset(fi, 0, sizeof(*fi));
    snprintf(fi->fname, sizeof(fi->fname), "%s", name);
    fi->fattrib = is_dir ? AM_DIR : 0;
    fi->fsize = (FSIZE_t)size;
    time_t t = (time_t)mtime_epoch;
    struct tm lt;
    if (localtime_r(&t, &lt) != NULL) {
        int yr = lt.tm_year + 1900;
        if (yr < 1980) yr = 1980;
        fi->fdate = (WORD)(((yr - 1980) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
        fi->ftime = (WORD)((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
    }
}

static void host_strip_fatfs_drive_keep_root(const char *in, char *out, int out_cap) {
    if (out_cap <= 0) return;
    if (in[0] && in[1] == ':') in += 2;
    snprintf(out, out_cap, "%s", *in ? in : "/");
}

FRESULT host_f_findfirst(DIR *dp, FILINFO *fi, const TCHAR *path,
                         const TCHAR *pattern) {
    if (!host_sd_root) {
        char stripped[HOST_PATH_MAX];
        host_strip_fatfs_drive_keep_root(path, stripped, sizeof(stripped));
        return f_findfirst(dp, fi, stripped, pattern);
    }
    /* FileIO.c hands us a FatFS-style path like "/build" or just "/"
     * (drive prefix already stripped by fullpath(). Join it onto
     * host_sd_root so FILES / COPY / KILL / DIR$ see the subdirectory
     * the user has CHDIR'd into. */
    char target[HOST_PATH_MAX];
    host_join_sd_root(path, target, sizeof(target));
    if (host_find_walker) host_fs_walk_close(host_find_walker);
    host_find_walker = host_fs_walk_open(target, pattern);
    if (!host_find_walker) { memset(fi, 0, sizeof(*fi)); return FR_NO_PATH; }
    return host_f_findnext(dp, fi);
}

FRESULT host_f_findnext(DIR *dp, FILINFO *fi) {
    if (!host_sd_root) return f_findnext(dp, fi);
    (void)dp;
    if (!host_find_walker) { memset(fi, 0, sizeof(*fi)); return FR_NO_FILE; }
    char name[FF_MAX_LFN + 1];
    int is_dir = 0;
    unsigned long long size = 0;
    long long mtime = 0;
    if (!host_fs_walk_next(host_find_walker, name, (int)sizeof(name),
                           &is_dir, &size, &mtime)) {
        memset(fi, 0, sizeof(*fi));
        fi->fname[0] = 0;
        return FR_OK;   /* FatFS signals end-of-dir by empty fname, FR_OK */
    }
    host_fill_finfo_from_posix(fi, name, is_dir, size, mtime);
    return FR_OK;
}

FRESULT host_f_closedir(DIR *dp) {
    if (!host_sd_root) return f_closedir(dp);
    (void)dp;
    if (host_find_walker) {
        host_fs_walk_close(host_find_walker);
        host_find_walker = NULL;
    }
    return FR_OK;
}

/* Whole-path wrappers. cmd_kill, cmd_name, cmd_mkdir, cmd_chdir, cmd_copy,
 * fun_cwd all call these. Without host_sd_root we go straight to FatFS
 * (vm_host_fat RAM disk for the test harness). With host_sd_root we
 * resolve the path against the user's root directory and hit POSIX.
 *
 * The path argument FileIO.c hands us has a FatFS drive prefix (e.g.
 * "0:/foo.bas" from getfullfilename). We strip that so POSIX paths
 * work cleanly under host_sd_root. */
static void host_strip_fatfs_drive(const char *in, char *out, int out_cap) {
    if (out_cap <= 0) return;
    /* Skip "N:" drive prefix if present. */
    if (in[0] && in[1] == ':') in += 2;
    /* Skip leading "/" so host_resolve_sd_path will join under host_sd_root. */
    while (*in == '/') in++;
    snprintf(out, out_cap, "%s", in);
}

void host_join_sd_root(const char *relpath, char *out, int out_cap) {
    char stripped[HOST_PATH_MAX];
    host_strip_fatfs_drive(relpath, stripped, sizeof(stripped));
    host_resolve_sd_path(stripped, out, (size_t)out_cap);
}

FRESULT host_f_unlink(const TCHAR *path) {
    if (!host_sd_root) return f_unlink(path);
    char p[HOST_PATH_MAX];
    host_join_sd_root(path, p, sizeof(p));
    return host_fs_unlink(p) == 0 ? FR_OK : FR_NO_FILE;
}

FRESULT host_f_rename(const TCHAR *from, const TCHAR *to) {
    if (!host_sd_root) return f_rename(from, to);
    char a[HOST_PATH_MAX], b[HOST_PATH_MAX];
    host_join_sd_root(from, a, sizeof(a));
    host_join_sd_root(to, b, sizeof(b));
    return host_fs_rename(a, b) == 0 ? FR_OK : FR_NO_FILE;
}

FRESULT host_f_mkdir(const TCHAR *path) {
    if (!host_sd_root) return f_mkdir(path);
    char p[HOST_PATH_MAX];
    host_join_sd_root(path, p, sizeof(p));
    return host_fs_mkdir(p) == 0 ? FR_OK : FR_EXIST;
}

FRESULT host_f_chdir(const TCHAR *path) {
    if (!host_sd_root) return f_chdir(path);
    char p[HOST_PATH_MAX];
    host_join_sd_root(path, p, sizeof(p));
    return host_fs_chdir(p) == 0 ? FR_OK : FR_NO_PATH;
}

FRESULT host_f_getcwd(TCHAR *buff, UINT len) {
    if (!host_sd_root) return f_getcwd(buff, len);
    char tmp[HOST_PATH_MAX];
    if (host_fs_getcwd(tmp, (int)sizeof(tmp)) != 0) return FR_INT_ERR;
    /* Prepend drive prefix so Editor.c / PRINT CWD$ format works. */
    snprintf(buff, len, "0:%s", tmp);
    return FR_OK;
}

/* hal_ff_* directory + path ops on host route through the host_f_*
 * wrappers above so vm_host_fat / POSIX dispatch via host_sd_root keeps
 * working without core seeing a #ifdef MMBASIC_HOST. */
#include "hal/hal_fatfs_dispatch.h"
FRESULT hal_ff_findfirst(DIR *dp, FILINFO *fi, const TCHAR *path,
                         const TCHAR *pattern) { return host_f_findfirst(dp, fi, path, pattern); }
FRESULT hal_ff_findnext (DIR *dp, FILINFO *fi)                  { return host_f_findnext(dp, fi); }
FRESULT hal_ff_closedir (DIR *dp)                                { return host_f_closedir(dp); }
FRESULT hal_ff_unlink   (const TCHAR *path)                      { return host_f_unlink(path); }
FRESULT hal_ff_chdir    (const TCHAR *path)                      { return host_f_chdir(path); }
FRESULT hal_ff_getcwd   (TCHAR *buf, UINT len)                   { return host_f_getcwd(buf, len); }

/* POSIX-backed existence check. The Editor's file-load path (EDIT "foo.bas")
 * needs this to return truthful answers — otherwise `edit` leaves its p
 * pointer NULL and dereferences it at Editor.c:511. Also relied on by
 * cmd_run "file", AUTOSAVE recovery, etc. When no --sd-root is configured
 * (host_sd_root == NULL) we fall back to the CWD so the tree-of-.bas files
 * in the repo root just work for direct-run invocations. */
int ExistsFile(char *fname) {
    if (!fname || !*fname) return 0;
    char path[STRINGSIZE];
    if (host_sd_root) {
        size_t rl = strlen(host_sd_root);
        size_t fl = strlen(fname);
        int need_sep = (fname[0] != '/' && rl > 0 && host_sd_root[rl - 1] != '/');
        if (fname[0] == '/') {
            snprintf(path, sizeof(path), "%s", fname);
        } else if (rl + (need_sep ? 1 : 0) + fl + 1 > sizeof(path)) {
            return 0;
        } else {
            snprintf(path, sizeof(path), "%s%s%s",
                     host_sd_root, need_sep ? "/" : "", fname);
        }
    } else {
        snprintf(path, sizeof(path), "%s", fname);
    }
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

int ExistsDir(char *p, char *q, int *filesystem) {
    (void)p; (void)q; (void)filesystem;
    return 0;
}

/* =========================================================================
 * Simulated flash operations.
 *
 * On the device these hit XIP flash; on host they write through to two
 * RAM-backed regions:
 *
 *   1. flash_prog_buf  (host_main.c, 256 KB) — program flash mirror at
 *      offset 0..256 KB.  This is what flash_progmemory points at and
 *      what cmd_new / cmd_run / SaveProgramToFlash exercise.
 *
 *   2. host_flash_target_buf (this file) — the user-program flash slot
 *      region (FLASH ERASE / DISK LOAD / LOAD IMAGE / SAVE / OVERWRITE).
 *      On device this is a contiguous chunk in XIP flash starting at
 *      `FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE`
 *      and running for `MAXFLASHSLOTS * MAX_PROG_SIZE` bytes; the
 *      pointer `flash_target_contents` is the XIP-mapped view of
 *      offset 0 within it.  On host we mirror the same layout: a single
 *      RAM buffer, indexed by the *device* flash offset so the existing
 *      `realflashpointer` arithmetic in cmd_flash works unchanged.
 *
 * flash_range_erase / flash_range_program route writes to whichever of
 * the two regions the offset lands in.  The program-flash region is
 * written by host_main.c's startup path (zeroed for program area,
 * 0xFF for the CFunction area); the slot region starts 0xFF-filled
 * (matching erased flash) via the constructor below.
 * ======================================================================= */
extern uint8_t flash_prog_buf[];
#define HOST_FLASH_SIZE        (2 * MAX_PROG_SIZE)
#define HOST_FLASH_PROG_SIZE   (HOST_FLASH_SIZE / 2)

/* Device address range of the user flash slot region. MAX_PROG_SIZE is
 * the per-slot stride and matches HEAP_MEMORY_SIZE for the variant
 * (PicoCalc rp2040 PICOMITE = 128 KB → 384 KB slot mirror). */
#define HOST_SLOT_REGION_BASE  ((uint32_t)(FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE))
#define HOST_SLOT_REGION_SIZE  ((uint32_t)(MAXFLASHSLOTS * MAX_PROG_SIZE))

/* Forward declaration so flash_range_* can find the slot mirror. */
extern uint8_t host_flash_target_buf[HOST_SLOT_REGION_SIZE];

static inline bool host_off_in_slot_region(uint32_t off, uint32_t count) {
    return (off >= HOST_SLOT_REGION_BASE) &&
           (off + count <= HOST_SLOT_REGION_BASE + HOST_SLOT_REGION_SIZE);
}

void flash_range_erase(uint32_t off, uint32_t count) {
    if (host_off_in_slot_region(off, count)) {
        memset(host_flash_target_buf + (off - HOST_SLOT_REGION_BASE), 0xFF, count);
        return;
    }
    if (off >= HOST_FLASH_SIZE) return;
    if (off + count > HOST_FLASH_SIZE) count = HOST_FLASH_SIZE - off;
    /* Device erase fills with 0xFF. The program region additionally gets
     * a leading zero terminator written by cmd_new right after the erase,
     * but host's ProgMemory scan accepts either 0 or 0xFF as end-of-program. */
    memset(flash_prog_buf + off, 0xFF, count);
}

void flash_range_program(uint32_t off, const uint8_t *data, uint32_t count) {
    if (host_off_in_slot_region(off, count)) {
        memcpy(host_flash_target_buf + (off - HOST_SLOT_REGION_BASE), data, count);
        return;
    }
    if (off >= HOST_FLASH_SIZE) return;
    if (off + count > HOST_FLASH_SIZE) count = HOST_FLASH_SIZE - off;
    memcpy(flash_prog_buf + off, data, count);
}

/* SaveProgramToFlash lives in PicoMite.c on device; on host that file
 * isn't linked so we provide a tokenise-in-place shim. Called from the
 * Editor (F1 Save) and from FileIO.c's cmd_load / FileLoadProgram with
 * EdBuff or an assembled source buffer. `pm` is NUL-terminated source
 * text, NOT tokens, so route it through the host tokeniser path.
 *
 * Do NOT call ClearRuntime here: EdBuff is allocated via GetTempMemory
 * inside edit(), and ClearRuntime frees temp memory — freeing the very
 * buffer we're about to tokenise. */
void SaveProgramToFlash(unsigned char *pm, int msg) {
    (void)msg;
    if (!pm) return;
    mmbasic_save_loaded_source((const char *)pm, MMBASIC_SOURCE_FLAGS_HOST_LOAD);
}

/* LFS stubs — FileIO.c references the full littlefs surface. On host
 * nothing actually stores to flash via LFS (BasicFileOpen routes through
 * POSIX / FatFS instead), but every reachable call site has to link. */
int lfs_file_close(lfs_t *l, lfs_file_t *file) { (void)l; (void)file; return 0; }
int lfs_file_open(lfs_t *l, lfs_file_t *file, const char *path, int flags) { (void)l; (void)file; (void)path; (void)flags; return -1; }
lfs_ssize_t lfs_file_read(lfs_t *l, lfs_file_t *file, void *buf, lfs_size_t size) { (void)l; (void)file; (void)buf; (void)size; return 0; }
lfs_soff_t lfs_file_seek(lfs_t *l, lfs_file_t *file, lfs_soff_t off, int whence) { (void)l; (void)file; (void)off; (void)whence; return 0; }
lfs_ssize_t lfs_file_write(lfs_t *l, lfs_file_t *file, const void *buf, lfs_size_t size) { (void)l; (void)file; (void)buf; (void)size; return 0; }
lfs_ssize_t lfs_fs_size(lfs_t *l) { (void)l; return 0; }
int lfs_remove(lfs_t *l, const char *path) { (void)l; (void)path; return 0; }
int lfs_stat(lfs_t *l, const char *path, struct lfs_info *info) { (void)l; (void)path; (void)info; return -1; }
int lfs_dir_open(lfs_t *l, lfs_dir_t *dir, const char *path) { (void)l; (void)dir; (void)path; return -1; }
int lfs_dir_close(lfs_t *l, lfs_dir_t *dir) { (void)l; (void)dir; return 0; }
int lfs_dir_read(lfs_t *l, lfs_dir_t *dir, struct lfs_info *info) { (void)l; (void)dir; (void)info; return 0; }
int lfs_file_rewind(lfs_t *l, lfs_file_t *file) { (void)l; (void)file; return 0; }
int lfs_file_sync(lfs_t *l, lfs_file_t *file) { (void)l; (void)file; return 0; }
lfs_soff_t lfs_file_tell(lfs_t *l, lfs_file_t *file) { (void)l; (void)file; return 0; }
int lfs_format(lfs_t *l, const struct lfs_config *cfg) { (void)l; (void)cfg; return 0; }
int lfs_mount(lfs_t *l, const struct lfs_config *cfg) { (void)l; (void)cfg; return 0; }
int lfs_unmount(lfs_t *l) { (void)l; return 0; }
lfs_ssize_t lfs_getattr(lfs_t *l, const char *path, uint8_t type, void *buf, lfs_size_t size) { (void)l; (void)path; (void)type; (void)buf; (void)size; return -1; }
int lfs_setattr(lfs_t *l, const char *path, uint8_t type, const void *buf, lfs_size_t size) { (void)l; (void)path; (void)type; (void)buf; (void)size; return 0; }
int lfs_removeattr(lfs_t *l, const char *path, uint8_t type) { (void)l; (void)path; (void)type; return 0; }
int lfs_mkdir(lfs_t *l, const char *path) { (void)l; (void)path; return 0; }
int lfs_rename(lfs_t *l, const char *oldpath, const char *newpath) { (void)l; (void)oldpath; (void)newpath; return 0; }

/* lfs_file_size is only reached from the FLASHFILE branch in Editor.c, which
 * is unreachable on host (filesource[] is always FATFSFILE). Stubbed so the
 * link succeeds. */
lfs_soff_t lfs_file_size(lfs_t *lfs, lfs_file_t *fp) { (void)lfs; (void)fp; return 0; }

/* =========================================================================
 * Flash layout externs referenced from FileIO.c.
 *
 * On device these live at fixed XIP addresses (linker script); on host
 * there's no flash so we back them with RAM buffers.
 *
 * flash_option_contents is memcpy()'d into Option by LoadOptions — NOT
 * just at startup but every time error() fires (see MMBasic.c:2835).
 * Host-specific terminal geometry (Option.Width / Height /
 * DISPLAY_CONSOLE / …) gets set by host_main.c at boot; if this
 * buffer is blank, every error wipes those fields and the framebuffer
 * console goes silent + cmd_files' wrap-at-Width collapses to zero.
 *
 * The fix: host_options_snapshot() (called by mmbasic_runtime_port_begin
 * after everyone has finished mutating Option) copies the current
 * Option back into this buffer so subsequent LoadOptions calls
 * restore the initialized state, not the zero-filled one.
 *
 * We still start zero-filled (not 0xFF) because Option.PIN is an int
 * and all-0xFF would be 0xFFFFFFFF which trips the PIN-lockdown
 * prompt in MMBasic_REPL.c:196.
 *
 * flash_target_buf uses 0xFF fill — PrepareProgramExt walks it looking
 * for the CFunction terminator, and 0xFF is correct "erased" semantics.
 * ======================================================================= */
static uint8_t host_flash_option_buf[sizeof(struct option_s)];
/* Sized to mirror the device's full flash slot region (see header above).
 * Non-static because flash_range_* in this same translation unit forward-
 * declares it; defining it here keeps initialization in one place. */
uint8_t host_flash_target_buf[HOST_SLOT_REGION_SIZE];
const uint8_t *flash_option_contents = host_flash_option_buf;
const uint8_t *flash_target_contents = host_flash_target_buf;
__attribute__((constructor))
static void host_flash_contents_init(void) {
    memset(host_flash_option_buf, 0x00, sizeof(host_flash_option_buf));
    memset(host_flash_target_buf, 0xFF, sizeof(host_flash_target_buf));
}

void host_options_snapshot(void) {
    memcpy(host_flash_option_buf, &Option, sizeof(Option));
}

/* pico_lfs_cfg lives in drivers/pico_flash/pico_flash_lfs.c on device,
 * which pulls in hal_flash + hardware headers (not present on host).
 * FileIO.c keeps an `extern struct lfs_config pico_lfs_cfg;` that the
 * host link also needs to satisfy, even though every host lfs_* call
 * above is a stub that doesn't touch the cfg. A zero-initialised
 * definition is enough. */
struct lfs_config pico_lfs_cfg;
