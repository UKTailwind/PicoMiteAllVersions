/*
 * ports/pico_sdk_common/bc_runtime_pico.c — Pico SDK VM runtime hooks.
 */

#include "bc_alloc.h"

void port_bc_runtime_free_source(const char ** source) {
    if (source && *source) {
        BC_FREE((void *)*source);
        *source = NULL;
    }
}
