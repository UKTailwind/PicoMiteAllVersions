/*
 * hal_flash_esp32.c - ESP32-S3 hal_flash implementation.
 *
 * Options are persisted through ESP-IDF NVS. Program flash, saved
 * variables, and numbered BASIC flash slots route through the ESP32
 * flash_range_* adapter backed by the mmslots partition.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "configuration.h"
#include "hal/hal_flash.h"

extern void flash_range_erase(uint32_t off, uint32_t count);
extern void flash_range_program(uint32_t off, const uint8_t * data, size_t len);
extern unsigned char esp32_flash_option_buf[];

static const char * TAG = "mm_options";
static int s_nvs_ready;

#define MM_OPTIONS_PARTITION "mmslots"
#define MM_OPTIONS_OFFSET 0u
#define MM_OPTIONS_SECTOR_SIZE 4096u
#define MM_OPTIONS_MAGIC 0x314f4d4du /* MMO1 */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_len;
    uint32_t option_len;
    uint32_t checksum;
} esp32_options_header_t;

static uint32_t esp32_options_checksum(const uint8_t * data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static const esp_partition_t * esp32_options_partition(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    MM_OPTIONS_PARTITION);
}

static int esp32_options_write_raw(const void * buf, size_t len) {
    const esp_partition_t * part = esp32_options_partition();
    if (!part || part->size < MM_OPTIONS_SECTOR_SIZE) return -EIO;
    if (len > MM_OPTIONS_SECTOR_SIZE - sizeof(esp32_options_header_t))
        return -ENOSPC;

    esp32_options_header_t hdr = {
        .magic = MM_OPTIONS_MAGIC,
        .version = 1,
        .header_len = sizeof hdr,
        .option_len = (uint32_t)len,
        .checksum = esp32_options_checksum((const uint8_t *)buf, len),
    };

    uint8_t * sector = malloc(MM_OPTIONS_SECTOR_SIZE);
    if (!sector) return -ENOMEM;
    memset(sector, 0xff, MM_OPTIONS_SECTOR_SIZE);
    memcpy(sector, &hdr, sizeof hdr);
    memcpy(sector + sizeof hdr, buf, len);

    esp_err_t err = esp_partition_erase_range(part, MM_OPTIONS_OFFSET,
                                              MM_OPTIONS_SECTOR_SIZE);
    if (err == ESP_OK) {
        err = esp_partition_write(part, MM_OPTIONS_OFFSET, sector,
                                  MM_OPTIONS_SECTOR_SIZE);
    }
    free(sector);
    return err == ESP_OK ? 0 : -EIO;
}

static int esp32_flash_ensure_nvs(void) {
    if (s_nvs_ready) return 0;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return -EIO;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return -EIO;

    s_nvs_ready = 1;
    return 0;
}

static int esp32_options_open(nvs_handle_t * out) {
    int rc = esp32_flash_ensure_nvs();
    if (rc != 0) return rc;
    return (nvs_open("mmbasic", NVS_READWRITE, out) == ESP_OK) ? 0 : -EIO;
}

int hal_flash_erase(uint32_t off, size_t len) {
    if (len == 0) return 0;
    flash_range_erase(off, (uint32_t)len);
    return 0;
}

int hal_flash_program(uint32_t off, const void * buf, size_t len) {
    if (len == 0) return 0;
    if (buf == NULL) return -EINVAL;
    flash_range_program(off, (const uint8_t *)buf, len);
    return 0;
}

int hal_flash_unique_id(uint8_t out[8]) {
    if (out == NULL) return -EINVAL;
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return -EIO;
    out[0] = 'E';
    out[1] = 'S';
    memcpy(out + 2, mac, sizeof mac);
    return 0;
}

int hal_flash_read_jedec_id(uint8_t out[4]) {
    if (out == NULL) return -EINVAL;

    uint32_t size = 0;
    if (esp_flash_get_size(NULL, &size) != ESP_OK || size == 0) {
        size = 16u * 1024u * 1024u;
    }

    uint8_t cap = 0;
    while ((1u << cap) < size && cap < 31) cap++;

    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = cap;
    return 0;
}

void hal_flash_write_begin(void) {}
void hal_flash_write_end(void) {}
int hal_flash_write_active(void) {
    return 0;
}

int hal_flash_read_options(void * buf, size_t len) {
    if (buf == NULL) return -EINVAL;

    const esp_partition_t * part = esp32_options_partition();
    if (part && part->size >= MM_OPTIONS_SECTOR_SIZE) {
        esp32_options_header_t hdr;
        esp_err_t err = esp_partition_read(part, MM_OPTIONS_OFFSET, &hdr,
                                           sizeof hdr);
        if (err == ESP_OK && hdr.magic == MM_OPTIONS_MAGIC &&
            hdr.version == 1 && hdr.header_len == sizeof hdr &&
            hdr.option_len > 0 && hdr.option_len <= len &&
            hdr.option_len <= MM_OPTIONS_SECTOR_SIZE - sizeof hdr) {
            memset(buf, 0, len);
            err = esp_partition_read(part, MM_OPTIONS_OFFSET + sizeof hdr, buf,
                                     hdr.option_len);
            if (err == ESP_OK &&
                esp32_options_checksum((const uint8_t *)buf, hdr.option_len) ==
                    hdr.checksum) {
                ESP_LOGI(TAG, "loaded options from raw mmslots sector");
                return 0;
            }
        }
    }

    /* Compatibility fallback for images that stored MMBasic options in NVS.
     * Do not erase NVS on ESP_ERR_NVS_NO_FREE_PAGES; that was the source of
     * apparently spontaneous option loss after repeated saves. */
    nvs_handle_t nvs;
    int rc = esp32_options_open(&nvs);
    if (rc != 0) {
        memset(buf, 0, len);
        return rc;
    }

    size_t stored_len = 0;
    esp_err_t err = nvs_get_blob(nvs, "options", NULL, &stored_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(buf, 0, len);
        nvs_close(nvs);
        return 0;
    }
    if (err != ESP_OK) {
        memset(buf, 0, len);
        nvs_close(nvs);
        return -EIO;
    }

    uint8_t * tmp = malloc(stored_len);
    if (!tmp) {
        memset(buf, 0, len);
        nvs_close(nvs);
        return -ENOMEM;
    }
    err = nvs_get_blob(nvs, "options", tmp, &stored_len);
    if (err != ESP_OK) {
        free(tmp);
        memset(buf, 0, len);
        nvs_close(nvs);
        return -EIO;
    }
    memset(buf, 0, len);
    memcpy(buf, tmp, stored_len < len ? stored_len : len);
    free(tmp);
    if (esp32_options_write_raw(buf, len) == 0) {
        ESP_LOGW(TAG, "migrated MMBasic options from NVS to raw mmslots sector");
        nvs_erase_key(nvs, "options");
        nvs_commit(nvs);
    } else {
        ESP_LOGE(TAG, "failed to migrate MMBasic options from NVS");
    }
    nvs_close(nvs);
    return 0;
}

int hal_flash_write_options(const void * buf, size_t len) {
    if (buf == NULL || len == 0) return -EINVAL;

    int rc = esp32_options_write_raw(buf, len);
    if (rc != 0) return rc;

    memcpy(esp32_flash_option_buf, buf, len);
    ESP_LOGI(TAG, "saved options to raw mmslots sector");
    return 0;

#if 0
    /* Retained for reference: old NVS-backed options storage. */

    nvs_handle_t nvs;
    int rc = esp32_options_open(&nvs);
    if (rc != 0) return rc;

    esp_err_t err = nvs_set_blob(nvs, "options", buf, len);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return -EIO;

    memcpy(esp32_flash_option_buf, buf, len);
    return 0;
#endif
}

int hal_flash_erase_program_area(void) {
    flash_range_erase(0, MAX_PROG_SIZE);
    return 0;
}
