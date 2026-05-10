/*
 * ports/pc386/heap_region.c — MMBasic heap region (BSS-backed).
 */

#include "heap_region.h"

#include "port_config.h"

/* 16-byte aligned: matches RP2040/ESP32 heap alignment (the
 * interpreter's internal allocator assumes 8-byte at minimum, and
 * 16 helps any future SIMD path land naturally aligned). */
static __attribute__((aligned(16)))
uint8_t mmbasic_heap_storage[HAL_PORT_HEAP_MEMORY_SIZE];

void *heap_region_base(void) {
    return mmbasic_heap_storage;
}

size_t heap_region_size(void) {
    return sizeof(mmbasic_heap_storage);
}
