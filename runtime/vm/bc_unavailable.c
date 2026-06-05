/*
 * Bytecode VM unavailable stub.
 *
 * Linked by ports that do not opt in to cmake/bytecode_vm.cmake. Keeps the
 * shared command table and MEMORY diagnostics linkable while making FRUN a
 * clear runtime error.
 */
#include <stddef.h>
#include "MMBasic.h"
#include "bc_run_diag.h"

int bc_bridge_last_vm_line = 0;
char bc_bridge_last_stmt[80] = {0};

void cmd_frun(void) {
    error("FRUN not available in this build");
}

void bc_run_source_string(const char * source, const char * source_name) {
    (void)source;
    (void)source_name;
    error("FRUN not available in this build");
}

void * bc_alloc(size_t size) {
    (void)size;
    return NULL;
}
void bc_free(void * ptr) {
    (void)ptr;
}
void bc_alloc_reset(void) {}
void * bc_compile_alloc(size_t size) {
    (void)size;
    return NULL;
}
void bc_compile_free(void * ptr) {
    (void)ptr;
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

void bc_run_diag_reset(void) {}
void bc_run_diag_note_before_clear(unsigned old_used, unsigned old_free, unsigned old_contig) {
    (void)old_used;
    (void)old_free;
    (void)old_contig;
}
void bc_run_diag_note_after_clear(unsigned old_used, unsigned old_free, unsigned old_contig) {
    (void)old_used;
    (void)old_free;
    (void)old_contig;
}
void bc_run_diag_note_after_vm_reset(unsigned old_used, unsigned old_free, unsigned old_contig,
                                     unsigned vm_used, unsigned c_used, unsigned c_free, unsigned cap) {
    (void)old_used;
    (void)old_free;
    (void)old_contig;
    (void)vm_used;
    (void)c_used;
    (void)c_free;
    (void)cap;
}
void bc_run_diag_note_load(char fs, unsigned size, unsigned old_free, unsigned vm_cfree) {
    (void)fs;
    (void)size;
    (void)old_free;
    (void)vm_cfree;
}
void bc_run_diag_note_source_alloc_fail(unsigned need, unsigned cfree, unsigned cap) {
    (void)need;
    (void)cfree;
    (void)cap;
}
void bc_run_diag_note_vm_stage(BCRunVmStage stage, unsigned vm_used, unsigned vm_hw,
                               unsigned c_used, unsigned c_free, unsigned r_lim, unsigned cap) {
    (void)stage;
    (void)vm_used;
    (void)vm_hw;
    (void)c_used;
    (void)c_free;
    (void)r_lim;
    (void)cap;
}
void bc_run_diag_dump(const char * reason) {
    (void)reason;
}

void bc_crash_dump_if_any(void) {}
void bc_crash_save_fault(void) {}
