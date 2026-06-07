/*
 * esp32_lfs.c — LittleFS over esp_partition_* for the A: drive.
 *
 * Models drivers/pico_flash/pico_flash_lfs.c on PicoMite: provides
 * pico_lfs_cfg with read/prog/erase/sync callbacks the vendored
 * lfs.c calls into. Backing store is the "lfsdata" flash partition
 * (4 KB native erase block, matches LFS's native block size — no
 * wear-leveling library needed).
 *
 * Boot flow: ensure_mounted() opens the partition, attempts lfs_mount,
 * and lfs_format-then-lfs_mount on FR_NO_FILESYSTEM equivalent.
 * Idempotent; called from hal_filesystem_esp32 on first path op.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_partition.h"

#include "lfs.h"

#define BLOCK_SIZE 4096 /* matches ESP32 internal flash erase */
#define READ_SIZE 1
#define PROG_SIZE 256
#define CACHE_SIZE 256
#define LOOKAHEAD_SIZE 256

static const esp_partition_t * s_part = NULL;

static int esp_lfs_read(const struct lfs_config * c, lfs_block_t block,
                        lfs_off_t off, void * buf, lfs_size_t size) {
    (void)c;
    if (!s_part) return LFS_ERR_IO;
    if (esp_partition_read(s_part, block * BLOCK_SIZE + off, buf, size) != ESP_OK)
        return LFS_ERR_IO;
    return 0;
}

static int esp_lfs_prog(const struct lfs_config * c, lfs_block_t block,
                        lfs_off_t off, const void * buf, lfs_size_t size) {
    (void)c;
    if (!s_part) return LFS_ERR_IO;
    if (esp_partition_write(s_part, block * BLOCK_SIZE + off, buf, size) != ESP_OK)
        return LFS_ERR_IO;
    return 0;
}

static int esp_lfs_erase(const struct lfs_config * c, lfs_block_t block) {
    (void)c;
    if (!s_part) return LFS_ERR_IO;
    if (esp_partition_erase_range(s_part, block * BLOCK_SIZE, BLOCK_SIZE) != ESP_OK)
        return LFS_ERR_IO;
    return 0;
}

static int esp_lfs_sync(const struct lfs_config * c) {
    (void)c;
    return 0;
}

static uint8_t s_read_buf[CACHE_SIZE];
static uint8_t s_prog_buf[CACHE_SIZE];
static uint8_t s_lookahead_buf[LOOKAHEAD_SIZE];

struct lfs_config pico_lfs_cfg = {
    .read = esp_lfs_read,
    .prog = esp_lfs_prog,
    .erase = esp_lfs_erase,
    .sync = esp_lfs_sync,

    .read_size = READ_SIZE,
    .prog_size = PROG_SIZE,
    .block_size = BLOCK_SIZE,
    .block_count = 0, /* filled at boot from partition size */
    .block_cycles = 500,
    .cache_size = CACHE_SIZE,
    .lookahead_size = LOOKAHEAD_SIZE,

    .read_buffer = s_read_buf,
    .prog_buffer = s_prog_buf,
    .lookahead_buffer = s_lookahead_buf,
};

/* Single LFS instance shared with FileIO.c via the `extern lfs_t lfs`. */
lfs_t lfs;

static int s_mounted = 0;

/* Embedded demo files. IDF's EMBED_TXTFILES generates these symbols
 * as `_binary_<filename>_start/end`. The asm() rename matches
 * IDF's mangling (dots replaced with underscores). */
extern const char demo_hello_start[] asm("_binary_hello_bas_start");
extern const char demo_hello_end[] asm("_binary_hello_bas_end");
extern const char demo_fizzbuzz_start[] asm("_binary_fizzbuzz_bas_start");
extern const char demo_fizzbuzz_end[] asm("_binary_fizzbuzz_bas_end");
extern const char demo_sieve_start[] asm("_binary_sieve_bas_start");
extern const char demo_sieve_end[] asm("_binary_sieve_bas_end");
extern const char demo_mand_start[] asm("_binary_mand_bas_start");
extern const char demo_mand_end[] asm("_binary_mand_bas_end");
extern const char demo_web_hello_start[] asm("_binary_web_hello_bas_start");
extern const char demo_web_hello_end[] asm("_binary_web_hello_bas_end");
extern const char demo_site_start[] asm("_binary_site_bas_start");
extern const char demo_site_end[] asm("_binary_site_bas_end");
extern const char demo_site_index_start[] asm("_binary_site_index_htm_start");
extern const char demo_site_index_end[] asm("_binary_site_index_htm_end");
extern const char demo_site_status_start[] asm("_binary_site_status_htm_start");
extern const char demo_site_status_end[] asm("_binary_site_status_htm_end");
extern const char demo_site_about_start[] asm("_binary_site_about_htm_start");
extern const char demo_site_about_end[] asm("_binary_site_about_htm_end");
extern const char demo_site_gpio_start[] asm("_binary_site_gpio_htm_start");
extern const char demo_site_gpio_end[] asm("_binary_site_gpio_htm_end");
extern const char demo_site_files_start[] asm("_binary_site_files_htm_start");
extern const char demo_site_files_end[] asm("_binary_site_files_htm_end");
extern const char demo_site_style_start[] asm("_binary_site_style_css_start");
extern const char demo_site_style_end[] asm("_binary_site_style_css_end");

struct embedded_demo {
    const char * name;
    const char * start;
    const char * end;
    int refresh;
};

static const struct embedded_demo s_demos[] = {
    {"hello.bas", demo_hello_start, demo_hello_end, 0},
    {"fizzbuzz.bas", demo_fizzbuzz_start, demo_fizzbuzz_end, 0},
    {"sieve.bas", demo_sieve_start, demo_sieve_end, 0},
    {"mand.bas", demo_mand_start, demo_mand_end, 0},
    {"web_hello.bas", demo_web_hello_start, demo_web_hello_end, 0},
    {"site.bas", demo_site_start, demo_site_end, 1},
    {"server.bas", demo_site_start, demo_site_end, 1},
    {"index.htm", demo_site_index_start, demo_site_index_end, 1},
    {"status.htm", demo_site_status_start, demo_site_status_end, 1},
    {"about.htm", demo_site_about_start, demo_site_about_end, 1},
    {"gpio.htm", demo_site_gpio_start, demo_site_gpio_end, 1},
    {"files.htm", demo_site_files_start, demo_site_files_end, 1},
    {"style.css", demo_site_style_start, demo_site_style_end, 1},
};

static void populate_demos(void) {
    for (size_t i = 0; i < sizeof s_demos / sizeof s_demos[0]; i++) {
        const struct embedded_demo * d = &s_demos[i];
        size_t len = (size_t)(d->end - d->start);
        if (len && d->start[len - 1] == '\0') len--;
        lfs_file_t f;
        int err = lfs_file_open(&lfs, &f, d->name, LFS_O_RDONLY);
        if (!err && !d->refresh) {
            lfs_soff_t existing_size = lfs_file_size(&lfs, &f);
            lfs_file_close(&lfs, &f);
            if (existing_size > 0) continue;
        }
        if (!err) lfs_file_close(&lfs, &f);
        err = lfs_file_open(&lfs, &f, d->name, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (err) {
            ESP_LOGW("lfs", "demo %s: open failed (%d)", d->name, err);
            continue;
        }
        lfs_file_write(&lfs, &f, d->start, len);
        lfs_file_close(&lfs, &f);
        ESP_LOGI("lfs", "wrote demo %s (%u bytes)", d->name, (unsigned)len);
    }
}

int esp32_lfs_mount(void) {
    if (s_mounted) return 0;
    if (!s_part) {
        s_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "lfsdata");
        if (!s_part) {
            ESP_LOGE("lfs", "no 'lfsdata' partition in partition table");
            return -1;
        }
        pico_lfs_cfg.block_count = s_part->size / BLOCK_SIZE;
        ESP_LOGI("lfs", "lfsdata partition: size=%lu, %lu blocks",
                 (unsigned long)s_part->size,
                 (unsigned long)pico_lfs_cfg.block_count);
    }
    int err = lfs_mount(&lfs, &pico_lfs_cfg);
    int formatted = 0;
    if (err == LFS_ERR_CORRUPT) {
        ESP_LOGI("lfs", "no filesystem; formatting...");
        err = lfs_format(&lfs, &pico_lfs_cfg);
        if (err) {
            ESP_LOGE("lfs", "lfs_format failed: %d", err);
            return -1;
        }
        err = lfs_mount(&lfs, &pico_lfs_cfg);
        formatted = 1;
    }
    if (err) {
        ESP_LOGE("lfs", "lfs_mount failed: %d", err);
        return -1;
    }
    s_mounted = 1;
    ESP_LOGI("lfs", "LittleFS mounted (A:)");
    (void)formatted;
    populate_demos();
    return 0;
}

/* MMBasic core (FileIO.c) declares these as globals it walks for FILES /
 * DIR$ scan state. Pico's port owns them; host_runtime.c owned them on
 * host. Each port supplies its own definitions. ESP32 mounts LFS on the
 * `lfs` instance above; the dir/info structs are the user-facing scan
 * state, distinct from the mount handle. */
lfs_dir_t lfs_dir;
struct lfs_info lfs_info;
