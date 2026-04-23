/*
 * ports/pico_sdk_common/bc_runtime_pico.c — device impl of the
 * bc_runtime.c port hooks.
 *
 *   - port_bc_runtime_free_source() : release the source buffer the
 *     VM compiler was handed. Device caller allocates through
 *     GetMemory (MMHeap) so the buffer MUST be released via
 *     BC_FREE / FreeMemory before the VM's runtime tables allocate —
 *     otherwise compact-heap reallocation can overlap.
 *
 *   - port_bc_frun_load_source / _release_source : FRUN statement
 *     source file reader.  Opens the file via the interpreter's
 *     BasicFileOpen + hal_fs_size (not the VM's vm_sys_file_* syscalls)
 *     because FRUN runs from the interpreter prompt before any VM
 *     exists.  Filters inbound bytes to printable + CR/LF/TAB — SD
 *     cards on field devices can return stray high-bit bytes that the
 *     tokenizer doesn't like.  Release is a no-op: bc_run_source_string
 *     frees through port_bc_runtime_free_source already.
 *
 *   - port_bc_run_file_load_source / _release_source : RUN statement
 *     source file reader (called from inside a running VM).  Uses
 *     vm_sys_file_* syscalls since the outer VM owns the file table.
 *     Release is a no-op (same reason).
 */

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bc_alloc.h"
#include "vm_sys_file.h"

void port_bc_runtime_free_source(const char **source) {
    if (source && *source) {
        BC_FREE((void *)*source);
        *source = NULL;
    }
}

char *port_bc_frun_load_source(const char *fname_buf) {
    int fnbr;
    int c, fsize;
    char *source;
    char *p;

    if (!InitSDCard()) error("SD card not found");
    fnbr = FindFreeFileNbr();
    if (!BasicFileOpen((char *)fname_buf, fnbr, FA_READ)) error("File not found");

    fsize = (int)hal_fs_size(hal_fds[fnbr]);

    if (fsize < 0 || fsize >= EDIT_BUFFER_SIZE - 2048) {
        FileClose(fnbr);
        error("File too large");
    }

    source = (char *)GetMemory(fsize + 1);
    if (!source) { FileClose(fnbr); error("NEM[frun:src] want=%", fsize + 1); }

    p = source;
    while (!FileEOF(fnbr)) {
        if ((p - source) >= fsize) break;
        c = FileGetChar(fnbr) & 0x7f;
        if (isprint(c) || c == '\r' || c == '\n' || c == '\t') {
            if (c == '\t') c = ' ';
            *p++ = (char)c;
        }
    }
    *p = '\0';
    FileClose(fnbr);
    return source;
}

void port_bc_frun_release_source(char **source) {
    /* Device path: source was already released inside
     * bc_run_source_string via port_bc_runtime_free_source. */
    if (source) *source = NULL;
}

char *port_bc_run_file_load_source(const char *fname_buf) {
    int fnbr = 1;
    int c, fsize;
    char *source;
    char *p;

    vm_sys_file_open(fname_buf, fnbr, VM_FILE_MODE_INPUT);
    fsize = vm_sys_file_lof(fnbr);
    source = (char *)bc_compile_alloc((size_t)fsize + 1);
    if (!source) { vm_sys_file_close(fnbr); error("NEM[run_file:src] want=%", fsize + 1); }
    p = source;
    while (!vm_sys_file_eof(fnbr)) {
        c = vm_sys_file_getc(fnbr) & 0x7f;
        if (isprint(c) || c == '\r' || c == '\n' || c == '\t') {
            if (c == '\t') c = ' ';
            *p++ = (char)c;
        }
    }
    *p = '\0';
    vm_sys_file_close(fnbr);
    return source;
}

void port_bc_run_file_release_source(char **source) {
    /* Device path: source already released inside bc_run_source_string. */
    if (source) *source = NULL;
}
