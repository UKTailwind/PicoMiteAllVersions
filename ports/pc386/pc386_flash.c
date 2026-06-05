/*
 * ports/pc386/pc386_flash.c — RAM-backed flash buffers + Pico-SDK
 * compatible flash_range_erase/program shims.
 *
 * The PC has no flash chip. ProgMemory lives in BSS; the Option block
 * is cached in BSS but backed by C:/OPTIONS.INI, a human-readable
 * key/value file on the boot FAT volume.
 *
 * Layout (offsets in the flash address space):
 *   0..MAX_PROG_SIZE-1            — program area (tokenised BASIC)
 *   FLASH_TARGET_OFFSET .. +sz    — Option block
 *
 * Mirrors host_native's pattern (flash_prog_buf + host_flash_option_buf
 * in host_fs_shims.c) but with one program-area buffer instead of the
 * multi-slot region.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ff.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "options_ini.h"

/* Program area: RAM-backed flash image sized by MAX_PROG_SIZE. */
uint8_t pc386_flash_prog_buf[MAX_PROG_SIZE];

/* Option block: just the option_s struct, padded for sector alignment. */
static uint8_t pc386_flash_option_buf[sizeof(struct option_s)];
static uint8_t pc386_default_option_buf[sizeof(struct option_s)];
static int pc386_options_capturing_defaults;
static int pc386_options_sparse_ini;

/* Read-only handles consumed by Memory.c's scan loops + LoadOptions's
 * memcpy-from-flash path. */
const uint8_t * flash_option_contents = pc386_flash_option_buf;
const uint8_t * flash_target_contents = pc386_flash_prog_buf;

/* flash_progmemory is the runtime ProgMemory base; bc_alloc reads from
 * it. Initialised here so kmain doesn't have to. */
extern const uint8_t * flash_progmemory;
extern void port_apply_load_overrides(void);
extern void pc386_apply_runtime_option_defaults(void);

static int pc386_is_legacy_runtime_field(const char * name, void * ctx) {
    (void)ctx;
    return strcasecmp(name, "DefaultFont") == 0 ||
           strcasecmp(name, "Height") == 0 ||
           strcasecmp(name, "Width") == 0 ||
           strcasecmp(name, "DISPLAY_TYPE") == 0 ||
           strcasecmp(name, "DISPLAY_CONSOLE") == 0;
}

static int pc386_is_runtime_derived_field(const char * name, void * ctx) {
    (void)ctx;
    return strcasecmp(name, "Height") == 0 ||
           strcasecmp(name, "Width") == 0 ||
           strcasecmp(name, "DISPLAY_TYPE") == 0 ||
           strcasecmp(name, "DISPLAY_CONSOLE") == 0;
}

static void pc386_options_load_ini(void) {
    FIL fp;
    if (f_open(&fp, "C:/OPTIONS.INI", FA_READ) != FR_OK) return;
    static char buf[16384];
    UINT got = 0;
    if (f_read(&fp, buf, sizeof(buf) - 1, &got) == FR_OK) {
        buf[got] = 0;
        pc386_options_sparse_ini = mm_options_ini_is_sparse(buf);
        mm_options_ini_parse(buf, pc386_flash_option_buf, pc386_options_sparse_ini,
                             pc386_is_legacy_runtime_field, NULL);
    }
    f_close(&fp);
}

static int pc386_write_ini_line(void * ctx, const char * line) {
    FIL * fp = (FIL *)ctx;
    UINT wrote = 0;
    FRESULT fr = f_write(fp, line, (UINT)strlen(line), &wrote);
    return (fr == FR_OK && wrote == strlen(line)) ? 0 : -1;
}

static void pc386_options_capture_defaults(void) {
    struct option_s saved;
    memcpy(&saved, &Option, sizeof(saved));
    memset(&Option, 0, sizeof(Option));
    port_apply_load_overrides();
    pc386_apply_runtime_option_defaults();
    memcpy(pc386_default_option_buf, &Option, sizeof(Option));
    memcpy(&Option, &saved, sizeof(Option));
}

static void pc386_options_write_ini(void) {
    struct option_s zero;
    memset(&zero, 0, sizeof(zero));
    if (memcmp(pc386_default_option_buf, &zero, sizeof(zero)) == 0) {
        pc386_options_capture_defaults();
    }

    if (!mm_options_ini_has_changes(pc386_flash_option_buf, pc386_default_option_buf,
                                    pc386_is_runtime_derived_field, NULL)) {
        f_unlink("C:/OPTIONS.INI");
        return;
    }

    FIL fp;
    if (f_open(&fp, "C:/OPTIONS.INI", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
    pc386_write_ini_line(&fp, "# PicoMite PC386 options\r\n");
    pc386_write_ini_line(&fp, "# Only values that differ from pc386 defaults are written.\r\n");
    pc386_write_ini_line(&fp, "# Edit as key=value. Hex values may use 0xNN or &HNN.\r\n\r\n");
    mm_options_ini_write_changed(pc386_flash_option_buf, pc386_default_option_buf,
                                 pc386_is_runtime_derived_field, NULL,
                                 pc386_write_ini_line, &fp);
    f_close(&fp);
}

void pc386_flash_init(void) {
    /* Program area starts erased (0xFF). Option block reads as zero on
     * first boot — matches the freshness contract in hal_flash.h, which
     * is *different* from raw flash 0xFF state. */
    memset(pc386_flash_prog_buf, 0xFF, sizeof(pc386_flash_prog_buf));
    memset(pc386_flash_option_buf, 0x00, sizeof(pc386_flash_option_buf));
    memset(pc386_default_option_buf, 0x00, sizeof(pc386_default_option_buf));
    pc386_options_capturing_defaults = 0;
    pc386_options_sparse_ini = 0;
    flash_progmemory = pc386_flash_prog_buf;
    pc386_options_load_ini();
}

void pc386_options_defaults_ready(void) {
    pc386_options_capture_defaults();
}

/* ===== Pico-SDK shape ====================================================
 * MMBasic core still calls flash_range_erase/flash_range_program in a
 * few spots (Memory.c, FileIO.c). Route by offset: anything inside the
 * program area writes to pc386_flash_prog_buf; anything inside the Option
 * region writes to pc386_flash_option_buf; anything else is a no-op
 * (other ports' flash slots that pc386 doesn't carry).
 */

void flash_range_erase(uint32_t off, uint32_t count) {
    if (off + count <= sizeof(pc386_flash_prog_buf)) {
        memset(pc386_flash_prog_buf + off, 0xFF, count);
        return;
    }
    if (off >= FLASH_TARGET_OFFSET &&
        off + count <= FLASH_TARGET_OFFSET + sizeof(pc386_flash_option_buf)) {
        memset(pc386_flash_option_buf + (off - FLASH_TARGET_OFFSET), 0xFF, count);
        return;
    }
    /* Out-of-range write — silently ignored (matches host posture). */
}

void flash_range_program(uint32_t off, const uint8_t * data, uint32_t count) {
    if (off + count <= sizeof(pc386_flash_prog_buf)) {
        memcpy(pc386_flash_prog_buf + off, data, count);
        return;
    }
    if (off >= FLASH_TARGET_OFFSET &&
        off + count <= FLASH_TARGET_OFFSET + sizeof(pc386_flash_option_buf)) {
        memcpy(pc386_flash_option_buf + (off - FLASH_TARGET_OFFSET), data, count);
        return;
    }
}

/* SaveOptions calls this after mutating Option in RAM to refresh the
 * flash-backing snapshot — same pattern as host_options_snapshot. */
void pc386_options_snapshot(void) {
    if (pc386_options_capturing_defaults) return;
    memcpy(pc386_flash_option_buf, &Option, sizeof(Option));
    pc386_options_write_ini();
}
