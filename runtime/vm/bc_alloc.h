/*
 * bc_alloc.h - allocation boundary for the bytecode VM.
 *
 * Host builds use calloc/free.  Device builds use a VM-owned arena so the
 * VM no longer depends on the legacy interpreter heap in Memory.c.
 */
#ifndef __BC_ALLOC_H
#define __BC_ALLOC_H

#include <stddef.h>

void *bc_alloc(size_t size);
void bc_free(void *ptr);
void bc_alloc_reset(void);
void *bc_compile_alloc(size_t size);
void bc_compile_free(void *ptr);
void bc_compile_release_all(void);
int bc_compile_owns(const void *ptr);
size_t bc_alloc_bytes_used(void);
size_t bc_alloc_bytes_high_water(void);
size_t bc_alloc_bytes_capacity(void);
size_t bc_alloc_usable_size(void *ptr);
int bc_alloc_owns(const void *ptr);
size_t bc_alloc_bytes_used_peek(void);
size_t bc_alloc_bytes_high_water_peek(void);
size_t bc_compile_bytes_used(void);
size_t bc_compile_bytes_free(void);
size_t bc_runtime_bytes_limit(void);

#define BC_ALLOC(sz) bc_alloc((sz))
#define BC_FREE(p)   bc_free((p))
#define BC_COMPILER_ALLOC(sz) bc_compile_alloc((sz))
#define BC_COMPILER_FREE(p)   bc_compile_free((p))

#endif
