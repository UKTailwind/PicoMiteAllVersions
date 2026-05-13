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
#include <string.h>

#include "esp_err.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "configuration.h"
#include "hal/hal_flash.h"

extern void flash_range_erase(uint32_t off, uint32_t count);
extern void flash_range_program(uint32_t off, const uint8_t *data, size_t len);
extern unsigned char esp32_flash_option_buf[];

static int s_nvs_ready;

static int esp32_flash_ensure_nvs(void)
{
    if (s_nvs_ready) return 0;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return -EIO;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return -EIO;

    s_nvs_ready = 1;
    return 0;
}

static int esp32_options_open(nvs_handle_t *out)
{
    int rc = esp32_flash_ensure_nvs();
    if (rc != 0) return rc;
    return (nvs_open("mmbasic", NVS_READWRITE, out) == ESP_OK) ? 0 : -EIO;
}

int hal_flash_erase(uint32_t off, size_t len)
{
    if (len == 0) return 0;
    flash_range_erase(off, (uint32_t)len);
    return 0;
}

int hal_flash_program(uint32_t off, const void *buf, size_t len)
{
    if (len == 0) return 0;
    if (buf == NULL) return -EINVAL;
    flash_range_program(off, (const uint8_t *)buf, len);
    return 0;
}

int hal_flash_unique_id(uint8_t out[8])
{
    if (out == NULL) return -EINVAL;
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return -EIO;
    out[0] = 'E';
    out[1] = 'S';
    memcpy(out + 2, mac, sizeof mac);
    return 0;
}

int hal_flash_read_jedec_id(uint8_t out[4])
{
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
int hal_flash_write_active(void) { return 0; }

int hal_flash_read_options(void *buf, size_t len)
{
    if (buf == NULL) return -EINVAL;

    nvs_handle_t nvs;
    int rc = esp32_options_open(&nvs);
    if (rc != 0) {
        memset(buf, 0, len);
        return rc;
    }

    size_t stored_len = 0;
    esp_err_t err = nvs_get_blob(nvs, "options", NULL, &stored_len);
    if (err == ESP_ERR_NVS_NOT_FOUND || stored_len != len) {
        memset(buf, 0, len);
        nvs_close(nvs);
        return 0;
    }
    if (err != ESP_OK) {
        memset(buf, 0, len);
        nvs_close(nvs);
        return -EIO;
    }

    err = nvs_get_blob(nvs, "options", buf, &stored_len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        memset(buf, 0, len);
        return -EIO;
    }
    return 0;
}

int hal_flash_write_options(const void *buf, size_t len)
{
    if (buf == NULL || len == 0) return -EINVAL;

    nvs_handle_t nvs;
    int rc = esp32_options_open(&nvs);
    if (rc != 0) return rc;

    esp_err_t err = nvs_set_blob(nvs, "options", buf, len);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return -EIO;

    memcpy(esp32_flash_option_buf, buf, len);
    return 0;
}

int hal_flash_erase_program_area(void)
{
    flash_range_erase(0, MAX_PROG_SIZE);
    return 0;
}
