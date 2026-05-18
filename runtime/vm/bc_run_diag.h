#ifndef BC_RUN_DIAG_H
#define BC_RUN_DIAG_H

#include <stdint.h>

typedef enum {
    BC_RUN_VM_NONE = 0,
    BC_RUN_VM_ENTRY,
    BC_RUN_VM_COMPILER_ALLOC_FAIL,
    BC_RUN_VM_COMPILER_ALLOC_OK,
    BC_RUN_VM_COMPILE_OK,
    BC_RUN_VM_COMPACT_FAIL,
    BC_RUN_VM_COMPACT_OK,
    BC_RUN_VM_COMPILE_RELEASED,
    BC_RUN_VM_RUNTIME_ALLOC_FAIL,
    BC_RUN_VM_RUNTIME_ALLOC_OK
} BCRunVmStage;

void bc_run_diag_reset(void);
void bc_run_diag_note_before_clear(unsigned old_used, unsigned old_free, unsigned old_contig);
void bc_run_diag_note_after_clear(unsigned old_used, unsigned old_free, unsigned old_contig);
void bc_run_diag_note_after_vm_reset(unsigned old_used, unsigned old_free, unsigned old_contig,
                                     unsigned vm_used, unsigned c_used, unsigned c_free, unsigned cap);
void bc_run_diag_note_load(char fs, unsigned size, unsigned old_free, unsigned vm_cfree);
void bc_run_diag_note_source_alloc_fail(unsigned need, unsigned cfree, unsigned cap);
void bc_run_diag_note_vm_stage(BCRunVmStage stage, unsigned vm_used, unsigned vm_hw,
                               unsigned c_used, unsigned c_free, unsigned r_lim, unsigned cap);
void bc_run_diag_dump(const char *reason);

#endif
