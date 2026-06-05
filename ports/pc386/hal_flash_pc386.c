/*
 * ports/pc386/hal_flash_pc386.c — hal_flash over the BSS-resident
 * flash buffers in pc386_flash.c.
 *
 * Same shape as host_native: every entry routes through the
 * flash_range_erase/program shims in pc386_flash.c (which dispatch
 * by offset to the program area or Option block). pc386_options_snapshot
 * keeps the Option-block buffer in sync with the live struct option_s.
 */

#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_flash.h"

extern void flash_range_erase(uint32_t off, uint32_t count);
extern void flash_range_program(uint32_t off, const uint8_t * data, uint32_t count);
extern const uint8_t * flash_option_contents;
extern void pc386_options_snapshot(void);

int hal_flash_erase(uint32_t offset, size_t len) {
    if (len == 0) return 0;
    flash_range_erase(offset, (uint32_t)len);
    return 0;
}

int hal_flash_program(uint32_t offset, const void * buf, size_t len) {
    if (len == 0) return 0;
    if (buf == NULL) return -EINVAL;
    flash_range_program(offset, (const uint8_t *)buf, (uint32_t)len);
    return 0;
}

int hal_flash_unique_id(uint8_t out[8]) {
    if (out == NULL) return -EINVAL;
    /* No real unique ID. Return a recognisable byte pattern; callers
     * that need stable per-install identity can derive their own. */
    static const uint8_t pc386_id[8] = {'P', 'C', '3', '8', '6', 'B', 'I', 'O'};
    memcpy(out, pc386_id, 8);
    return 0;
}

int hal_flash_read_jedec_id(uint8_t out[4]) {
    if (out == NULL) return -EINVAL;
    /* No SPI flash. Canned 8 MB response so callers that read
     * Option.FlashSize from this don't see zero. */
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 23; /* log2(8 MB) */
    return 0;
}

void hal_flash_write_begin(void) {}
void hal_flash_write_end(void) {}
int hal_flash_write_active(void) {
    return 0;
}

int hal_flash_read_options(void * buf, size_t len) {
    if (buf == NULL) return -EINVAL;
    memcpy(buf, flash_option_contents, len);
    return 0;
}

int hal_flash_write_options(const void * buf, size_t len) {
    if (buf == NULL) return -EINVAL;
    /* Sync the live Option struct back into the flash-backing buffer
     * so the LoadOptions call inside error()'s reset path restores
     * current state, not a zero-filled default. */
    (void)len;
    pc386_options_snapshot();
    return 0;
}

int hal_flash_erase_program_area(void) {
    flash_range_erase(0, MAX_PROG_SIZE);
    return 0;
}
