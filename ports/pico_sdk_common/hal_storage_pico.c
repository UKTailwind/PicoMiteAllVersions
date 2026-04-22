/*
 * ports/pico_sdk_common/hal_storage_pico.c — hal_storage over the
 * existing FatFS diskio and LFS-backing flash callbacks.
 *
 * HAL_STORAGE_DEV_SDCARD         -> FatFS disk_* (mmc_stm32.c)
 * HAL_STORAGE_DEV_INTERNAL_FLASH -> fs_flash_*  (FileIO.c) via pico_lfs_cfg
 *
 * Thin adapter, no behaviour change. The underlying implementations
 * stay where they are until their driver homes (drivers/sd_spi/,
 * drivers/pico_flash/) land as part of the later phase that extracts
 * them.
 */

#include <stddef.h>
#include <stdint.h>

#include "hal/hal_storage.h"

/* -- FatFS diskio shape (ff/ff.h / ff/diskio.h). We forward-declare
 *    locally so this TU doesn't need the full FatFS header tree. ----- */
typedef uint8_t  BYTE;
typedef uint32_t LBA_t;
typedef unsigned UINT;
typedef BYTE DSTATUS;
typedef enum { RES_OK = 0, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;

extern DSTATUS disk_status    (BYTE pdrv);
extern DSTATUS disk_initialize(BYTE pdrv);
extern DRESULT disk_read      (BYTE pdrv,       BYTE *buf, LBA_t lba, UINT count);
extern DRESULT disk_write     (BYTE pdrv, const BYTE *buf, LBA_t lba, UINT count);
extern DRESULT disk_ioctl     (BYTE pdrv, BYTE cmd, void *buf);

/* FatFS STA_* and ioctl commands (from diskio.h). */
#define STA_NOINIT    0x01
#define STA_NODISK    0x02
#define STA_PROTECT   0x04

#define CTRL_SYNC         0
#define GET_SECTOR_COUNT  1
#define GET_SECTOR_SIZE   2
#define GET_BLOCK_SIZE    3
#define CTRL_TRIM         4

/* -- LFS backing (FileIO.c). We read pico_lfs_cfg's block_size /
 *    block_count fields to report INTERNAL_FLASH geometry. The struct
 *    type is opaque here; we access leading fields via a minimal shape
 *    matching lfs.h's public layout. ---------------------------------- */
struct lfs_config;
extern const struct lfs_config pico_lfs_cfg;

/* Mirror leading portion of struct lfs_config (lfs.h) so we can read
 * block_size / block_count without pulling lfs.h into this TU. The
 * fields are stable ABI within the vendored littlefs copy. */
struct lfs_cfg_shape {
    void *context;
    int (*read) (const struct lfs_config *, uint32_t, uint32_t,       void *, uint32_t);
    int (*prog) (const struct lfs_config *, uint32_t, uint32_t, const void *, uint32_t);
    int (*erase)(const struct lfs_config *, uint32_t);
    int (*sync) (const struct lfs_config *);
    uint32_t read_size;
    uint32_t prog_size;
    uint32_t block_size;
    uint32_t block_count;
};

/* -------------------------------------------------------------------- */

int hal_storage_init(hal_storage_dev_t dev)
{
    switch (dev) {
    case HAL_STORAGE_DEV_SDCARD: {
        DSTATUS s = disk_initialize(0);
        if (s & STA_NODISK) return HAL_STORAGE_ERR_NODISK;
        if (s & STA_NOINIT) return HAL_STORAGE_ERR_IO;
        return HAL_STORAGE_OK;
    }
    case HAL_STORAGE_DEV_INTERNAL_FLASH:
        /* XIP-mapped; no init required. */
        return HAL_STORAGE_OK;
    default:
        return HAL_STORAGE_ERR_BAD_ARG;
    }
}

size_t hal_storage_block_size(hal_storage_dev_t dev)
{
    if (dev == HAL_STORAGE_DEV_SDCARD) {
        uint32_t sz = 0;
        if (disk_ioctl(0, GET_SECTOR_SIZE, &sz) == RES_OK && sz) return sz;
        return 512;
    }
    if (dev == HAL_STORAGE_DEV_INTERNAL_FLASH) {
        const struct lfs_cfg_shape *c = (const struct lfs_cfg_shape *)&pico_lfs_cfg;
        return c->block_size;
    }
    return 0;
}

size_t hal_storage_block_count(hal_storage_dev_t dev)
{
    if (dev == HAL_STORAGE_DEV_SDCARD) {
        LBA_t n = 0;
        if (disk_ioctl(0, GET_SECTOR_COUNT, &n) == RES_OK) return (size_t)n;
        return 0;
    }
    if (dev == HAL_STORAGE_DEV_INTERNAL_FLASH) {
        const struct lfs_cfg_shape *c = (const struct lfs_cfg_shape *)&pico_lfs_cfg;
        return c->block_count;
    }
    return 0;
}

static int sd_dresult_to_hal(DRESULT r)
{
    switch (r) {
    case RES_OK:     return HAL_STORAGE_OK;
    case RES_NOTRDY: return HAL_STORAGE_ERR_NODISK;
    case RES_WRPRT:  return HAL_STORAGE_ERR_PROTECTED;
    default:         return HAL_STORAGE_ERR_IO;
    }
}

int hal_storage_read(hal_storage_dev_t dev, uint32_t lba, uint32_t count, void *buf)
{
    if (dev != HAL_STORAGE_DEV_SDCARD) return HAL_STORAGE_ERR_UNSUPPORTED;
    return sd_dresult_to_hal(disk_read(0, (BYTE *)buf, lba, count));
}

int hal_storage_write(hal_storage_dev_t dev, uint32_t lba, uint32_t count, const void *buf)
{
    if (dev != HAL_STORAGE_DEV_SDCARD) return HAL_STORAGE_ERR_UNSUPPORTED;
    return sd_dresult_to_hal(disk_write(0, (const BYTE *)buf, lba, count));
}

int hal_storage_erase(hal_storage_dev_t dev, uint32_t lba, uint32_t count)
{
    if (dev == HAL_STORAGE_DEV_SDCARD) {
        LBA_t range[2] = { lba, lba + count - 1 };
        (void)disk_ioctl(0, CTRL_TRIM, range);   /* advisory */
        return HAL_STORAGE_OK;
    }
    /* INTERNAL_FLASH erase flows through littlefs' own erase callback. */
    (void)count;
    return HAL_STORAGE_OK;
}

int hal_storage_sync(hal_storage_dev_t dev)
{
    if (dev == HAL_STORAGE_DEV_SDCARD) {
        return (disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK) ? HAL_STORAGE_OK : HAL_STORAGE_ERR_IO;
    }
    return HAL_STORAGE_OK;
}

bool hal_storage_present(hal_storage_dev_t dev)
{
    if (dev == HAL_STORAGE_DEV_SDCARD) {
        return (disk_status(0) & (STA_NOINIT | STA_NODISK)) == 0;
    }
    return dev == HAL_STORAGE_DEV_INTERNAL_FLASH;
}
