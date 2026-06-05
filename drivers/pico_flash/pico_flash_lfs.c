/*
 * drivers/pico_flash/pico_flash_lfs.c — internal flash backing for
 * littlefs (A: drive on device).
 *
 * Lifted from FileIO.c with no behavioural change. The four lfs_config
 * callbacks (fs_flash_read / prog / erase / sync) are __not_in_flash_func
 * so they remain reachable while XIP is disabled during flash programming.
 *
 * pico_lfs_cfg itself lives here too; PicoMite.c fills in .block_count
 * at boot once Option.FlashSize is known, the same way it did before
 * the move.
 *
 * Reaches fileio_flash_write_begin / fileio_flash_write_end via FileIO.h;
 * they serialise PSRAM setting save/restore around the flash write, a
 * concern that belongs to those helpers, not to this driver.
 *
 * Reaches XIP base + RoundUpK4(TOP_OF_SYSTEM_FLASH) + Option.modbuff
 * via Hardware_Includes.h + addressmap.h, matching the original file's
 * include graph.
 */

#include <assert.h>
#include <string.h>
#include <stdint.h>

#include "Hardware_Includes.h"
#include "hardware/regs/addressmap.h" /* XIP_BASE */

#include "hal/hal_flash.h"
#include "FileIO.h" /* fileio_flash_write_begin/end */
#include "lfs.h"

#define BLOCK_SIZE 4096

char FlashReadBuffer[256];
char FlashProgBuffer[256];
char FlashLookBuffer[256];

int fs_flash_read(const struct lfs_config * cfg, lfs_block_t block, lfs_off_t off, void * buffer, lfs_size_t size);
int fs_flash_prog(const struct lfs_config * cfg, lfs_block_t block, lfs_off_t off, const void * buffer, lfs_size_t size);
int fs_flash_erase(const struct lfs_config * cfg, lfs_block_t block);
int fs_flash_sync(const struct lfs_config * c);

struct lfs_config pico_lfs_cfg = {
    .read = fs_flash_read,
    .prog = fs_flash_prog,
    .erase = fs_flash_erase,
    .sync = fs_flash_sync,

    .read_size = 1,
    .prog_size = 256,
    .block_size = BLOCK_SIZE,
    .block_count = 0, /* filled in at boot once Option.FlashSize known */
    .block_cycles = 500,
    .cache_size = 256,
    .lookahead_size = 256,

    .read_buffer = (void *)FlashReadBuffer,
    .prog_buffer = (void *)FlashProgBuffer,
    .lookahead_buffer = (void *)FlashLookBuffer,
};

int __not_in_flash_func(fs_flash_read)(const struct lfs_config * cfg, lfs_block_t block,
                                       lfs_off_t off, void * buffer, lfs_size_t size) {
    assert(off % cfg->read_size == 0);
    assert(size % cfg->read_size == 0);
    assert(block < cfg->block_count);
    uint32_t addr = XIP_BASE + RoundUpK4(TOP_OF_SYSTEM_FLASH) + (Option.modbuff ? 1024 * Option.modbuffsize : 0) + block * 4096 + off;
    memcpy(buffer, (char *)addr, size);
    return 0;
}

int __not_in_flash_func(fs_flash_prog)(const struct lfs_config * cfg, lfs_block_t block,
                                       lfs_off_t off, const void * buffer, lfs_size_t size) {
    assert(off % cfg->prog_size == 0);
    assert(size % cfg->prog_size == 0);
    assert(block < cfg->block_count);

    uint32_t addr = RoundUpK4(TOP_OF_SYSTEM_FLASH) + (Option.modbuff ? 1024 * Option.modbuffsize : 0) + block * 4096 + off;
    fileio_flash_write_begin();
    hal_flash_program(addr, buffer, size);
    fileio_flash_write_end();
    return 0;
}

int __not_in_flash_func(fs_flash_erase)(const struct lfs_config * cfg, lfs_block_t block) {
    assert(block < cfg->block_count);

    uint32_t block_addr = RoundUpK4(TOP_OF_SYSTEM_FLASH) + (Option.modbuff ? 1024 * Option.modbuffsize : 0) + block * 4096;
    fileio_flash_write_begin();
    hal_flash_erase(block_addr, BLOCK_SIZE);
    fileio_flash_write_end();
    return 0;
}

int __not_in_flash_func(fs_flash_sync)(const struct lfs_config * c) {
    (void)c;
    return 0;
}
