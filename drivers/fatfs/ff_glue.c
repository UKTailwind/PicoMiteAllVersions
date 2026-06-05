/*
 * drivers/fatfs/ff_glue.c — FatFs disk_io adapter for the pc386 port.
 *
 * Maps FatFs physical-drive IDs onto the pc386 storage layout:
 *   pdrv 0  ->  A: drive (FDC drive 0, 1.44 MB floppy)
 *   pdrv 1  ->  B: drive (FDC drive 1, 1.44 MB floppy if present)
 *   pdrv 2  ->  C: drive (ATA primary master)
 *
 * FatFs configuration: see ffconf.h at repo root. Notable settings:
 *   FF_VOLUMES   = 3   (matches A: + B: + C:)
 *   FF_MIN/MAX_SS = 512
 *   FF_LBA64     = 0   (LBA28 is sufficient; ATA driver caps at 128 GB)
 *   FF_FS_REENTRANT = 0 (single-threaded kernel, no mutex needed)
 *   FF_USE_LFN   = 1   (static LFN buffer; no ff_memalloc path)
 *   FF_FS_NORTC  = 0   (we provide a get_fattime stub below)
 */

#include "ff.h"
#include "diskio.h"

#include "../../drivers/ata_pio/ata_pio.h"
#include "../../drivers/fdc_82077/fdc_82077.h"

static unsigned ata_drive_for_pdrv(BYTE pdrv) {
    switch (pdrv) {
    case 2:
        return ATA_PRIMARY_MASTER;
    default:
        return ATA_DRIVE_COUNT; /* invalid */
    }
}

DSTATUS disk_initialize(BYTE pdrv) {
#ifdef FATFS_NO_FDC
    if (pdrv == 0 || pdrv == 1) return STA_NODISK;
#else
    if (pdrv == 0 || pdrv == 1) return fdc_present(pdrv) ? 0 : STA_NODISK;
#endif
    unsigned drive = ata_drive_for_pdrv(pdrv);
    if (drive >= ATA_DRIVE_COUNT) return STA_NODISK;
    const ata_drive_info_t * info = ata_drive(drive);
    if (!info || !info->present) return STA_NODISK;
    return 0; /* ready */
}

DSTATUS disk_status(BYTE pdrv) {
    return disk_initialize(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv == 0 || pdrv == 1) {
#ifdef FATFS_NO_FDC
        (void)buff;
        (void)sector;
        (void)count;
        return RES_NOTRDY;
#else
        while (count > 0) {
            UINT chunk = (count > 255) ? 255 : count;
            if (fdc_read_sectors(pdrv, (uint32_t)sector, (uint8_t)chunk, buff) != 0) {
                return RES_ERROR;
            }
            buff += (size_t)chunk * 512;
            sector += chunk;
            count -= chunk;
        }
        return RES_OK;
#endif
    }
    unsigned drive = ata_drive_for_pdrv(pdrv);
    if (drive >= ATA_DRIVE_COUNT) return RES_PARERR;
    while (count > 0) {
        /* LBA28 sector-count register is 8-bit (max 256 sectors per
         * command; 0 == 256). Cap at 255 to keep the cast obvious. */
        UINT chunk = (count > 255) ? 255 : count;
        if (ata_read_sectors(drive, (uint32_t)sector,
                             (uint8_t)chunk, buff) != 0) {
            return RES_ERROR;
        }
        buff += (size_t)chunk * 512;
        sector += chunk;
        count -= chunk;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv == 0 || pdrv == 1) {
        (void)buff;
        (void)sector;
        (void)count;
        return RES_WRPRT;
    }
    unsigned drive = ata_drive_for_pdrv(pdrv);
    if (drive >= ATA_DRIVE_COUNT) return RES_PARERR;
    while (count > 0) {
        UINT chunk = (count > 255) ? 255 : count;
        if (ata_write_sectors(drive, (uint32_t)sector,
                              (uint8_t)chunk, buff) != 0) {
            return RES_ERROR;
        }
        buff += (size_t)chunk * 512;
        sector += chunk;
        count -= chunk;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void * buff) {
    if (pdrv == 0 || pdrv == 1) {
#ifdef FATFS_NO_FDC
        (void)cmd;
        (void)buff;
        return RES_NOTRDY;
#else
        if (!fdc_present(pdrv)) return RES_NOTRDY;
        switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = FDC_1440_SECTORS;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        }
        return RES_PARERR;
#endif
    }
    unsigned drive = ata_drive_for_pdrv(pdrv);
    if (drive >= ATA_DRIVE_COUNT) return RES_PARERR;
    const ata_drive_info_t * info = ata_drive(drive);
    if (!info || !info->present) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        /* Every disk_write already issues FLUSH CACHE per the
             * ATA-PIO driver's per-sector loop; nothing else to do. */
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = info->sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        /* No erase-block concept on a real spinning/SSD disk
             * surface; FatFs uses this only for FF_USE_MKFS=1, and
             * a value of 1 means "no preferred alignment". */
        *(DWORD *)buff = 1;
        return RES_OK;
    }
    return RES_PARERR;
}

/* No RTC yet. Return a fixed date until Stage 4 lands the PIT-driven
 * tick or a full RTC driver. Format (FatFs):
 *   bit 31:25 = year - 1980
 *   bit 24:21 = month (1..12)
 *   bit 20:16 = day (1..31)
 *   bit 15:11 = hour (0..23)
 *   bit 10:5  = minute (0..59)
 *   bit  4:0  = second / 2 (0..29) */
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)5 << 21) | ((DWORD)10 << 16) | ((DWORD)12 << 11) | ((DWORD)0 << 5) | ((DWORD)0 << 0);
}
