/*
 * ports/pc386/heap_region.c — MMBasic heap region (BSS-backed).
 */

#include "heap_region.h"

#include "port_config.h"

/* C-library bump heap for malloc/calloc/realloc users in port and
 * third-party code. MMBasic's allocator uses core/mmbasic/Memory.c's
 * AllMemory slab, so this must not scale with the BASIC heap. */
static __attribute__((aligned(16)))
uint8_t pc386_libc_heap_storage[PC386_LIBC_HEAP_SIZE];

void *heap_region_base(void) {
    return pc386_libc_heap_storage;
}

size_t heap_region_size(void) {
    return sizeof(pc386_libc_heap_storage);
}
