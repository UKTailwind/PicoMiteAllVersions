/*
 * hal/hal_storage.h — block-level persistent storage (SD card, internal
 * flash as LFS backing).
 *
 * Callers address the device via a small integer ID (hal_storage_dev_t).
 * The HAL does not name block sizes or counts in the header — the impl
 * reports them. Writes are not required to be atomic; callers that need
 * durability call hal_storage_sync.
 *
 * Return codes: 0 = success, negative = errno-style error.
 * Sentinel codes the upper layer cares about:
 *   HAL_STORAGE_ERR_NODISK        — media not present
 *   HAL_STORAGE_ERR_MEDIA_CHANGED — media swapped since last op
 *   HAL_STORAGE_ERR_IO            — underlying bus/controller failure
 *   HAL_STORAGE_ERR_PROTECTED     — write-protect asserted
 *
 * Global HAL conventions apply (see hal/CONTRACT.md).
 */

#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_STORAGE_DEV_SDCARD = 0,      /* removable SD/MMC (FatFS) */
    HAL_STORAGE_DEV_INTERNAL_FLASH,  /* internal flash backing LFS (A: on device) */
    HAL_STORAGE_DEV_COUNT,
} hal_storage_dev_t;

#define HAL_STORAGE_OK                  0
#define HAL_STORAGE_ERR_IO             -1
#define HAL_STORAGE_ERR_NODISK         -2
#define HAL_STORAGE_ERR_MEDIA_CHANGED  -3
#define HAL_STORAGE_ERR_PROTECTED      -4
#define HAL_STORAGE_ERR_BAD_ARG        -5
#define HAL_STORAGE_ERR_UNSUPPORTED    -6

int    hal_storage_init       (hal_storage_dev_t dev);
size_t hal_storage_block_size (hal_storage_dev_t dev);
size_t hal_storage_block_count(hal_storage_dev_t dev);
int    hal_storage_read       (hal_storage_dev_t dev, uint32_t lba, uint32_t count,       void *buf);
int    hal_storage_write      (hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf);
int    hal_storage_erase      (hal_storage_dev_t dev, uint32_t lba, uint32_t count);
int    hal_storage_sync       (hal_storage_dev_t dev);
bool   hal_storage_present    (hal_storage_dev_t dev);

#ifdef __cplusplus
}
#endif

#endif  /* HAL_STORAGE_H */
