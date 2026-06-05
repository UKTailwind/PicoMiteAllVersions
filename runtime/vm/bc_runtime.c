/*
 * bc_runtime.c - VM command entrypoints and host test hooks.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"
#include "bc_alloc.h"
#include "bc_run_diag.h"
#include "bc_source.h"
#include "vm_device_support.h"
#include "MMBasic.h"
#include "FileIO.h" /* hal_fds[], BasicFileOpen, FileGetChar, FileEOF, FileClose, FindFreeFileNbr */
#include "hal/hal_filesystem.h"
#include "vm_sys_file.h"
/* vm_device_support.h (above) pulls in Hardware_Includes.h transitively
 * — that gives us FA_READ, EDIT_BUFFER_SIZE, and GetMemory/FreeMemory. */

/* Last-failed-alloc diagnostics, defined in Memory.c (device) or
 * bc_alloc.c (host).  Used to enrich OOM error messages. */
extern unsigned int bc_alloc_fail_size;
extern unsigned int bc_alloc_fail_pages;
extern unsigned int bc_alloc_fail_used;
extern unsigned int bc_alloc_fail_free;
extern unsigned int bc_alloc_fail_longest;
extern unsigned int bc_alloc_fail_total;
#include "vm_sys_graphics.h"

#define VMRUN_DBG(s) ((void)0)
#define VMRUN_DBGF(fmt...) ((void)0)

/* Runtime diagnostic state. Populated per VM stage by
 * bc_run_diag_note_* and dumped by bc_run_diag_dump when the VM
 * reports a fatal runtime error (NEM[vm:...]). Kept unconditional so
 * bc_runtime.c's stage-transition calls don't need target gates —
 * host writes the struct fields too (they just go unread; host
 * doesn't call bc_run_diag_dump). */
typedef struct {
    unsigned before_clear_used, before_clear_free, before_clear_contig;
    unsigned after_clear_used, after_clear_free, after_clear_contig;
    unsigned after_vm_reset_old_used, after_vm_reset_old_free, after_vm_reset_old_contig;
    unsigned after_vm_reset_vm_used, after_vm_reset_c_used, after_vm_reset_c_free, after_vm_reset_cap;
    char load_fs;
    unsigned load_size, load_old_free, load_cfree;
    unsigned source_need, source_cfree, source_cap;
    BCRunVmStage vm_stage;
    unsigned vm_used, vm_hw, vm_c_used, vm_c_free, vm_r_lim, vm_cap;
} BCRunDiagState;

static BCRunDiagState g_run_diag;

static const char * bc_run_vm_stage_name(BCRunVmStage stage) {
    switch (stage) {
    case BC_RUN_VM_ENTRY:
        return "entry";
    case BC_RUN_VM_COMPILER_ALLOC_FAIL:
        return "compiler_alloc_fail";
    case BC_RUN_VM_COMPILER_ALLOC_OK:
        return "compiler_alloc_ok";
    case BC_RUN_VM_COMPILE_OK:
        return "compile_ok";
    case BC_RUN_VM_COMPACT_FAIL:
        return "compact_fail";
    case BC_RUN_VM_COMPACT_OK:
        return "compact_ok";
    case BC_RUN_VM_COMPILE_RELEASED:
        return "compile_released";
    case BC_RUN_VM_RUNTIME_ALLOC_FAIL:
        return "runtime_alloc_fail";
    case BC_RUN_VM_RUNTIME_ALLOC_OK:
        return "runtime_alloc_ok";
    default:
        return "none";
    }
}

void bc_run_diag_reset(void) {
    memset(&g_run_diag, 0, sizeof(g_run_diag));
}

void bc_run_diag_note_before_clear(unsigned old_used, unsigned old_free, unsigned old_contig) {
    g_run_diag.before_clear_used = old_used;
    g_run_diag.before_clear_free = old_free;
    g_run_diag.before_clear_contig = old_contig;
}

void bc_run_diag_note_after_clear(unsigned old_used, unsigned old_free, unsigned old_contig) {
    g_run_diag.after_clear_used = old_used;
    g_run_diag.after_clear_free = old_free;
    g_run_diag.after_clear_contig = old_contig;
}

void bc_run_diag_note_after_vm_reset(unsigned old_used, unsigned old_free, unsigned old_contig,
                                     unsigned vm_used, unsigned c_used, unsigned c_free, unsigned cap) {
    g_run_diag.after_vm_reset_old_used = old_used;
    g_run_diag.after_vm_reset_old_free = old_free;
    g_run_diag.after_vm_reset_old_contig = old_contig;
    g_run_diag.after_vm_reset_vm_used = vm_used;
    g_run_diag.after_vm_reset_c_used = c_used;
    g_run_diag.after_vm_reset_c_free = c_free;
    g_run_diag.after_vm_reset_cap = cap;
}

void bc_run_diag_note_load(char fs, unsigned size, unsigned old_free, unsigned vm_cfree) {
    g_run_diag.load_fs = fs;
    g_run_diag.load_size = size;
    g_run_diag.load_old_free = old_free;
    g_run_diag.load_cfree = vm_cfree;
}

void bc_run_diag_note_source_alloc_fail(unsigned need, unsigned cfree, unsigned cap) {
    g_run_diag.source_need = need;
    g_run_diag.source_cfree = cfree;
    g_run_diag.source_cap = cap;
}

void bc_run_diag_note_vm_stage(BCRunVmStage stage, unsigned vm_used, unsigned vm_hw,
                               unsigned c_used, unsigned c_free, unsigned r_lim, unsigned cap) {
    g_run_diag.vm_stage = stage;
    g_run_diag.vm_used = vm_used;
    g_run_diag.vm_hw = vm_hw;
    g_run_diag.vm_c_used = c_used;
    g_run_diag.vm_c_free = c_free;
    g_run_diag.vm_r_lim = r_lim;
    g_run_diag.vm_cap = cap;
}

void bc_run_diag_dump(const char * reason) {
    char b[160];
    MMPrintString("\r\n");
    snprintf(b, sizeof(b), "RUNMEM fail=%s vmstage=%s\r\n",
             reason ? reason : "unknown",
             bc_run_vm_stage_name(g_run_diag.vm_stage));
    MMPrintString(b);
    snprintf(b, sizeof(b), "bc old u=%u f=%u c=%u\r\n",
             g_run_diag.before_clear_used, g_run_diag.before_clear_free, g_run_diag.before_clear_contig);
    MMPrintString(b);
    snprintf(b, sizeof(b), "ac old u=%u f=%u c=%u\r\n",
             g_run_diag.after_clear_used, g_run_diag.after_clear_free, g_run_diag.after_clear_contig);
    MMPrintString(b);
    snprintf(b, sizeof(b), "vr old u=%u f=%u c=%u vm u=%u cu=%u cf=%u cap=%u\r\n",
             g_run_diag.after_vm_reset_old_used, g_run_diag.after_vm_reset_old_free, g_run_diag.after_vm_reset_old_contig,
             g_run_diag.after_vm_reset_vm_used, g_run_diag.after_vm_reset_c_used,
             g_run_diag.after_vm_reset_c_free, g_run_diag.after_vm_reset_cap);
    MMPrintString(b);
    snprintf(b, sizeof(b), "ld fs=%c sz=%u oldf=%u cf=%u\r\n",
             g_run_diag.load_fs ? g_run_diag.load_fs : '?',
             g_run_diag.load_size, g_run_diag.load_old_free, g_run_diag.load_cfree);
    MMPrintString(b);
    snprintf(b, sizeof(b), "vm u=%u hw=%u cu=%u cf=%u rl=%u cap=%u\r\n",
             g_run_diag.vm_used, g_run_diag.vm_hw, g_run_diag.vm_c_used,
             g_run_diag.vm_c_free, g_run_diag.vm_r_lim, g_run_diag.vm_cap);
    MMPrintString(b);
    if (g_run_diag.source_need) {
        snprintf(b, sizeof(b), "src need=%u cf=%u cap=%u\r\n",
                 g_run_diag.source_need, g_run_diag.source_cfree, g_run_diag.source_cap);
        MMPrintString(b);
    }
}

/*
 * Compiles raw BASIC source directly to bytecode and executes it on the VM.
 * This is the VM-owned frontend path; it must not depend on legacy tokenised
 * ProgMemory or interpreter command/function dispatch.  The caller owns the
 * On PicoCalc RP2350 the legacy file loader and the VM share the same allocator
 * boundary, so this path must free only its own allocations and must not reset
 * the heap wholesale.
 */
void bc_run_source_string_ex(const char * source, const char * source_name, int is_immediate);

/* Free the source buffer between compile and runtime-table allocation.
 * Each port must provide exactly one strong definition. Heap-tight
 * device ports usually BC_FREE the source buffer; host ports keep it
 * because their test harness may own malloc-backed source memory. */
void port_bc_runtime_free_source(const char ** source);
#define bc_release_source(p) port_bc_runtime_free_source(p)

/* FRUN / RUN load their source through BasicFileOpen + FileGetChar
 * (interpreter side) or vm_sys_file_* (VM side). Both ultimately route
 * through hal_fs_*, so any port with a real HAL gets file I/O for free
 * here — no per-port indirection. */

void bc_run_source_string(const char * source, const char * source_name) {
    bc_run_source_string_ex(source, source_name, 0);
}

void bc_run_source_string_ex(const char * source, const char * source_name, int is_immediate) {
    int err;
    if (!is_immediate) {
        bc_fastgfx_reset();
        vm_sys_file_reset();
        vm_sys_graphics_reset();
    }
    bc_crash_checkpoint(BC_CK_VM_ENTRY, "source entry");
    VMRUN_DBG("VM: entry\r\n");
    bc_run_diag_note_vm_stage(BC_RUN_VM_ENTRY,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());

    bc_crash_checkpoint(BC_CK_VM_ALLOC_CS, "alloc BCCompiler");
    BCCompiler * cs = (BCCompiler *)BC_ALLOC(sizeof(BCCompiler));
    bc_crash_checkpoint(BC_CK_VM_ALLOC_VM, "alloc BCVMState");
    BCVMState * vm = (BCVMState *)BC_ALLOC(sizeof(BCVMState));
    if (!cs || !vm) {
        if (cs) BC_FREE(cs);
        if (vm) BC_FREE(vm);
        bc_alloc_reset();
        error("NEM[vm:structs] cs=% vm=% want=% pg=% used=%/% free=% run=%",
              (int)sizeof(BCCompiler), (int)sizeof(BCVMState),
              (int)bc_alloc_fail_size, (int)bc_alloc_fail_pages,
              (int)bc_alloc_fail_used, (int)bc_alloc_fail_total,
              (int)bc_alloc_fail_free, (int)bc_alloc_fail_longest);
        return;
    }
    memset(cs, 0, sizeof(BCCompiler));
    memset(vm, 0, sizeof(BCVMState));

    bc_crash_checkpoint(BC_CK_VM_COMP_ALLOC, "bc_compiler_alloc");
    if (bc_compiler_alloc(cs) != 0) {
        VMRUN_DBG("VM: compiler alloc failed\r\n");
        bc_run_diag_note_vm_stage(BC_RUN_VM_COMPILER_ALLOC_FAIL,
                                  (unsigned)bc_alloc_bytes_used_peek(),
                                  (unsigned)bc_alloc_bytes_high_water_peek(),
                                  (unsigned)bc_compile_bytes_used(),
                                  (unsigned)bc_compile_bytes_free(),
                                  (unsigned)bc_runtime_bytes_limit(),
                                  (unsigned)bc_alloc_bytes_capacity());
        bc_run_diag_dump("vm compiler alloc");
        if (bc_compile_owns(source)) {
            bc_compile_release_all();
            source = NULL;
        } else if (bc_alloc_owns(source)) {
            BC_FREE((void *)source);
            source = NULL;
        }
        bc_release_source(&source);
        BC_FREE(cs);
        BC_FREE(vm);
        error("NEM[vm:comptbl] want=% pg=% used=%/% free=% run=%",
              (int)bc_alloc_fail_size, (int)bc_alloc_fail_pages,
              (int)bc_alloc_fail_used, (int)bc_alloc_fail_total,
              (int)bc_alloc_fail_free, (int)bc_alloc_fail_longest);
        return;
    }
    VMRUN_DBG("VM: compiler alloc ok\r\n");
    bc_run_diag_note_vm_stage(BC_RUN_VM_COMPILER_ALLOC_OK,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());

    bc_compiler_init(cs);

    /* bc_bridge_prepare_subfun is called AFTER compact (below), at the
     * lowest-pressure moment in the FRUN pipeline — compile-only arrays
     * have been freed, runtime arrays shrunk, and the peak 20 KB of
     * compile metadata has dropped to ~5 KB. On a 128 KB heap this is
     * the only ordering where the ~src_len bridge buffer reliably
     * finds contiguous space. Deferring the sub table also means
     * ExecuteProgram's label walker (called via findlabel during a
     * bridged TILEMAP CREATE / RESTORE / GOTO) sees a fully tokenised
     * buffer populated with all main-program labels. */

    bc_crash_checkpoint(BC_CK_VM_COMPILE, "bc_compile_source");
    err = bc_compile_source(cs, source, source_name);
    if (err) {
        char msg[160];
        snprintf(msg, sizeof(msg), "VM source compile error at line %d: %.100s",
                 cs->error_line, cs->error_msg);
        bc_compiler_free(cs);
        if (bc_compile_owns(source)) {
            bc_compile_release_all();
            source = NULL;
        } else if (bc_alloc_owns(source)) {
            BC_FREE((void *)source);
            source = NULL;
        }
        bc_release_source(&source);
        BC_FREE(cs);
        BC_FREE(vm);
        if (!is_immediate) bc_bridge_release_subfun_buffer();
        error("$", msg);
        return;
    }
    VMRUN_DBGF("VM: compile ok code=%u const=%u slots=%u subs=%u data=%u meta=%u\r\n",
               (unsigned)cs->code_len, (unsigned)cs->const_count, (unsigned)cs->slot_count,
               (unsigned)cs->subfun_count, (unsigned)cs->data_count, (unsigned)cs->local_meta_count);
    /* bc_debug_enabled defaults to 0 on device (no way to turn it on
     * from BASIC); host's --dump-vm-disasm flag sets it to 1. */
    if (bc_debug_enabled)
        bc_disassemble(cs);
    bc_run_diag_note_vm_stage(BC_RUN_VM_COMPILE_OK,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());

    /* Source kept alive through compact + bc_bridge_prepare_subfun
     * below. Freed afterwards (device catch-all) so the bridge buffer
     * alloc gets maximum contiguous headroom. */

    if (bc_compiler_compact(cs) != 0) {
        VMRUN_DBG("VM: compact failed\r\n");
        bc_run_diag_note_vm_stage(BC_RUN_VM_COMPACT_FAIL,
                                  (unsigned)bc_alloc_bytes_used_peek(),
                                  (unsigned)bc_alloc_bytes_high_water_peek(),
                                  (unsigned)bc_compile_bytes_used(),
                                  (unsigned)bc_compile_bytes_free(),
                                  (unsigned)bc_runtime_bytes_limit(),
                                  (unsigned)bc_alloc_bytes_capacity());
        bc_run_diag_dump("vm compact");
        bc_compiler_free(cs);
        bc_compile_release_all();
        bc_release_source(&source);
        BC_FREE(cs);
        BC_FREE(vm);
        if (!is_immediate) bc_bridge_release_subfun_buffer();
        error("NEM[vm:compact] want=% pg=% used=%/% free=% run=%",
              (int)bc_alloc_fail_size, (int)bc_alloc_fail_pages,
              (int)bc_alloc_fail_used, (int)bc_alloc_fail_total,
              (int)bc_alloc_fail_free, (int)bc_alloc_fail_longest);
        return;
    }
    VMRUN_DBG("VM: compact ok\r\n");
    bc_run_diag_note_vm_stage(BC_RUN_VM_COMPACT_OK,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());
    bc_compile_release_all();
    bc_run_diag_note_vm_stage(BC_RUN_VM_COMPILE_RELEASED,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());

    /* Prepare the bridge subfun buffer NOW — compile-only arrays have
     * been freed, runtime arrays shrunk, so the ~src_len buffer lands
     * in the loosest heap state we can arrange. Skipped in immediate
     * mode — single-line REPL entries can't define SUBs. */
    if (!is_immediate) {
        if (bc_bridge_prepare_subfun(source) < 0) {
            bc_compiler_free(cs);
            BC_FREE(cs);
            BC_FREE(vm);
            bc_release_source(&source);
            error("FRUN: out of memory (bridge sub table)");
            return;
        }
    }

    /* Source no longer needed on device — free before VM runtime tables
     * allocate. Host leaves source to the caller (existing contract). */
    bc_release_source(&source);

    bc_crash_checkpoint(BC_CK_VM_ALLOC, "bc_vm_alloc");
    if (bc_vm_alloc(vm) != 0) {
        VMRUN_DBG("VM: runtime alloc failed\r\n");
        bc_run_diag_note_vm_stage(BC_RUN_VM_RUNTIME_ALLOC_FAIL,
                                  (unsigned)bc_alloc_bytes_used_peek(),
                                  (unsigned)bc_alloc_bytes_high_water_peek(),
                                  (unsigned)bc_compile_bytes_used(),
                                  (unsigned)bc_compile_bytes_free(),
                                  (unsigned)bc_runtime_bytes_limit(),
                                  (unsigned)bc_alloc_bytes_capacity());
        bc_run_diag_dump("vm runtime alloc");
        bc_compiler_free(cs);
        bc_compile_release_all();
        BC_FREE(cs);
        BC_FREE(vm);
        if (!is_immediate) bc_bridge_release_subfun_buffer();
        error("NEM[vm:runtime] want=% pg=% used=%/% free=% run=%",
              (int)bc_alloc_fail_size, (int)bc_alloc_fail_pages,
              (int)bc_alloc_fail_used, (int)bc_alloc_fail_total,
              (int)bc_alloc_fail_free, (int)bc_alloc_fail_longest);
        return;
    }
    VMRUN_DBG("VM: runtime alloc ok\r\n");
    bc_run_diag_note_vm_stage(BC_RUN_VM_RUNTIME_ALLOC_OK,
                              (unsigned)bc_alloc_bytes_used_peek(),
                              (unsigned)bc_alloc_bytes_high_water_peek(),
                              (unsigned)bc_compile_bytes_used(),
                              (unsigned)bc_compile_bytes_free(),
                              (unsigned)bc_runtime_bytes_limit(),
                              (unsigned)bc_alloc_bytes_capacity());

    bc_crash_checkpoint(BC_CK_VM_INIT, "bc_vm_init");
    bc_vm_init(vm, cs);
    bc_bridge_reset_sync();
    VMRUN_DBG("VM: execute\r\n");

    jmp_buf saved_mark;
    memcpy(saved_mark, mark, sizeof(jmp_buf));

    /* Point ProgMemory at the bridge's tokenised program buffer for the
     * duration of the VM run. Bridged interpreter commands that scan
     * the program — findlabel (TILEMAP CREATE / READ via label / RESTORE
     * label / ON … GOTO label / …), findline, ListProgram via FLASH LIST
     * — all walk ProgMemory by convention. Without this swap they see
     * whatever ProgMemory pointed at before FRUN (REPL: empty), which is
     * how `Cannot find label` surfaces in bridged TILEMAP CREATE under
     * FRUN. Skip the swap in immediate mode (no source program). */
    unsigned char * saved_prog_memory = ProgMemory;
    if (!is_immediate) {
        unsigned char * bridge_buf = bc_bridge_get_prog_buf();
        if (bridge_buf) ProgMemory = bridge_buf;
    }

    bc_crash_checkpoint(BC_CK_VM_EXECUTE, "bc_vm_execute");
    if (setjmp(mark) == 0) {
        bc_vm_execute(vm);
    }
    bc_crash_checkpoint(BC_CK_VM_CLEANUP, "vm_execute returned");
    VMRUN_DBG("VM: returned\r\n");

    ProgMemory = saved_prog_memory;
    memcpy(mark, saved_mark, sizeof(jmp_buf));

    bc_vm_free(vm);
    bc_compiler_free(cs);
    bc_compile_release_all();
    BC_FREE(cs);
    BC_FREE(vm);
    if (!is_immediate) bc_bridge_release_subfun_buffer();
    vm_sys_file_reset();
    vm_sys_graphics_reset();
    bc_fastgfx_reset();
    bc_crash_clear();
}

/*
 * Helper: capture VM output to a buffer
 *
 * Call before bc_vm_execute to redirect PRINT output to a string buffer.
 */
void bc_vm_start_capture(BCVMState * vm, char * buf, int capacity) {
    vm->capture_buf = buf;
    vm->capture_len = 0;
    vm->capture_cap = capacity;
    if (capacity > 0) buf[0] = '\0';
}

/*
 * Helper: append to capture buffer (used by VM print operations)
 */
void bc_vm_capture_write(BCVMState * vm, const char * text, int len) {
    if (!vm->capture_buf) return;
    if (vm->capture_len + len >= vm->capture_cap) {
        len = vm->capture_cap - vm->capture_len - 1;
        if (len <= 0) return;
    }
    memcpy(vm->capture_buf + vm->capture_len, text, len);
    vm->capture_len += len;
    vm->capture_buf[vm->capture_len] = '\0';
}

void bc_vm_capture_char(BCVMState * vm, char c) {
    bc_vm_capture_write(vm, &c, 1);
}

void bc_vm_capture_string(BCVMState * vm, const char * s) {
    bc_vm_capture_write(vm, s, strlen(s));
}

/*
 * Try to compile a single line as BASIC.
 * Returns 1 if compilation succeeds, 0 if it fails.
 * Does NOT call error() -- safe to use as a probe.
 * Caller must have called bc_alloc_reset() first.
 */
int bc_try_compile_line(const char * line) {
    int err;
    BCCompiler * cs = (BCCompiler *)BC_ALLOC(sizeof(BCCompiler));
    if (!cs) return 0;
    memset(cs, 0, sizeof(BCCompiler));
    if (bc_compiler_alloc(cs) != 0) {
        bc_compiler_free(cs);
        bc_compile_release_all();
        BC_FREE(cs);
        return 0;
    }
    bc_compiler_init(cs);
    err = bc_compile_source(cs, line, "<immediate>");
    bc_compiler_free(cs);
    bc_compile_release_all();
    BC_FREE(cs);
    return err == 0;
}

/*
 * Compile and execute a single line of BASIC (immediate mode).
 * Resets the VM heap, compiles, executes, and cleans up.
 */
void bc_run_immediate(const char * line) {
    bc_alloc_reset();
    bc_run_source_string_ex(line, "<immediate>", 1);
}

/*
 * Read a .bas file into a fresh GetMemory buffer through the
 * interpreter's HAL-backed file path (BasicFileOpen + FileGetChar).
 * Filters inbound bytes to printable + CR/LF/TAB so any stray high-bit
 * bytes from flaky media don't reach the tokenizer. Returns a
 * NUL-terminated source string the caller can hand to
 * bc_run_source_string and FreeMemory afterwards.
 */
static char * bc_load_source_via_hal(const char * fname_buf) {
    int fnbr = FindFreeFileNbr();
    if (!BasicFileOpen((char *)fname_buf, fnbr, FA_READ)) error("File not found");

    int fsize = (int)hal_fs_size(hal_fds[fnbr]);
    if (fsize < 0 || fsize >= EDIT_BUFFER_SIZE - 2048) {
        FileClose(fnbr);
        error("File too large");
    }

    char * source = (char *)GetMemory(fsize + 1);
    if (!source) {
        FileClose(fnbr);
        error("NEM[frun:src] want=%", fsize + 1);
    }

    char * p = source;
    while (!FileEOF(fnbr)) {
        if ((p - source) >= fsize) break;
        int c = FileGetChar(fnbr) & 0x7f;
        if (isprint(c) || c == '\r' || c == '\n' || c == '\t') {
            if (c == '\t') c = ' ';
            *p++ = (char)c;
        }
    }
    *p = '\0';
    FileClose(fnbr);
    return source;
}

/*
 * cmd_frun() — The FRUN command
 *
 * Loads a BASIC source file and executes it via the bytecode VM.
 * Called from the interpreter prompt like any command.
 */
void cmd_frun(void) {
    unsigned char * filename = getCstring(cmdline);
    if (!*filename) error("Syntax");

    char fname_buf[STRINGSIZE];
    strncpy(fname_buf, (const char *)filename, STRINGSIZE - 5);
    fname_buf[STRINGSIZE - 5] = '\0';
    if (!strchr(fname_buf, '.')) strcat(fname_buf, ".bas");

    /* Match RUN's clean execution state.  FRUN compiles source through
     * the interpreter bridge, so stale prompt/program options such as
     * OPTION EXPLICIT must not leak into the next file. */
    ClearRuntime(true);

    char * source = bc_load_source_via_hal(fname_buf);
    bc_run_source_string(source, fname_buf);
    /* On heap-tight ports the hook inside bc_run_source_string already
     * freed source; on host it's a no-op there and we own the release. */
    if (source) FreeMemory((unsigned char *)source);
}

/*
 * Load and execute a BASIC program from file (RUN syscall).
 * Resets the VM heap, loads source, compiles, executes, then longjmps
 * back to the caller's mark — the outer VM is abandoned. Reads through
 * vm_sys_file_* since the outer VM owns the file table here.
 */
void bc_run_file(const char * filename) {
    char fname_buf[STRINGSIZE];

    strncpy(fname_buf, filename, STRINGSIZE - 5);
    fname_buf[STRINGSIZE - 5] = '\0';
    if (!strchr(fname_buf, '.')) strcat(fname_buf, ".bas");

    int fnbr = 1;
    vm_sys_file_open(fname_buf, fnbr, VM_FILE_MODE_INPUT);
    int fsize = vm_sys_file_lof(fnbr);
    char * source = (char *)BC_ALLOC((size_t)fsize + 1);
    if (!source) {
        vm_sys_file_close(fnbr);
        error("Not enough memory");
    }

    char * p = source;
    while (vm_sys_file_eof(fnbr) == 0) {
        if ((p - source) >= fsize) break;
        int c = vm_sys_file_getc(fnbr) & 0x7f;
        if (isprint(c) || c == '\r' || c == '\n' || c == '\t') {
            if (c == '\t') c = ' ';
            *p++ = (char)c;
        }
    }
    *p = '\0';
    vm_sys_file_close(fnbr);

    bc_run_source_string(source, fname_buf);

    longjmp(mark, 1);
}
