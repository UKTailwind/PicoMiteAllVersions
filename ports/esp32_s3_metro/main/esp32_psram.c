/*
 * esp32_psram.c — ESP32-S3 PSRAM policy hooks.
 *
 * PSRAM is owned by ESP-IDF. This file reports heap_caps-visible SPIRAM and
 * provides the opt-in smoke march. It deliberately does not update
 * MMBasic's PSRAMsize or route generic Memory.c allocations to SPIRAM.
 */

#include "esp32_psram.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"

static const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

extern void MMPrintString(char *s);
extern void IntToStrPad(char *p, long long int nbr, signed char padch,
                        int maxch, int radix);

size_t esp32_psram_detected_bytes(void) {
#ifdef CONFIG_SPIRAM
    return esp_psram_is_initialized() ? esp_psram_get_size() : 0;
#else
    return 0;
#endif
}

size_t esp32_psram_free_bytes(void) {
    return heap_caps_get_free_size(psram_caps);
}

size_t esp32_psram_largest_block_bytes(void) {
    return heap_caps_get_largest_free_block(psram_caps);
}

void esp32_psram_print_boot_report(void) {
    size_t detected = esp32_psram_detected_bytes();
    size_t free_bytes = esp32_psram_free_bytes();
    size_t largest = esp32_psram_largest_block_bytes();
    printf("ESP32 PSRAM: detected=%u free=%u largest=%u caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT\n",
           (unsigned)detected, (unsigned)free_bytes, (unsigned)largest);
}

static void print_k_line(size_t bytes, const char *label) {
    char buf[80];
    IntToStrPad(buf, (long long int)((bytes + 512u) / 1024u), ' ', 4, 10);
    strcat(buf, "K ");
    strcat(buf, label);
    strcat(buf, "\r\n");
    MMPrintString(buf);
}

void port_memory_report_extra(void) {
    size_t detected = esp32_psram_detected_bytes();
    size_t free_bytes = esp32_psram_free_bytes();
    size_t largest = esp32_psram_largest_block_bytes();
    if (!detected && !free_bytes && !largest) return;
    MMPrintString("\r\nESP32 PSRAM:\r\n");
    print_k_line(detected, "Detected");
    print_k_line(free_bytes, "Free");
    print_k_line(largest, "Largest block");
}

static uint32_t psram_pattern(uintptr_t addr, size_t index) {
    uint32_t x = (uint32_t)(addr >> 2) ^ (uint32_t)index ^ 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static int psram_fail(esp32_psram_smoke_result_t *result, const char *phase,
                      uintptr_t addr, uint32_t expected, uint32_t actual) {
    if (result) {
        result->failed_phase = phase;
        result->failed_addr = addr;
        result->expected = expected;
        result->actual = actual;
    }
    return -1;
}

int esp32_psram_smoke_march(size_t bytes, esp32_psram_smoke_result_t *result) {
    esp32_psram_smoke_result_t local = {0};
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));

    result->requested_bytes = bytes;
    result->detected_bytes = esp32_psram_detected_bytes();
    result->free_before = esp32_psram_free_bytes();
    result->largest_before = esp32_psram_largest_block_bytes();

    if (bytes < sizeof(uint32_t)) {
        return psram_fail(result, "size", 0, (uint32_t)sizeof(uint32_t), (uint32_t)bytes);
    }
    bytes &= ~(sizeof(uint32_t) - 1u);
    result->allocated_bytes = bytes;

    uint32_t *mem = (uint32_t *)heap_caps_malloc(bytes, psram_caps);
    if (!mem) return psram_fail(result, "alloc", 0, (uint32_t)bytes, 0);

    volatile uint32_t *vmem = (volatile uint32_t *)mem;
    size_t words = bytes / sizeof(uint32_t);

    for (size_t i = 0; i < words; i++) vmem[i] = 0u;
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = vmem[i];
        if (actual != 0u) {
            heap_caps_free(mem);
            return psram_fail(result, "up0", (uintptr_t)&vmem[i], 0u, actual);
        }
        vmem[i] = 0xffffffffu;
    }
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = vmem[i];
        if (actual != 0xffffffffu) {
            heap_caps_free(mem);
            return psram_fail(result, "up1", (uintptr_t)&vmem[i], 0xffffffffu, actual);
        }
        vmem[i] = psram_pattern((uintptr_t)&vmem[i], i);
    }
    for (size_t i = words; i-- > 0;) {
        uint32_t expected = psram_pattern((uintptr_t)&vmem[i], i);
        uint32_t actual = vmem[i];
        if (actual != expected) {
            heap_caps_free(mem);
            return psram_fail(result, "downp", (uintptr_t)&vmem[i], expected, actual);
        }
        vmem[i] = ~expected;
    }
    for (size_t i = words; i-- > 0;) {
        uint32_t expected = ~psram_pattern((uintptr_t)&vmem[i], i);
        uint32_t actual = vmem[i];
        if (actual != expected) {
            heap_caps_free(mem);
            return psram_fail(result, "downi", (uintptr_t)&vmem[i], expected, actual);
        }
        vmem[i] = 0u;
    }
    for (size_t i = 0; i < words; i++) {
        uint32_t actual = vmem[i];
        if (actual != 0u) {
            heap_caps_free(mem);
            return psram_fail(result, "final0", (uintptr_t)&vmem[i], 0u, actual);
        }
    }

    heap_caps_free(mem);
    result->free_after = esp32_psram_free_bytes();
    result->largest_after = esp32_psram_largest_block_bytes();
    return 0;
}
