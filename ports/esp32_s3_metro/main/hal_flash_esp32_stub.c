/*
 * hal_flash_esp32_stub.c — Phase B stub for hal/hal_flash.h.
 * Phase E replaces with esp_partition_*-backed real impl.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hal/hal_flash.h"

int hal_flash_erase(uint32_t off, size_t len) { (void)off; (void)len; return 0; }
int hal_flash_program(uint32_t off, const void *buf, size_t len) { (void)off; (void)buf; (void)len; return 0; }
int hal_flash_unique_id(uint8_t out[8]) { memset(out, 0, 8); return 0; }
int hal_flash_read_jedec_id(uint8_t out[4]) { memset(out, 0, 4); return 0; }
void hal_flash_write_begin(void) {}
void hal_flash_write_end(void) {}
int hal_flash_write_active(void) { return 0; }
int hal_flash_read_options(void *buf, size_t len) { memset(buf, 0xff, len); return 0; }
int hal_flash_write_options(const void *buf, size_t len) { (void)buf; (void)len; return 0; }
int hal_flash_erase_program_area(void) { return 0; }
