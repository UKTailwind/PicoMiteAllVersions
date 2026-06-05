/*
 * esp32_sd_diskio.c - FatFs diskio backend for the Adafruit Metro ESP32-S3
 * onboard microSD slot.
 *
 * The MMBasic core owns FatFs and mounts it as B:. This file only provides
 * the block-device hooks that ff.c calls.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include "ff.h"
#include "diskio.h"

#define METRO_SD_PIN_CLK GPIO_NUM_39
#define METRO_SD_PIN_MOSI GPIO_NUM_42
#define METRO_SD_PIN_MISO GPIO_NUM_21
#define METRO_SD_PIN_CS GPIO_NUM_45

#define METRO_SD_SPI_FREQ_KHZ 10000
#define METRO_SD_SECTOR_SIZE 512

static const char * TAG = "sdcard";

extern volatile BYTE SDCardStat;

static sdmmc_card_t s_card;
static sdmmc_card_t * s_cardp;
static sdmmc_host_t s_host;
static sdspi_dev_handle_t s_sdspi_handle = -1;
static int s_host_inited;

static DSTATUS sd_not_ready(void) {
    SDCardStat |= STA_NOINIT | STA_NODISK;
    return SDCardStat;
}

static esp_err_t init_sdspi_card(void) {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = METRO_SD_SPI_FREQ_KHZ;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = METRO_SD_PIN_MOSI,
        .miso_io_num = METRO_SD_PIN_MISO,
        .sclk_io_num = METRO_SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    err = host.init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sdspi_host_init failed: %s", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id = host.slot;
    dev_cfg.gpio_cs = METRO_SD_PIN_CS;

    sdspi_dev_handle_t handle = -1;
    err = sdspi_host_init_device(&dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init_device failed: %s", esp_err_to_name(err));
        return err;
    }
    if (handle != host.slot) host.slot = handle;

    memset(&s_card, 0, sizeof(s_card));
    err = sdmmc_card_init(&host, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: %s", esp_err_to_name(err));
        (void)host.deinit_p(host.slot);
        return err;
    }
    if (s_card.csd.sector_size != METRO_SD_SECTOR_SIZE) {
        ESP_LOGE(TAG, "unsupported sector size: %d", s_card.csd.sector_size);
        (void)host.deinit_p(host.slot);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_host = host;
    s_sdspi_handle = handle;
    s_host_inited = 1;
    s_cardp = &s_card;
    SDCardStat &= (BYTE) ~(STA_NOINIT | STA_NODISK | STA_PROTECT);

    ESP_LOGI(TAG, "microSD ready: %llu MB",
             ((uint64_t)s_card.csd.capacity * s_card.csd.sector_size) /
                 (1024ULL * 1024ULL));
    return ESP_OK;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    if (s_cardp) return SDCardStat;
    return init_sdspi_card() == ESP_OK ? SDCardStat : sd_not_ready();
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0 || !s_cardp) return STA_NOINIT;
    return SDCardStat;
}

static DRESULT with_sector_scratch(BYTE * scratch,
                                   BYTE * dst,
                                   const BYTE * src,
                                   LBA_t sector,
                                   UINT count,
                                   int write) {
    for (UINT i = 0; i < count; i++) {
        esp_err_t err;
        if (write) {
            memcpy(scratch, src + (size_t)i * METRO_SD_SECTOR_SIZE,
                   METRO_SD_SECTOR_SIZE);
            err = sdmmc_write_sectors(s_cardp, scratch, sector + i, 1);
        } else {
            err = sdmmc_read_sectors(s_cardp, scratch, sector + i, 1);
            if (err == ESP_OK) {
                memcpy(dst + (size_t)i * METRO_SD_SECTOR_SIZE, scratch,
                       METRO_SD_SECTOR_SIZE);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s sector %llu failed: %s",
                     write ? "write" : "read",
                     (unsigned long long)(sector + i),
                     esp_err_to_name(err));
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !s_cardp) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    BYTE * scratch = (BYTE *)heap_caps_malloc(METRO_SD_SECTOR_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!scratch) return RES_ERROR;
    DRESULT r = with_sector_scratch(scratch, buff, NULL, sector, count, 0);
    heap_caps_free(scratch);
    return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !s_cardp) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    if (SDCardStat & STA_PROTECT) return RES_WRPRT;
    BYTE * scratch = (BYTE *)heap_caps_malloc(METRO_SD_SECTOR_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!scratch) return RES_ERROR;
    DRESULT r = with_sector_scratch(scratch, NULL, buff, sector, count, 1);
    heap_caps_free(scratch);
    return r;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void * buff) {
    if (pdrv != 0 || !s_cardp) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return sdmmc_get_status(s_cardp) == ESP_OK ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!buff) return RES_PARERR;
        *(LBA_t *)buff = (LBA_t)s_cardp->csd.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buff) return RES_PARERR;
        *(WORD *)buff = METRO_SD_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buff) return RES_PARERR;
        *(DWORD *)buff = 1;
        return RES_OK;
    case MMC_GET_TYPE:
        if (!buff) return RES_PARERR;
        *(BYTE *)buff = CT_SD2 | CT_BLOCK;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void) {
    time_t now = time(NULL);
    struct tm tmv;
    if (now <= 0 || localtime_r(&now, &tmv) == NULL || tmv.tm_year + 1900 < 1980) {
        return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
    }
    return ((DWORD)(tmv.tm_year + 1900 - 1980) << 25) |
           ((DWORD)(tmv.tm_mon + 1) << 21) |
           ((DWORD)tmv.tm_mday << 16) |
           ((DWORD)tmv.tm_hour << 11) |
           ((DWORD)tmv.tm_min << 5) |
           ((DWORD)(tmv.tm_sec / 2));
}

void esp32_sd_diskio_reset(void) {
    if (s_host_inited && s_sdspi_handle >= 0) {
        (void)s_host.deinit_p(s_host.slot);
    }
    s_cardp = NULL;
    s_sdspi_handle = -1;
    s_host_inited = 0;
    SDCardStat = STA_NOINIT | STA_NODISK;
}
