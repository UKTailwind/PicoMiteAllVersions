/*
 * esp32_flash_storage.c — backing storage + flash helpers for FileIO.c.
 *
 * Provides the symbols FileIO.c expects from a port that has flash:
 *
 *   flash_prog_buf[]          — RAM mirror of the program-memory region
 *                               (currently 2 × MAX_PROG_SIZE).
 *   host_flash_target_buf[]   — RAM mirror of the flash-slot region used
 *                               for SAVE/CHAIN/etc.
 *   host_flash_option_buf[]   — RAM mirror of the Options blob.
 *   flash_target_contents     — pointer to host_flash_target_buf.
 *   flash_option_contents     — pointer to host_flash_option_buf.
 *   host_options_snapshot()   — copy current Option struct into the
 *                               option mirror so error()'s LoadOptions
 *                               restores our defaults rather than zeros.
 *   flash_range_erase()       — fill a range with 0xff (RAM-mirror erase).
 *   flash_range_program()     — memcpy a buffer into the mirror.
 *
 * RAM-backed for now; Phase E replaces with esp_partition_*-backed
 * persistence (and at that point the giant host_flash_target_buf goes
 * away — the real flash partition is the storage).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"   /* struct option_s + extern Option */

/* SAVE-to-slot storage isn't wired up yet — it'll live on a real
 * flash partition via esp_partition_mmap. Until then, host_flash_target_buf
 * is a single-page stub: link-satisfying for the `flash_target_contents`
 * pointer and the bounds checks in flash_range_*, but small enough that
 * it doesn't eat the dram0_0_seg budget. SaveProgramToFlash routes the
 * live program through the tokenizer into ProgMemory directly, so it
 * doesn't depend on this region. */
unsigned char host_flash_target_buf[256];
unsigned char host_flash_option_buf[sizeof(struct option_s)];

const uint8_t *flash_target_contents = host_flash_target_buf;
const uint8_t *flash_option_contents = host_flash_option_buf;

__attribute__((constructor))
static void esp32_flash_storage_init(void) {
    memset(host_flash_option_buf, 0xff, sizeof host_flash_option_buf);
    memset(host_flash_target_buf, 0xff, sizeof host_flash_target_buf);
}

void host_options_snapshot(void) {
    memcpy(host_flash_option_buf, &Option, sizeof Option);
}

/* Two regions, mirroring host_native's offset-routing in
 * host_fs_shims.c::flash_range_*:
 *
 *   1. Program-flash region (off ∈ [0, sizeof flash_prog_buf)): real
 *      RAM mirror of the program-memory region. SaveProgramToFlash /
 *      load_basic_source call flash_range_erase(0, MAX_PROG_SIZE) here
 *      to clear ProgMemory before tokenising into it. Works.
 *
 *   2. Slot region (off >= FLASH_TARGET_OFFSET + ...): SAVE-to-slot
 *      storage. Placeholder is 256 bytes, real backing is Stage E1
 *      (esp_partition_*). Anything past the placeholder errors out
 *      loudly so SAVE doesn't look like it worked when bytes had
 *      nowhere to go.
 *
 * SLOT_REGION_BASE matches host_native's HOST_SLOT_REGION_BASE, so
 * cmd_save / cmd_load's `realflashpointer` arithmetic resolves to the
 * same offsets. */
extern unsigned char flash_prog_buf[];
#define FLASH_PROG_REGION_SIZE  (MAX_PROG_SIZE + 4096)  /* matches esp32_compat.c */
#define SLOT_REGION_BASE        ((uint32_t)(FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE + SAVEDVARS_FLASH_SIZE))
#define SLOT_REGION_SIZE        ((uint32_t)(MAXFLASHSLOTS * MAX_PROG_SIZE))

static inline int off_in_slot_region(uint32_t off, uint32_t count) {
    return (off >= SLOT_REGION_BASE) &&
           (off + count <= SLOT_REGION_BASE + SLOT_REGION_SIZE);
}

void flash_range_erase(uint32_t off, uint32_t count) {
    if (off_in_slot_region(off, count)) {
        if (off + count - SLOT_REGION_BASE > sizeof host_flash_target_buf) {
            error("SAVE-to-slot not supported on this port yet");
        }
        memset(host_flash_target_buf + (off - SLOT_REGION_BASE), 0xff, count);
        return;
    }
    if (off + count > FLASH_PROG_REGION_SIZE) return;
    memset(flash_prog_buf + off, 0xff, count);
}

void flash_range_program(uint32_t off, const uint8_t *data, size_t len) {
    if (off_in_slot_region(off, (uint32_t)len)) {
        if (off + len - SLOT_REGION_BASE > sizeof host_flash_target_buf) {
            error("SAVE-to-slot not supported on this port yet");
        }
        memcpy(host_flash_target_buf + (off - SLOT_REGION_BASE), data, len);
        return;
    }
    if (off + len > FLASH_PROG_REGION_SIZE) return;
    memcpy(flash_prog_buf + off, data, len);
}

/* SaveProgramToFlash: FileLoadProgram passes us the raw .bas text buffer
 * after reading the file. Tokenize it into ProgMemory so subsequent LIST
 * / RUN see a valid program. ProgMemory is erased to 0xff first so any
 * residual tokens from a previous load past the new program's terminator
 * don't fool PrepareProgramExt's CFunPtr walk — that walk skips zero
 * bytes after the program terminator looking for the 0xff "erased flash"
 * sentinel, and stale token bytes there cause it to dereference garbage. */
extern int load_basic_source(const char *source);
extern void PrepareProgram(int);
extern unsigned char *ProgMemory;
extern unsigned char flash_prog_buf[];

void SaveProgramToFlash(unsigned char *pm, int msg) {
    (void)msg;
    if (!pm) return;
    memset(flash_prog_buf, 0xff, MAX_PROG_SIZE);
    load_basic_source((const char *)pm);
    PrepareProgram(0);
}

/* ---- ExistsFile / ExistsDir on LFS ---- */

#include "lfs.h"
extern lfs_t lfs;
extern int   esp32_lfs_mount(void);

int ExistsFile(char *p) {
    if (!p || !*p || esp32_lfs_mount() != 0) return 0;
    /* Strip "A:" / "B:" prefix if present. */
    if (p[0] && p[1] == ':') p += 2;
    struct lfs_info info;
    if (lfs_stat(&lfs, p, &info) < 0) return 0;
    return (info.type == LFS_TYPE_REG) ? 1 : 0;
}

int ExistsDir(char *p, char *q, int *filesystem) {
    if (filesystem) *filesystem = 0;   /* A: */
    if (!p || esp32_lfs_mount() != 0) return 0;
    if (p[0] && p[1] == ':') p += 2;
    if (q) snprintf(q, FF_MAX_LFN, "%s", p);
    if (!*p || strcmp(p, "/") == 0) return 1;
    struct lfs_info info;
    if (lfs_stat(&lfs, p, &info) < 0) return 0;
    return (info.type == LFS_TYPE_DIR) ? 1 : 0;
}

/* ---- hal_ff_* stubs ----
 * These wrap FatFS dir/path ops for the B: (SD) drive. With no SD on
 * this board, B: is unreachable (port_drive_check errors first), so
 * these are link-satisfying stubs only. Real impls land when an SD
 * driver is wired up. */
#include "ff.h"
FRESULT hal_ff_findfirst(DIR *d, FILINFO *f, const TCHAR *p, const TCHAR *q) {
    (void)d; (void)f; (void)p; (void)q; return FR_NOT_ENABLED;
}
FRESULT hal_ff_findnext (DIR *d, FILINFO *f)        { (void)d; (void)f; return FR_NOT_ENABLED; }
FRESULT hal_ff_closedir (DIR *d)                    { (void)d; return FR_NOT_ENABLED; }
FRESULT hal_ff_unlink   (const TCHAR *p)            { (void)p; return FR_NOT_ENABLED; }
FRESULT hal_ff_chdir    (const TCHAR *p)            { (void)p; return FR_NOT_ENABLED; }
FRESULT hal_ff_getcwd   (TCHAR *b, UINT n)          { (void)b; (void)n; return FR_NOT_ENABLED; }

/* host_runtime.c used to reference host_sd_root for the --sd-root REPL
 * flag. Not meaningful on device; symbol kept (NULL) in case any
 * surviving caller still reads it. */
const char *host_sd_root = NULL;

/* MMBasic VAR SAVE / VAR RESTORE persists user-saved variables to a
 * dedicated flash region. Pico ports point SavedVarsFlash at the
 * SAVEDVARS_FLASH_SIZE-bound chunk inside flash_target_contents; host
 * uses a 32-byte RAM buffer (just enough to satisfy the symbol).
 *
 * Stdio scope follows host's pattern — RAM-backed mirror. Real flash
 * persistence comes when Stage E1 wires esp_partition_* into the slot
 * region; at that point this buffer goes away. */
static unsigned char esp32_saved_vars_flash_buf[32] = { 0xff, 0xff };
unsigned char *SavedVarsFlash = esp32_saved_vars_flash_buf;

/* Read-only pointer to the program-memory region. On Pico this points
 * at the XIP-mapped flash partition that holds the saved BASIC program.
 * On ESP32 stdio scope it points at our RAM mirror (flash_prog_buf in
 * esp32_compat.c) — same byte content, just not actually in flash yet.
 * Stage E1 swaps in an esp_partition_mmap-backed const view. */
extern unsigned char flash_prog_buf[];
const uint8_t *flash_progmemory = flash_prog_buf;
