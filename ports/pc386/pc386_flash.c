/*
 * ports/pc386/pc386_flash.c — RAM-backed flash buffers + Pico-SDK
 * compatible flash_range_erase/program shims.
 *
 * The PC has no flash chip; ProgMemory and the Option block live in
 * BSS and lose state across reboot. Persistent BASIC programs land
 * on FAT (A:\PROG.BAS / `SAVE`) — this layer is just the in-RAM
 * staging area MMBasic core uses while compiling and running.
 *
 * Layout (offsets in the flash address space):
 *   0..MAX_PROG_SIZE-1            — program area (tokenised BASIC)
 *   FLASH_TARGET_OFFSET .. +sz    — Option block
 *
 * Mirrors host_native's pattern (flash_prog_buf + host_flash_option_buf
 * in host_fs_shims.c) but with one program-area buffer instead of the
 * multi-slot region.
 */

#include <string.h>
#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Program area: 1 MB on pc386 (MAX_PROG_SIZE = HEAP_MEMORY_SIZE). */
uint8_t pc386_flash_prog_buf[MAX_PROG_SIZE];

/* Option block: just the option_s struct, padded for sector alignment. */
static uint8_t pc386_flash_option_buf[sizeof(struct option_s)];

/* Read-only handles consumed by Memory.c's scan loops + LoadOptions's
 * memcpy-from-flash path. */
const uint8_t *flash_option_contents = pc386_flash_option_buf;
const uint8_t *flash_target_contents = pc386_flash_prog_buf;

/* flash_progmemory is the runtime ProgMemory base; bc_alloc reads from
 * it. Initialised here so kmain doesn't have to. */
extern const uint8_t *flash_progmemory;

void pc386_flash_init(void) {
    /* Program area starts erased (0xFF). Option block reads as zero on
     * first boot — matches the freshness contract in hal_flash.h, which
     * is *different* from raw flash 0xFF state. */
    memset(pc386_flash_prog_buf,   0xFF, sizeof(pc386_flash_prog_buf));
    memset(pc386_flash_option_buf, 0x00, sizeof(pc386_flash_option_buf));
    flash_progmemory = pc386_flash_prog_buf;
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

void flash_range_program(uint32_t off, const uint8_t *data, uint32_t count) {
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
    memcpy(pc386_flash_option_buf, &Option, sizeof(Option));
}
