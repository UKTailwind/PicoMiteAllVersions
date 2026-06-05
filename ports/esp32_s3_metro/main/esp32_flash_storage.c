/*
 * esp32_flash_storage.c — backing storage + flash helpers for FileIO.c.
 *
 * Provides the symbols FileIO.c expects from a port that has flash:
 *
 *   flash_prog_buf[]          — RAM mirror of the program-memory region
 *                               (currently 2 × MAX_PROG_SIZE).
 *   mmslots partition         — real flash backing for VAR SAVE plus
 *                               numbered SAVE/LOAD slots.
 *   esp32_flash_option_buf[]  — RAM mirror of the Options blob.
 *   flash_target_contents     — mmap view of the numbered slot region.
 *   flash_option_contents     — pointer to esp32_flash_option_buf.
 *   esp32_options_snapshot()  — copy current Option struct into the
 *                               option mirror so error()'s LoadOptions
 *                               restores our defaults rather than zeros.
 *   flash_range_erase()       — erase either program RAM or mmslots flash.
 *   flash_range_program()     — program either program RAM or mmslots flash.
 */

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h" /* struct option_s + extern Option */
#include "hal/hal_flash.h"
#include "runtime/runtime.h"

#define TAG "mmslots"

extern unsigned char flash_prog_buf[];

#define FLASH_PROG_REGION_SIZE (MAX_PROG_SIZE + 4096) /* matches esp32_compat.c */
#define SAVED_VARS_REGION_BASE ((uint32_t)(FLASH_TARGET_OFFSET + FLASH_ERASE_SIZE))
#define SLOT_REGION_BASE ((uint32_t)(SAVED_VARS_REGION_BASE + SAVEDVARS_FLASH_SIZE))
#define SLOT_REGION_SIZE ((uint32_t)(MAXFLASHSLOTS * MAX_PROG_SIZE))
#define MMSLOTS_REGION_SIZE ((uint32_t)(SAVEDVARS_FLASH_SIZE + SLOT_REGION_SIZE))

static const esp_partition_t * s_mmslots_part;
static esp_partition_mmap_handle_t s_mmslots_mmap;
static const uint8_t * s_mmslots_view;
static unsigned char esp32_flash_target_fallback[256] = {0xff, 0xff};
static unsigned char esp32_saved_vars_flash_fallback[32] = {0xff, 0xff};

unsigned char esp32_flash_option_buf[sizeof(struct option_s)];

const uint8_t * flash_target_contents = esp32_flash_target_fallback;
const uint8_t * flash_option_contents = esp32_flash_option_buf;
unsigned char * SavedVarsFlash = esp32_saved_vars_flash_fallback;

__attribute__((constructor)) static void esp32_flash_storage_init_buffers(void) {
    memset(esp32_flash_option_buf, 0xff, sizeof esp32_flash_option_buf);
    memset(esp32_flash_target_fallback, 0xff, sizeof esp32_flash_target_fallback);
    memset(esp32_saved_vars_flash_fallback, 0xff, sizeof esp32_saved_vars_flash_fallback);
}

int esp32_flash_storage_init(void) {
    if (s_mmslots_part && s_mmslots_view) return 0;

    s_mmslots_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "mmslots");
    if (!s_mmslots_part) {
        ESP_LOGE(TAG, "no 'mmslots' partition in partition table");
        return -ENOENT;
    }
    if (s_mmslots_part->size < MMSLOTS_REGION_SIZE) {
        ESP_LOGE(TAG, "partition too small: have=%lu need=%lu",
                 (unsigned long)s_mmslots_part->size,
                 (unsigned long)MMSLOTS_REGION_SIZE);
        return -ENOSPC;
    }

    const void * mapped = NULL;
    esp_err_t err = esp_partition_mmap(s_mmslots_part, 0, MMSLOTS_REGION_SIZE,
                                       ESP_PARTITION_MMAP_DATA, &mapped,
                                       &s_mmslots_mmap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_mmap failed: %s", esp_err_to_name(err));
        return -EIO;
    }

    s_mmslots_view = (const uint8_t *)mapped;
    SavedVarsFlash = (unsigned char *)s_mmslots_view;
    flash_target_contents = s_mmslots_view + SAVEDVARS_FLASH_SIZE;
    ESP_LOGI(TAG, "partition: size=%lu slots=%d slot_size=%u",
             (unsigned long)s_mmslots_part->size,
             MAXFLASHSLOTS,
             (unsigned)MAX_PROG_SIZE);
    return 0;
}

void esp32_options_snapshot(void) {
    memcpy(esp32_flash_option_buf, &Option, sizeof Option);
}

int esp32_flash_storage_load_options(void) {
    return hal_flash_read_options(esp32_flash_option_buf, sizeof esp32_flash_option_buf);
}

/* Regions, mirroring host_native's offset-routing in host_fs_shims.c:
 *
 *   1. Program-flash region: real RAM mirror of the program-memory
 *      region. Some shared paths use offset 0, while FLASH LOAD and
 *      FlashWriteInit(PROGRAM_FLASH) use legacy PROGSTART offsets; both
 *      forms map to flash_prog_buf.
 *
 *   2. Saved-vars + numbered-slot region: backed by the ESP-IDF
 *      `mmslots` data partition. The legacy offsets still include
 *      FLASH_TARGET_OFFSET; translate them to partition-relative
 *      offsets at the boundary. */

static inline int off_in_mmslots_region(uint32_t off, uint32_t count) {
    return (off >= SAVED_VARS_REGION_BASE) &&
           (off + count <= SLOT_REGION_BASE + SLOT_REGION_SIZE);
}

static uint32_t mmslots_partition_offset(uint32_t legacy_off) {
    return legacy_off - SAVED_VARS_REGION_BASE;
}

static int program_region_offset(uint32_t off, uint32_t count, uint32_t * out) {
    if (off + count <= FLASH_PROG_REGION_SIZE) {
        *out = off;
        return 1;
    }
    if (off >= PROGSTART && off + count <= PROGSTART + FLASH_PROG_REGION_SIZE) {
        *out = off - PROGSTART;
        return 1;
    }
    return 0;
}

void flash_range_erase(uint32_t off, uint32_t count) {
    uint32_t prog_off = 0;
    if (off_in_mmslots_region(off, count)) {
        if (esp32_flash_storage_init() != 0) {
            error("MMBasic flash partition not available");
        }
        if (esp_partition_erase_range(s_mmslots_part,
                                      mmslots_partition_offset(off),
                                      count) != ESP_OK) {
            error("Flash erase failed");
        }
        return;
    }
    if (!program_region_offset(off, count, &prog_off)) return;
    memset(flash_prog_buf + prog_off, 0xff, count);
}

void flash_range_program(uint32_t off, const uint8_t * data, size_t len) {
    uint32_t prog_off = 0;
    if (off_in_mmslots_region(off, (uint32_t)len)) {
        if (esp32_flash_storage_init() != 0) {
            error("MMBasic flash partition not available");
        }
        if (esp_partition_write(s_mmslots_part,
                                mmslots_partition_offset(off),
                                data, len) != ESP_OK) {
            error("Flash program failed");
        }
        return;
    }
    if (!program_region_offset(off, (uint32_t)len, &prog_off)) return;
    memcpy(flash_prog_buf + prog_off, data, len);
}

/* SaveProgramToFlash: FileLoadProgram passes us the raw .bas text buffer
 * after reading the file. Tokenize it into ProgMemory so subsequent LIST
 * / RUN see a valid program. ProgMemory is erased to 0xff first so any
 * residual tokens from a previous load past the new program's terminator
 * don't fool PrepareProgramExt's CFunPtr walk — that walk skips zero
 * bytes after the program terminator looking for the 0xff "erased flash"
 * sentinel, and stale token bytes there cause it to dereference garbage. */
extern unsigned char * ProgMemory;
extern unsigned char flash_prog_buf[];

void SaveProgramToFlash(unsigned char * pm, int msg) {
    (void)msg;
    if (!pm) return;
    memset(flash_prog_buf, 0xff, MAX_PROG_SIZE);
    mmbasic_save_loaded_source((const char *)pm, MMBASIC_SOURCE_FLAGS_BATCH_LOAD);
}

/* ExistsFile / FileSize / ExistsDir live in mm_misc_shared.c. They use
 * the shared drivecheck + getfullfilename + InitSDCard pipeline and route
 * to lfs_stat (A:) or f_stat (B:). The ESP32 port supplies the lfs global
 * (esp32_lfs.c) and port_mount_sd_drive (esp32_cmd_files_hooks.c). */

#include "lfs.h"
#include "ff.h"
#include "diskio.h"

/* ---- hal_ff_* wrappers for the B: FatFS drive. */
FRESULT hal_ff_findfirst(DIR * d, FILINFO * f, const TCHAR * p, const TCHAR * q) {
    return f_findfirst(d, f, p, q);
}
FRESULT hal_ff_findnext(DIR * d, FILINFO * f) {
    return f_findnext(d, f);
}
FRESULT hal_ff_closedir(DIR * d) {
    return f_closedir(d);
}
FRESULT hal_ff_unlink(const TCHAR * p) {
    return f_unlink(p);
}
FRESULT hal_ff_chdir(const TCHAR * p) {
    return f_chdir(p);
}
FRESULT hal_ff_getcwd(TCHAR * b, UINT n) {
    return f_getcwd(b, n);
}

/* Read-only pointer to the program-memory region. On Pico this points
 * at the XIP-mapped flash partition that holds the saved BASIC program.
 * On ESP32 stdio scope it points at our RAM mirror (flash_prog_buf in
 * esp32_compat.c) — same byte content, just not actually in flash yet. */
extern unsigned char flash_prog_buf[];
const uint8_t * flash_progmemory = flash_prog_buf;
