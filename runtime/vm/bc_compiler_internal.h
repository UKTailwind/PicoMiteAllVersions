/*
 * bc_compiler_internal.h — Internal API shared between compiler modules
 */
#ifndef __BC_COMPILER_INTERNAL_H
#define __BC_COMPILER_INTERNAL_H

#include "bytecode.h"

/* Type extraction from variable name suffix.
 * Returns T_INT for %, T_NBR for !, T_STR for $, or 0 for no suffix. */
static inline uint8_t bc_type_from_suffix(char c) {
    if (c == '%') return T_INT;
    if (c == '!') return T_NBR;
    if (c == '$') return T_STR;
    return 0;
}

/* Parse a variable name from tokenized stream, returning length.
 * Sets *type_out to the type (T_INT/T_NBR/T_STR) or 0 if untyped.
 * Sets *is_array to 1 if followed by '('. */
static inline int bc_parse_varname(unsigned char *p, uint8_t *type_out, int *is_array) {
    int len = 0;
    *type_out = 0;
    *is_array = 0;

    if (!isnamestart(*p)) return 0;
    while (isnamechar(p[len])) len++;

    /* Check for type suffix */
    if (p[len] == '%' || p[len] == '!' || p[len] == '$') {
        *type_out = bc_type_from_suffix(p[len]);
        len++;
    }

    /* Check for array */
    if (p[len] == '(') *is_array = 1;

    return len;
}

/* Compiler error helper — sets error state without aborting */
static inline void bc_set_error(BCCompiler *cs, const char *fmt, ...) {
    if (cs->has_error) return;  /* keep first error */
    cs->has_error = 1;
    cs->error_line = cs->current_line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cs->error_msg, sizeof(cs->error_msg), fmt, ap);
    va_end(ap);
}

/* Emit a LOAD for a variable (handles local vs global, scalar vs array) */
void bc_emit_load_var(BCCompiler *cs, uint16_t slot, uint8_t type, int is_local);
void bc_emit_store_var(BCCompiler *cs, uint16_t slot, uint8_t type, int is_local);

/* Skip past a variable reference in raw source (name, suffix, array indices) */
unsigned char *bc_skip_var(unsigned char *p);

/* Nesting stack helpers */
void bc_nest_push(BCCompiler *cs, BCNestType type);
BCNestEntry *bc_nest_top(BCCompiler *cs);
BCNestEntry *bc_nest_find(BCCompiler *cs, BCNestType type);
void bc_nest_pop(BCCompiler *cs);

/* Find a local variable in the current SUB/FUNCTION scope, returns offset or -1 */
int bc_find_local(BCCompiler *cs, const char *name, int name_len);
int bc_add_local(BCCompiler *cs, const char *name, int name_len, uint8_t type, int is_array);

/* Add a fixup for a forward reference */
void bc_add_fixup_line(BCCompiler *cs, uint32_t patch_addr, int target_line, uint8_t size, uint8_t is_relative);
void bc_add_fixup_label(BCCompiler *cs, uint32_t patch_addr, const char *name, uint8_t size, uint8_t is_relative);
void bc_add_fixup_label_data_index(BCCompiler *cs, uint32_t patch_addr, const char *name);
void bc_resolve_fixups(BCCompiler *cs);

/* BASIC labels: register / look up by name (case-insensitive). */
int bc_add_labelmap_entry(BCCompiler *cs, const char *name, uint32_t offset);
uint32_t bc_labelmap_lookup(const BCCompiler *cs, const char *name);

/* Output capture API (from bc_runtime.c) */
void bc_vm_start_capture(BCVMState *vm, char *buf, int capacity);
void bc_vm_capture_write(BCVMState *vm, const char *text, int len);
void bc_vm_capture_char(BCVMState *vm, char c);
void bc_vm_capture_string(BCVMState *vm, const char *s);

#endif /* __BC_COMPILER_INTERNAL_H */
