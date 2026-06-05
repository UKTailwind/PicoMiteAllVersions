/*
 * bc_alloc.c - VM allocator wrapper.
 *
 * Routes VM allocations to the interpreter's page-based heap (MMHeap)
 * via TryGetMemory/FreeMemory. On host this is the same AllMemory[]
 * pool — no separate arena. `vm_device_support.h` provides the
 * `TryGetMemory`/`FreeMemory` prototypes on every target (host's
 * Memory.c supplies the real impl via mm_misc_shared's AllMemory).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bytecode.h"
#include "bc_alloc.h"
#include "vm_device_support.h"

void * bc_alloc(size_t size) {
    if (size == 0) size = 1;
    return TryGetMemory((int)size); /* returns NULL on OOM; already zeroed */
}

void bc_free(void * ptr) {
    if (ptr) FreeMemory((unsigned char *)ptr);
}

void bc_alloc_reset(void) {
    /* no-op — interpreter owns heap lifecycle via InitHeap */
}

void bc_alloc_set_heap_capacity(size_t bytes) {
    (void)bytes;
}

void * bc_compile_alloc(size_t size) {
    return bc_alloc(size);
}
void bc_compile_free(void * ptr) {
    bc_free(ptr);
}
void bc_compile_release_all(void) {}
int bc_compile_owns(const void * ptr) {
    (void)ptr;
    return 0;
}

size_t bc_alloc_bytes_used(void) {
    return 0;
}
size_t bc_alloc_bytes_high_water(void) {
    return 0;
}
size_t bc_alloc_bytes_capacity(void) {
    return 0;
}
size_t bc_alloc_usable_size(void * ptr) {
    (void)ptr;
    return 0;
}
int bc_alloc_owns(const void * ptr) {
    (void)ptr;
    return 0;
}
size_t bc_alloc_bytes_used_peek(void) {
    return 0;
}
size_t bc_alloc_bytes_high_water_peek(void) {
    return 0;
}
size_t bc_compile_bytes_used(void) {
    return 0;
}
size_t bc_compile_bytes_free(void) {
    return 0;
}
size_t bc_runtime_bytes_limit(void) {
    return 0;
}
