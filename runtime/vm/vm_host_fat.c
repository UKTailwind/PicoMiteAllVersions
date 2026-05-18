#include "MMBasic_Includes.h"

#ifdef MMBASIC_HOST

#include <stdlib.h>
#include <string.h>

#include "vm_host_fat.h"
#include "diskio.h"

#define HOST_FAT_SECTOR_SIZE 512
#define HOST_FAT_SECTORS     16384

static BYTE *host_fat_disk;
static FATFS host_fat_fs;
static int host_fat_mounted;
static char host_fat_path_buf[FF_MAX_LFN + 1];

static int host_fat_ready(void) {
    if (host_fat_disk) return 1;
    host_fat_disk = (BYTE *)calloc(HOST_FAT_SECTORS, HOST_FAT_SECTOR_SIZE);
    return host_fat_disk != NULL;
}

void vm_host_fat_reset(void) {
    if (host_fat_mounted) {
        f_mount(NULL, "", 0);
        host_fat_mounted = 0;
    }
    if (host_fat_disk) {
        memset(host_fat_disk, 0, (size_t)HOST_FAT_SECTORS * HOST_FAT_SECTOR_SIZE);
    }
}

FRESULT vm_host_fat_mount(void) {
    if (host_fat_mounted) return FR_OK;
    if (!host_fat_ready()) return FR_NOT_ENOUGH_CORE;

    MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
    BYTE work[HOST_FAT_SECTOR_SIZE];
    FRESULT res = f_mkfs("", &opt, work, sizeof(work));
    if (res != FR_OK) return res;

    res = f_mount(&host_fat_fs, "", 1);
    if (res == FR_OK) host_fat_mounted = 1;
    return res;
}

const char *vm_host_fat_path(const char *filename) {
    const char *name = filename;

    if (name[0] == '/' &&
        (name[1] == 'B' || name[1] == 'b' || name[1] == 'C' || name[1] == 'c') &&
        name[2] == ':') {
        name += 3;
    } else if ((name[0] == 'B' || name[0] == 'b' || name[0] == 'C' || name[0] == 'c') &&
               name[1] == ':') {
        name += 2;
    }

    if (*name == '\0') name = "/";
    if (*name == '/') {
        strncpy(host_fat_path_buf, name, sizeof(host_fat_path_buf) - 1);
    } else {
        host_fat_path_buf[0] = '/';
        strncpy(host_fat_path_buf + 1, name, sizeof(host_fat_path_buf) - 2);
    }
    host_fat_path_buf[sizeof(host_fat_path_buf) - 1] = '\0';
    return host_fat_path_buf;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES) return STA_NOINIT;
    return host_fat_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !host_fat_disk) return STA_NOINIT;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !host_fat_disk || !buff) return RES_NOTRDY;
    if (sector + count > HOST_FAT_SECTORS) return RES_PARERR;
    memcpy(buff, host_fat_disk + (size_t)sector * HOST_FAT_SECTOR_SIZE,
           (size_t)count * HOST_FAT_SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !host_fat_disk || !buff) return RES_NOTRDY;
    if (sector + count > HOST_FAT_SECTORS) return RES_PARERR;
    memcpy(host_fat_disk + (size_t)sector * HOST_FAT_SECTOR_SIZE, buff,
           (size_t)count * HOST_FAT_SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv >= FF_VOLUMES || !host_fat_disk) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = HOST_FAT_SECTORS;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = HOST_FAT_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

#endif
