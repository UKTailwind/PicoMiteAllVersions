/*
 * bc_vm.c - Bytecode VM for MMBasic
 *
 * Implements a stack-based virtual machine that executes the bytecode
 * produced by the bytecode compiler.  Uses computed goto (GCC extension)
 * for fast dispatch on RP2040.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "bytecode.h"
#include "bc_alloc.h"
#include "hal/hal_time.h"
#include "port_config.h"
#include "vm_device_support.h"
#include "vm_sys_input.h"
#include "vm_sys_graphics.h"
#include "vm_sys_time.h"
#include "vm_sys_pin.h"
#include "vm_sys_file.h"

/* Ensure __not_in_flash_func is defined (from pico SDK platform headers) */
#ifndef __not_in_flash_func
#define __not_in_flash_func(func) func
#endif

/* Zero-length MMBasic string for uninitialized string variables */
static uint8_t vm_empty_string[1] = { 0 };

/* ======================================================================
 * Helper: get next rotating string temp buffer
 * ====================================================================== */
static uint8_t *vm_get_str_temp(BCVMState *vm) {
    int idx = vm->str_temp_idx & 3;
    uint8_t *p = vm->str_temp[idx];
    vm->str_temp_idx = (idx + 1) & 3;
    return p;
}

static void bc_push_array_ref(BCVMState *vm, uint8_t tag, int64_t ref) {
    if (vm->sp + 1 >= VM_STACK_SIZE)
        bc_vm_error(vm, "Stack overflow");
    vm->sp++;
    vm->stack[vm->sp].i = ref;
    vm->stack_types[vm->sp] = tag;
}

static int bc_stack_references_string(BCVMState *vm, uint8_t *s) {
    if (!s) return 0;
    for (int i = 0; i <= vm->sp; i++) {
        if (vm->stack_types[i] == T_STR && vm->stack[i].s == s)
            return 1;
    }
    return 0;
}

static int bc_stack_type_matches_array_param(uint8_t stack_type, uint8_t param_type) {
    switch (param_type) {
        case T_INT:
            return stack_type == BC_STK_GARR_I || stack_type == BC_STK_LARR_I;
        case T_NBR:
            return stack_type == BC_STK_GARR_F || stack_type == BC_STK_LARR_F;
        case T_STR:
            return stack_type == BC_STK_GARR_S || stack_type == BC_STK_LARR_S;
        default:
            return 0;
    }
}

static uint8_t *bc_vm_string_lvalue(BCVMState *vm, uint8_t is_local, uint16_t slot) {
    BCValue *value;
    uint8_t *type;

    if (is_local) {
        int idx = vm->frame_base + slot;
        if (idx < 0 || idx >= VM_MAX_LOCALS)
            bc_vm_error(vm, "Local index out of range");
        value = &vm->locals[idx];
        type = &vm->local_types[idx];
    } else {
        if (slot >= BC_MAX_SLOTS)
            bc_vm_error(vm, "Global slot out of range");
        value = &vm->globals[slot];
        type = &vm->global_types[slot];
    }

    if (!value->s) {
        value->s = BC_ALLOC(STRINGSIZE);
        if (!value->s) bc_vm_error(vm, "Out of memory for string");
        value->s[0] = 0;
    }
    *type = T_STR;
    return value->s;
}

static BCArray *bc_resolve_array_ref(BCVMState *vm, BCValue value, uint8_t type) {
    int idx = (int)value.i;
    switch (type) {
        case BC_STK_GARR_I:
        case BC_STK_GARR_F:
        case BC_STK_GARR_S:
            if (idx < 0 || idx >= BC_MAX_SLOTS)
                bc_vm_error(vm, "Global array reference out of range");
            return &vm->arrays[idx];
        case BC_STK_LARR_I:
        case BC_STK_LARR_F:
        case BC_STK_LARR_S:
            if (idx < 0 || idx >= VM_MAX_LOCALS)
                bc_vm_error(vm, "Local array reference out of range");
            return &vm->local_arrays[idx];
        default:
            bc_vm_error(vm, "Invalid array reference");
            return NULL;
    }
}

/* ======================================================================
 * Helper: calculate linear offset into an array (row-major)
 *
 * MMBasic arrays have dimension 0..N, so each dimension has size N+1.
 * ====================================================================== */
static uint32_t calc_array_offset(BCVMState *vm, BCArray *arr,
                                  int64_t *indices, int ndim) {
    /* MMBasic uses column-major (Fortran) ordering: first index varies
     * fastest.  offset = idx[0] + idx[1]*stride1 + idx[2]*stride1*stride2 …
     * This must match the interpreter's array layout so the VM can be
     * compared directly against the host oracle. */
    for (int d = 0; d < ndim; d++) {
        int dim_size = arr->dims[d] + 1;   /* 0..N inclusive = N+1 elements */
        if (indices[d] < 0 || indices[d] >= dim_size)
            bc_vm_error(vm, "Array index out of bounds");
    }
    uint32_t offset = (uint32_t)indices[0];
    uint32_t stride = 1;
    for (int d = 1; d < ndim; d++) {
        stride *= (uint32_t)(arr->dims[d - 1] + 1);
        offset += (uint32_t)indices[d] * stride;
    }
    if (offset >= arr->total_elements)
        bc_vm_error(vm, "Array index out of bounds");
    return offset;
}

/* ======================================================================
 * Helper: append to capture buffer or print to console
 * ====================================================================== */
static void vm_output(BCVMState *vm, const char *s) {
    if (vm->capture_buf) {
        int len = (int)strlen(s);
        /* Grow buffer if needed */
        while (vm->capture_len + len + 1 > vm->capture_cap) {
            int newcap = vm->capture_cap ? vm->capture_cap * 2 : 1024;
            char *nb = realloc(vm->capture_buf, newcap);
            if (!nb) bc_vm_error(vm, "Out of memory in capture buffer");
            vm->capture_buf = nb;
            vm->capture_cap = newcap;
        }
        memcpy(vm->capture_buf + vm->capture_len, s, len);
        vm->capture_len += len;
        vm->capture_buf[vm->capture_len] = '\0';
    } else {
        MMPrintString((char *)s);
    }
}

/* Output an MMBasic-format string (length-prefixed, not null-terminated) */
static void vm_output_mstr(BCVMState *vm, uint8_t *s) {
    if (!s) return;
    int len = s[0];
    if (len == 0) return;
    /* Build a null-terminated copy */
    char tmp[STRINGSIZE + 1];
    memcpy(tmp, s + 1, len);
    tmp[len] = '\0';
    vm_output(vm, tmp);
}

/* ======================================================================
 * bc_vm_alloc / bc_vm_free — dynamic allocation for large VM arrays
 * ====================================================================== */

static void bc_array_release(BCArray *arr) {
    if (!arr || !arr->data) return;
    if (arr->data_external) {
        /* Buffer is aliased into g_vartbl — the interpreter owns the
         * memory (and the element strings) via cmd_redim / cmd_erase.
         * We only zero our handle. */
        memset(arr, 0, sizeof(*arr));
        return;
    }
    if (arr->elem_type == T_STR) {
        for (uint32_t i = 0; i < arr->total_elements; i++) {
            if (arr->data[i].s) {
                BC_FREE(arr->data[i].s);
                arr->data[i].s = NULL;
            }
        }
    }
    BC_FREE(arr->data);
    memset(arr, 0, sizeof(*arr));
}

void bc_vm_release_arrays(BCVMState *vm) {
    if (vm->arrays) {
        for (int i = 0; i < BC_MAX_SLOTS; i++)
            bc_array_release(&vm->arrays[i]);
    }
    if (vm->local_arrays) {
        for (int i = 0; i < VM_MAX_LOCALS; i++) {
            if (!vm->local_array_is_alias[i])
                bc_array_release(&vm->local_arrays[i]);
            else
                memset(&vm->local_arrays[i], 0, sizeof(vm->local_arrays[i]));
            vm->local_array_is_alias[i] = 0;
        }
    }
}

int bc_vm_alloc(BCVMState *vm) {
    vm->globals      = (BCValue *)BC_ALLOC(BC_MAX_SLOTS * sizeof(BCValue));
    vm->global_types = (uint8_t *)BC_ALLOC(BC_MAX_SLOTS);
    vm->arrays       = (BCArray *)BC_ALLOC(BC_MAX_SLOTS * sizeof(BCArray));
    vm->locals       = (BCValue *)BC_ALLOC(VM_MAX_LOCALS * sizeof(BCValue));
    vm->local_types  = (uint8_t *)BC_ALLOC(VM_MAX_LOCALS);
    vm->local_arrays = (BCArray *)BC_ALLOC(VM_MAX_LOCALS * sizeof(BCArray));
    if (!vm->globals || !vm->global_types || !vm->arrays ||
        !vm->locals || !vm->local_types || !vm->local_arrays) {
        bc_vm_free(vm);
        return -1;
    }
    return 0;
}

void bc_vm_free(BCVMState *vm) {
    bc_vm_release_arrays(vm);
    if (vm->globals && vm->global_types) {
        for (int i = 0; i < BC_MAX_SLOTS; i++) {
            if (vm->global_types[i] == T_STR && vm->globals[i].s) {
                BC_FREE(vm->globals[i].s);
                vm->globals[i].s = NULL;
            }
        }
    }
    if (vm->globals) {
        BC_FREE(vm->globals);
    }
    if (vm->global_types) {
        BC_FREE(vm->global_types);
    }
    if (vm->arrays) {
        BC_FREE(vm->arrays);
    }
    if (vm->locals) {
        if (vm->local_types) {
            for (int i = 0; i < VM_MAX_LOCALS; i++) {
                if (vm->local_types[i] == T_STR && vm->locals[i].s) {
                    BC_FREE(vm->locals[i].s);
                    vm->locals[i].s = NULL;
                }
            }
        }
        BC_FREE(vm->locals);
    }
    if (vm->local_types) {
        BC_FREE(vm->local_types);
    }
    if (vm->local_arrays) {
        BC_FREE(vm->local_arrays);
    }
    vm->globals = NULL;
    vm->global_types = NULL;
    vm->arrays = NULL;
    vm->locals = NULL;
    vm->local_types = NULL;
    vm->local_arrays = NULL;
}

/* ======================================================================
 * bc_vm_init — initialise VM state from compiled output
 *
 * Arrays must already be allocated via bc_vm_alloc().
 * ====================================================================== */
void bc_vm_init(BCVMState *vm, BCCompiler *cs) {
    /* Zero inline fields without touching the dynamic array pointers */
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->stack_types, 0, sizeof(vm->stack_types));
    memset(vm->call_stack, 0, sizeof(vm->call_stack));
    memset(vm->gosub_stack, 0, sizeof(vm->gosub_stack));
    memset(vm->for_stack, 0, sizeof(vm->for_stack));
    memset(vm->str_temp, 0, sizeof(vm->str_temp));
    memset(vm->local_array_is_alias, 0, sizeof(vm->local_array_is_alias));

    /* BC_ALLOC() zeros allocations; keep explicit zeroing for object reuse. */
    if (vm->arrays)
        memset(vm->arrays, 0, BC_MAX_SLOTS * sizeof(BCArray));
    if (vm->local_arrays)
        memset(vm->local_arrays, 0, VM_MAX_LOCALS * sizeof(BCArray));
    if (vm->globals)
        memset(vm->globals, 0, BC_MAX_SLOTS * sizeof(BCValue));
    if (vm->global_types)
        memset(vm->global_types, 0, BC_MAX_SLOTS);
    if (vm->locals)
        memset(vm->locals, 0, VM_MAX_LOCALS * sizeof(BCValue));
    if (vm->local_types)
        memset(vm->local_types, 0, VM_MAX_LOCALS);
    vm->bytecode     = cs->code;
    vm->bytecode_len = cs->code_len;
    vm->compiler     = cs;
    vm->pc           = vm->bytecode;
    vm->sp           = -1;
    vm->csp          = 0;
    vm->gsp          = 0;
    vm->fsp          = 0;
    vm->frame_base   = 0;
    vm->locals_top   = 0;
    vm->capture_buf  = NULL;
    vm->capture_len  = 0;
    vm->capture_cap  = 0;
    vm->str_temp_idx = 0;
    vm->current_line = 0;

    /* Initialise global variable types from compiler slot table */
    for (int i = 0; i < cs->slot_count; i++) {
        vm->global_types[i] = cs->slots[i].type;
        /* Zero out the value */
        vm->globals[i].i = 0;
        /* For string slots, allocate a buffer */
        if ((cs->slots[i].type & (T_NBR | T_INT | T_STR)) == T_STR && !cs->slots[i].is_array) {
            /* Point to a zeroed temp; will be overwritten on first STORE_S */
        }
    }
}

/* ======================================================================
 * bc_vm_error — report error with current line number
 * ====================================================================== */
void bc_vm_error(BCVMState *vm, const char *msg, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, msg);
    vsnprintf(buf, sizeof(buf), msg, ap);
    va_end(ap);

    /* Dump VM state for debugging before the longjmp */
    bc_dump_vm_state(vm);

    /* Format with line number and call MMBasic's error() which longjmps */
    char errbuf[320];
    snprintf(errbuf, sizeof(errbuf), "[VM line %d] %s", (int)vm->current_line, buf);
    error("$", errbuf);
}

static inline BCValue bc_vm_coerce_arg_value(BCValue value, uint8_t from_type, uint8_t to_type) {
    BCValue out = value;

    if (to_type == T_INT) {
        if (from_type == T_NBR) out.i = (int64_t)value.f;
        else if (from_type == T_STR) out.i = 0;
    } else if (to_type == T_NBR) {
        if (from_type == T_INT) out.f = (MMFLOAT)value.i;
        else if (from_type == T_STR) out.f = 0;
    }

    return out;
}

static int bc_vm_box_is_array_kind(uint8_t kind) {
    return kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F ||
           kind == BC_BOX_ARG_LOCAL_ARR_I || kind == BC_BOX_ARG_LOCAL_ARR_F;
}

static int bc_vm_box_is_float_array_kind(uint8_t kind) {
    return kind == BC_BOX_ARG_GLOBAL_ARR_F || kind == BC_BOX_ARG_LOCAL_ARR_F;
}

static BCArray *bc_vm_box_get_array(BCVMState *vm, uint8_t kind, uint16_t slot, int *count_out) {
    BCArray *arr = NULL;
    int idx;

    switch (kind) {
        case BC_BOX_ARG_GLOBAL_ARR_I:
        case BC_BOX_ARG_GLOBAL_ARR_F:
            if (slot >= BC_MAX_SLOTS) bc_vm_error(vm, "Array index out of range");
            arr = &vm->arrays[slot];
            if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
            break;
        case BC_BOX_ARG_LOCAL_ARR_I:
        case BC_BOX_ARG_LOCAL_ARR_F:
            idx = vm->frame_base + slot;
            if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
            arr = &vm->local_arrays[idx];
            if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
            break;
        default:
            bc_vm_error(vm, "Invalid BOX array kind");
    }

    if (arr->ndims != 1) bc_vm_error(vm, "Invalid variable");
    if (count_out) *count_out = arr->dims[0] + 1 - g_OptionBase;
    return arr;
}

static int bc_vm_box_array_int(BCVMState *vm, uint8_t kind, uint16_t slot, int index) {
    int count = 0;
    BCArray *arr = bc_vm_box_get_array(vm, kind, slot, &count);
    if (index < 0 || index >= count) bc_vm_error(vm, "Array index out of bounds");
    if (bc_vm_box_is_float_array_kind(kind)) return (int)arr->data[index].f;
    return (int)arr->data[index].i;
}

static MMFLOAT bc_vm_box_array_float(BCVMState *vm, uint8_t kind, uint16_t slot, int index) {
    int count = 0;
    BCArray *arr = bc_vm_box_get_array(vm, kind, slot, &count);
    if (index < 0 || index >= count) bc_vm_error(vm, "Array index out of bounds");
    if (bc_vm_box_is_float_array_kind(kind)) return arr->data[index].f;
    return (MMFLOAT)arr->data[index].i;
}

static int bc_vm_box_scalar_int(BCVMState *vm, BCValue value, uint8_t type) {
    if (type == T_INT) return (int)value.i;
    if (type == T_NBR) return (int)FloatToInt64(value.f);
    bc_vm_error(vm, "BOX requires numeric arguments");
    return 0;
}

static MMFLOAT bc_vm_box_scalar_float(BCVMState *vm, BCValue value, uint8_t type) {
    if (type == T_INT) return (MMFLOAT)value.i;
    if (type == T_NBR) return value.f;
    bc_vm_error(vm, "Numeric argument required");
    return 0;
}

static uint8_t *bc_vm_string_to_c_temp(BCVMState *vm, uint8_t *src) {
    uint8_t *tmp;
    int len;
    if (!src) return NULL;
    tmp = vm_get_str_temp(vm);
    Mstrcpy(tmp, src);
    len = tmp[0];
    memmove(tmp, tmp + 1, (size_t)len);
    tmp[len] = 0;
    return tmp;
}

typedef struct {
    int value;
} VMBoxScalarCtx;

typedef struct {
    MMFLOAT value;
} VMFloatScalarCtx;

typedef struct {
    BCVMState *vm;
    uint8_t *value;
} VMStringScalarCtx;

typedef struct {
    BCVMState *vm;
    uint8_t kind;
    uint16_t slot;
} VMBoxArrayCtx;

static int vm_box_scalar_get_int(void *ctx, int index) {
    (void)index;
    return ((VMBoxScalarCtx *)ctx)->value;
}

static int vm_box_array_get_int(void *ctx, int index) {
    VMBoxArrayCtx *arg = (VMBoxArrayCtx *)ctx;
    return bc_vm_box_array_int(arg->vm, arg->kind, arg->slot, index);
}

static MMFLOAT vm_box_scalar_get_float(void *ctx, int index) {
    (void)index;
    return ((VMFloatScalarCtx *)ctx)->value;
}

static MMFLOAT vm_box_array_get_float(void *ctx, int index) {
    VMBoxArrayCtx *arg = (VMBoxArrayCtx *)ctx;
    return bc_vm_box_array_float(arg->vm, arg->kind, arg->slot, index);
}

static char *vm_text_scalar_get_str(void *ctx) {
    VMStringScalarCtx *arg = (VMStringScalarCtx *)ctx;
    return (char *)bc_vm_string_to_c_temp(arg->vm, arg->value);
}

static int vm_text_scalar_get_int(void *ctx) {
    return ((VMBoxScalarCtx *)ctx)->value;
}

static void vm_box_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_box_fail_range(void *ctx, const char *label, int value, int min, int max) {
    BCVMState *vm = (BCVMState *)ctx;
    if (label)
        bc_vm_error(vm, "%s %d is invalid (valid is %d to %d)", label, value, min, max);
    else
        bc_vm_error(vm, "%d is invalid (valid is %d to %d)", value, min, max);
}

static void vm_circle_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_circle_fail_range(void *ctx, const char *label, int value, int min, int max) {
    (void)label;
    bc_vm_error((BCVMState *)ctx, "%d is invalid (valid is %d to %d)", value, min, max);
}

static void vm_line_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_line_fail_range(void *ctx, const char *label, int value, int min, int max) {
    (void)label;
    bc_vm_error((BCVMState *)ctx, "%d is invalid (valid is %d to %d)", value, min, max);
}

static void vm_pixel_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_pixel_fail_range(void *ctx, const char *label, int value, int min, int max) {
    (void)label;
    bc_vm_error((BCVMState *)ctx, "%d is invalid (valid is %d to %d)", value, min, max);
}

static int vm_cls_get_int(void *ctx) {
    VMBoxScalarCtx *arg = (VMBoxScalarCtx *)ctx;
    return arg->value;
}

static void vm_cls_do_clear(void *ctx, int use_default, int colour) {
    (void)ctx;
    ClearScreen(use_default ? gui_bcolour : colour);
}

static void vm_cls_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_cls_fail_range(void *ctx, int value, int min, int max) {
    bc_vm_error((BCVMState *)ctx, "%d is invalid (valid is %d to %d)", value, min, max);
}

static void vm_text_get_defaults(void *ctx, int *font, int *scale, int *fc, int *bc) {
    (void)ctx;
    *font = (gui_font >> 4) + 1;
    *scale = gui_font & 0x0F;
    *fc = gui_fcolour;
    *bc = gui_bcolour;
}

static int vm_text_font_valid(void *ctx, int font) {
    (void)ctx;
    if (font < 1 || font > FONT_TABLE_SIZE) return 0;
    return FontTable[font - 1] != NULL;
}

static void vm_text_fail_msg(void *ctx, const char *msg) {
    bc_vm_error((BCVMState *)ctx, "%s", msg);
}

static void vm_text_fail_range(void *ctx, int value, int min, int max) {
    bc_vm_error((BCVMState *)ctx, "%d is invalid (valid is %d to %d)", value, min, max);
}

static void bc_vm_poll_interrupts(void) {
    static uint32_t loop_poll_counter = 0;
    /* Host --slowdown applies per-backward-branch so FRUN throttles at
     * the same granularity as RUN. No-op on device. */
    hal_time_slowdown_tick();
    loop_poll_counter++;
    if ((loop_poll_counter & 0x3FFu) != 0) return;
    CheckAbort();
    vm_sys_graphics_service();
    check_interrupt();
}

static uint64_t bc_vm_uabs64(int64_t v) {
    if (v >= 0) return (uint64_t)v;
    return (uint64_t)(-(v + 1)) + 1u;
}

static int64_t bc_vm_mulshr_int(int64_t a, int64_t b, int bits) {
    uint64_t ua, ub;
    uint64_t a0, a1, b0, b1;
    uint64_t p0, p1, p2, p3;
    uint64_t hi, lo, add, mag;
    int negative;

    if (bits < 0 || bits > 62)
        error("Number out of bounds");

    ua = bc_vm_uabs64(a);
    ub = bc_vm_uabs64(b);
    negative = ((a < 0) != (b < 0));

    a0 = ua & 0xFFFFFFFFu;
    a1 = ua >> 32;
    b0 = ub & 0xFFFFFFFFu;
    b1 = ub >> 32;

    p0 = a0 * b0;
    p1 = a0 * b1;
    p2 = a1 * b0;
    p3 = a1 * b1;

    lo = p0;
    hi = p3;

    add = p1 << 32;
    lo += add;
    if (lo < add) hi++;
    hi += p1 >> 32;

    add = p2 << 32;
    lo += add;
    if (lo < add) hi++;
    hi += p2 >> 32;

    if (bits == 0) {
        if (hi != 0) error("Integer overflow");
        mag = lo;
    } else {
        mag = (hi << (64 - bits)) | (lo >> bits);
    }

    if (!negative) {
        if (mag > (uint64_t)INT64_MAX) error("Integer overflow");
        return (int64_t)mag;
    }

    if (mag > (uint64_t)INT64_MAX + 1u) error("Integer overflow");
    return -(int64_t)mag;
}

static uint16_t bc_vm_read_u16_at(const uint8_t **pp) {
    uint16_t v;
    memcpy(&v, *pp, 2);
    *pp += 2;
    return v;
}

static int16_t bc_vm_read_i16_at(const uint8_t **pp) {
    int16_t v;
    memcpy(&v, *pp, 2);
    *pp += 2;
    return v;
}

static void bc_vm_push_int(BCVMState *vm, int64_t val) {
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow");
    vm->sp++;
    vm->stack[vm->sp].i = val;
    vm->stack_types[vm->sp] = T_INT;
}

static void bc_vm_push_string(BCVMState *vm, uint8_t *val) {
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow");
    vm->sp++;
    vm->stack[vm->sp].s = val;
    vm->stack_types[vm->sp] = T_STR;
}

static uint8_t *bc_vm_pop_string_value(BCVMState *vm) {
    if (vm->sp < 0) bc_vm_error(vm, "Stack underflow");
    return vm->stack[vm->sp--].s;
}

static int64_t bc_vm_pop_numeric_i_value(BCVMState *vm) {
    if (vm->sp < 0) bc_vm_error(vm, "Stack underflow");
    if (vm->stack_types[vm->sp] == T_NBR) {
        MMFLOAT x = vm->stack[vm->sp--].f;
        return (x >= 0) ? (int64_t)(x + 0.5) : (int64_t)(x - 0.5);
    }
    return vm->stack[vm->sp--].i;
}

static MMFLOAT bc_vm_pop_numeric_f_value(BCVMState *vm) {
    if (vm->sp < 0) bc_vm_error(vm, "Stack underflow");
    if (vm->stack_types[vm->sp] == T_NBR) return vm->stack[vm->sp--].f;
    return (MMFLOAT)vm->stack[vm->sp--].i;
}

static void bc_vm_mstr_to_c_checked(BCVMState *vm, uint8_t *src, char *dest,
                                    int min_len, const char *msg) {
    int len = src ? src[0] : 0;
    if (len < min_len || len > STRINGSIZE) bc_vm_error(vm, "%s", msg);
    memcpy(dest, src + 1, len);
    dest[len] = '\0';
}

static void bc_vm_require_display(BCVMState *vm) {
    if (HRes <= 0 || VRes <= 0) bc_vm_error(vm, "Display not configured");
}

static void bc_vm_execute_syscall(BCVMState *vm, uint16_t sysid, uint8_t argc,
                                  const uint8_t *aux, uint8_t aux_len) {
    const uint8_t *p = aux;
    (void)argc;
    (void)aux_len;

    switch (sysid) {
        case BC_SYS_FASTGFX_CREATE:
            bc_fastgfx_create();
            return;
        case BC_SYS_FASTGFX_CLOSE:
            bc_fastgfx_close();
            return;
        case BC_SYS_FASTGFX_SWAP:
            bc_fastgfx_swap();
            return;
        case BC_SYS_FASTGFX_SYNC:
            bc_fastgfx_sync();
            return;
        case BC_SYS_FASTGFX_FPS:
            bc_fastgfx_set_fps((int)bc_vm_pop_numeric_i_value(vm));
            return;
        case BC_SYS_GFX_RGB: {
            int b = (int)bc_vm_pop_numeric_i_value(vm);
            int g = (int)bc_vm_pop_numeric_i_value(vm);
            int r = (int)bc_vm_pop_numeric_i_value(vm);
            bc_vm_push_int(vm, rgb(r, g, b));
            return;
        }
        case BC_SYS_MM_HRES:
            bc_vm_push_int(vm, HRes);
            return;
        case BC_SYS_MM_VRES:
            bc_vm_push_int(vm, VRes);
            return;
        case BC_SYS_MM_FONTWIDTH:
            bc_vm_push_int(vm, gui_font_width);
            return;
        case BC_SYS_MM_FONTHEIGHT:
            bc_vm_push_int(vm, gui_font_height);
            return;
        case BC_SYS_PRINT_AT: {
            /* PRINT @(x, y) — move text cursor to pixel (x, y). Stack has
             * x at depth 1, y at top (pushed left-to-right by compiler).
             * Mirrors fun_at in Draw.c without the VT100 SerialConsole
             * emit: on sim/wasm the canvas reads CurrentX/Y directly,
             * and on the hardware device DisplayPutC / GUIPrintChar do
             * the same. The VT100 escape only mattered for terminal-
             * cursor mirroring in the REPL, which FRUN'd programs
             * don't care about. */
            int64_t y = bc_vm_pop_numeric_i_value(vm);
            int64_t x = bc_vm_pop_numeric_i_value(vm);
            CurrentX = (int)x;
            CurrentY = (int)y;
            return;
        }
        case BC_SYS_DATE_STR: {
            uint8_t *buf = vm_get_str_temp(vm);
            vm_sys_time_date(buf);
            bc_vm_push_string(vm, buf);
            return;
        }
        case BC_SYS_TIME_STR: {
            uint8_t *buf = vm_get_str_temp(vm);
            vm_sys_time_time(buf);
            bc_vm_push_string(vm, buf);
            return;
        }
        case BC_SYS_KEYDOWN:
            bc_vm_push_int(vm, vm_sys_input_keydown((int)bc_vm_pop_numeric_i_value(vm)));
            return;
        case BC_SYS_PAUSE: {
            int64_t ms = bc_vm_pop_numeric_i_value(vm);
            if (ms < 0) bc_vm_error(vm, "Number out of bounds");
            if (ms < 1) return;
            uint64_t until = readusclock() + (uint64_t)ms * 1000ULL;
            while (readusclock() < until) CheckAbort();
            return;
        }
        case BC_SYS_SETPIN: {
            int mode = (int)bc_vm_read_i16_at(&p);
            int option = (int)bc_vm_read_i16_at(&p);
            int64_t pin = bc_vm_pop_numeric_i_value(vm);
            vm_sys_pin_setpin(pin, mode, option);
            return;
        }
        case BC_SYS_PIN_READ: {
            int64_t pin = bc_vm_pop_numeric_i_value(vm);
            bc_vm_push_int(vm, vm_sys_pin_read(pin));
            return;
        }
        case BC_SYS_PIN_WRITE: {
            int64_t value = bc_vm_pop_numeric_i_value(vm);
            int64_t pin = bc_vm_pop_numeric_i_value(vm);
            vm_sys_pin_write(pin, value);
            return;
        }
        case BC_SYS_PWM_OFF: {
            int slice = (int)bc_vm_pop_numeric_i_value(vm);
            vm_sys_pwm_off(slice);
            return;
        }
        case BC_SYS_PWM_CONFIG: {
            uint8_t present = *p++;
            int delaystart = 0;
            int phase_correct = 0;
            int has_duty2 = (present & 0x02) != 0;
            int has_duty1 = (present & 0x01) != 0;
            MMFLOAT duty2 = 0;
            MMFLOAT duty1 = 0;
            MMFLOAT frequency;
            int slice;
            if (present & 0x08) delaystart = (int)bc_vm_pop_numeric_i_value(vm);
            if (present & 0x04) phase_correct = (int)bc_vm_pop_numeric_i_value(vm);
            if (has_duty2) duty2 = bc_vm_pop_numeric_f_value(vm);
            if (has_duty1) duty1 = bc_vm_pop_numeric_f_value(vm);
            frequency = bc_vm_pop_numeric_f_value(vm);
            slice = (int)bc_vm_pop_numeric_i_value(vm);
            vm_sys_pwm_configure(slice, frequency, has_duty1, duty1, has_duty2, duty2,
                                 phase_correct, delaystart);
            return;
        }
        case BC_SYS_PWM_SYNC: {
            uint16_t present = bc_vm_read_u16_at(&p);
            MMFLOAT counts[12];
            for (int i = 0; i < 12; i++) counts[i] = -1.0;
            for (int i = 11; i >= 0; i--) {
                if (present & (1u << i))
                    counts[i] = bc_vm_pop_numeric_f_value(vm);
            }
            vm_sys_pwm_sync(present, counts);
            return;
        }
        case BC_SYS_SERVO: {
            uint8_t present = *p++;
            int has_pos2 = (present & 0x02) != 0;
            int has_pos1 = (present & 0x01) != 0;
            MMFLOAT pos2 = 0;
            MMFLOAT pos1 = 0;
            int slice;
            if (has_pos2) pos2 = bc_vm_pop_numeric_f_value(vm);
            if (has_pos1) pos1 = bc_vm_pop_numeric_f_value(vm);
            slice = (int)bc_vm_pop_numeric_i_value(vm);
            vm_sys_servo_configure(slice, has_pos1, pos1, has_pos2, pos2);
            return;
        }
        case BC_SYS_FILE_OPEN: {
            int mode = *p++;
            int fnbr = (int)bc_vm_read_u16_at(&p);
            uint8_t *name = bc_vm_pop_string_value(vm);
            char filename[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, name, filename, 1, "File name");
            vm_sys_file_open(filename, fnbr, mode);
            return;
        }
        case BC_SYS_FILE_CLOSE: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            vm_sys_file_close(fnbr);
            return;
        }
        case BC_SYS_FILE_PRINT_INT: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            int64_t val = bc_vm_pop_numeric_i_value(vm);
            char num[64];
            char out[80];
            int pos = 0;
            IntToStr(num, val, 10);
            if (val >= 0) out[pos++] = ' ';
            {
                int len = (int)strlen(num);
                memcpy(out + pos, num, len);
                pos += len;
            }
            vm_sys_file_print_buf(fnbr, out, pos);
            return;
        }
        case BC_SYS_FILE_PRINT_FLT: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            MMFLOAT val = bc_vm_pop_numeric_f_value(vm);
            char num[64];
            char out[80];
            int pos = 0;
            int start = 0;
            FloatToStr(num, val, 0, STR_AUTO_PRECISION, ' ');
            if (val >= 0.0) out[pos++] = ' ';
            while (num[start] == ' ') start++;
            memcpy(out + pos, num + start, strlen(num + start));
            pos += (int)strlen(num + start);
            vm_sys_file_print_buf(fnbr, out, pos);
            return;
        }
        case BC_SYS_FILE_PRINT_STR: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            uint8_t *val = bc_vm_pop_string_value(vm);
            vm_sys_file_print_str(fnbr, val);
            return;
        }
        case BC_SYS_FILE_PRINT_NEWLINE: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            vm_sys_file_print_newline(fnbr);
            return;
        }
        case BC_SYS_FILE_LINE_INPUT: {
            uint8_t is_local = *p++;
            uint16_t slot = bc_vm_read_u16_at(&p);
            int fnbr = (int)bc_vm_read_u16_at(&p);
            uint8_t *dest = bc_vm_string_lvalue(vm, is_local, slot);
            vm_sys_file_line_input(fnbr, dest);
            return;
        }
        case BC_SYS_FILE_DRIVE: {
            uint8_t *s = bc_vm_pop_string_value(vm);
            char drive[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, s, drive, 1, "Invalid disk");
            vm_sys_file_drive(drive);
            return;
        }
        case BC_SYS_FILE_SEEK: {
            int fnbr = (int)bc_vm_read_u16_at(&p);
            int position = (int)bc_vm_pop_numeric_i_value(vm);
            vm_sys_file_seek(fnbr, position);
            return;
        }
        case BC_SYS_FILE_MKDIR:
        case BC_SYS_FILE_CHDIR:
        case BC_SYS_FILE_RMDIR:
        case BC_SYS_FILE_KILL: {
            uint8_t *s = bc_vm_pop_string_value(vm);
            char path[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, s, path, 1, "File name");
            if (sysid == BC_SYS_FILE_MKDIR) vm_sys_file_mkdir(path);
            else if (sysid == BC_SYS_FILE_CHDIR) vm_sys_file_chdir(path);
            else if (sysid == BC_SYS_FILE_RMDIR) vm_sys_file_rmdir(path);
            else vm_sys_file_kill(path);
            return;
        }
        case BC_SYS_FILE_RENAME: {
            uint8_t *new_s = bc_vm_pop_string_value(vm);
            uint8_t *old_s = bc_vm_pop_string_value(vm);
            char old_name[STRINGSIZE + 1];
            char new_name[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, old_s, old_name, 1, "File name");
            bc_vm_mstr_to_c_checked(vm, new_s, new_name, 1, "File name");
            vm_sys_file_rename(old_name, new_name);
            return;
        }
        case BC_SYS_FILE_COPY: {
            int mode = *p++;
            uint8_t *to_s = bc_vm_pop_string_value(vm);
            uint8_t *from_s = bc_vm_pop_string_value(vm);
            char from_name[STRINGSIZE + 1];
            char to_name[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, from_s, from_name, 1, "File name");
            bc_vm_mstr_to_c_checked(vm, to_s, to_name, 1, "File name");
            vm_sys_file_copy(from_name, to_name, mode);
            return;
        }
        case BC_SYS_GFX_CLS: {
            uint8_t has_arg = *p++;
            GfxClsArg arg = {0};
            GfxClsOps ops;
            VMBoxScalarCtx scalar_ctx;
            if (has_arg) {
                BCValue v;
                uint8_t type;
                if (vm->sp < 0) bc_vm_error(vm, "CLS stack underflow");
                v = vm->stack[vm->sp];
                type = vm->stack_types[vm->sp];
                vm->sp--;
                if ((type & (T_INT | T_NBR)) == 0 || (type & T_STR))
                    bc_vm_error(vm, "CLS requires numeric arguments");
                scalar_ctx.value = bc_vm_box_scalar_int(vm, v, type);
                arg.ctx = &scalar_ctx;
                arg.get_int = vm_cls_get_int;
            }
            bc_vm_require_display(vm);
#ifdef GUICONTROLS
            HideAllControls();
#endif
            ops.ctx = vm;
            ops.do_clear = vm_cls_do_clear;
            ops.fail_msg = vm_cls_fail_msg;
            ops.fail_range = vm_cls_fail_range;
            vm_sys_graphics_cls_execute(has_arg, &arg, &ops);
            if (Option.Refresh) Display_Refresh();
            return;
        }
        case BC_SYS_GFX_PIXEL_READ: {
            int y = (int)bc_vm_pop_numeric_i_value(vm);
            int x = (int)bc_vm_pop_numeric_i_value(vm);
            bc_vm_push_int(vm, vm_sys_graphics_read_pixel(x, y));
            return;
        }
        case BC_SYS_GFX_COLOUR: {
            int fore, back = 0;
            if (argc < 1 || argc > 2) bc_vm_error(vm, "Argument count");
            if (argc == 2) back = (int)bc_vm_pop_numeric_i_value(vm);
            fore = (int)bc_vm_pop_numeric_i_value(vm);
            if (fore < 0 || fore > WHITE) bc_vm_error(vm, "Number out of bounds");
            if (argc == 2 && (back < 0 || back > WHITE)) bc_vm_error(vm, "Number out of bounds");
            gui_fcolour = fore;
            if (argc == 2) gui_bcolour = back;
            last_fcolour = gui_fcolour;
            last_bcolour = gui_bcolour;
            if (!CurrentLinePtr) {
                PromptFC = gui_fcolour;
                PromptBC = gui_bcolour;
            }
            return;
        }
        case BC_SYS_GFX_FONT: {
            int font;
            int scale = 1;
            if (argc < 1 || argc > 2) bc_vm_error(vm, "Argument count");
            if (argc == 2) scale = (int)bc_vm_pop_numeric_i_value(vm);
            font = (int)bc_vm_pop_numeric_i_value(vm);
            if (font < 1 || font > FONT_TABLE_SIZE) bc_vm_error(vm, "Number out of bounds");
            if (scale < 1 || scale > 15) bc_vm_error(vm, "Number out of bounds");
            SetFont(((font - 1) << 4) | scale);
            if (Option.DISPLAY_CONSOLE && !CurrentLinePtr)
                PromptFont = gui_font;
            return;
        }
        case BC_SYS_GFX_FRAMEBUFFER: {
            uint8_t op = *p++;
            switch (op) {
                case BC_FB_OP_CREATE: {
                    int fast = (int)*p++;
                    vm_sys_graphics_framebuffer_create(fast);
                    return;
                }
                case BC_FB_OP_LAYER: {
                    int has_colour = (int)*p++;
                    int colour = 0;
                    if (has_colour) colour = (int)bc_vm_pop_numeric_i_value(vm);
                    vm_sys_graphics_framebuffer_layer(has_colour, colour);
                    return;
                }
                case BC_FB_OP_WRITE:
                    vm_sys_graphics_framebuffer_write((char)*p++);
                    return;
                case BC_FB_OP_CLOSE:
                    vm_sys_graphics_framebuffer_close((char)*p++);
                    return;
                case BC_FB_OP_MERGE: {
                    int mode = (int)*p++;
                    int has_colour = (int)*p++;
                    int has_rate = (int)*p++;
                    int rate_ms = 0;
                    int colour = 0;
                    if (has_rate) rate_ms = (int)bc_vm_pop_numeric_i_value(vm);
                    if (has_colour) colour = (int)bc_vm_pop_numeric_i_value(vm);
                    vm_sys_graphics_framebuffer_merge(has_colour, colour, mode, has_rate, rate_ms);
                    return;
                }
                case BC_FB_OP_SYNC:
                    vm_sys_graphics_framebuffer_sync();
                    return;
                case BC_FB_OP_WAIT:
                    vm_sys_graphics_framebuffer_wait();
                    return;
                case BC_FB_OP_COPY: {
                    char from = (char)*p++;
                    char to = (char)*p++;
                    int background = (int)*p++;
                    vm_sys_graphics_framebuffer_copy(from, to, background);
                    return;
                }
                default:
                    bc_vm_error(vm, "Invalid FRAMEBUFFER operation");
                    return;
            }
        }
        case BC_SYS_GFX_BOX:
        case BC_SYS_GFX_RBOX:
        case BC_SYS_GFX_TRIANGLE:
        case BC_SYS_GFX_POLYGON:
        case BC_SYS_GFX_CIRCLE:
        case BC_SYS_GFX_LINE:
        case BC_SYS_GFX_PIXEL: {
            /* handled below */
            break;
        }
        case BC_SYS_GFX_ARC: {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_BOX_ARG_COUNT];
            BCValue scalars[BC_BOX_ARG_COUNT];
            uint8_t scalar_types[BC_BOX_ARG_COUNT];
            int i;
            int x, y, r1, has_r2 = 0, r2 = 0, has_c = 0, c = 0;
            MMFLOAT a1, a2;
            for (i = 0; i < BC_BOX_ARG_COUNT; i++) {
                kinds[i] = *p++;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    (void)bc_vm_read_u16_at(&p);
                    bc_vm_error(vm, "ARC does not support array arguments");
                }
            }
            for (i = BC_BOX_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "ARC stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            if (field_count < 6 || field_count > BC_BOX_ARG_COUNT) bc_vm_error(vm, "Argument count");
            if (kinds[0] != BC_BOX_ARG_STACK || kinds[1] != BC_BOX_ARG_STACK || kinds[2] != BC_BOX_ARG_STACK ||
                kinds[4] != BC_BOX_ARG_STACK || kinds[5] != BC_BOX_ARG_STACK)
                bc_vm_error(vm, "Argument count");
            x = bc_vm_box_scalar_int(vm, scalars[0], scalar_types[0]);
            y = bc_vm_box_scalar_int(vm, scalars[1], scalar_types[1]);
            r1 = bc_vm_box_scalar_int(vm, scalars[2], scalar_types[2]);
            a1 = bc_vm_box_scalar_float(vm, scalars[4], scalar_types[4]);
            a2 = bc_vm_box_scalar_float(vm, scalars[5], scalar_types[5]);
            if (field_count > 3 && kinds[3] == BC_BOX_ARG_STACK) {
                has_r2 = 1;
                r2 = bc_vm_box_scalar_int(vm, scalars[3], scalar_types[3]);
            }
            if (field_count > 6 && kinds[6] == BC_BOX_ARG_STACK) {
                has_c = 1;
                c = bc_vm_box_scalar_int(vm, scalars[6], scalar_types[6]);
            }
            vm_sys_graphics_arc_execute(x, y, r1, has_r2, r2, a1, a2, has_c, c);
            if (Option.Refresh) Display_Refresh();
            return;
        }
        case BC_SYS_GFX_TEXT: {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_TEXT_ARG_COUNT];
            BCValue scalars[BC_TEXT_ARG_COUNT];
            uint8_t scalar_types[BC_TEXT_ARG_COUNT];
            GfxTextArg args[GFX_TEXT_ARG_COUNT] = {0};
            VMBoxScalarCtx int_ctx[GFX_TEXT_ARG_COUNT];
            VMStringScalarCtx str_ctx[GFX_TEXT_ARG_COUNT];
            GfxTextOps ops;
            int i;
            for (i = 0; i < BC_TEXT_ARG_COUNT; i++) {
                kinds[i] = *p++;
                scalar_types[i] = 0;
            }
            for (i = BC_TEXT_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "TEXT stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            for (i = 0; i < GFX_TEXT_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                if (kinds[i] != BC_BOX_ARG_STACK) bc_vm_error(vm, "Argument count");
                args[i].present = 1;
                if (scalar_types[i] == T_STR) {
                    str_ctx[i].vm = vm;
                    str_ctx[i].value = scalars[i].s;
                    args[i].ctx = &str_ctx[i];
                    args[i].get_str = vm_text_scalar_get_str;
                } else {
                    int_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    args[i].ctx = &int_ctx[i];
                    args[i].get_int = vm_text_scalar_get_int;
                }
            }
            ops.ctx = vm;
            ops.get_defaults = vm_text_get_defaults;
            ops.font_valid = vm_text_font_valid;
            ops.render = NULL;
            ops.fail_msg = vm_text_fail_msg;
            ops.fail_range = vm_text_fail_range;
            vm_sys_graphics_text_execute(args, field_count, &ops);
            if (Option.Refresh) Display_Refresh();
            return;
        }
        case BC_SYS_RUN: {
            uint8_t *s = bc_vm_pop_string_value(vm);
            char filename[STRINGSIZE + 1];
            bc_vm_mstr_to_c_checked(vm, s, filename, 0, "File name");
            bc_run_file(filename);
            /* bc_run_file does not return here — it longjmps */
            return;
        }
        default:
            bc_vm_error(vm, "Invalid syscall id");
    }

    if (sysid == BC_SYS_GFX_BOX || sysid == BC_SYS_GFX_RBOX) {
        uint8_t field_count = *p++;
        uint8_t kinds[BC_BOX_ARG_COUNT];
        uint16_t slots[BC_BOX_ARG_COUNT];
        BCValue scalars[BC_BOX_ARG_COUNT];
        uint8_t scalar_types[BC_BOX_ARG_COUNT];
        GfxBoxIntArg args[GFX_BOX_ARG_COUNT] = {0};
        VMBoxScalarCtx scalar_ctx[GFX_BOX_ARG_COUNT];
        VMBoxArrayCtx array_ctx[GFX_BOX_ARG_COUNT];
        GfxBoxErrorSink errors;
        int i;
        for (i = 0; i < BC_BOX_ARG_COUNT; i++) {
            kinds[i] = *p++;
            slots[i] = 0;
            scalar_types[i] = 0;
            if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
        }
        for (i = BC_BOX_ARG_COUNT - 1; i >= 0; i--) {
            if (kinds[i] == BC_BOX_ARG_STACK) {
                if (vm->sp < 0) bc_vm_error(vm, sysid == BC_SYS_GFX_BOX ? "BOX stack underflow" : "RBOX stack underflow");
                scalars[i] = vm->stack[vm->sp];
                scalar_types[i] = vm->stack_types[vm->sp];
                vm->sp--;
            }
        }
        bc_vm_require_display(vm);
        errors.ctx = vm;
        errors.fail_msg = vm_box_fail_msg;
        errors.fail_range = vm_box_fail_range;
        for (i = 0; i < GFX_BOX_ARG_COUNT; i++) {
            if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
            args[i].present = 1;
            if (bc_vm_box_is_array_kind(kinds[i])) {
                int count = 0;
                array_ctx[i].vm = vm;
                array_ctx[i].kind = kinds[i];
                array_ctx[i].slot = slots[i];
                bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                args[i].count = count;
                args[i].ctx = &array_ctx[i];
                args[i].get_int = vm_box_array_get_int;
            } else if (kinds[i] == BC_BOX_ARG_STACK) {
                scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                args[i].count = 1;
                args[i].ctx = &scalar_ctx[i];
                args[i].get_int = vm_box_scalar_get_int;
            } else {
                bc_vm_error(vm, "Argument count");
            }
        }
        if (sysid == BC_SYS_GFX_BOX) {
            vm_sys_graphics_box_execute((bc_vm_box_is_array_kind(kinds[0]) && bc_vm_box_is_array_kind(kinds[1])) ?
                                            GFX_BOX_MODE_VECTOR : GFX_BOX_MODE_SCALAR,
                                        args, field_count, &errors);
        } else {
            vm_sys_graphics_rbox_execute((bc_vm_box_is_array_kind(kinds[0]) &&
                                          bc_vm_box_is_array_kind(kinds[1]) &&
                                          bc_vm_box_is_array_kind(kinds[2]) &&
                                          bc_vm_box_is_array_kind(kinds[3])) ?
                                             GFX_BOX_MODE_VECTOR : GFX_BOX_MODE_SCALAR,
                                         args, field_count, &errors);
        }
        if (Option.Refresh) Display_Refresh();
        return;
    }

    if (sysid == BC_SYS_GFX_TRIANGLE || sysid == BC_SYS_GFX_POLYGON || sysid == BC_SYS_GFX_CIRCLE ||
        sysid == BC_SYS_GFX_LINE || sysid == BC_SYS_GFX_PIXEL) {
        if (sysid == BC_SYS_GFX_TRIANGLE) {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_TRIANGLE_ARG_COUNT];
            uint16_t slots[BC_TRIANGLE_ARG_COUNT];
            BCValue scalars[BC_TRIANGLE_ARG_COUNT];
            uint8_t scalar_types[BC_TRIANGLE_ARG_COUNT];
            GfxBoxIntArg args[BC_TRIANGLE_ARG_COUNT] = {0};
            VMBoxScalarCtx scalar_ctx[BC_TRIANGLE_ARG_COUNT];
            VMBoxArrayCtx array_ctx[BC_TRIANGLE_ARG_COUNT];
            GfxBoxErrorSink errors;
            int i;
            for (i = 0; i < BC_TRIANGLE_ARG_COUNT; i++) {
                kinds[i] = *p++;
                slots[i] = 0;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
            }
            for (i = BC_TRIANGLE_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "TRIANGLE stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            errors.ctx = vm;
            errors.fail_msg = vm_box_fail_msg;
            errors.fail_range = vm_box_fail_range;
            for (i = 0; i < BC_TRIANGLE_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                args[i].present = 1;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    int count = 0;
                    array_ctx[i].vm = vm;
                    array_ctx[i].kind = kinds[i];
                    array_ctx[i].slot = slots[i];
                    bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                    args[i].count = count;
                    args[i].ctx = &array_ctx[i];
                    args[i].get_int = vm_box_array_get_int;
                } else if (kinds[i] == BC_BOX_ARG_STACK) {
                    scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    args[i].count = 1;
                    args[i].ctx = &scalar_ctx[i];
                    args[i].get_int = vm_box_scalar_get_int;
                } else {
                    bc_vm_error(vm, "Argument count");
                }
            }
            vm_sys_graphics_triangle_execute((bc_vm_box_is_array_kind(kinds[0]) &&
                                              bc_vm_box_is_array_kind(kinds[1]) &&
                                              bc_vm_box_is_array_kind(kinds[2]) &&
                                              bc_vm_box_is_array_kind(kinds[3]) &&
                                              bc_vm_box_is_array_kind(kinds[4]) &&
                                              bc_vm_box_is_array_kind(kinds[5])) ?
                                                 GFX_BOX_MODE_VECTOR : GFX_BOX_MODE_SCALAR,
                                             args, field_count, &errors);
            if (Option.Refresh) Display_Refresh();
            return;
        } else if (sysid == BC_SYS_GFX_POLYGON) {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_POLYGON_ARG_COUNT];
            uint16_t slots[BC_POLYGON_ARG_COUNT];
            BCValue scalars[BC_POLYGON_ARG_COUNT];
            uint8_t scalar_types[BC_POLYGON_ARG_COUNT];
            GfxBoxIntArg args[BC_POLYGON_ARG_COUNT] = {0};
            VMBoxScalarCtx scalar_ctx[BC_POLYGON_ARG_COUNT];
            VMBoxArrayCtx array_ctx[BC_POLYGON_ARG_COUNT];
            GfxBoxErrorSink errors;
            int i;
            for (i = 0; i < BC_POLYGON_ARG_COUNT; i++) {
                kinds[i] = *p++;
                slots[i] = 0;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
            }
            for (i = BC_POLYGON_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "POLYGON stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            errors.ctx = vm;
            errors.fail_msg = vm_box_fail_msg;
            errors.fail_range = vm_box_fail_range;
            for (i = 0; i < BC_POLYGON_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                args[i].present = 1;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    int count = 0;
                    array_ctx[i].vm = vm;
                    array_ctx[i].kind = kinds[i];
                    array_ctx[i].slot = slots[i];
                    bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                    args[i].count = count;
                    args[i].ctx = &array_ctx[i];
                    args[i].get_int = vm_box_array_get_int;
                } else if (kinds[i] == BC_BOX_ARG_STACK) {
                    scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    args[i].count = 1;
                    args[i].ctx = &scalar_ctx[i];
                    args[i].get_int = vm_box_scalar_get_int;
                } else {
                    bc_vm_error(vm, "Argument count");
                }
            }
            vm_sys_graphics_polygon_execute(args, field_count, &errors);
            if (Option.Refresh) Display_Refresh();
            return;
        } else if (sysid == BC_SYS_GFX_CIRCLE) {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_BOX_ARG_COUNT];
            uint16_t slots[BC_BOX_ARG_COUNT];
            BCValue scalars[BC_BOX_ARG_COUNT];
            uint8_t scalar_types[BC_BOX_ARG_COUNT];
            GfxCircleArg args[GFX_CIRCLE_ARG_COUNT] = {0};
            VMBoxScalarCtx int_scalar_ctx[GFX_CIRCLE_ARG_COUNT];
            VMFloatScalarCtx float_scalar_ctx[GFX_CIRCLE_ARG_COUNT];
            VMBoxArrayCtx array_ctx[GFX_CIRCLE_ARG_COUNT];
            GfxCircleErrorSink errors;
            int i;
            for (i = 0; i < BC_BOX_ARG_COUNT; i++) {
                kinds[i] = *p++;
                slots[i] = 0;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
            }
            for (i = BC_BOX_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "CIRCLE stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            errors.ctx = vm;
            errors.fail_msg = vm_circle_fail_msg;
            errors.fail_range = vm_circle_fail_range;
            for (i = 0; i < GFX_CIRCLE_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                args[i].present = 1;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    int count = 0;
                    array_ctx[i].vm = vm;
                    array_ctx[i].kind = kinds[i];
                    array_ctx[i].slot = slots[i];
                    bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                    args[i].count = count;
                    args[i].ctx = &array_ctx[i];
                    args[i].get_int = vm_box_array_get_int;
                    args[i].get_float = vm_box_array_get_float;
                } else if (kinds[i] == BC_BOX_ARG_STACK) {
                    int_scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    float_scalar_ctx[i].value = bc_vm_box_scalar_float(vm, scalars[i], scalar_types[i]);
                    args[i].count = 1;
                    args[i].ctx = &int_scalar_ctx[i];
                    args[i].get_int = vm_box_scalar_get_int;
                    if (i == 4) {
                        args[i].ctx = &float_scalar_ctx[i];
                        args[i].get_float = vm_box_scalar_get_float;
                    } else {
                        args[i].get_float = NULL;
                    }
                } else {
                    bc_vm_error(vm, "Argument count");
                }
            }
            vm_sys_graphics_circle_execute((bc_vm_box_is_array_kind(kinds[0]) &&
                                            bc_vm_box_is_array_kind(kinds[1]) &&
                                            bc_vm_box_is_array_kind(kinds[2])) ?
                                               GFX_CIRCLE_MODE_VECTOR : GFX_CIRCLE_MODE_SCALAR,
                                           args, field_count, &errors);
            if (Option.Refresh) Display_Refresh();
            return;
        } else if (sysid == BC_SYS_GFX_LINE) {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_LINE_ARG_COUNT];
            uint16_t slots[BC_LINE_ARG_COUNT];
            BCValue scalars[BC_LINE_ARG_COUNT];
            uint8_t scalar_types[BC_LINE_ARG_COUNT];
            GfxLineArg args[GFX_LINE_ARG_COUNT] = {0};
            VMBoxScalarCtx scalar_ctx[GFX_LINE_ARG_COUNT];
            VMBoxArrayCtx array_ctx[GFX_LINE_ARG_COUNT];
            GfxLineErrorSink errors;
            int i;
            for (i = 0; i < BC_LINE_ARG_COUNT; i++) {
                kinds[i] = *p++;
                slots[i] = 0;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
            }
            for (i = BC_LINE_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "LINE stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            errors.ctx = vm;
            errors.fail_msg = vm_line_fail_msg;
            errors.fail_range = vm_line_fail_range;
            for (i = 0; i < GFX_LINE_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                args[i].present = 1;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    int count = 0;
                    array_ctx[i].vm = vm;
                    array_ctx[i].kind = kinds[i];
                    array_ctx[i].slot = slots[i];
                    bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                    args[i].count = count;
                    args[i].ctx = &array_ctx[i];
                    args[i].get_int = vm_box_array_get_int;
                } else if (kinds[i] == BC_BOX_ARG_STACK) {
                    scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    args[i].count = 1;
                    args[i].ctx = &scalar_ctx[i];
                    args[i].get_int = vm_box_scalar_get_int;
                } else {
                    bc_vm_error(vm, "Argument count");
                }
            }
            vm_sys_graphics_line_execute((bc_vm_box_is_array_kind(kinds[0]) &&
                                          bc_vm_box_is_array_kind(kinds[1]) &&
                                          field_count > 3 &&
                                          bc_vm_box_is_array_kind(kinds[2]) &&
                                          bc_vm_box_is_array_kind(kinds[3])) ?
                                             GFX_LINE_MODE_VECTOR : GFX_LINE_MODE_SCALAR,
                                         args, field_count, &errors);
            return;
        } else {
            uint8_t field_count = *p++;
            uint8_t kinds[BC_PIXEL_ARG_COUNT];
            uint16_t slots[BC_PIXEL_ARG_COUNT];
            BCValue scalars[BC_PIXEL_ARG_COUNT];
            uint8_t scalar_types[BC_PIXEL_ARG_COUNT];
            GfxPixelArg args[GFX_PIXEL_ARG_COUNT] = {0};
            VMBoxScalarCtx scalar_ctx[GFX_PIXEL_ARG_COUNT];
            VMBoxArrayCtx array_ctx[GFX_PIXEL_ARG_COUNT];
            GfxPixelErrorSink errors;
            int i;
            for (i = 0; i < BC_PIXEL_ARG_COUNT; i++) {
                kinds[i] = *p++;
                slots[i] = 0;
                scalar_types[i] = 0;
                if (bc_vm_box_is_array_kind(kinds[i])) slots[i] = bc_vm_read_u16_at(&p);
            }
            for (i = BC_PIXEL_ARG_COUNT - 1; i >= 0; i--) {
                if (kinds[i] == BC_BOX_ARG_STACK) {
                    if (vm->sp < 0) bc_vm_error(vm, "PIXEL stack underflow");
                    scalars[i] = vm->stack[vm->sp];
                    scalar_types[i] = vm->stack_types[vm->sp];
                    vm->sp--;
                }
            }
            bc_vm_require_display(vm);
            errors.ctx = vm;
            errors.fail_msg = vm_pixel_fail_msg;
            errors.fail_range = vm_pixel_fail_range;
            for (i = 0; i < GFX_PIXEL_ARG_COUNT; i++) {
                if (kinds[i] == BC_BOX_ARG_EMPTY) continue;
                args[i].present = 1;
                if (bc_vm_box_is_array_kind(kinds[i])) {
                    int count = 0;
                    array_ctx[i].vm = vm;
                    array_ctx[i].kind = kinds[i];
                    array_ctx[i].slot = slots[i];
                    bc_vm_box_get_array(vm, kinds[i], slots[i], &count);
                    args[i].count = count;
                    args[i].ctx = &array_ctx[i];
                    args[i].get_int = vm_box_array_get_int;
                } else if (kinds[i] == BC_BOX_ARG_STACK) {
                    scalar_ctx[i].value = bc_vm_box_scalar_int(vm, scalars[i], scalar_types[i]);
                    args[i].count = 1;
                    args[i].ctx = &scalar_ctx[i];
                    args[i].get_int = vm_box_scalar_get_int;
                } else {
                    bc_vm_error(vm, "Argument count");
                }
            }
            vm_sys_graphics_pixel_execute((bc_vm_box_is_array_kind(kinds[0]) && bc_vm_box_is_array_kind(kinds[1])) ?
                                              GFX_PIXEL_MODE_VECTOR : GFX_PIXEL_MODE_SCALAR,
                                          args, field_count, &errors);
            if (Option.Refresh) Display_Refresh();
            return;
        }
    }
}

/* ======================================================================
 * bc_vm_execute — main dispatch loop using computed goto
 * ====================================================================== */
void bc_vm_execute(BCVMState *vm) {

    /* ---- Helper macros ---- */
#define DISPATCH() goto *dispatch_table[*vm->pc++]

#define READ_U8()  (*vm->pc++)
#define READ_U16() ({ uint16_t _v; memcpy(&_v, vm->pc, 2); vm->pc += 2; _v; })
#define READ_I16() ({ int16_t  _v; memcpy(&_v, vm->pc, 2); vm->pc += 2; _v; })
#define READ_U32() ({ uint32_t _v; memcpy(&_v, vm->pc, 4); vm->pc += 4; _v; })
#define READ_I64() ({ int64_t  _v; memcpy(&_v, vm->pc, 8); vm->pc += 8; _v; })
#define READ_F64() ({ MMFLOAT  _v; memcpy(&_v, vm->pc, 8); vm->pc += 8; _v; })

#define PUSH_I(val) do { \
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow"); \
    vm->sp++; vm->stack[vm->sp].i = (val); vm->stack_types[vm->sp] = T_INT; \
} while(0)

#define PUSH_F(val) do { \
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow"); \
    vm->sp++; vm->stack[vm->sp].f = (val); vm->stack_types[vm->sp] = T_NBR; \
} while(0)

#define PUSH_S(val) do { \
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow"); \
    vm->sp++; vm->stack[vm->sp].s = (val); vm->stack_types[vm->sp] = T_STR; \
} while(0)

#define POP_I() (vm->stack[vm->sp--].i)
#define POP_F() (vm->stack[vm->sp--].f)
#define POP_S() (vm->stack[vm->sp--].s)
#define TOS_I() (vm->stack[vm->sp].i)
#define TOS_F() (vm->stack[vm->sp].f)
#define POP_NUMERIC_I() \
    ((vm->sp < 0) ? (bc_vm_error(vm, "Stack underflow"), (int64_t)0) : \
     (vm->stack_types[vm->sp] == T_NBR ? \
        ({ MMFLOAT _x = vm->stack[vm->sp--].f; (_x >= 0) ? (int64_t)(_x + 0.5) : (int64_t)(_x - 0.5); }) : \
        POP_I()))
#define POP_NUMERIC_F() \
    ((vm->sp < 0) ? (bc_vm_error(vm, "Stack underflow"), (MMFLOAT)0) : \
     (vm->stack_types[vm->sp] == T_NBR ? POP_F() : (MMFLOAT)POP_I()))

    /* ---- Build dispatch table ---- */
    static const void *dispatch_table[256] = {
        [0 ... 255] = &&op_invalid,

        /* Stack / Value Operations */
        [OP_NOP]            = &&op_nop,
        [OP_PUSH_INT]       = &&op_push_int,
        [OP_PUSH_FLT]       = &&op_push_flt,
        [OP_PUSH_STR]       = &&op_push_str,
        [OP_PUSH_ZERO]      = &&op_push_zero,
        [OP_PUSH_ONE]       = &&op_push_one,
        [OP_LOAD_I]         = &&op_load_i,
        [OP_LOAD_F]         = &&op_load_f,
        [OP_LOAD_S]         = &&op_load_s,
        [OP_STORE_I]        = &&op_store_i,
        [OP_STORE_F]        = &&op_store_f,
        [OP_STORE_S]        = &&op_store_s,
        [OP_LOAD_ARR_I]     = &&op_load_arr_i,
        [OP_LOAD_ARR_F]     = &&op_load_arr_f,
        [OP_LOAD_ARR_S]     = &&op_load_arr_s,
        [OP_STORE_ARR_I]    = &&op_store_arr_i,
        [OP_STORE_ARR_F]    = &&op_store_arr_f,
        [OP_STORE_ARR_S]    = &&op_store_arr_s,
        [OP_POP]            = &&op_pop,
        [OP_DUP]            = &&op_dup,
        [OP_CVT_I2F]        = &&op_cvt_i2f,
        [OP_CVT_F2I]        = &&op_cvt_f2i,

        /* Integer Arithmetic */
        [OP_ADD_I]          = &&op_add_i,
        [OP_SUB_I]          = &&op_sub_i,
        [OP_MUL_I]          = &&op_mul_i,
        [OP_IDIV_I]         = &&op_idiv_i,
        [OP_MOD_I]          = &&op_mod_i,

        /* Float Arithmetic */
        [OP_ADD_F]          = &&op_add_f,
        [OP_SUB_F]          = &&op_sub_f,
        [OP_MUL_F]          = &&op_mul_f,
        [OP_DIV_F]          = &&op_div_f,
        [OP_POW_F]          = &&op_pow_f,
        [OP_MOD_F]          = &&op_mod_f,

        /* String Operations */
        [OP_ADD_S]          = &&op_add_s,

        /* Unary */
        [OP_NEG_I]          = &&op_neg_i,
        [OP_NEG_F]          = &&op_neg_f,
        [OP_NOT]            = &&op_not,
        [OP_INV]            = &&op_inv,

        /* Bitwise / Logical */
        [OP_AND]            = &&op_and,
        [OP_OR]             = &&op_or,
        [OP_XOR]            = &&op_xor,
        [OP_SHL]            = &&op_shl,
        [OP_SHR]            = &&op_shr,

        /* Integer Comparison */
        [OP_EQ_I]           = &&op_eq_i,
        [OP_NE_I]           = &&op_ne_i,
        [OP_LT_I]           = &&op_lt_i,
        [OP_GT_I]           = &&op_gt_i,
        [OP_LE_I]           = &&op_le_i,
        [OP_GE_I]           = &&op_ge_i,

        /* Float Comparison */
        [OP_EQ_F]           = &&op_eq_f,
        [OP_NE_F]           = &&op_ne_f,
        [OP_LT_F]           = &&op_lt_f,
        [OP_GT_F]           = &&op_gt_f,
        [OP_LE_F]           = &&op_le_f,
        [OP_GE_F]           = &&op_ge_f,

        /* String Comparison */
        [OP_EQ_S]           = &&op_eq_s,
        [OP_NE_S]           = &&op_ne_s,
        [OP_LT_S]           = &&op_lt_s,
        [OP_GT_S]           = &&op_gt_s,
        [OP_LE_S]           = &&op_le_s,
        [OP_GE_S]           = &&op_ge_s,

        /* Control Flow */
        [OP_JMP]            = &&op_jmp,
        [OP_JMP_ABS]        = &&op_jmp_abs,
        [OP_JZ]             = &&op_jz,
        [OP_JNZ]            = &&op_jnz,
        [OP_JCMP_I]         = &&op_jcmp_i,
        [OP_JCMP_F]         = &&op_jcmp_f,
        [OP_MOV_VAR]        = &&op_mov_var,
        [OP_GOSUB]          = &&op_gosub,
        [OP_RETURN]         = &&op_return,

        /* FOR loop */
        [OP_FOR_INIT_I]     = &&op_for_init_i,
        [OP_FOR_NEXT_I]     = &&op_for_next_i,
        [OP_FOR_INIT_F]     = &&op_for_init_f,
        [OP_FOR_NEXT_F]     = &&op_for_next_f,

        /* SUB / FUNCTION */
        [OP_CALL_SUB]       = &&op_call_sub,
        [OP_CALL_FUN]       = &&op_call_fun,
        [OP_RET_SUB]        = &&op_ret_sub,
        [OP_RET_FUN]        = &&op_ret_fun,
        [OP_ENTER_FRAME]    = &&op_enter_frame,
        [OP_LEAVE_FRAME]    = &&op_leave_frame,
        [OP_LOAD_LOCAL_I]   = &&op_load_local_i,
        [OP_LOAD_LOCAL_F]   = &&op_load_local_f,
        [OP_LOAD_LOCAL_S]   = &&op_load_local_s,
        [OP_STORE_LOCAL_I]  = &&op_store_local_i,
        [OP_STORE_LOCAL_F]  = &&op_store_local_f,
        [OP_STORE_LOCAL_S]  = &&op_store_local_s,
        [OP_LOAD_LOCAL_ARR_I]  = &&op_load_local_arr_i,
        [OP_LOAD_LOCAL_ARR_F]  = &&op_load_local_arr_f,
        [OP_LOAD_LOCAL_ARR_S]  = &&op_load_local_arr_s,
        [OP_STORE_LOCAL_ARR_I] = &&op_store_local_arr_i,
        [OP_STORE_LOCAL_ARR_F] = &&op_store_local_arr_f,
        [OP_STORE_LOCAL_ARR_S] = &&op_store_local_arr_s,
        /* PRINT */
        [OP_PRINT_INT]      = &&op_print_int,
        [OP_PRINT_FLT]      = &&op_print_flt,
        [OP_PRINT_STR]      = &&op_print_str,
        [OP_PRINT_NEWLINE]  = &&op_print_newline,
        [OP_PRINT_TAB]      = &&op_print_tab,

        /* DIM arrays */
        [OP_DIM_ARR_I]      = &&op_dim_arr_i,
        [OP_DIM_ARR_F]      = &&op_dim_arr_f,
        [OP_DIM_ARR_S]      = &&op_dim_arr_s,

        /* TYPE / STRUCT field access (offsets baked in at compile time) */
        [OP_LOAD_STRUCT_FIELD_I]  = &&op_load_struct_field_i,
        [OP_LOAD_STRUCT_FIELD_F]  = &&op_load_struct_field_f,
        [OP_LOAD_STRUCT_FIELD_S]  = &&op_load_struct_field_s,
        [OP_STORE_STRUCT_FIELD_I] = &&op_store_struct_field_i,
        [OP_STORE_STRUCT_FIELD_F] = &&op_store_struct_field_f,
        [OP_STORE_STRUCT_FIELD_S] = &&op_store_struct_field_s,
        [OP_LOAD_STRUCT_ELEM_I]   = &&op_load_struct_elem_i,
        [OP_LOAD_STRUCT_ELEM_F]   = &&op_load_struct_elem_f,
        [OP_LOAD_STRUCT_ELEM_S]   = &&op_load_struct_elem_s,
        [OP_STORE_STRUCT_ELEM_I]  = &&op_store_struct_elem_i,
        [OP_STORE_STRUCT_ELEM_F]  = &&op_store_struct_elem_f,
        [OP_STORE_STRUCT_ELEM_S]  = &&op_store_struct_elem_s,
        [OP_LOAD_STRUCT_NESTED_I]   = &&op_load_struct_nested_i,
        [OP_LOAD_STRUCT_NESTED_F]   = &&op_load_struct_nested_f,
        [OP_LOAD_STRUCT_NESTED_S]   = &&op_load_struct_nested_s,
        [OP_STORE_STRUCT_NESTED_I]  = &&op_store_struct_nested_i,
        [OP_STORE_STRUCT_NESTED_F]  = &&op_store_struct_nested_f,
        [OP_STORE_STRUCT_NESTED_S]  = &&op_store_struct_nested_s,

        /* Native string functions */
        [OP_STR_LEN]        = &&op_str_len,
        [OP_STR_LEFT]       = &&op_str_left,
        [OP_STR_RIGHT]      = &&op_str_right,
        [OP_STR_MID2]       = &&op_str_mid2,
        [OP_STR_MID3]       = &&op_str_mid3,
        [OP_STR_UCASE]      = &&op_str_ucase,
        [OP_STR_LCASE]      = &&op_str_lcase,
        [OP_STR_VAL]        = &&op_str_val,
        [OP_STR_STR]        = &&op_str_str,
        [OP_STR_CHR]        = &&op_str_chr,
        [OP_STR_ASC]        = &&op_str_asc,
        [OP_STR_INSTR]      = &&op_str_instr,
        [OP_STR_HEX]        = &&op_str_hex,
        [OP_STR_OCT]        = &&op_str_oct,
        [OP_STR_BIN]        = &&op_str_bin,

        /* Native math functions */
        [OP_MATH_SIN]       = &&op_math_sin,
        [OP_MATH_COS]       = &&op_math_cos,
        [OP_MATH_TAN]       = &&op_math_tan,
        [OP_MATH_ATN]       = &&op_math_atn,
        [OP_MATH_SQR]       = &&op_math_sqr,
        [OP_MATH_LOG]       = &&op_math_log,
        [OP_MATH_EXP]       = &&op_math_exp,
        [OP_MATH_ABS]       = &&op_math_abs,
        [OP_MATH_SGN]       = &&op_math_sgn,
        [OP_MATH_INT]       = &&op_math_int,
        [OP_MATH_FIX]       = &&op_math_fix,
        [OP_MATH_CINT]      = &&op_math_cint,
        [OP_MATH_RAD]       = &&op_math_rad,
        [OP_MATH_DEG]       = &&op_math_deg,
        [OP_MATH_ASIN]      = &&op_math_asin,
        [OP_MATH_ACOS]      = &&op_math_acos,
        [OP_MATH_ATAN2]     = &&op_math_atan2,
        [OP_MATH_PI]        = &&op_math_pi,
        [OP_MATH_MAX]       = &&op_math_max,
        [OP_MATH_MIN]       = &&op_math_min,

        /* DATA / READ / RESTORE */
        [OP_READ_I]         = &&op_read_i,
        [OP_READ_F]         = &&op_read_f,
        [OP_READ_S]         = &&op_read_s,
        [OP_RESTORE]        = &&op_restore,

        /* Additional string functions */
        [OP_STR_SPACE]      = &&op_str_space,
        [OP_STR_STRING]     = &&op_str_string,
        [OP_STR_FIELD3]     = &&op_str_field3,
        [OP_STR_INKEY]      = &&op_str_inkey,

        /* Additional numeric functions */
        [OP_RND]            = &&op_rnd,
        [OP_TIMER]          = &&op_timer,
        [OP_MM_HRES]        = &&op_mm_hres,
        [OP_MM_VRES]        = &&op_mm_vres,

        /* Additional statements */
        [OP_INC_I]          = &&op_inc_i,
        [OP_INC_F]          = &&op_inc_f,
        [OP_RANDOMIZE]      = &&op_randomize,
        [OP_ERROR_S]        = &&op_error_s,
        [OP_ERROR_EMPTY]    = &&op_error_empty,
        [OP_CLEAR]          = &&op_clear,
        [OP_FASTGFX_SWAP]   = &&op_fastgfx_swap,
        [OP_FASTGFX_SYNC]   = &&op_fastgfx_sync,
        [OP_RGB]            = &&op_rgb,
        [OP_FASTGFX_CREATE] = &&op_fastgfx_create,
        [OP_FASTGFX_CLOSE]  = &&op_fastgfx_close,
        [OP_FASTGFX_FPS]    = &&op_fastgfx_fps,
        [OP_COLOUR]         = &&op_colour,
        [OP_PAUSE]          = &&op_pause,
        [OP_STR_DATE]       = &&op_str_date,
        [OP_STR_TIME]       = &&op_str_time,
        [OP_KEYDOWN]        = &&op_keydown,
        [OP_SETPIN]         = &&op_setpin,
        [OP_PIN_READ]       = &&op_pin_read,
        [OP_PIN_WRITE]      = &&op_pin_write,
        [OP_PWM]            = &&op_pwm,
        [OP_SERVO]          = &&op_servo,
        [OP_FILE]           = &&op_file,
        [OP_PIXEL_READ]     = &&op_pixel_read,
        [OP_MATH_MULSHR]    = &&op_math_mulshr,
        [OP_FONT]           = &&op_font,
        [OP_SYSCALL]        = &&op_syscall,
        [OP_MATH_SQRSHR]    = &&op_math_sqrshr,
        [OP_MATH_MULSHRADD] = &&op_math_mulshradd,

        /* Fast loop */
        [OP_FAST_LOOP]      = &&op_fast_loop,

        /* Bridge */
        [OP_BRIDGE_CMD]     = &&op_bridge_cmd,
        [OP_BRIDGE_FUN_I]   = &&op_bridge_fun_i,
        [OP_BRIDGE_FUN_F]   = &&op_bridge_fun_f,
        [OP_BRIDGE_FUN_S]   = &&op_bridge_fun_s,

        /* Housekeeping */
        [OP_LINE]           = &&op_line,
        [OP_CHECKINT]       = &&op_checkint,
        [OP_END]            = &&op_end,
        [OP_HALT]           = &&op_halt,
    };

    /* ---- Begin dispatch ---- */
    DISPATCH();

    /* ==================================================================
     * Stack / Value Operations
     * ================================================================== */

op_nop:
    DISPATCH();

op_push_int: {
    int64_t v = READ_I64();
    PUSH_I(v);
    DISPATCH();
}

op_push_flt: {
    MMFLOAT v = READ_F64();
    PUSH_F(v);
    DISPATCH();
}

op_push_str: {
    uint16_t idx = READ_U16();
    if (idx >= vm->compiler->const_count)
        bc_vm_error(vm, "Invalid string constant index %d", idx);
    BCConstant *c = &vm->compiler->constants[idx];
    /* Copy to a rotating temp buffer (MMBasic format: byte 0 = length) */
    uint8_t *tmp = vm_get_str_temp(vm);
    tmp[0] = (uint8_t)c->len;
    if (c->len > 0)
        memcpy(tmp + 1, c->data, c->len);
    PUSH_S(tmp);
    DISPATCH();
}

op_push_zero:
    PUSH_I(0);
    DISPATCH();

op_push_one:
    PUSH_I(1);
    DISPATCH();

op_load_i: {
    uint16_t slot = READ_U16();
    PUSH_I(vm->globals[slot].i);
    DISPATCH();
}

op_load_f: {
    uint16_t slot = READ_U16();
    PUSH_F(vm->globals[slot].f);
    DISPATCH();
}

op_load_s: {
    uint16_t slot = READ_U16();
    uint8_t *s = vm->globals[slot].s;
    PUSH_S(s ? s : vm_empty_string);
    DISPATCH();
}

op_store_i: {
    uint16_t slot = READ_U16();
    if (vm->stack_types[vm->sp] == T_NBR)
        vm->globals[slot].i = (int64_t)POP_F();
    else
        vm->globals[slot].i = POP_I();
    DISPATCH();
}

op_store_f: {
    uint16_t slot = READ_U16();
    if (vm->stack_types[vm->sp] == T_INT)
        vm->globals[slot].f = (MMFLOAT)POP_I();
    else
        vm->globals[slot].f = POP_F();
    DISPATCH();
}

op_store_s: {
    uint16_t slot = READ_U16();
    uint8_t *src = POP_S();
    /* Allocate persistent storage for this global string if needed */
    if (!vm->globals[slot].s) {
        vm->globals[slot].s = BC_ALLOC(STRINGSIZE);
        if (!vm->globals[slot].s) bc_vm_error(vm, "Out of memory for string");
    }
    Mstrcpy(vm->globals[slot].s, src);
    DISPATCH();
}

    /* ==================================================================
     * Array operations
     * ================================================================== */

op_load_arr_i: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_GARR_I, slot);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    /* Indices are pushed first-dim-first, so pop in reverse */
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_I(arr->data[off].i);
    DISPATCH();
}

op_load_arr_f: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_GARR_F, slot);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_F(arr->data[off].f);
    DISPATCH();
}

op_load_arr_s: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_GARR_S, slot);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_S(arr->data[off].s);
    DISPATCH();
}

op_store_arr_i: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    int64_t val = POP_NUMERIC_I();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    arr->data[off].i = val;
    DISPATCH();
}

op_store_arr_f: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    MMFLOAT val = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    arr->data[off].f = val;
    DISPATCH();
}

op_store_arr_s: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (!arr->data) bc_vm_error(vm, "Array not dimensioned");
    uint8_t *val = POP_S();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    /* Allocate string storage in the array element if needed */
    if (!arr->data[off].s) {
        arr->data[off].s = BC_ALLOC(STRINGSIZE);
        if (!arr->data[off].s) bc_vm_error(vm, "Out of memory for string array");
    }
    Mstrcpy(arr->data[off].s, val);
    DISPATCH();
}

op_pop:
    if (vm->sp < 0) bc_vm_error(vm, "Stack underflow");
    vm->sp--;
    DISPATCH();

op_dup: {
    if (vm->sp < 0) bc_vm_error(vm, "Stack underflow on DUP");
    if (vm->sp >= VM_STACK_SIZE - 1) bc_vm_error(vm, "Stack overflow on DUP");
    vm->stack[vm->sp + 1] = vm->stack[vm->sp];
    vm->stack_types[vm->sp + 1] = vm->stack_types[vm->sp];
    vm->sp++;
    DISPATCH();
}

op_cvt_i2f:
    vm->stack[vm->sp].f = (MMFLOAT)vm->stack[vm->sp].i;
    vm->stack_types[vm->sp] = T_NBR;
    DISPATCH();

op_cvt_f2i: {
    /* Match MMBasic FloatToInt64: round half away from zero */
    MMFLOAT x = vm->stack[vm->sp].f;
    vm->stack[vm->sp].i = (x >= 0) ? (int64_t)(x + 0.5) : (int64_t)(x - 0.5);
    vm->stack_types[vm->sp] = T_INT;
    DISPATCH();
}

    /* ==================================================================
     * Integer Arithmetic
     * ================================================================== */

op_add_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i += b;
    DISPATCH();
}

op_sub_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i -= b;
    DISPATCH();
}

op_mul_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i *= b;
    DISPATCH();
}

op_idiv_i: {
    int64_t b = POP_I();
    if (b == 0) bc_vm_error(vm, "Division by zero");
    vm->stack[vm->sp].i /= b;
    DISPATCH();
}

op_mod_i: {
    int64_t b = POP_I();
    if (b == 0) bc_vm_error(vm, "Division by zero");
    vm->stack[vm->sp].i %= b;
    DISPATCH();
}

    /* ==================================================================
     * Float Arithmetic
     * ================================================================== */

op_add_f: {
    MMFLOAT b = POP_F();
    vm->stack[vm->sp].f += b;
    DISPATCH();
}

op_sub_f: {
    MMFLOAT b = POP_F();
    vm->stack[vm->sp].f -= b;
    DISPATCH();
}

op_mul_f: {
    MMFLOAT b = POP_F();
    vm->stack[vm->sp].f *= b;
    DISPATCH();
}

op_div_f: {
    MMFLOAT b = POP_F();
    if (b == 0.0) bc_vm_error(vm, "Division by zero");
    vm->stack[vm->sp].f /= b;
    DISPATCH();
}

op_pow_f: {
    MMFLOAT b = POP_F();
    vm->stack[vm->sp].f = pow(vm->stack[vm->sp].f, b);
    DISPATCH();
}

op_mod_f: {
    MMFLOAT b = POP_F();
    if (b == 0.0) bc_vm_error(vm, "Division by zero");
    vm->stack[vm->sp].f = fmod(vm->stack[vm->sp].f, b);
    DISPATCH();
}

    /* ==================================================================
     * String Concatenation
     * ================================================================== */

op_add_s: {
    /* MMBasic string format: byte[0] = length, bytes[1..len] = data */
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    uint8_t *tmp = vm_get_str_temp(vm);
    int alen = a ? a[0] : 0;
    int blen = b ? b[0] : 0;
    int total = alen + blen;
    if (total > MAXSTRLEN)
        bc_vm_error(vm, "String too long");
    tmp[0] = (uint8_t)total;
    if (alen > 0) memcpy(tmp + 1, a + 1, alen);
    if (blen > 0) memcpy(tmp + 1 + alen, b + 1, blen);
    PUSH_S(tmp);
    DISPATCH();
}

    /* ==================================================================
     * Unary
     * ================================================================== */

op_neg_i:
    vm->stack[vm->sp].i = -vm->stack[vm->sp].i;
    DISPATCH();

op_neg_f:
    vm->stack[vm->sp].f = -vm->stack[vm->sp].f;
    DISPATCH();

op_not:
    /* MMBasic NOT is logical NOT (NOT 0 = 1, NOT nonzero = 0) */
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i != 0) ? 0 : 1;
    DISPATCH();

op_inv:
    vm->stack[vm->sp].i = ~vm->stack[vm->sp].i;
    DISPATCH();

    /* ==================================================================
     * Bitwise / Logical
     * ================================================================== */

op_and: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i &= b;
    DISPATCH();
}

op_or: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i |= b;
    DISPATCH();
}

op_xor: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i ^= b;
    DISPATCH();
}

op_shl: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i <<= b;
    DISPATCH();
}

op_shr: {
    int64_t b = POP_I();
    /* Logical right shift via unsigned cast */
    vm->stack[vm->sp].i = (int64_t)((uint64_t)vm->stack[vm->sp].i >> b);
    DISPATCH();
}

    /* ==================================================================
     * Integer Comparison — produce int 0 or 1
     * ================================================================== */

op_eq_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i == b) ? 1 : 0;
    DISPATCH();
}

op_ne_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i != b) ? 1 : 0;
    DISPATCH();
}

op_lt_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i < b) ? 1 : 0;
    DISPATCH();
}

op_gt_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i > b) ? 1 : 0;
    DISPATCH();
}

op_le_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i <= b) ? 1 : 0;
    DISPATCH();
}

op_ge_i: {
    int64_t b = POP_I();
    vm->stack[vm->sp].i = (vm->stack[vm->sp].i >= b) ? 1 : 0;
    DISPATCH();
}

    /* ==================================================================
     * Float Comparison — produce int 0 or 1
     * ================================================================== */

op_eq_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a == b) ? 1 : 0);
    DISPATCH();
}

op_ne_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a != b) ? 1 : 0);
    DISPATCH();
}

op_lt_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a < b) ? 1 : 0);
    DISPATCH();
}

op_gt_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a > b) ? 1 : 0);
    DISPATCH();
}

op_le_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a <= b) ? 1 : 0);
    DISPATCH();
}

op_ge_f: {
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_I((a >= b) ? 1 : 0);
    DISPATCH();
}

    /* ==================================================================
     * String Comparison — produce int 0 or 1
     *
     * Mstrcmp returns <0, 0, or >0 (like strcmp but for MMBasic strings).
     * ================================================================== */

op_eq_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) == 0 ? 1 : 0);
    DISPATCH();
}

op_ne_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) != 0 ? 1 : 0);
    DISPATCH();
}

op_lt_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) < 0 ? 1 : 0);
    DISPATCH();
}

op_gt_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) > 0 ? 1 : 0);
    DISPATCH();
}

op_le_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) <= 0 ? 1 : 0);
    DISPATCH();
}

op_ge_s: {
    uint8_t *b = POP_S();
    uint8_t *a = POP_S();
    PUSH_I(Mstrcmp(a, b) >= 0 ? 1 : 0);
    DISPATCH();
}

    /* ==================================================================
     * Control Flow
     * ================================================================== */

op_jmp: {
    int16_t offset = READ_I16();
    if (offset < 0) bc_vm_poll_interrupts();
    vm->pc += offset;
    DISPATCH();
}

op_jmp_abs: {
    uint32_t addr = READ_U32();
    vm->pc = vm->bytecode + addr;
    DISPATCH();
}

op_jz: {
    int16_t offset = READ_I16();
    int64_t val = POP_NUMERIC_I();
    if (val == 0) {
        if (offset < 0) bc_vm_poll_interrupts();
        vm->pc += offset;
    }
    DISPATCH();
}

op_jnz: {
    int16_t offset = READ_I16();
    int64_t val = POP_NUMERIC_I();
    if (val != 0) {
        if (offset < 0) bc_vm_poll_interrupts();
        vm->pc += offset;
    }
    DISPATCH();
}

op_jcmp_i: {
    uint8_t rel = *vm->pc++;
    int16_t offset = READ_I16();
    int64_t b = POP_I();
    int64_t a = POP_I();
    int take = 0;
    switch (rel) {
        case BC_JCMP_EQ: take = (a == b); break;
        case BC_JCMP_NE: take = (a != b); break;
        case BC_JCMP_LT: take = (a <  b); break;
        case BC_JCMP_GT: take = (a >  b); break;
        case BC_JCMP_LE: take = (a <= b); break;
        case BC_JCMP_GE: take = (a >= b); break;
        default: bc_vm_error(vm, "Invalid JCMP_I relation %u", (unsigned)rel);
    }
    if (take) {
        if (offset < 0) bc_vm_poll_interrupts();
        vm->pc += offset;
    }
    DISPATCH();
}

op_jcmp_f: {
    uint8_t rel = *vm->pc++;
    int16_t offset = READ_I16();
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    int take = 0;
    switch (rel) {
        case BC_JCMP_EQ: take = (a == b); break;
        case BC_JCMP_NE: take = (a != b); break;
        case BC_JCMP_LT: take = (a <  b); break;
        case BC_JCMP_GT: take = (a >  b); break;
        case BC_JCMP_LE: take = (a <= b); break;
        case BC_JCMP_GE: take = (a >= b); break;
        default: bc_vm_error(vm, "Invalid JCMP_F relation %u", (unsigned)rel);
    }
    if (take) {
        if (offset < 0) bc_vm_poll_interrupts();
        vm->pc += offset;
    }
    DISPATCH();
}

op_mov_var: {
    uint8_t kind = *vm->pc++;
    uint16_t src_raw = READ_U16();
    uint16_t dst_raw = READ_U16();
    int src_is_local = (src_raw & 0x8000u) != 0;
    int dst_is_local = (dst_raw & 0x8000u) != 0;
    uint16_t src_slot = src_raw & 0x7FFFu;
    uint16_t dst_slot = dst_raw & 0x7FFFu;
    BCValue *src;
    BCValue *dst;

    if (src_is_local) {
        int idx = vm->frame_base + src_slot;
        if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local variable overflow");
        src = &vm->locals[idx];
    } else {
        src = &vm->globals[src_slot];
    }

    if (dst_is_local) {
        int idx = vm->frame_base + dst_slot;
        if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local variable overflow");
        dst = &vm->locals[idx];
    } else {
        dst = &vm->globals[dst_slot];
    }

    switch (kind) {
        case BC_MOV_INT:
            dst->i = src->i;
            if (dst_is_local) vm->local_types[vm->frame_base + dst_slot] = T_INT;
            break;
        case BC_MOV_FLT:
            dst->f = src->f;
            if (dst_is_local) vm->local_types[vm->frame_base + dst_slot] = T_NBR;
            break;
        case BC_MOV_STR:
            if (!dst->s) {
                dst->s = BC_ALLOC(STRINGSIZE);
                if (!dst->s) bc_vm_error(vm, "Out of memory for string");
            }
            Mstrcpy(dst->s, src->s ? src->s : (uint8_t *)"");
            if (dst_is_local) vm->local_types[vm->frame_base + dst_slot] = T_STR;
            break;
        default:
            bc_vm_error(vm, "Invalid MOV_VAR kind %u", (unsigned)kind);
    }
    DISPATCH();
}

op_gosub: {
    uint32_t addr = READ_U32();
    if (vm->gsp >= VM_MAX_GOSUB)
        bc_vm_error(vm, "GOSUB stack overflow");
    vm->gosub_stack[vm->gsp].return_pc = vm->pc;
    vm->gsp++;
    vm->pc = vm->bytecode + addr;
    DISPATCH();
}

op_return: {
    if (vm->gsp <= 0)
        bc_vm_error(vm, "RETURN without GOSUB");
    vm->gsp--;
    vm->pc = vm->gosub_stack[vm->gsp].return_pc;
    DISPATCH();
}

    /* ==================================================================
     * FOR Loop — Integer
     * ================================================================== */

op_for_init_i: {
    uint16_t raw_var   = READ_U16();
    uint16_t raw_lim   = READ_U16();
    uint16_t raw_step  = READ_U16();
    int16_t  exit_off  = READ_I16();

    int var_is_local = (raw_var & 0x8000) != 0;
    uint16_t var_slot = raw_var & 0x7FFF;
    int lim_is_local  = (raw_lim & 0x8000) != 0;
    uint16_t lim_slot = raw_lim & 0x7FFF;
    int step_is_local = (raw_step & 0x8000) != 0;
    uint16_t step_slot = raw_step & 0x7FFF;

    if (vm->fsp >= VM_MAX_FOR)
        bc_vm_error(vm, "FOR stack overflow");

    /* Push for-stack entry */
    BCForEntry *fe = &vm->for_stack[vm->fsp];
    fe->var_slot  = raw_var;  /* preserve flag for NEXT */
    fe->lim_slot  = raw_lim;
    fe->step_slot = raw_step;
    fe->loop_top  = vm->pc;       /* loop body starts here */
    fe->is_local  = var_is_local;
    fe->var_type  = T_INT;
    vm->fsp++;

    /* Check if already past limit */
    BCValue *var_ptr = var_is_local ? &vm->locals[vm->frame_base + var_slot] : &vm->globals[var_slot];
    int64_t val  = var_ptr->i;
    int64_t lim  = lim_is_local ? vm->locals[vm->frame_base + lim_slot].i : vm->globals[lim_slot].i;
    int64_t step = step_is_local ? vm->locals[vm->frame_base + step_slot].i : vm->globals[step_slot].i;
    if (step > 0 && val > lim) {
        vm->fsp--;
        vm->pc += exit_off;
    } else if (step < 0 && val < lim) {
        vm->fsp--;
        vm->pc += exit_off;
    } else if (step == 0) {
        bc_vm_error(vm, "FOR step cannot be zero");
    }
    DISPATCH();
}

op_for_next_i: {
    uint16_t raw_var   = READ_U16();
    uint16_t raw_lim   = READ_U16();
    uint16_t raw_step  = READ_U16();
    int16_t  loop_off  = READ_I16();

    int var_is_local = (raw_var & 0x8000) != 0;
    uint16_t var_slot = raw_var & 0x7FFF;
    int lim_is_local  = (raw_lim & 0x8000) != 0;
    uint16_t lim_slot = raw_lim & 0x7FFF;
    int step_is_local = (raw_step & 0x8000) != 0;
    uint16_t step_slot = raw_step & 0x7FFF;
    BCValue *var_ptr = var_is_local ? &vm->locals[vm->frame_base + var_slot] : &vm->globals[var_slot];

    int64_t step = step_is_local ? vm->locals[vm->frame_base + step_slot].i : vm->globals[step_slot].i;
    var_ptr->i += step;

    int64_t val = var_ptr->i;
    int64_t lim = lim_is_local ? vm->locals[vm->frame_base + lim_slot].i : vm->globals[lim_slot].i;

    int past;
    if (step > 0)
        past = (val > lim);
    else
        past = (val < lim);

    if (!past) {
        if (loop_off < 0) bc_vm_poll_interrupts();
        vm->pc += loop_off;
    } else {
        /* Loop done, pop for-stack */
        if (vm->fsp > 0) vm->fsp--;
    }
    DISPATCH();
}

    /* ==================================================================
     * FOR Loop — Float
     * ================================================================== */

op_for_init_f: {
    uint16_t raw_var   = READ_U16();
    uint16_t raw_lim   = READ_U16();
    uint16_t raw_step  = READ_U16();
    int16_t  exit_off  = READ_I16();

    int var_is_local = (raw_var & 0x8000) != 0;
    uint16_t var_slot = raw_var & 0x7FFF;
    int lim_is_local  = (raw_lim & 0x8000) != 0;
    uint16_t lim_slot = raw_lim & 0x7FFF;
    int step_is_local = (raw_step & 0x8000) != 0;
    uint16_t step_slot = raw_step & 0x7FFF;

    if (vm->fsp >= VM_MAX_FOR)
        bc_vm_error(vm, "FOR stack overflow");

    BCForEntry *fe = &vm->for_stack[vm->fsp];
    fe->var_slot  = raw_var;  /* preserve flag for NEXT */
    fe->lim_slot  = raw_lim;
    fe->step_slot = raw_step;
    fe->loop_top  = vm->pc;
    fe->is_local  = var_is_local;
    fe->var_type  = T_NBR;
    vm->fsp++;

    BCValue *var_ptr = var_is_local ? &vm->locals[vm->frame_base + var_slot] : &vm->globals[var_slot];
    MMFLOAT val  = var_ptr->f;
    MMFLOAT lim  = lim_is_local ? vm->locals[vm->frame_base + lim_slot].f : vm->globals[lim_slot].f;
    MMFLOAT step = step_is_local ? vm->locals[vm->frame_base + step_slot].f : vm->globals[step_slot].f;
    if (step > 0.0 && val > lim) {
        vm->fsp--;
        vm->pc += exit_off;
    } else if (step < 0.0 && val < lim) {
        vm->fsp--;
        vm->pc += exit_off;
    } else if (step == 0.0) {
        bc_vm_error(vm, "FOR step cannot be zero");
    }
    DISPATCH();
}

op_for_next_f: {
    uint16_t raw_var   = READ_U16();
    uint16_t raw_lim   = READ_U16();
    uint16_t raw_step  = READ_U16();
    int16_t  loop_off  = READ_I16();

    int var_is_local = (raw_var & 0x8000) != 0;
    uint16_t var_slot = raw_var & 0x7FFF;
    int lim_is_local  = (raw_lim & 0x8000) != 0;
    uint16_t lim_slot = raw_lim & 0x7FFF;
    int step_is_local = (raw_step & 0x8000) != 0;
    uint16_t step_slot = raw_step & 0x7FFF;
    BCValue *var_ptr = var_is_local ? &vm->locals[vm->frame_base + var_slot] : &vm->globals[var_slot];

    MMFLOAT step = step_is_local ? vm->locals[vm->frame_base + step_slot].f : vm->globals[step_slot].f;
    var_ptr->f += step;

    MMFLOAT val = var_ptr->f;
    MMFLOAT lim = lim_is_local ? vm->locals[vm->frame_base + lim_slot].f : vm->globals[lim_slot].f;

    int past;
    if (step > 0.0)
        past = (val > lim);
    else
        past = (val < lim);

    if (!past) {
        if (loop_off < 0) bc_vm_poll_interrupts();
        vm->pc += loop_off;
    } else {
        if (vm->fsp > 0) vm->fsp--;
    }
    DISPATCH();
}

    /* ==================================================================
     * SUB / FUNCTION calls
     * ================================================================== */

op_call_sub: {
    uint16_t idx   = READ_U16();
    uint8_t  nargs = *vm->pc++;

    if (idx >= vm->compiler->subfun_count)
        bc_vm_error(vm, "Invalid SUB index %d", idx);

    if (vm->csp >= VM_MAX_CALL)
        bc_vm_error(vm, "Call stack overflow");

    BCSubFun *sf = &vm->compiler->subfuns[idx];

    /* Push call frame */
    BCCallFrame *cf = &vm->call_stack[vm->csp];
    cf->return_pc  = vm->pc;
    cf->frame_base = vm->frame_base;
    cf->locals_top = vm->locals_top;
    cf->for_sp     = vm->fsp;
    cf->saved_sp   = vm->sp - nargs;  /* SP after popping args */
    cf->nlocals    = sf->nlocals;
    cf->subfun_idx = idx;
    vm->csp++;

    /* Set new frame base */
    int new_base = vm->locals_top;
    vm->frame_base = new_base;

    /* Pop arguments into local slots (they are pushed left-to-right,
       so the first arg is deepest on the stack) */
    for (int i = nargs - 1; i >= 0; i--) {
        int slot = new_base + i;
        if (slot >= VM_MAX_LOCALS)
            bc_vm_error(vm, "Local variable overflow");
        uint8_t arg_type = vm->stack_types[vm->sp];
        uint8_t param_type = (i < sf->nparams && i < BC_MAX_PARAMS) ? sf->param_types[i] : arg_type;
        if (param_type == 0) param_type = arg_type;
        if (i < sf->nparams && i < BC_MAX_PARAMS && sf->param_is_array[i]) {
            BCArray *src;
            if (!bc_stack_type_matches_array_param(arg_type, param_type))
                bc_vm_error(vm, "Array parameter type mismatch");
            src = bc_resolve_array_ref(vm, vm->stack[vm->sp], arg_type);
            vm->local_arrays[slot] = *src;
            vm->local_array_is_alias[slot] = 1;
            vm->local_types[slot] = param_type;
            vm->locals[slot].i = 0;
            vm->sp--;
            continue;
        }
        vm->locals[slot] = bc_vm_coerce_arg_value(vm->stack[vm->sp], arg_type, param_type);
        vm->local_types[slot] = param_type;
        vm->local_array_is_alias[slot] = 0;
        memset(&vm->local_arrays[slot], 0, sizeof(BCArray));
        /* Deep-copy strings so they survive temp buffer rotation */
        if (param_type == T_STR && vm->stack[vm->sp].s) {
            uint8_t *copy = BC_ALLOC(STRINGSIZE);
            if (!copy) bc_vm_error(vm, "Out of memory for string");
            Mstrcpy(copy, vm->stack[vm->sp].s);
            vm->locals[slot].s = copy;
        }
        vm->sp--;
    }

    /* Update locals_top so ENTER_FRAME won't zero the args */
    vm->locals_top = new_base + nargs;

    /* Jump to entry point */
    vm->pc = vm->bytecode + sf->entry_addr;
    DISPATCH();
}

op_call_fun: {
    uint16_t idx   = READ_U16();
    uint8_t  nargs = *vm->pc++;

    if (idx >= vm->compiler->subfun_count)
        bc_vm_error(vm, "Invalid FUNCTION index %d", idx);

    if (vm->csp >= VM_MAX_CALL)
        bc_vm_error(vm, "Call stack overflow");

    BCSubFun *sf = &vm->compiler->subfuns[idx];

    /* Push call frame */
    BCCallFrame *cf = &vm->call_stack[vm->csp];
    cf->return_pc  = vm->pc;
    cf->frame_base = vm->frame_base;
    cf->locals_top = vm->locals_top;
    cf->for_sp     = vm->fsp;
    cf->saved_sp   = vm->sp - nargs;  /* Where SP should be after popping args (return val goes here+1) */
    cf->nlocals    = sf->nlocals;
    cf->subfun_idx = idx;
    vm->csp++;

    int new_base = vm->locals_top;
    vm->frame_base = new_base;

    /* For FUNCTIONs, slot 0 is the return value — args go to slots 1..nargs */
    for (int i = nargs - 1; i >= 0; i--) {
        int slot = new_base + 1 + i;
        if (slot >= VM_MAX_LOCALS)
            bc_vm_error(vm, "Local variable overflow");
        uint8_t arg_type = vm->stack_types[vm->sp];
        uint8_t param_type = (i < sf->nparams && i < BC_MAX_PARAMS) ? sf->param_types[i] : arg_type;
        if (param_type == 0) param_type = arg_type;
        if (i < sf->nparams && i < BC_MAX_PARAMS && sf->param_is_array[i]) {
            BCArray *src;
            if (!bc_stack_type_matches_array_param(arg_type, param_type))
                bc_vm_error(vm, "Array parameter type mismatch");
            src = bc_resolve_array_ref(vm, vm->stack[vm->sp], arg_type);
            vm->local_arrays[slot] = *src;
            vm->local_array_is_alias[slot] = 1;
            vm->local_types[slot] = param_type;
            vm->locals[slot].i = 0;
            vm->sp--;
            continue;
        }
        vm->locals[slot] = bc_vm_coerce_arg_value(vm->stack[vm->sp], arg_type, param_type);
        vm->local_types[slot] = param_type;
        vm->local_array_is_alias[slot] = 0;
        memset(&vm->local_arrays[slot], 0, sizeof(BCArray));
        /* Deep-copy strings so they survive temp buffer rotation */
        if (param_type == T_STR && vm->stack[vm->sp].s) {
            uint8_t *copy = BC_ALLOC(STRINGSIZE);
            if (!copy) bc_vm_error(vm, "Out of memory for string");
            Mstrcpy(copy, vm->stack[vm->sp].s);
            vm->locals[slot].s = copy;
        }
        vm->sp--;
    }

    /* Zero out slot 0 (return value) */
    vm->locals[new_base].i = 0;
    vm->local_types[new_base] = sf->return_type;
    vm->local_array_is_alias[new_base] = 0;
    memset(&vm->local_arrays[new_base], 0, sizeof(BCArray));

    /* Update locals_top so ENTER_FRAME won't zero the args */
    vm->locals_top = new_base + 1 + nargs;

    vm->pc = vm->bytecode + sf->entry_addr;
    DISPATCH();
}

op_enter_frame: {
    uint16_t nlocals = READ_U16();
    /* Reserve nlocals slots in the locals array.  The first N may already
       be populated by arguments (from CALL_SUB/CALL_FUN).  The rest are
       zeroed for LOCAL variables. */
    if (vm->locals_top + nlocals > VM_MAX_LOCALS)
        bc_vm_error(vm, "Local variable overflow in ENTER_FRAME");
    /* Zero out local slots beyond what args may have set */
    for (int i = 0; i < nlocals; i++) {
        int idx = vm->frame_base + i;
        /* Only zero if this slot wasn't set by args.  However, since we
           always write args before ENTER_FRAME runs, we can safely
           just zero-init everything beyond the arg range.  ENTER_FRAME
           should reflect total locals including params. The args were
           already placed. */
        if (idx >= vm->locals_top) {
            vm->locals[idx].i = 0;
            vm->local_types[idx] = T_INT;
            vm->local_array_is_alias[idx] = 0;
            memset(&vm->local_arrays[idx], 0, sizeof(BCArray));
        }
    }
    vm->locals_top = vm->frame_base + nlocals;
    DISPATCH();
}

op_leave_frame: {
    if (vm->csp > 0) {
        BCCallFrame *cf = &vm->call_stack[vm->csp - 1];
        vm->locals_top = vm->frame_base;
        /* local_arrays cleanup: zero out local array data pointers */
        for (int i = vm->frame_base; i < vm->frame_base + (int)cf->nlocals; i++) {
            if (i < VM_MAX_LOCALS && vm->local_arrays[i].data) {
                if (!vm->local_array_is_alias[i])
                    bc_array_release(&vm->local_arrays[i]);
            }
            if (i < VM_MAX_LOCALS) {
                if (vm->local_types[i] == T_STR && vm->locals[i].s) {
                    uint8_t *local_buf = vm->locals[i].s;
                    if (!vm->local_array_is_alias[i]) {
                        /* If the stack still references this local buffer,
                         * copy the string into the VM's str_temp ring and
                         * redirect every stack entry that pointed at the
                         * local buffer. Then free it. Without the
                         * redirect+free, every FUNCTION call that returns
                         * a string leaks its STRINGSIZE local slot — the
                         * return value lives on the stack, caller consumes
                         * it via op_print_str / Mstrcpy, stack reference
                         * goes away, and nothing owns the buffer anymore
                         * (rnd_chr$() in a render loop blew the BASIC
                         * heap in a few hundred calls). */
                        uint8_t *temp = NULL;
                        for (int s = 0; s <= vm->sp; s++) {
                            if (vm->stack_types[s] == T_STR && vm->stack[s].s == local_buf) {
                                if (!temp) {
                                    temp = vm_get_str_temp(vm);
                                    Mstrcpy(temp, local_buf);
                                }
                                vm->stack[s].s = temp;
                            }
                        }
                        BC_FREE(local_buf);
                    }
                    vm->locals[i].s = NULL;
                }
                vm->local_types[i] = 0;
                memset(&vm->local_arrays[i], 0, sizeof(BCArray));
                vm->local_array_is_alias[i] = 0;
            }
        }
    }
    DISPATCH();
}

op_ret_sub: {
    if (vm->csp <= 0)
        bc_vm_error(vm, "RETURN SUB without matching CALL");
    vm->csp--;
    BCCallFrame *cf = &vm->call_stack[vm->csp];
    vm->frame_base = cf->frame_base;
    vm->locals_top = cf->locals_top;
    vm->fsp = cf->for_sp;
    vm->sp = cf->saved_sp;
    vm->pc = cf->return_pc;
    DISPATCH();
}

op_ret_fun: {
    if (vm->csp <= 0)
        bc_vm_error(vm, "RETURN FUN without matching CALL");
    /* Return value is on TOS — save it. op_leave_frame has already
     * redirected any stack slots that pointed at local string buffers
     * into str_temp, so ret_val.s (if T_STR) is stable here. */
    BCValue ret_val = vm->stack[vm->sp];
    uint8_t ret_type = vm->stack_types[vm->sp];

    vm->csp--;
    BCCallFrame *cf = &vm->call_stack[vm->csp];
    vm->frame_base = cf->frame_base;
    vm->locals_top = cf->locals_top;
    vm->fsp = cf->for_sp;
    vm->sp = cf->saved_sp;
    vm->pc = cf->return_pc;

    /* Push return value onto the caller's stack */
    vm->sp++;
    vm->stack[vm->sp] = ret_val;
    vm->stack_types[vm->sp] = ret_type;
    DISPATCH();
}

    /* ==================================================================
     * Local variable access
     * ================================================================== */

op_load_local_i: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    PUSH_I(vm->locals[idx].i);
    DISPATCH();
}

op_load_local_f: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    PUSH_F(vm->locals[idx].f);
    DISPATCH();
}

op_load_local_s: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    uint8_t *s = vm->locals[idx].s;
    PUSH_S(s ? s : vm_empty_string);
    DISPATCH();
}

op_store_local_i: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    if (vm->stack_types[vm->sp] == T_NBR)
        vm->locals[idx].i = (int64_t)POP_F();
    else
        vm->locals[idx].i = POP_I();
    vm->local_types[idx] = T_INT;
    DISPATCH();
}

op_store_local_f: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    if (vm->stack_types[vm->sp] == T_INT)
        vm->locals[idx].f = (MMFLOAT)POP_I();
    else
        vm->locals[idx].f = POP_F();
    vm->local_types[idx] = T_NBR;
    DISPATCH();
}

op_store_local_s: {
    uint16_t offset = READ_U16();
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local index out of range");
    uint8_t *src = POP_S();
    if (!vm->locals[idx].s) {
        vm->locals[idx].s = BC_ALLOC(STRINGSIZE);
        if (!vm->locals[idx].s) bc_vm_error(vm, "Out of memory for string");
    }
    Mstrcpy(vm->locals[idx].s, src);
    vm->local_types[idx] = T_STR;
    DISPATCH();
}

    /* ==================================================================
     * Local array access
     * ================================================================== */

op_load_local_arr_i: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_LARR_I, idx);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_I(arr->data[off].i);
    DISPATCH();
}

op_load_local_arr_f: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_LARR_F, idx);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_F(arr->data[off].f);
    DISPATCH();
}

op_load_local_arr_s: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    if (ndim == 0) {
        bc_push_array_ref(vm, BC_STK_LARR_S, idx);
        DISPATCH();
    }
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    PUSH_S(arr->data[off].s);
    DISPATCH();
}

op_store_local_arr_i: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    int64_t val = POP_NUMERIC_I();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    arr->data[off].i = val;
    DISPATCH();
}

op_store_local_arr_f: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    MMFLOAT val = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    arr->data[off].f = val;
    DISPATCH();
}

op_store_local_arr_s: {
    uint16_t offset = READ_U16();
    uint8_t ndim = *vm->pc++;
    int idx = vm->frame_base + offset;
    if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local array index out of range");
    BCArray *arr = &vm->local_arrays[idx];
    if (!arr->data) bc_vm_error(vm, "Local array not dimensioned");
    uint8_t *val = POP_S();
    int64_t indices[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        indices[d] = POP_NUMERIC_I();
    uint32_t off = calc_array_offset(vm, arr, indices, ndim);
    if (!arr->data[off].s) {
        arr->data[off].s = BC_ALLOC(STRINGSIZE);
        if (!arr->data[off].s) bc_vm_error(vm, "Out of memory for string array");
    }
    Mstrcpy(arr->data[off].s, val);
    DISPATCH();
}

    /* ==================================================================
     * PRINT
     *
     * MMBasic PRINT behaviour:
     *   - Positive numbers get a leading space (in place of the + sign)
     *   - Numbers get a trailing space
     *   - PRINT_SEMICOLON suppresses the trailing newline / separator
     * ================================================================== */

op_print_int: {
    uint8_t flags = *vm->pc++;
    (void)flags;
    int64_t val = POP_NUMERIC_I();
    char buf[64];
    IntToStr(buf, val, 10);
    /* MMBasic: positive numbers get leading space, no trailing space */
    char out[80];
    int pos = 0;
    if (val >= 0) out[pos++] = ' ';
    int blen = (int)strlen(buf);
    memcpy(out + pos, buf, blen);
    pos += blen;
    out[pos] = '\0';
    vm_output(vm, out);
    DISPATCH();
}

op_print_flt: {
    uint8_t flags = *vm->pc++;
    (void)flags;
    MMFLOAT val = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    char buf[64];
    FloatToStr(buf, val, 0, STR_AUTO_PRECISION, ' ');
    /* MMBasic: positive floats get leading space, no trailing space */
    char out[80];
    int pos = 0;
    if (val >= 0.0) out[pos++] = ' ';
    int blen = (int)strlen(buf);
    /* FloatToStr may already include leading space — skip it */
    int start = 0;
    while (start < blen && buf[start] == ' ') start++;
    memcpy(out + pos, buf + start, blen - start);
    pos += blen - start;
    out[pos] = '\0';
    vm_output(vm, out);
    DISPATCH();
}

op_print_str: {
    uint8_t flags = *vm->pc++;
    uint8_t *val = POP_S();
    (void)flags;
    vm_output_mstr(vm, val);
    DISPATCH();
}

op_print_newline:
    vm_output(vm, "\r\n");
    DISPATCH();

op_print_tab:
    vm_output(vm, "\t");
    DISPATCH();

    /* ==================================================================
     * DIM arrays
     * ================================================================== */

op_dim_arr_i: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (arr->data) bc_vm_error(vm, "Array already dimensioned");
    arr->ndims = ndim;
    arr->elem_type = T_INT;
    uint32_t total = 1;
    int64_t dims[MAXDIM];
    /* Pop dimension sizes (last dim first) */
    for (int d = ndim - 1; d >= 0; d--)
        dims[d] = POP_NUMERIC_I();
    for (int d = 0; d < ndim; d++) {
        if (dims[d] < 0) bc_vm_error(vm, "Invalid array dimension");
        arr->dims[d] = (int)dims[d];
        total *= (uint32_t)(dims[d] + 1);  /* 0..N inclusive */
    }
    arr->total_elements = total;
    arr->data = (BCValue *)BC_ALLOC(total * sizeof(BCValue));
    if (!arr->data) bc_vm_error(vm, "Out of memory for array");
    memset(arr->data, 0, total * sizeof(BCValue));
    DISPATCH();
}

op_dim_arr_f: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (arr->data) bc_vm_error(vm, "Array already dimensioned");
    arr->ndims = ndim;
    arr->elem_type = T_NBR;
    uint32_t total = 1;
    int64_t dims[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        dims[d] = POP_NUMERIC_I();
    for (int d = 0; d < ndim; d++) {
        if (dims[d] < 0) bc_vm_error(vm, "Invalid array dimension");
        arr->dims[d] = (int)dims[d];
        total *= (uint32_t)(dims[d] + 1);
    }
    arr->total_elements = total;
    arr->data = (BCValue *)BC_ALLOC(total * sizeof(BCValue));
    if (!arr->data) bc_vm_error(vm, "Out of memory for array");
    memset(arr->data, 0, total * sizeof(BCValue));
    DISPATCH();
}

op_dim_arr_s: {
    uint16_t slot = READ_U16();
    uint8_t ndim = *vm->pc++;
    BCArray *arr = &vm->arrays[slot];
    if (arr->data) bc_vm_error(vm, "Array already dimensioned");
    arr->ndims = ndim;
    arr->elem_type = T_STR;
    uint32_t total = 1;
    int64_t dims[MAXDIM];
    for (int d = ndim - 1; d >= 0; d--)
        dims[d] = POP_NUMERIC_I();
    for (int d = 0; d < ndim; d++) {
        if (dims[d] < 0) bc_vm_error(vm, "Invalid array dimension");
        arr->dims[d] = (int)dims[d];
        total *= (uint32_t)(dims[d] + 1);
    }
    arr->total_elements = total;
    arr->data = (BCValue *)BC_ALLOC(total * sizeof(BCValue));
    if (!arr->data) bc_vm_error(vm, "Out of memory for array");
    /* Allocate string buffers for each element */
    for (uint32_t i = 0; i < total; i++) {
        arr->data[i].s = BC_ALLOC(STRINGSIZE);
        if (!arr->data[i].s) bc_vm_error(vm, "Out of memory for string array");
        arr->data[i].s[0] = 0;  /* empty string (length prefix = 0) */
    }
    DISPATCH();
}

    /* ==================================================================
     * TYPE / STRUCT field access
     *
     * Compile-time resolved: each opcode carries the VM slot and a byte offset
     * into g_vartbl[vi].val.s.  sync_mmbasic_to_vm (after the DIM bridge)
     * caches the vartbl pointer into vm->arrays[slot].data so we don't call
     * findvar on every access.  If data is NULL the struct wasn't dimensioned
     * (or the bridge hasn't run yet) — that's a programmer error, not a silent
     * miss, so we bail loudly.
     * ================================================================== */

op_load_struct_field_i: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    int64_t v;
    memcpy(&v, base + offset, sizeof(v));
    PUSH_I(v);
    DISPATCH();
}

op_load_struct_field_f: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    MMFLOAT v;
    memcpy(&v, base + offset, sizeof(v));
    PUSH_F(v);
    DISPATCH();
}

op_load_struct_field_s: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    PUSH_S(base + offset);
    DISPATCH();
}

op_store_struct_field_i: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    int64_t v = (vm->stack_types[vm->sp] == T_NBR) ? (int64_t)POP_F() : POP_I();
    memcpy(base + offset, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_field_f: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    MMFLOAT v = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    memcpy(base + offset, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_field_s: {
    uint16_t slot   = READ_U16();
    uint16_t offset = READ_U16();
    uint16_t size   = READ_U16();   /* max string length declared for this member */
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    uint8_t *src = POP_S();
    if (src[0] > size) bc_vm_error(vm, "String too long");
    Mstrcpy(base + offset, src);
    DISPATCH();
}

    /* ==================================================================
     * TYPE / STRUCT — array element field access
     *
     * Pops ndim indices off the stack, uses vm->arrays[slot].dims[] to
     * compute a linear offset (same convention as MMBasic: OPTION BASE is
     * applied by the interpreter when dims[] are copied from g_vartbl, so
     * incoming indices are already base-adjusted — we just multiply).
     * Then reads/writes at base + linear * elem_size + offset.
     *
     * ELEM_LINEAR is a macro (not a helper fn) because POP_I is only in
     * scope inside bc_vm_execute.  It leaves the linear element index in
     * _linear; caller multiplies by elem_size.
     * ================================================================== */

#define ELEM_LINEAR(_slot, _ndim, _linear)                                    \
    int64_t _linear##_idx[MAXDIM];                                            \
    for (int _i = (_ndim) - 1; _i >= 0; _i--) _linear##_idx[_i] = POP_I();    \
    int64_t _linear = _linear##_idx[0] - g_OptionBase;                        \
    {                                                                         \
        int64_t _j = 1;                                                       \
        for (int _i = 1; _i < (_ndim); _i++) {                                \
            _j *= (int64_t)(vm->arrays[_slot].dims[_i - 1] + 1 - g_OptionBase); \
            _linear += (_linear##_idx[_i] - g_OptionBase) * _j;               \
        }                                                                     \
    }

op_load_struct_elem_i: {
    uint16_t slot      = READ_U16();
    uint16_t offset    = READ_U16();
    uint16_t elem_size = READ_U16();
    uint8_t  ndim      = READ_U8();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    ELEM_LINEAR(slot, ndim, nbr);
    int64_t v;
    memcpy(&v, base + nbr * elem_size + offset, sizeof(v));
    PUSH_I(v);
    DISPATCH();
}

op_load_struct_elem_f: {
    uint16_t slot      = READ_U16();
    uint16_t offset    = READ_U16();
    uint16_t elem_size = READ_U16();
    uint8_t  ndim      = READ_U8();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    ELEM_LINEAR(slot, ndim, nbr);
    MMFLOAT v;
    memcpy(&v, base + nbr * elem_size + offset, sizeof(v));
    PUSH_F(v);
    DISPATCH();
}

op_load_struct_elem_s: {
    uint16_t slot      = READ_U16();
    uint16_t offset    = READ_U16();
    uint16_t elem_size = READ_U16();
    uint8_t  ndim      = READ_U8();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    ELEM_LINEAR(slot, ndim, nbr);
    PUSH_S(base + nbr * elem_size + offset);
    DISPATCH();
}

op_store_struct_elem_i: {
    uint16_t slot      = READ_U16();
    uint16_t offset    = READ_U16();
    uint16_t elem_size = READ_U16();
    uint8_t  ndim      = READ_U8();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    int64_t v = (vm->stack_types[vm->sp] == T_NBR) ? (int64_t)POP_F() : POP_I();
    ELEM_LINEAR(slot, ndim, nbr);
    memcpy(base + nbr * elem_size + offset, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_elem_f: {
    uint16_t slot      = READ_U16();
    uint16_t offset    = READ_U16();
    uint16_t elem_size = READ_U16();
    uint8_t  ndim      = READ_U8();
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    MMFLOAT v = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    ELEM_LINEAR(slot, ndim, nbr);
    memcpy(base + nbr * elem_size + offset, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_elem_s: {
    uint16_t slot       = READ_U16();
    uint16_t offset     = READ_U16();
    uint16_t elem_size  = READ_U16();
    uint8_t  ndim       = READ_U8();
    uint16_t maxstrlen  = READ_U16();   /* declared LENGTH of the member */
    uint8_t *base = (uint8_t *)vm->arrays[slot].data;
    if (!base) bc_vm_error(vm, "Struct variable not dimensioned");
    uint8_t *src = POP_S();
    ELEM_LINEAR(slot, ndim, nbr);
    if (src[0] > maxstrlen) bc_vm_error(vm, "String too long");
    Mstrcpy(base + nbr * elem_size + offset, src);
    DISPATCH();
}

    /* ==================================================================
     * Phase 4 — nested struct access with intermediate array indices.
     * See bytecode.h for the encoding.  The NESTED_RESOLVE macro reads
     * the opcode operands, pops nested indices (innermost first) then
     * outer indices, and computes the final base pointer.
     * ================================================================== */
#define NESTED_MAX_SEG 8
#define NESTED_RESOLVE(_base)                                                 \
    uint16_t _ns_slot = READ_U16();                                           \
    uint8_t  _ns_outer_ndim = READ_U8();                                      \
    uint8_t  _ns_nseg = READ_U8();                                            \
    uint16_t _ns_outer_stride = READ_U16();                                   \
    uint16_t _ns_seg_off[NESTED_MAX_SEG];                                     \
    uint16_t _ns_seg_str[NESTED_MAX_SEG];                                     \
    for (int _ns_i = 0; _ns_i < _ns_nseg; _ns_i++) {                          \
        _ns_seg_off[_ns_i] = READ_U16();                                      \
        _ns_seg_str[_ns_i] = READ_U16();                                      \
    }                                                                         \
    uint16_t _ns_final = READ_U16();                                          \
    int64_t _ns_idx[NESTED_MAX_SEG];                                          \
    for (int _ns_i = _ns_nseg - 1; _ns_i >= 0; _ns_i--) _ns_idx[_ns_i] = POP_I(); \
    uint8_t *_base = (uint8_t *)vm->arrays[_ns_slot].data;                    \
    if (!_base) bc_vm_error(vm, "Struct variable not dimensioned");           \
    if (_ns_outer_ndim > 0) {                                                 \
        int64_t _ns_outer_idx[MAXDIM];                                        \
        for (int _ns_i = _ns_outer_ndim - 1; _ns_i >= 0; _ns_i--)             \
            _ns_outer_idx[_ns_i] = POP_I();                                   \
        int64_t _ns_linear = _ns_outer_idx[0] - g_OptionBase;                 \
        int64_t _ns_mult = 1;                                                 \
        for (int _ns_i = 1; _ns_i < _ns_outer_ndim; _ns_i++) {                \
            _ns_mult *= (int64_t)(vm->arrays[_ns_slot].dims[_ns_i - 1] + 1 - g_OptionBase); \
            _ns_linear += (_ns_outer_idx[_ns_i] - g_OptionBase) * _ns_mult;   \
        }                                                                     \
        _base += _ns_linear * _ns_outer_stride;                               \
    }                                                                         \
    for (int _ns_i = 0; _ns_i < _ns_nseg; _ns_i++) {                          \
        _base += _ns_seg_off[_ns_i] + (_ns_idx[_ns_i] - g_OptionBase) * _ns_seg_str[_ns_i]; \
    }                                                                         \
    _base += _ns_final

op_load_struct_nested_i: {
    NESTED_RESOLVE(base);
    int64_t v;
    memcpy(&v, base, sizeof(v));
    PUSH_I(v);
    DISPATCH();
}

op_load_struct_nested_f: {
    NESTED_RESOLVE(base);
    MMFLOAT v;
    memcpy(&v, base, sizeof(v));
    PUSH_F(v);
    DISPATCH();
}

op_load_struct_nested_s: {
    NESTED_RESOLVE(base);
    PUSH_S(base);
    DISPATCH();
}

op_store_struct_nested_i: {
    int64_t v = (vm->stack_types[vm->sp] == T_NBR) ? (int64_t)POP_F() : POP_I();
    NESTED_RESOLVE(base);
    memcpy(base, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_nested_f: {
    MMFLOAT v = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
    NESTED_RESOLVE(base);
    memcpy(base, &v, sizeof(v));
    DISPATCH();
}

op_store_struct_nested_s: {
    uint8_t *src = POP_S();
    NESTED_RESOLVE(base);
    uint16_t maxstrlen = READ_U16();
    if (src[0] > maxstrlen) bc_vm_error(vm, "String too long");
    Mstrcpy(base, src);
    DISPATCH();
}
#undef NESTED_RESOLVE
#undef NESTED_MAX_SEG
#undef ELEM_LINEAR

    /* ==================================================================
     * Native String Functions
     * ================================================================== */

/* Helper: get a rotating temp string buffer */
#define STR_TEMP() vm_get_str_temp(vm)

op_str_len: {
    /* LEN(str$) -> int */
    uint8_t *s = POP_S();
    PUSH_I((int64_t)(s ? s[0] : 0));
    DISPATCH();
}

op_str_left: {
    /* LEFT$(str$, n) -> str$ */
    int64_t n = POP_I();
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    if (n < 0) n = 0;
    if (n > slen) n = slen;
    temp[0] = (uint8_t)n;
    if (n > 0) memcpy(&temp[1], &s[1], n);
    PUSH_S(temp);
    DISPATCH();
}

op_str_right: {
    /* RIGHT$(str$, n) -> str$ */
    int64_t n = POP_I();
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    if (n < 0) n = 0;
    if (n > slen) n = slen;
    temp[0] = (uint8_t)n;
    if (n > 0) memcpy(&temp[1], &s[1 + slen - (int)n], n);
    PUSH_S(temp);
    DISPATCH();
}

op_str_mid2: {
    /* MID$(str$, start) -> str$ (from start to end) */
    int64_t start = POP_I();
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    if (start < 1) start = 1;
    if (start > slen) { temp[0] = 0; PUSH_S(temp); DISPATCH(); }
    int n = slen - (int)start + 1;
    temp[0] = (uint8_t)n;
    memcpy(&temp[1], &s[(int)start], n);
    PUSH_S(temp);
    DISPATCH();
}

op_str_mid3: {
    /* MID$(str$, start, len) -> str$ */
    int64_t len = POP_I();
    int64_t start = POP_I();
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    if (start < 1) start = 1;
    if (start > slen || len <= 0) { temp[0] = 0; PUSH_S(temp); DISPATCH(); }
    int avail = slen - (int)start + 1;
    if (len > avail) len = avail;
    temp[0] = (uint8_t)len;
    memcpy(&temp[1], &s[(int)start], (int)len);
    PUSH_S(temp);
    DISPATCH();
}

op_str_ucase: {
    /* UCASE$(str$) -> str$ */
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    temp[0] = (uint8_t)slen;
    for (int i = 0; i < slen; i++)
        temp[1 + i] = toupper(s[1 + i]);
    PUSH_S(temp);
    DISPATCH();
}

op_str_lcase: {
    /* LCASE$(str$) -> str$ */
    uint8_t *s = POP_S();
    uint8_t *temp = STR_TEMP();
    int slen = s ? s[0] : 0;
    temp[0] = (uint8_t)slen;
    for (int i = 0; i < slen; i++)
        temp[1 + i] = tolower(s[1 + i]);
    PUSH_S(temp);
    DISPATCH();
}

    /* ==================================================================
     * Additional Native String Functions
     * ================================================================== */

op_str_val: {
    /* VAL(str$) -> float */
    uint8_t *s = POP_S();
    if (!s || s[0] == 0) { PUSH_F(0.0); DISPATCH(); }
    char tmp[STRINGSIZE + 1];
    int slen = s[0];
    memcpy(tmp, s + 1, slen);
    tmp[slen] = '\0';
    MMFLOAT v = (MMFLOAT)strtod(tmp, NULL);
    PUSH_F(v);
    DISPATCH();
}

op_str_str: {
    /* STR$(n) -> str$ */
    /* The argument could be int or float — check stack type */
    uint8_t *temp = STR_TEMP();
    if (vm->stack_types[vm->sp] == T_INT) {
        int64_t v = POP_I();
        char buf[64];
        IntToStr(buf, v, 10);
        int blen = (int)strlen(buf);
        temp[0] = (uint8_t)blen;
        memcpy(temp + 1, buf, blen);
    } else {
        MMFLOAT v = POP_F();
        char buf[64];
        FloatToStr(buf, v, 0, STR_AUTO_PRECISION, ' ');
        /* Strip leading spaces */
        char *bp = buf;
        while (*bp == ' ') bp++;
        int blen = (int)strlen(bp);
        temp[0] = (uint8_t)blen;
        memcpy(temp + 1, bp, blen);
    }
    PUSH_S(temp);
    DISPATCH();
}

op_str_chr: {
    /* CHR$(n%) -> str$ */
    int64_t n = POP_I();
    uint8_t *temp = STR_TEMP();
    temp[0] = 1;
    temp[1] = (uint8_t)(n & 0xFF);
    PUSH_S(temp);
    DISPATCH();
}

op_str_asc: {
    /* ASC(str$) -> int */
    uint8_t *s = POP_S();
    if (!s || s[0] == 0) { PUSH_I(0); DISPATCH(); }
    PUSH_I((int64_t)s[1]);
    DISPATCH();
}

op_str_instr: {
    /* INSTR([start%,] haystack$, needle$) -> int (1-based, 0 if not found)
     * Followed by 1-byte arg count (2 or 3). */
    uint8_t nargs = *vm->pc++;
    uint8_t *needle = POP_S();
    uint8_t *haystack = POP_S();
    int start = 0;
    if (nargs >= 3) {
        int64_t s = POP_I();
        start = (s < 1) ? 0 : (int)(s - 1);
    }
    if (!haystack || !needle || haystack[0] == 0 || needle[0] == 0) {
        PUSH_I(0);
        DISPATCH();
    }
    int hlen = haystack[0], nlen = needle[0];
    int found = 0;
    for (int i = start; i <= hlen - nlen; i++) {
        if (memcmp(&haystack[1 + i], &needle[1], nlen) == 0) {
            found = i + 1;  /* 1-based */
            break;
        }
    }
    PUSH_I((int64_t)found);
    DISPATCH();
}

op_str_hex: {
    /* HEX$(n%) -> str$ */
    int64_t n = POP_I();
    uint8_t *temp = STR_TEMP();
    char buf[32];
    if (n < 0) {
        /* For negative numbers, print full 64-bit hex */
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)(uint64_t)n);
    } else {
        snprintf(buf, sizeof(buf), "%llX", (unsigned long long)n);
    }
    int blen = (int)strlen(buf);
    temp[0] = (uint8_t)blen;
    memcpy(temp + 1, buf, blen);
    PUSH_S(temp);
    DISPATCH();
}

op_str_oct: {
    /* OCT$(n%) -> str$ */
    int64_t n = POP_I();
    uint8_t *temp = STR_TEMP();
    char buf[32];
    snprintf(buf, sizeof(buf), "%llo", (unsigned long long)(uint64_t)n);
    int blen = (int)strlen(buf);
    temp[0] = (uint8_t)blen;
    memcpy(temp + 1, buf, blen);
    PUSH_S(temp);
    DISPATCH();
}

op_str_bin: {
    /* BIN$(n%) -> str$ */
    int64_t n = POP_I();
    uint8_t *temp = STR_TEMP();
    uint64_t v = (uint64_t)n;
    if (v == 0) {
        temp[0] = 1; temp[1] = '0';
    } else {
        char buf[65];
        int pos = 0;
        /* Find highest bit */
        int bits = 64;
        while (bits > 0 && !((v >> (bits - 1)) & 1)) bits--;
        for (int i = bits - 1; i >= 0; i--)
            buf[pos++] = ((v >> i) & 1) ? '1' : '0';
        temp[0] = (uint8_t)pos;
        memcpy(temp + 1, buf, pos);
    }
    PUSH_S(temp);
    DISPATCH();
}

    /* ==================================================================
     * Native Math Functions
     * ================================================================== */

op_math_sin: {
    MMFLOAT v = POP_F();
    PUSH_F(sin(v));
    DISPATCH();
}

op_math_cos: {
    MMFLOAT v = POP_F();
    PUSH_F(cos(v));
    DISPATCH();
}

op_math_tan: {
    MMFLOAT v = POP_F();
    PUSH_F(tan(v));
    DISPATCH();
}

op_math_atn: {
    MMFLOAT v = POP_F();
    PUSH_F(atan(v));
    DISPATCH();
}

op_math_asin: {
    MMFLOAT v = POP_F();
    PUSH_F(asin(v));
    DISPATCH();
}

op_math_acos: {
    MMFLOAT v = POP_F();
    PUSH_F(acos(v));
    DISPATCH();
}

op_math_atan2: {
    MMFLOAT x = POP_F();
    MMFLOAT y = POP_F();
    PUSH_F(atan2(y, x));
    DISPATCH();
}

op_math_sqr: {
    MMFLOAT v = POP_F();
    if (v < 0.0) bc_vm_error(vm, "SQR of negative number");
    PUSH_F(sqrt(v));
    DISPATCH();
}

op_math_log: {
    MMFLOAT v = POP_F();
    if (v <= 0.0) bc_vm_error(vm, "LOG of non-positive number");
    PUSH_F(log(v));
    DISPATCH();
}

op_math_exp: {
    MMFLOAT v = POP_F();
    PUSH_F(exp(v));
    DISPATCH();
}

op_math_abs: {
    /* Preserves type: if TOS is int, result is int; if float, result is float */
    if (vm->stack_types[vm->sp] == T_INT) {
        int64_t v = vm->stack[vm->sp].i;
        vm->stack[vm->sp].i = (v < 0) ? -v : v;
    } else {
        MMFLOAT v = vm->stack[vm->sp].f;
        vm->stack[vm->sp].f = fabs(v);
    }
    DISPATCH();
}

op_math_sgn: {
    /* SGN returns -1, 0, or 1 as integer */
    if (vm->stack_types[vm->sp] == T_INT) {
        int64_t v = vm->stack[vm->sp].i;
        vm->stack[vm->sp].i = (v > 0) ? 1 : (v < 0) ? -1 : 0;
    } else {
        MMFLOAT v = vm->stack[vm->sp].f;
        vm->stack[vm->sp].i = (v > 0.0) ? 1 : (v < 0.0) ? -1 : 0;
        vm->stack_types[vm->sp] = T_INT;
    }
    DISPATCH();
}

op_math_int: {
    /* INT(x) = floor(x), returns float */
    MMFLOAT v = POP_F();
    PUSH_F(floor(v));
    DISPATCH();
}

op_math_fix: {
    /* FIX(x) = truncate toward zero, returns int */
    MMFLOAT v = POP_F();
    PUSH_I((int64_t)v);
    DISPATCH();
}

op_math_cint: {
    /* CINT(x) = round to nearest integer */
    MMFLOAT v = POP_F();
    PUSH_I((int64_t)(v + (v >= 0.0 ? 0.5 : -0.5)));
    DISPATCH();
}

op_math_rad: {
    /* RAD(degrees) -> radians */
    MMFLOAT v = POP_F();
    PUSH_F(v * M_PI / 180.0);
    DISPATCH();
}

op_math_deg: {
    /* DEG(radians) -> degrees */
    MMFLOAT v = POP_F();
    PUSH_F(v * 180.0 / M_PI);
    DISPATCH();
}

op_math_pi: {
    /* PI — no argument, push pi */
    PUSH_F(M_PI);
    DISPATCH();
}

op_math_max: {
    /* MAX(a, b) -> float */
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_F(a > b ? a : b);
    DISPATCH();
}

op_math_min: {
    /* MIN(a, b) -> float */
    MMFLOAT b = POP_F();
    MMFLOAT a = POP_F();
    PUSH_F(a < b ? a : b);
    DISPATCH();
}

op_math_mulshr: {
    int bits = (int)POP_NUMERIC_I();
    int64_t b = POP_NUMERIC_I();
    int64_t a = POP_NUMERIC_I();
    PUSH_I(bc_vm_mulshr_int(a, b, bits));
    DISPATCH();
}

op_math_sqrshr: {
    int bits = (int)POP_NUMERIC_I();
    int64_t a = POP_NUMERIC_I();
    PUSH_I(bc_vm_mulshr_int(a, a, bits));
    DISPATCH();
}

op_math_mulshradd: {
    int64_t c = POP_NUMERIC_I();
    int bits = (int)POP_NUMERIC_I();
    int64_t b = POP_NUMERIC_I();
    int64_t a = POP_NUMERIC_I();
    PUSH_I(bc_vm_mulshr_int(a, b, bits) + c);
    DISPATCH();
}

op_inc_i: {
    uint16_t raw_slot = READ_U16();
    int is_local = (raw_slot & 0x8000u) != 0;
    uint16_t slot = raw_slot & 0x7FFFu;
    int64_t delta = POP_NUMERIC_I();
    BCValue *dst;

    if (is_local) {
        int idx = vm->frame_base + slot;
        if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local variable overflow");
        dst = &vm->locals[idx];
    } else {
        dst = &vm->globals[slot];
    }

    dst->i += delta;
    DISPATCH();
}

op_inc_f: {
    uint16_t raw_slot = READ_U16();
    int is_local = (raw_slot & 0x8000u) != 0;
    uint16_t slot = raw_slot & 0x7FFFu;
    MMFLOAT delta = POP_NUMERIC_F();
    BCValue *dst;

    if (is_local) {
        int idx = vm->frame_base + slot;
        if (idx >= VM_MAX_LOCALS) bc_vm_error(vm, "Local variable overflow");
        dst = &vm->locals[idx];
    } else {
        dst = &vm->globals[slot];
    }

    dst->f += delta;
    DISPATCH();
}

    /* ==================================================================
     * DATA / READ / RESTORE
     * ================================================================== */

op_read_i: {
    /* Read next data item, convert to integer, push on stack */
    BCCompiler *cs = vm->compiler;
    if (vm->data_ptr >= cs->data_count)
        bc_vm_error(vm, "No DATA to read");
    BCDataItem *item = &cs->data_pool[vm->data_ptr++];
    int64_t val = 0;
    if (item->type == T_INT)
        val = item->value.i;
    else if (item->type == T_NBR)
        val = (int64_t)item->value.f;
    else
        bc_vm_error(vm, "Type mismatch in READ (expected number, got string)");
    PUSH_I(val);
    DISPATCH();
}

op_read_f: {
    /* Read next data item, convert to float, push on stack */
    BCCompiler *cs = vm->compiler;
    if (vm->data_ptr >= cs->data_count)
        bc_vm_error(vm, "No DATA to read");
    BCDataItem *item = &cs->data_pool[vm->data_ptr++];
    MMFLOAT val = 0;
    if (item->type == T_NBR)
        val = item->value.f;
    else if (item->type == T_INT)
        val = (MMFLOAT)item->value.i;
    else
        bc_vm_error(vm, "Type mismatch in READ (expected number, got string)");
    PUSH_F(val);
    DISPATCH();
}

op_read_s: {
    /* Read next data item as string, push on stack */
    BCCompiler *cs = vm->compiler;
    if (vm->data_ptr >= cs->data_count)
        bc_vm_error(vm, "No DATA to read");
    BCDataItem *item = &cs->data_pool[vm->data_ptr++];
    if (item->type == T_STR) {
        /* String stored as constant pool index */
        uint16_t cidx = (uint16_t)item->value.i;
        BCConstant *c = &cs->constants[cidx];
        uint8_t *buf = vm_get_str_temp(vm);
        buf[0] = (uint8_t)c->len;
        memcpy(buf + 1, c->data, c->len);
        PUSH_S(buf);
    } else {
        /* Convert number to string */
        uint8_t *buf = vm_get_str_temp(vm);
        char tmp[64];
        if (item->type == T_INT)
            snprintf(tmp, sizeof(tmp), "%lld", (long long)item->value.i);
        else
            snprintf(tmp, sizeof(tmp), "%g", item->value.f);
        int len = strlen(tmp);
        buf[0] = (uint8_t)len;
        memcpy(buf + 1, tmp, len);
        PUSH_S(buf);
    }
    DISPATCH();
}

op_restore: {
    vm->data_ptr = 0;
    DISPATCH();
}

    /* ==================================================================
     * Additional string functions
     * ================================================================== */

op_str_space: {
    /* SPACE$(n%) -> str$ of n spaces */
    int64_t n = POP_I();
    if (n < 0 || n > MAXSTRLEN) bc_vm_error(vm, "SPACE$ count out of range");
    uint8_t *buf = STR_TEMP();
    memset(buf + 1, ' ', (int)n);
    buf[0] = (uint8_t)n;
    PUSH_S(buf);
    DISPATCH();
}

op_str_string: {
    /* STRING$(n%, char%) -> str$ of n copies of char */
    int64_t ch = POP_I();
    int64_t n = POP_I();
    if (n < 0 || n > MAXSTRLEN) bc_vm_error(vm, "STRING$ count out of range");
    if (ch < 0 || ch > 255) bc_vm_error(vm, "STRING$ char out of range");
    uint8_t *buf = STR_TEMP();
    memset(buf + 1, (int)ch, (int)n);
    buf[0] = (uint8_t)n;
    PUSH_S(buf);
    DISPATCH();
}

op_str_field3: {
    /* FIELD$(source$, field%, delims$), matching the common 3-arg form. */
    uint8_t *delims = POP_S();
    int64_t field = POP_I();
    uint8_t *src = POP_S();
    uint8_t *out = STR_TEMP();
    int start = 1;
    int end;

    out[0] = 0;
    if (!src || !delims || field < 1) {
        PUSH_S(out);
        DISPATCH();
    }

    while (--field > 0) {
        while (start <= src[0]) {
            int is_delim = 0;
            for (int d = 1; d <= delims[0]; d++) {
                if (src[start] == delims[d]) { is_delim = 1; break; }
            }
            if (is_delim) break;
            start++;
        }
        if (start > src[0]) {
            PUSH_S(out);
            DISPATCH();
        }
        start++;
    }

    while (start <= src[0] && src[start] == ' ') start++;
    end = start;
    while (end <= src[0]) {
        int is_delim = 0;
        for (int d = 1; d <= delims[0]; d++) {
            if (src[end] == delims[d]) { is_delim = 1; break; }
        }
        if (is_delim) break;
        end++;
    }
    while (end > start && src[end - 1] == ' ') end--;
    int len = end - start;
    if (len > MAXSTRLEN) len = MAXSTRLEN;
    out[0] = (uint8_t)len;
    if (len > 0) memcpy(out + 1, src + start, len);
    PUSH_S(out);
    DISPATCH();
}

op_str_inkey: {
    /* INKEY$ -> str$ (0 or 1 char) */
    uint8_t *buf = STR_TEMP();
    int i = MMInkey();
    if (i != -1) {
        buf[0] = 1;
        buf[1] = (uint8_t)i;
    } else {
        buf[0] = 0;
    }
    PUSH_S(buf);
    DISPATCH();
}

op_str_date: {
    uint8_t *buf = STR_TEMP();
    vm_sys_time_date(buf);
    PUSH_S(buf);
    DISPATCH();
}

op_str_time: {
    uint8_t *buf = STR_TEMP();
    vm_sys_time_time(buf);
    PUSH_S(buf);
    DISPATCH();
}

    /* ==================================================================
     * Additional numeric functions
     * ================================================================== */

op_rnd: {
    /* RND -> float 0.0 <= x < 1.0 */
    MMFLOAT f = (MMFLOAT)rand() / ((MMFLOAT)RAND_MAX + (MMFLOAT)RAND_MAX / 1000000);
    PUSH_F(f);
    DISPATCH();
}

op_timer: {
    PUSH_F((MMFLOAT)(readusclock() - timeroffset) / 1000.0);
    DISPATCH();
}

op_mm_hres: {
    PUSH_I(HRes);
    DISPATCH();
}

op_mm_vres: {
    PUSH_I(VRes);
    DISPATCH();
}

op_keydown: {
    int n = (int)POP_NUMERIC_I();
    PUSH_I(vm_sys_input_keydown(n));
    DISPATCH();
}

    /* ==================================================================
     * Additional statements
     * ================================================================== */

op_randomize: {
    /* RANDOMIZE seed — pop int seed (0 = use time / port default) */
    int64_t seed = POP_I();
    if (seed == 0) seed = HAL_PORT_RANDOMIZE_DEFAULT_SEED();
    if (seed < 0) bc_vm_error(vm, "Number out of bounds");
    srand((unsigned int)seed);
    DISPATCH();
}

op_error_s: {
    /* ERROR "message" — pop string, raise error */
    uint8_t *s = POP_S();
    char buf[STRINGSIZE];
    int len = s ? s[0] : 0;
    if (len > 0) memcpy(buf, s + 1, len);
    buf[len] = 0;
    error("$", buf);
    DISPATCH();  /* unreachable, error() longjmps */
}

op_error_empty: {
    /* ERROR — raise empty error */
    error("");
    DISPATCH();  /* unreachable */
}

op_clear: {
    /* CLEAR — wipe all variables */
    ClearVars(0, true);
    DISPATCH();
}

op_syscall: {
    uint16_t sysid = READ_U16();
    uint8_t argc = *vm->pc++;
    uint8_t auxlen = *vm->pc++;
    const uint8_t *aux = vm->pc;
    vm->pc += auxlen;
    bc_vm_execute_syscall(vm, sysid, argc, aux, auxlen);
    DISPATCH();
}

op_bridge_cmd: {
    uint16_t len = READ_U16();
    const uint8_t *tok = vm->pc;
    vm->pc += len;
    bc_bridge_call_cmd(vm, tok, len);
    DISPATCH();
}

op_bridge_fun_i: {
    uint16_t fun_idx = READ_U16();
    uint16_t arg_len = READ_U16();
    const uint8_t *args = vm->pc;
    vm->pc += arg_len;
    bc_bridge_call_fun(vm, fun_idx, args, arg_len, T_INT);
    DISPATCH();
}

op_bridge_fun_f: {
    uint16_t fun_idx = READ_U16();
    uint16_t arg_len = READ_U16();
    const uint8_t *args = vm->pc;
    vm->pc += arg_len;
    bc_bridge_call_fun(vm, fun_idx, args, arg_len, T_NBR);
    DISPATCH();
}

op_bridge_fun_s: {
    uint16_t fun_idx = READ_U16();
    uint16_t arg_len = READ_U16();
    const uint8_t *args = vm->pc;
    vm->pc += arg_len;
    bc_bridge_call_fun(vm, fun_idx, args, arg_len, T_STR);
    DISPATCH();
}

op_fastgfx_swap:
    bc_fastgfx_swap();
    DISPATCH();

op_fastgfx_sync:
    bc_fastgfx_sync();
    DISPATCH();

op_pixel_read: {
    int y = (int)POP_NUMERIC_I();
    int x = (int)POP_NUMERIC_I();
    PUSH_I(vm_sys_graphics_read_pixel(x, y));
    DISPATCH();
}

op_fastgfx_create:
    bc_fastgfx_create();
    DISPATCH();

op_fastgfx_close:
    bc_fastgfx_close();
    DISPATCH();

op_fastgfx_fps:
    bc_fastgfx_set_fps((int)POP_NUMERIC_I());
    DISPATCH();

op_colour: {
    uint8_t argc = *vm->pc++;
    int fore, back = 0;
    if (argc < 1 || argc > 2) bc_vm_error(vm, "Argument count");
    if (argc == 2) back = (int)POP_NUMERIC_I();
    fore = (int)POP_NUMERIC_I();
    if (fore < 0 || fore > WHITE) bc_vm_error(vm, "Number out of bounds");
    if (argc == 2 && (back < 0 || back > WHITE)) bc_vm_error(vm, "Number out of bounds");
    gui_fcolour = fore;
    if (argc == 2) gui_bcolour = back;
    last_fcolour = gui_fcolour;
    last_bcolour = gui_bcolour;
    if (!CurrentLinePtr) {
        PromptFC = gui_fcolour;
        PromptBC = gui_bcolour;
    }
    DISPATCH();
}

op_font: {
    uint8_t argc = *vm->pc++;
    int font;
    int scale = 1;
    if (argc < 1 || argc > 2) bc_vm_error(vm, "Argument count");
    if (argc == 2) scale = (int)POP_NUMERIC_I();
    font = (int)POP_NUMERIC_I();
    if (font < 1 || font > FONT_TABLE_SIZE) bc_vm_error(vm, "Number out of bounds");
    if (scale < 1 || scale > 15) bc_vm_error(vm, "Number out of bounds");
    SetFont(((font - 1) << 4) | scale);
    if (Option.DISPLAY_CONSOLE && !CurrentLinePtr) {
        PromptFont = gui_font;
    }
    DISPATCH();
}

op_pause: {
    int64_t ms = POP_NUMERIC_I();
    if (ms < 0) bc_vm_error(vm, "Number out of bounds");
    if (ms < 1) DISPATCH();
    uint64_t until = readusclock() + (uint64_t)ms * 1000ULL;
    while (readusclock() < until) CheckAbort();
    DISPATCH();
}

op_setpin: {
    int mode = (int)READ_I16();
    int option = (int)READ_I16();
    int64_t pin = POP_NUMERIC_I();
    vm_sys_pin_setpin(pin, mode, option);
    DISPATCH();
}

op_pin_read: {
    int64_t pin = POP_NUMERIC_I();
    PUSH_I(vm_sys_pin_read(pin));
    DISPATCH();
}

op_pin_write: {
    int64_t value = POP_NUMERIC_I();
    int64_t pin = POP_NUMERIC_I();
    vm_sys_pin_write(pin, value);
    DISPATCH();
}

op_pwm: {
    uint8_t subop = *vm->pc++;
    if (subop == BC_PWM_OFF) {
        int slice = (int)POP_NUMERIC_I();
        vm_sys_pwm_off(slice);
        DISPATCH();
    }
    if (subop == BC_PWM_CONFIG) {
        uint8_t present = *vm->pc++;
        int delaystart = 0;
        int phase_correct = 0;
        int has_duty2 = (present & 0x02) != 0;
        int has_duty1 = (present & 0x01) != 0;
        MMFLOAT duty2 = 0;
        MMFLOAT duty1 = 0;
        MMFLOAT frequency;
        int slice;
        if (present & 0x08) delaystart = (int)POP_NUMERIC_I();
        if (present & 0x04) phase_correct = (int)POP_NUMERIC_I();
        if (has_duty2) duty2 = POP_NUMERIC_F();
        if (has_duty1) duty1 = POP_NUMERIC_F();
        frequency = POP_NUMERIC_F();
        slice = (int)POP_NUMERIC_I();
        vm_sys_pwm_configure(slice, frequency, has_duty1, duty1, has_duty2, duty2,
                             phase_correct, delaystart);
        DISPATCH();
    }
    if (subop == BC_PWM_SYNC) {
        uint16_t present = READ_U16();
        MMFLOAT counts[12];
        for (int i = 0; i < 12; i++) counts[i] = -1.0;
        for (int i = 11; i >= 0; i--) {
            if (present & (1u << i))
                counts[i] = POP_NUMERIC_F();
        }
        vm_sys_pwm_sync(present, counts);
        DISPATCH();
    }
    bc_vm_error(vm, "Invalid PWM sub-operation");
}

op_servo: {
    uint8_t present = *vm->pc++;
    int has_pos2 = (present & 0x02) != 0;
    int has_pos1 = (present & 0x01) != 0;
    MMFLOAT pos2 = 0;
    MMFLOAT pos1 = 0;
    int slice;
    if (has_pos2) pos2 = POP_NUMERIC_F();
    if (has_pos1) pos1 = POP_NUMERIC_F();
    slice = (int)POP_NUMERIC_I();
    vm_sys_servo_configure(slice, has_pos1, pos1, has_pos2, pos2);
    DISPATCH();
}

op_file: {
    uint8_t subop = *vm->pc++;
    switch (subop) {
        case BC_FILE_OPEN: {
            int mode = *vm->pc++;
            int fnbr = (int)READ_U16();
            uint8_t *name = POP_S();
            int len = name ? name[0] : 0;
            char filename[STRINGSIZE + 1];
            if (len <= 0 || len > STRINGSIZE) bc_vm_error(vm, "File name");
            memcpy(filename, name + 1, len);
            filename[len] = '\0';
            vm_sys_file_open(filename, fnbr, mode);
            break;
        }
        case BC_FILE_CLOSE: {
            int fnbr = (int)READ_U16();
            vm_sys_file_close(fnbr);
            break;
        }
        case BC_FILE_PRINT_INT: {
            int fnbr = (int)READ_U16();
            int64_t val = POP_NUMERIC_I();
            char num[64];
            char out[80];
            int pos = 0;
            IntToStr(num, val, 10);
            if (val >= 0) out[pos++] = ' ';
            int len = (int)strlen(num);
            memcpy(out + pos, num, len);
            pos += len;
            vm_sys_file_print_buf(fnbr, out, pos);
            break;
        }
        case BC_FILE_PRINT_FLT: {
            int fnbr = (int)READ_U16();
            MMFLOAT val = (vm->stack_types[vm->sp] == T_INT) ? (MMFLOAT)POP_I() : POP_F();
            char num[64];
            char out[80];
            int pos = 0;
            FloatToStr(num, val, 0, STR_AUTO_PRECISION, ' ');
            if (val >= 0.0) out[pos++] = ' ';
            int len = (int)strlen(num);
            int start = 0;
            while (start < len && num[start] == ' ') start++;
            memcpy(out + pos, num + start, len - start);
            pos += len - start;
            vm_sys_file_print_buf(fnbr, out, pos);
            break;
        }
        case BC_FILE_PRINT_STR: {
            int fnbr = (int)READ_U16();
            uint8_t *val = POP_S();
            vm_sys_file_print_str(fnbr, val);
            break;
        }
        case BC_FILE_PRINT_NEWLINE: {
            int fnbr = (int)READ_U16();
            vm_sys_file_print_newline(fnbr);
            break;
        }
        case BC_FILE_LINE_INPUT: {
            uint8_t is_local = *vm->pc++;
            uint16_t slot = READ_U16();
            int fnbr = (int)READ_U16();
            uint8_t *dest = bc_vm_string_lvalue(vm, is_local, slot);
            vm_sys_file_line_input(fnbr, dest);
            break;
        }
        case BC_FILE_DRIVE: {
            uint8_t *s = POP_S();
            int len = s ? s[0] : 0;
            char drive[STRINGSIZE + 1];
            if (len <= 0 || len > STRINGSIZE) bc_vm_error(vm, "Invalid disk");
            memcpy(drive, s + 1, len);
            drive[len] = '\0';
            vm_sys_file_drive(drive);
            break;
        }
        case BC_FILE_SEEK: {
            int fnbr = (int)READ_U16();
            int position = (int)POP_NUMERIC_I();
            vm_sys_file_seek(fnbr, position);
            break;
        }
        case BC_FILE_MKDIR:
        case BC_FILE_CHDIR:
        case BC_FILE_RMDIR:
        case BC_FILE_KILL: {
            uint8_t *s = POP_S();
            int len = s ? s[0] : 0;
            char path[STRINGSIZE + 1];
            if (len <= 0 || len > STRINGSIZE) bc_vm_error(vm, "File name");
            memcpy(path, s + 1, len);
            path[len] = '\0';
            if (subop == BC_FILE_MKDIR) vm_sys_file_mkdir(path);
            else if (subop == BC_FILE_CHDIR) vm_sys_file_chdir(path);
            else if (subop == BC_FILE_RMDIR) vm_sys_file_rmdir(path);
            else vm_sys_file_kill(path);
            break;
        }
        case BC_FILE_RENAME: {
            uint8_t *new_s = POP_S();
            uint8_t *old_s = POP_S();
            int old_len = old_s ? old_s[0] : 0;
            int new_len = new_s ? new_s[0] : 0;
            char old_name[STRINGSIZE + 1];
            char new_name[STRINGSIZE + 1];
            if (old_len <= 0 || old_len > STRINGSIZE || new_len <= 0 || new_len > STRINGSIZE)
                bc_vm_error(vm, "File name");
            memcpy(old_name, old_s + 1, old_len);
            old_name[old_len] = '\0';
            memcpy(new_name, new_s + 1, new_len);
            new_name[new_len] = '\0';
            vm_sys_file_rename(old_name, new_name);
            break;
        }
        case BC_FILE_COPY: {
            int mode = *vm->pc++;
            uint8_t *to_s = POP_S();
            uint8_t *from_s = POP_S();
            int from_len = from_s ? from_s[0] : 0;
            int to_len = to_s ? to_s[0] : 0;
            char from_name[STRINGSIZE + 1];
            char to_name[STRINGSIZE + 1];
            if (from_len <= 0 || from_len > STRINGSIZE || to_len <= 0 || to_len > STRINGSIZE)
                bc_vm_error(vm, "File name");
            memcpy(from_name, from_s + 1, from_len);
            from_name[from_len] = '\0';
            memcpy(to_name, to_s + 1, to_len);
            to_name[to_len] = '\0';
            vm_sys_file_copy(from_name, to_name, mode);
            break;
        }
        default:
            bc_vm_error(vm, "Invalid file syscall");
    }
    DISPATCH();
}

op_rgb: {
    int b = (int)POP_I();
    int g = (int)POP_I();
    int r = (int)POP_I();
    PUSH_I(rgb(r, g, b));
    DISPATCH();
}


    /* ==================================================================
     * Fast Loop — register micro-op executor
     * ================================================================== */

op_fast_loop: {
    /* Read header */
    uint16_t total_len = READ_U16();
    uint8_t *block_end = vm->pc + total_len;
    uint8_t nregs     = *vm->pc++;
    uint8_t nlocals   = *vm->pc++;
    uint8_t nglobals  = *vm->pc++;
    uint8_t nconsts   = *vm->pc++;
    uint8_t narrays   = *vm->pc++;

    if (nregs > MAX_FAST_REGS)
        bc_vm_error(vm, "FAST_LOOP: too many registers (%d)", nregs);

    /* Register file — on C stack */
    int64_t regs[MAX_FAST_REGS];
    memset(regs, 0, sizeof(int64_t) * nregs);

    /* Load locals into registers */
    for (int i = 0; i < nlocals && i < (int)(vm->locals_top - vm->frame_base); i++)
        regs[i] = vm->locals[vm->frame_base + i].i;

    /* Load globals */
    uint16_t global_slots[32];
    for (int i = 0; i < nglobals; i++) {
        uint16_t slot; memcpy(&slot, vm->pc, 2); vm->pc += 2;
        global_slots[i] = slot;
        regs[nlocals + i] = vm->globals[slot].i;
    }

    /* Load array references */
    BCArray *arr_ptrs[16];
    for (int i = 0; i < narrays; i++) {
        uint8_t is_local = *vm->pc++;
        uint16_t slot; memcpy(&slot, vm->pc, 2); vm->pc += 2;
        if (is_local)
            arr_ptrs[i] = &vm->local_arrays[vm->frame_base + slot];
        else
            arr_ptrs[i] = &vm->arrays[slot];
    }

    /* Load constants */
    for (int i = 0; i < nconsts; i++) {
        uint8_t ctype = *vm->pc++;
        int reg = nlocals + nglobals + i;
        if (ctype == T_INT) {
            int64_t val; memcpy(&val, vm->pc, 8); vm->pc += 8;
            regs[reg] = val;
        } else {
            double fval; memcpy(&fval, vm->pc, 8); vm->pc += 8;
            memcpy(&regs[reg], &fval, 8);
        }
    }

    /* Micro-op execution */
    uint8_t *rop_base = vm->pc;
    uint8_t *rpc = rop_base;

    for (;;) {
        uint8_t rop = *rpc++;
        switch (rop) {

        case ROP_END:
        case ROP_EXIT:
            goto fast_loop_done;

        case ROP_JMP: {
            int16_t off; memcpy(&off, rpc, 2); rpc += 2;
            rpc += off;
            break;
        }

        case ROP_CHECKINT:
            CheckAbort();
            check_interrupt();
            break;

        /* --- Integer arithmetic --- */
        case ROP_ADD_I: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] + regs[b];
            break;
        }
        case ROP_SUB_I: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] - regs[b];
            break;
        }
        case ROP_MUL_I: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] * regs[b];
            break;
        }
        case ROP_IDIV_I: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            if (regs[b] == 0) bc_vm_error(vm, "Division by zero");
            regs[d] = regs[a] / regs[b];
            break;
        }
        case ROP_MOD_I: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            if (regs[b] == 0) bc_vm_error(vm, "Division by zero");
            regs[d] = regs[a] % regs[b];
            break;
        }

        /* --- Float arithmetic --- */
        case ROP_ADD_F: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            double fa, fb, fr;
            memcpy(&fa, &regs[a], 8); memcpy(&fb, &regs[b], 8);
            fr = fa + fb;
            memcpy(&regs[d], &fr, 8);
            break;
        }
        case ROP_SUB_F: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            double fa, fb, fr;
            memcpy(&fa, &regs[a], 8); memcpy(&fb, &regs[b], 8);
            fr = fa - fb;
            memcpy(&regs[d], &fr, 8);
            break;
        }
        case ROP_MUL_F: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            double fa, fb, fr;
            memcpy(&fa, &regs[a], 8); memcpy(&fb, &regs[b], 8);
            fr = fa * fb;
            memcpy(&regs[d], &fr, 8);
            break;
        }
        case ROP_DIV_F: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            double fa, fb, fr;
            memcpy(&fa, &regs[a], 8); memcpy(&fb, &regs[b], 8);
            if (fb == 0.0) bc_vm_error(vm, "Division by zero");
            fr = fa / fb;
            memcpy(&regs[d], &fr, 8);
            break;
        }

        /* --- Unary --- */
        case ROP_NEG_I: {
            uint8_t d = *rpc++, s = *rpc++;
            regs[d] = -regs[s];
            break;
        }
        case ROP_NEG_F: {
            uint8_t d = *rpc++, s = *rpc++;
            double fv; memcpy(&fv, &regs[s], 8);
            fv = -fv;
            memcpy(&regs[d], &fv, 8);
            break;
        }
        case ROP_NOT: {
            uint8_t d = *rpc++, s = *rpc++;
            regs[d] = !regs[s];
            break;
        }
        case ROP_INV: {
            uint8_t d = *rpc++, s = *rpc++;
            regs[d] = ~regs[s];
            break;
        }

        /* --- Bitwise --- */
        case ROP_AND: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] & regs[b];
            break;
        }
        case ROP_OR: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] | regs[b];
            break;
        }
        case ROP_XOR: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] ^ regs[b];
            break;
        }
        case ROP_SHL: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] << regs[b];
            break;
        }
        case ROP_SHR: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++;
            regs[d] = regs[a] >> regs[b];
            break;
        }

        /* --- Move / convert --- */
        case ROP_MOV: {
            uint8_t d = *rpc++, s = *rpc++;
            regs[d] = regs[s];
            break;
        }
        case ROP_CVT_I2F: {
            uint8_t d = *rpc++, s = *rpc++;
            double fv = (double)regs[s];
            memcpy(&regs[d], &fv, 8);
            break;
        }
        case ROP_CVT_F2I: {
            uint8_t d = *rpc++, s = *rpc++;
            double fv; memcpy(&fv, &regs[s], 8);
            regs[d] = (int64_t)fv;
            break;
        }

        /* --- Load immediate --- */
        case ROP_LOAD_IMM_I: {
            uint8_t d = *rpc++;
            int64_t val; memcpy(&val, rpc, 8); rpc += 8;
            regs[d] = val;
            break;
        }
        case ROP_LOAD_IMM_F: {
            uint8_t d = *rpc++;
            memcpy(&regs[d], rpc, 8); rpc += 8;
            break;
        }

        /* --- Fused fixed-point --- */
        case ROP_SQRSHR: {
            uint8_t d = *rpc++, a = *rpc++, bits = *rpc++;
            regs[d] = bc_vm_mulshr_int(regs[a], regs[a], (int)regs[bits]);
            break;
        }
        case ROP_MULSHR: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++, bits = *rpc++;
            regs[d] = bc_vm_mulshr_int(regs[a], regs[b], (int)regs[bits]);
            break;
        }
        case ROP_MULSHRADD: {
            uint8_t d = *rpc++, a = *rpc++, b = *rpc++, bits = *rpc++, c = *rpc++;
            regs[d] = bc_vm_mulshr_int(regs[a], regs[b], (int)regs[bits]) + regs[c];
            break;
        }

        /* --- Integer comparison (produce 0/1) --- */
        case ROP_EQ_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]==regs[b]); break; }
        case ROP_NE_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]!=regs[b]); break; }
        case ROP_LT_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]< regs[b]); break; }
        case ROP_GT_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]> regs[b]); break; }
        case ROP_LE_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]<=regs[b]); break; }
        case ROP_GE_I: { uint8_t d=*rpc++,a=*rpc++,b=*rpc++; regs[d]=(regs[a]>=regs[b]); break; }

        /* --- Conditional jumps (integer) --- */
        case ROP_JCMP_EQ_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]==regs[b]) rpc+=o; break; }
        case ROP_JCMP_NE_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]!=regs[b]) rpc+=o; break; }
        case ROP_JCMP_LT_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]< regs[b]) rpc+=o; break; }
        case ROP_JCMP_GT_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]> regs[b]) rpc+=o; break; }
        case ROP_JCMP_LE_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]<=regs[b]) rpc+=o; break; }
        case ROP_JCMP_GE_I: { uint8_t a=*rpc++,b=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[a]>=regs[b]) rpc+=o; break; }

        /* --- JZ / JNZ --- */
        case ROP_JZ: { uint8_t s=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[s]==0) rpc+=o; break; }
        case ROP_JNZ: { uint8_t s=*rpc++; int16_t o; memcpy(&o,rpc,2); rpc+=2; if(regs[s]!=0) rpc+=o; break; }

        /* --- 1D Array access --- */
        case ROP_LOAD_ARR_I: {
            uint8_t d = *rpc++, ai = *rpc++, ir = *rpc++;
            BCArray *a = arr_ptrs[ai];
            int64_t idx = regs[ir];
            if (idx < 0 || idx >= (int64_t)a->total_elements)
                bc_vm_error(vm, "Array index out of bounds (%lld)", (long long)idx);
            regs[d] = a->data[idx].i;
            break;
        }
        case ROP_STORE_ARR_I: {
            uint8_t vr = *rpc++, ai = *rpc++, ir = *rpc++;
            BCArray *a = arr_ptrs[ai];
            int64_t idx = regs[ir];
            if (idx < 0 || idx >= (int64_t)a->total_elements)
                bc_vm_error(vm, "Array index out of bounds (%lld)", (long long)idx);
            a->data[idx].i = regs[vr];
            break;
        }
        case ROP_LOAD_ARR_F: {
            uint8_t d = *rpc++, ai = *rpc++, ir = *rpc++;
            BCArray *a = arr_ptrs[ai];
            int64_t idx = regs[ir];
            if (idx < 0 || idx >= (int64_t)a->total_elements)
                bc_vm_error(vm, "Array index out of bounds (%lld)", (long long)idx);
            memcpy(&regs[d], &a->data[idx].f, 8);
            break;
        }
        case ROP_STORE_ARR_F: {
            uint8_t vr = *rpc++, ai = *rpc++, ir = *rpc++;
            BCArray *a = arr_ptrs[ai];
            int64_t idx = regs[ir];
            if (idx < 0 || idx >= (int64_t)a->total_elements)
                bc_vm_error(vm, "Array index out of bounds (%lld)", (long long)idx);
            memcpy(&a->data[idx].f, &regs[vr], 8);
            break;
        }

        default:
            bc_vm_error(vm, "FAST_LOOP: invalid micro-op 0x%02X", rop);
        }
    }

fast_loop_done:
    /* Write back locals */
    for (int i = 0; i < nlocals && i < (int)(vm->locals_top - vm->frame_base); i++)
        vm->locals[vm->frame_base + i].i = regs[i];

    /* Write back globals */
    for (int i = 0; i < nglobals; i++)
        vm->globals[global_slots[i]].i = regs[nlocals + i];

    /* Advance past entire block */
    vm->pc = block_end;
    DISPATCH();
}

    /* ==================================================================
     * Housekeeping
     * ================================================================== */

op_line: {
    uint16_t lineno = READ_U16();
    vm->current_line = lineno;
    DISPATCH();
}

op_checkint:
    CheckAbort();
    check_interrupt();
    DISPATCH();

op_end:
    return;

op_halt:
    vm_output(vm, "STOP\r\n");
    return;

op_invalid:
    bc_vm_error(vm, "Invalid opcode 0x%02X at offset %u",
                *(vm->pc - 1),
                (unsigned)(vm->pc - 1 - vm->bytecode));

    /* Undefine local macros */
#undef DISPATCH
#undef READ_U16
#undef READ_I16
#undef READ_U32
#undef READ_I64
#undef READ_F64
#undef PUSH_I
#undef PUSH_F
#undef PUSH_S
#undef POP_I
#undef POP_F
#undef POP_S
#undef TOS_I
#undef TOS_F
}
