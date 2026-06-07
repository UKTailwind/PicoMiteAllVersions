/*
 * esp32_sd_diskio.c - FatFs SDSPI backend for ESP32-S3 board profiles.
 *
 * The MMBasic core owns FatFs and mounts it as B:. This file only provides
 * the block-device hooks that ff.c calls.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include "ff.h"
#include "esp32_board_profile.h"
#include "diskio.h"

static const char * TAG = "sdcard";

extern volatile BYTE SDCardStat;

static sdmmc_card_t s_card;
static sdmmc_card_t * s_cardp;
static sdmmc_host_t s_host;
static sdspi_dev_handle_t s_sdspi_handle = -1;
static int s_host_inited;
static spi_host_device_t s_spi_host_id = SDSPI_DEFAULT_HOST;
static int s_spi_bus_inited;

static void reset_sdspi_state(void);

static DSTATUS sd_not_ready(void) {
    reset_sdspi_state();
    SDCardStat = STA_NOINIT | STA_NODISK;
    return SDCardStat;
}

static DRESULT sd_io_failed(const char * op, LBA_t sector, esp_err_t err) {
    if (op) {
        ESP_LOGE(TAG, "%s sector %llu failed: %s", op,
                 (unsigned long long)sector, esp_err_to_name(err));
    }
    sd_not_ready();
    return RES_ERROR;
}

static int sd_gpio_is_set(int gpio) {
    return gpio != ESP32_BOARD_PROFILE_NO_PIN;
}

static void enable_sd_pullup(const esp32_board_profile_t * profile,
                             const char * signal,
                             int gpio) {
    if (!sd_gpio_is_set(gpio)) return;

    esp_err_t err = gpio_set_pull_mode((gpio_num_t)gpio, GPIO_PULLUP_ONLY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s SD %s pull-up on GPIO%d failed: %s",
                 profile->platform_name, signal, gpio, esp_err_to_name(err));
    }
}

static void enable_profile_sd_pullups(const esp32_board_profile_t * profile) {
    /*
     * SD cards require pull-ups on CMD and DAT lines. In SDSPI mode those are
     * MOSI/CMD, MISO/D0, CS/D3; D1/D2 remain idle but are still card contacts
     * on boards that expose them in the profile.
     */
    enable_sd_pullup(profile, "CMD/MOSI", profile->sd.mosi);
    enable_sd_pullup(profile, "D0/MISO", profile->sd.miso);
    enable_sd_pullup(profile, "D3/CS", profile->sd.cs);
    enable_sd_pullup(profile, "D1", profile->sd.d1);
    enable_sd_pullup(profile, "D2", profile->sd.d2);
}

static esp_err_t validate_profile_sd_config(const esp32_board_profile_t * profile) {
    if (!sd_gpio_is_set(profile->sd.sclk) ||
        !sd_gpio_is_set(profile->sd.mosi) ||
        !sd_gpio_is_set(profile->sd.miso) ||
        !sd_gpio_is_set(profile->sd.cs) ||
        profile->sd.spi_freq_khz <= 0) {
        ESP_LOGE(TAG, "%s SD profile is incomplete", profile->platform_name);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void deinit_sdspi_card(sdmmc_host_t * host,
                              sdspi_dev_handle_t handle,
                              spi_host_device_t host_id,
                              int remove_device,
                              int host_inited,
                              int bus_inited) {
    if (remove_device && handle != -1) {
        (void)host->deinit_p(handle);
    }
    if (host_inited) {
        (void)sdspi_host_deinit();
    }
    if (bus_inited) {
        (void)spi_bus_free(host_id);
    }
}

static esp_err_t init_sdspi_card(void) {
    const esp32_board_profile_t * profile = esp32_board_profile_current();
    if (!profile->has_sd) {
        ESP_LOGW(TAG, "selected board profile has no SD socket");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = validate_profile_sd_config(profile);
    if (err != ESP_OK) return err;
    enable_profile_sd_pullups(profile);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = profile->sd.spi_freq_khz;
    spi_host_device_t host_id = host.slot;
    int bus_inited = 0;
    int host_inited = 0;
    int device_inited = 0;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = profile->sd.mosi,
        .miso_io_num = profile->sd.miso,
        .sclk_io_num = profile->sd.sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    bus_inited = 1;

    err = host.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init failed: %s", esp_err_to_name(err));
        deinit_sdspi_card(&host, -1, host_id, 0, 0, bus_inited);
        return err;
    }
    host_inited = 1;

    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id = host.slot;
    dev_cfg.gpio_cs = profile->sd.cs;

    sdspi_dev_handle_t handle = -1;
    err = sdspi_host_init_device(&dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init_device failed: %s", esp_err_to_name(err));
        deinit_sdspi_card(&host, -1, host_id, 0, host_inited, bus_inited);
        return err;
    }
    device_inited = 1;
    if (handle != host.slot) host.slot = handle;

    memset(&s_card, 0, sizeof(s_card));
    err = sdmmc_card_init(&host, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: %s", esp_err_to_name(err));
        deinit_sdspi_card(&host, handle, host_id, device_inited, host_inited, bus_inited);
        return err;
    }
    if (s_card.csd.sector_size != ESP32_BOARD_PROFILE_SD_SECTOR_SIZE) {
        ESP_LOGE(TAG, "unsupported sector size: %d", s_card.csd.sector_size);
        deinit_sdspi_card(&host, handle, host_id, device_inited, host_inited, bus_inited);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_host = host;
    s_sdspi_handle = handle;
    s_host_inited = 1;
    s_spi_host_id = host_id;
    s_spi_bus_inited = 1;
    s_cardp = &s_card;
    SDCardStat &= (BYTE) ~(STA_NOINIT | STA_NODISK | STA_PROTECT);

    ESP_LOGI(TAG, "microSD ready: %llu MB",
             ((uint64_t)s_card.csd.capacity * s_card.csd.sector_size) /
                 (1024ULL * 1024ULL));
    return ESP_OK;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    if (s_cardp && !(SDCardStat & STA_NOINIT)) return SDCardStat;
    if (s_cardp) reset_sdspi_state();
    return init_sdspi_card() == ESP_OK ? SDCardStat : sd_not_ready();
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0 || !s_cardp) return SDCardStat | STA_NOINIT;
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
            memcpy(scratch, src + (size_t)i * ESP32_BOARD_PROFILE_SD_SECTOR_SIZE,
                   ESP32_BOARD_PROFILE_SD_SECTOR_SIZE);
            err = sdmmc_write_sectors(s_cardp, scratch, sector + i, 1);
        } else {
            err = sdmmc_read_sectors(s_cardp, scratch, sector + i, 1);
            if (err == ESP_OK) {
                memcpy(dst + (size_t)i * ESP32_BOARD_PROFILE_SD_SECTOR_SIZE, scratch,
                       ESP32_BOARD_PROFILE_SD_SECTOR_SIZE);
            }
        }
        if (err != ESP_OK)
            return sd_io_failed(write ? "write" : "read", sector + i, err);
    }
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !s_cardp || (SDCardStat & STA_NOINIT)) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    BYTE * scratch = (BYTE *)heap_caps_malloc(ESP32_BOARD_PROFILE_SD_SECTOR_SIZE,
                                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!scratch) return RES_ERROR;
    DRESULT r = with_sector_scratch(scratch, buff, NULL, sector, count, 0);
    heap_caps_free(scratch);
    return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE * buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || !s_cardp || (SDCardStat & STA_NOINIT)) return RES_NOTRDY;
    if (!buff || count == 0) return RES_PARERR;
    if (SDCardStat & STA_PROTECT) return RES_WRPRT;
    BYTE * scratch = (BYTE *)heap_caps_malloc(ESP32_BOARD_PROFILE_SD_SECTOR_SIZE,
                                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!scratch) return RES_ERROR;
    DRESULT r = with_sector_scratch(scratch, NULL, buff, sector, count, 1);
    heap_caps_free(scratch);
    return r;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void * buff) {
    if (pdrv != 0 || !s_cardp || (SDCardStat & STA_NOINIT)) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC: {
        esp_err_t err = sdmmc_get_status(s_cardp);
        return err == ESP_OK ? RES_OK : sd_io_failed("status", 0, err);
    }
    case GET_SECTOR_COUNT:
        if (!buff) return RES_PARERR;
        *(LBA_t *)buff = (LBA_t)s_cardp->csd.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buff) return RES_PARERR;
        *(WORD *)buff = ESP32_BOARD_PROFILE_SD_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buff) return RES_PARERR;
        *(DWORD *)buff = 1;
        return RES_OK;
    case MMC_GET_TYPE:
        if (!buff) return RES_PARERR;
        *(BYTE *)buff = CT_SD2 | CT_BLOCK;
        return RES_OK;
    case MMC_GET_OCR: {
        if (!buff) return RES_PARERR;
        esp_err_t err = sdmmc_get_status(s_cardp);
        if (err != ESP_OK) return sd_io_failed("status", 0, err);
        uint32_t ocr = s_cardp->ocr;
        BYTE * out = (BYTE *)buff;
        out[0] = (BYTE)(ocr >> 24);
        out[1] = (BYTE)(ocr >> 16);
        out[2] = (BYTE)(ocr >> 8);
        out[3] = (BYTE)ocr;
        return RES_OK;
    }
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

static void reset_sdspi_state(void) {
    deinit_sdspi_card(&s_host, s_sdspi_handle, s_spi_host_id,
                      s_sdspi_handle != -1, s_host_inited, s_spi_bus_inited);
    s_cardp = NULL;
    s_sdspi_handle = -1;
    s_host_inited = 0;
    s_spi_bus_inited = 0;
    s_spi_host_id = SDSPI_DEFAULT_HOST;
    memset(&s_card, 0, sizeof(s_card));
    memset(&s_host, 0, sizeof(s_host));
}

void esp32_sd_diskio_reset(void) {
    reset_sdspi_state();
    SDCardStat = STA_NOINIT | STA_NODISK;
}
