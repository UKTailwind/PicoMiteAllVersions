/*
 * ports/pico_sdk_common/bc_runtime_pico.c — device impl of the
 * bc_runtime.c port hooks.
 *
 *   - port_bc_runtime_free_source() : release the source buffer the VM
 *     compiler was handed. On device the caller allocates through
 *     GetMemory (MMHeap) so the buffer MUST be released via FreeMemory
 *     / BC_FREE before the VM's runtime tables allocate — otherwise
 *     compact-heap reallocation can overlap.
 */

#include <stddef.h>
#include "bc_alloc.h"

void port_bc_runtime_free_source(const char **source) {
    if (source && *source) {
        BC_FREE((void *)*source);
        *source = NULL;
    }
}
