/*
 * ports/pc386/hal_storage_pc386.c — block-level storage.
 *
 * Stage 2 already wired ATA-PIO into FatFs's disk_io adapter
 * directly (drivers/fatfs/ff_glue.c calls drivers/ata_pio
 * functions). This HAL surface exposes the same block layer
 * to non-FatFs callers (currently none on pc386 — LFS is
 * unused).
 *
 * Placeholder for sub-stage 3a — every entry panics. Real impl
 * in 3f passes through to drivers/ata_pio.
 */

#include "hal/hal_storage.h"
#include "pc386_panic.h"

int hal_storage_init(hal_storage_dev_t dev) {
    (void)dev;
    pc386_panic("hal_storage_init not yet implemented (3f)");
}

size_t hal_storage_block_size(hal_storage_dev_t dev) {
    (void)dev;
    pc386_panic("hal_storage_block_size not yet implemented (3f)");
}

size_t hal_storage_block_count(hal_storage_dev_t dev) {
    (void)dev;
    pc386_panic("hal_storage_block_count not yet implemented (3f)");
}

int hal_storage_read(hal_storage_dev_t dev, uint32_t lba, uint32_t count, void * buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    pc386_panic("hal_storage_read not yet implemented (3f)");
}

int hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void * buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    pc386_panic("hal_storage_write not yet implemented (3f)");
}

int hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count) {
    (void)dev;
    (void)lba;
    (void)count;
    pc386_panic("hal_storage_erase not yet implemented (3f)");
}

int hal_storage_sync(hal_storage_dev_t dev) {
    (void)dev;
    return HAL_STORAGE_OK;
}

bool hal_storage_present(hal_storage_dev_t dev) {
    (void)dev;
    return false;
}
