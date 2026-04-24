/*
 * host/hal_storage_host.c — hal_storage null impl for the native + WASM
 * host.
 *
 * The host has no block device. The shared file-I/O layer goes through
 * hal_filesystem (POSIX), not hal_storage. These stubs exist so the
 * symbols resolve at link time; every call reports "not present" /
 * "unsupported". If shared code paths start calling hal_storage on host
 * in a way that requires real behaviour, that's a design bug at the
 * boundary, not something to paper over here.
 */

#include "hal/hal_storage.h"

int  hal_storage_init       (hal_storage_dev_t dev) { (void)dev; return HAL_STORAGE_ERR_NODISK; }
size_t hal_storage_block_size (hal_storage_dev_t dev) { (void)dev; return 0; }
size_t hal_storage_block_count(hal_storage_dev_t dev) { (void)dev; return 0; }

int  hal_storage_read (hal_storage_dev_t dev, uint32_t lba, uint32_t count,       void *buf)
{ (void)dev; (void)lba; (void)count; (void)buf; return HAL_STORAGE_ERR_UNSUPPORTED; }

int  hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf)
{ (void)dev; (void)lba; (void)count; (void)buf; return HAL_STORAGE_ERR_UNSUPPORTED; }

int  hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count)
{ (void)dev; (void)lba; (void)count; return HAL_STORAGE_ERR_UNSUPPORTED; }

int  hal_storage_sync (hal_storage_dev_t dev) { (void)dev; return HAL_STORAGE_OK; }
bool hal_storage_present(hal_storage_dev_t dev) { (void)dev; return false; }
