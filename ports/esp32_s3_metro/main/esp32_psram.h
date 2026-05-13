#ifndef ESP32_PSRAM_H
#define ESP32_PSRAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t requested_bytes;
    size_t allocated_bytes;
    size_t detected_bytes;
    size_t free_before;
    size_t free_after;
    size_t largest_before;
    size_t largest_after;
    const char *failed_phase;
    uintptr_t failed_addr;
    uint32_t expected;
    uint32_t actual;
} esp32_psram_smoke_result_t;

size_t esp32_psram_detected_bytes(void);
size_t esp32_psram_free_bytes(void);
size_t esp32_psram_largest_block_bytes(void);
void esp32_psram_print_boot_report(void);
int esp32_psram_smoke_march(size_t bytes, esp32_psram_smoke_result_t *result);

#endif /* ESP32_PSRAM_H */
