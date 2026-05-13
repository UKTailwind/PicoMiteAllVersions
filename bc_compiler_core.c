/*
 * bc_compiler_core.c — Core compiler helpers: emission, slots, locals,
 *                       constants, nesting stack, fixups, line map, etc.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "bytecode.h"
#include "bc_compiler_internal.h"
#include "bc_alloc.h"

int bc_compiler_alloc(BCCompiler *cs) {
    memset(cs, 0, sizeof(*cs));
    /* Allocation order matters for post-compact heap fragmentation. The
     * MMBasic page allocator walks top-down, so the FIRST allocations
     * land at the HIGHEST addresses (just below cs/vm). Allocate the
     * five compile-only tables FIRST so they sit at high addresses and,
     * when bc_compiler_compact frees them, the resulting hole is
     * adjacent to vm. Step 2 of compact then alloc-news the shrunk
     * runtime tables into that hole — they cluster contiguously below
     * vm, and the freed-old-runtime positions merge with the heap
     * below into one big contiguous free region. With the opposite
     * order (runtime first, compile-only last) the hole is in the
     * middle of the alive cluster and the post-compact heap is split
     * into ~50% contiguous + scattered fragments. */
    cs->fixups     = (BCFixup *)BC_COMPILER_ALLOC(BC_MAX_FIXUPS * sizeof(BCFixup));
    cs->linemap    = (BCLineMap *)BC_COMPILER_ALLOC(BC_MAX_LINEMAP * sizeof(BCLineMap));
    cs->labelmap   = (BCLabelMap *)BC_COMPILER_ALLOC(BC_MAX_LABELS * sizeof(BCLabelMap));
    cs->nest_stack = (BCNestEntry *)BC_COMPILER_ALLOC(BC_MAX_NEST * sizeof(BCNestEntry));
    cs->locals     = (BCLocalVar *)BC_COMPILER_ALLOC(BC_MAX_LOCALS * sizeof(BCLocalVar));
    cs->code       = (uint8_t *)BC_COMPILER_ALLOC(BC_MAX_CODE);
    cs->constants  = (BCConstant *)BC_COMPILER_ALLOC(BC_MAX_CONSTANTS * sizeof(BCConstant));
    cs->slots      = (BCSlot *)BC_COMPILER_ALLOC(BC_MAX_SLOTS * sizeof(BCSlot));
    cs->subfuns    = (BCSubFun *)BC_COMPILER_ALLOC(BC_MAX_SUBFUNS * sizeof(BCSubFun));
    cs->subfun_locals_base = (uint16_t *)BC_COMPILER_ALLOC(BC_MAX_SUBFUNS * sizeof(uint16_t));
    cs->local_meta = (BCLocalMeta *)BC_COMPILER_ALLOC(BC_MAX_LOCAL_META * sizeof(BCLocalMeta));
    cs->data_pool  = (BCDataItem *)BC_COMPILER_ALLOC(BC_MAX_DATA_ITEMS * sizeof(BCDataItem));
    if (!cs->code || !cs->constants || !cs->slots || !cs->subfuns ||
        !cs->subfun_locals_base ||
        !cs->fixups || !cs->linemap || !cs->labelmap || !cs->nest_stack || !cs->locals ||
        !cs->local_meta ||
        !cs->data_pool) {
        bc_compiler_free(cs);
        return -1;
    }
    cs->current_subfun = -1;
    return 0;
}

void bc_compiler_free(BCCompiler *cs) {
    if (cs->code)       { if (bc_compile_owns(cs->code)) BC_COMPILER_FREE(cs->code); else BC_FREE(cs->code); }
    if (cs->constants)  { if (bc_compile_owns(cs->constants)) BC_COMPILER_FREE(cs->constants); else BC_FREE(cs->constants); }
    if (cs->slots)      { if (bc_compile_owns(cs->slots)) BC_COMPILER_FREE(cs->slots); else BC_FREE(cs->slots); }
    if (cs->subfuns)    { if (bc_compile_owns(cs->subfuns)) BC_COMPILER_FREE(cs->subfuns); else BC_FREE(cs->subfuns); }
    if (cs->subfun_locals_base) { if (bc_compile_owns(cs->subfun_locals_base)) BC_COMPILER_FREE(cs->subfun_locals_base); else BC_FREE(cs->subfun_locals_base); }
    if (cs->fixups)     { if (bc_compile_owns(cs->fixups)) BC_COMPILER_FREE(cs->fixups); else BC_FREE(cs->fixups); }
    if (cs->linemap)    { if (bc_compile_owns(cs->linemap)) BC_COMPILER_FREE(cs->linemap); else BC_FREE(cs->linemap); }
    if (cs->labelmap)   { if (bc_compile_owns(cs->labelmap)) BC_COMPILER_FREE(cs->labelmap); else BC_FREE(cs->labelmap); }
    if (cs->nest_stack) { if (bc_compile_owns(cs->nest_stack)) BC_COMPILER_FREE(cs->nest_stack); else BC_FREE(cs->nest_stack); }
    if (cs->locals)     { if (bc_compile_owns(cs->locals)) BC_COMPILER_FREE(cs->locals); else BC_FREE(cs->locals); }
    if (cs->local_meta) { if (bc_compile_owns(cs->local_meta)) BC_COMPILER_FREE(cs->local_meta); else BC_FREE(cs->local_meta); }
    if (cs->data_pool)  { if (bc_compile_owns(cs->data_pool)) BC_COMPILER_FREE(cs->data_pool); else BC_FREE(cs->data_pool); }
    memset(cs, 0, sizeof(*cs));
}

/*
 * Compact compiler state after compilation: free compile-only arrays and
 * shrink runtime arrays from MAX to actual size.  This reclaims the bulk
 * of the heap (often >100 KB) so the running program can allocate memory
 * for display buffers, large arrays, etc.
 *
 * Call after bc_compile() returns successfully, before bc_vm_execute().
 */
int bc_compiler_compact(BCCompiler *cs) {
    int ok = 1;
    /* 1. Free arrays only needed during compilation */
    if (cs->fixups)     { BC_COMPILER_FREE(cs->fixups);     cs->fixups = NULL; }
    if (cs->linemap)    { BC_COMPILER_FREE(cs->linemap);    cs->linemap = NULL; }
    if (cs->labelmap)   { BC_COMPILER_FREE(cs->labelmap);   cs->labelmap = NULL; }
    if (cs->nest_stack) { BC_COMPILER_FREE(cs->nest_stack); cs->nest_stack = NULL; }
    if (cs->locals)     { BC_COMPILER_FREE(cs->locals);     cs->locals = NULL; }

    /* 2. Shrink runtime arrays to actual size.
     *    Alloc new right-sized buffer, copy, free oversized original.
     *    If alloc fails, keep the original (still works, just wastes memory). */
#define COMPACT(field, count, type) do { \
    if (cs->field && (count) > 0) { \
        size_t sz = (size_t)(count) * sizeof(type); \
        type *p = (type *)BC_ALLOC(sz); \
        if (p) { memcpy(p, cs->field, sz); BC_COMPILER_FREE(cs->field); cs->field = p; } \
        else if (bc_compile_owns(cs->field)) { ok = 0; } \
    } else if (cs->field && (count) == 0) { \
        BC_COMPILER_FREE(cs->field); cs->field = NULL; \
    } \
} while(0)

    /* Shrink code buffer from BC_MAX_CODE to actual code_len */
    if (cs->code && cs->code_len > 0) {
        uint8_t *p = (uint8_t *)BC_ALLOC(cs->code_len);
        if (p) { memcpy(p, cs->code, cs->code_len); BC_COMPILER_FREE(cs->code); cs->code = p; }
        else if (bc_compile_owns(cs->code)) { ok = 0; }
    }

    COMPACT(constants, cs->const_count, BCConstant);
    COMPACT(slots, cs->slot_count, BCSlot);
    COMPACT(subfuns, cs->subfun_count, BCSubFun);
    COMPACT(subfun_locals_base, cs->subfun_count, uint16_t);
    COMPACT(local_meta, cs->local_meta_count, BCLocalMeta);
    COMPACT(data_pool, cs->data_count, BCDataItem);

#undef COMPACT
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/*  Compiler initialisation (reset state, arrays must be allocated)   */
/* ------------------------------------------------------------------ */

void bc_compiler_init(BCCompiler *cs) {
    cs->code_len         = 0;
    cs->const_count      = 0;
    cs->slot_count       = 0;
    cs->next_hidden_slot = 0;
    cs->subfun_count     = 0;
    if (cs->subfun_locals_base) memset(cs->subfun_locals_base, 0, BC_MAX_SUBFUNS * sizeof(uint16_t));
    cs->fixup_count      = 0;
    cs->linemap_count    = 0;
    cs->labelmap_count   = 0;
    cs->nest_depth       = 0;
    cs->current_subfun   = -1;
    cs->current_line     = 0;
    cs->local_count      = 0;
    cs->local_meta_count = 0;
    cs->data_count       = 0;
    cs->error_line       = 0;
    cs->error_msg[0]     = '\0';
    cs->has_error        = 0;
}

/* ------------------------------------------------------------------ */
/*  Bytecode emission helpers                                         */
/* ------------------------------------------------------------------ */

void bc_emit_byte(BCCompiler *cs, uint8_t b) {
    if (cs->code_len >= BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    cs->code[cs->code_len++] = b;
}

void bc_emit_u16(BCCompiler *cs, uint16_t v) {
    if (cs->code_len + 2 > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, 2);
    cs->code_len += 2;
}

void bc_emit_i16(BCCompiler *cs, int16_t v) {
    if (cs->code_len + 2 > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, 2);
    cs->code_len += 2;
}

void bc_emit_u32(BCCompiler *cs, uint32_t v) {
    if (cs->code_len + 4 > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, 4);
    cs->code_len += 4;
}

void bc_emit_ptr(BCCompiler *cs, const void *ptr) {
    uintptr_t v = (uintptr_t)ptr;
    if (cs->code_len + (uint32_t)sizeof(v) > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, sizeof(v));
    cs->code_len += sizeof(v);
}

void bc_emit_i64(BCCompiler *cs, int64_t v) {
    if (cs->code_len + 8 > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, 8);
    cs->code_len += 8;
}

void bc_emit_f64(BCCompiler *cs, MMFLOAT v) {
    if (cs->code_len + (uint32_t)sizeof(MMFLOAT) > BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    memcpy(&cs->code[cs->code_len], &v, sizeof(MMFLOAT));
    cs->code_len += sizeof(MMFLOAT);
}

/* ------------------------------------------------------------------ */
/*  Bytecode patching helpers (write at an arbitrary address)          */
/* ------------------------------------------------------------------ */

void bc_patch_u16(BCCompiler *cs, uint32_t addr, uint16_t v) {
    if (addr + 2 > cs->code_len) {
        bc_set_error(cs, "Patch address %u out of range (code_len=%u)", addr, cs->code_len);
        return;
    }
    memcpy(&cs->code[addr], &v, 2);
}

void bc_patch_i16(BCCompiler *cs, uint32_t addr, int16_t v) {
    if (addr + 2 > cs->code_len) {
        bc_set_error(cs, "Patch address %u out of range (code_len=%u)", addr, cs->code_len);
        return;
    }
    memcpy(&cs->code[addr], &v, 2);
}

void bc_patch_u32(BCCompiler *cs, uint32_t addr, uint32_t v) {
    if (addr + 4 > cs->code_len) {
        bc_set_error(cs, "Patch address %u out of range (code_len=%u)", addr, cs->code_len);
        return;
    }
    memcpy(&cs->code[addr], &v, 4);
}

/* ------------------------------------------------------------------ */
/*  Variable slot management (global compile-time slots)              */
/* ------------------------------------------------------------------ */

uint16_t bc_find_slot(BCCompiler *cs, const char *name, int name_len) {
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        if ((int)strlen(cs->slots[i].name) == name_len &&
            strncasecmp(cs->slots[i].name, name, name_len) == 0) {
            return i;
        }
    }
    return 0xFFFF;
}

uint16_t bc_add_slot(BCCompiler *cs, const char *name, int name_len, uint8_t type, int is_array) {
    if (cs->slot_count >= BC_MAX_SLOTS) {
        bc_set_error(cs, "Too many variable slots (max %d)", BC_MAX_SLOTS);
        return 0xFFFF;
    }
    uint16_t idx = cs->slot_count++;
    int copy_len = name_len;
    if (copy_len > MAXVARLEN) copy_len = MAXVARLEN;
    memcpy(cs->slots[idx].name, name, copy_len);
    cs->slots[idx].name[copy_len] = '\0';
    cs->slots[idx].type     = type;
    cs->slots[idx].is_array = (uint8_t)is_array;
    cs->slots[idx].ndims    = 0;
    memset(cs->slots[idx].dims, 0, sizeof(cs->slots[idx].dims));
    cs->slots[idx].struct_idx = -1;
    return idx;
}

/* ------------------------------------------------------------------ */
/*  Local variable management (per SUB/FUNCTION scope)                */
/* ------------------------------------------------------------------ */

int bc_find_local(BCCompiler *cs, const char *name, int name_len) {
    for (int i = 0; i < (int)cs->local_count; i++) {
        if ((int)strlen(cs->locals[i].name) == name_len &&
            strncasecmp(cs->locals[i].name, name, name_len) == 0) {
            return i;
        }
    }
    return -1;
}

int bc_add_local(BCCompiler *cs, const char *name, int name_len, uint8_t type, int is_array) {
    if (cs->local_count >= BC_MAX_LOCALS) {
        bc_set_error(cs, "Too many local variables (max %d)", BC_MAX_LOCALS);
        return -1;
    }
    int idx = (int)cs->local_count++;
    int copy_len = name_len;
    if (copy_len > MAXVARLEN) copy_len = MAXVARLEN;
    memcpy(cs->locals[idx].name, name, copy_len);
    cs->locals[idx].name[copy_len] = '\0';
    cs->locals[idx].type     = type;
    cs->locals[idx].is_array = (uint8_t)is_array;
    return idx;
}

int bc_commit_locals(BCCompiler *cs, int sf_idx) {
    if (sf_idx < 0 || sf_idx >= (int)cs->subfun_count) {
        bc_set_error(cs, "Invalid SUB/FUNCTION local metadata");
        return -1;
    }
    if ((uint32_t)cs->local_meta_count + (uint32_t)cs->local_count > BC_MAX_LOCAL_META) {
        bc_set_error(cs, "Too many local metadata entries (max %d)", BC_MAX_LOCAL_META);
        return -1;
    }
    cs->subfun_locals_base[sf_idx] = cs->local_meta_count;
    for (int i = 0; i < (int)cs->local_count; i++) {
        memcpy(&cs->local_meta[cs->local_meta_count + i], &cs->locals[i], sizeof(BCLocalMeta));
    }
    cs->local_meta_count += cs->local_count;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Constant pool                                                     */
/* ------------------------------------------------------------------ */

uint16_t bc_add_constant_string(BCCompiler *cs, const uint8_t *data, int len) {
    /* Check for a duplicate first */
    for (uint16_t i = 0; i < cs->const_count; i++) {
        if (cs->constants[i].len == (uint16_t)len &&
            memcmp(cs->constants[i].data, data, len) == 0) {
            return i;
        }
    }

    if (cs->const_count >= BC_MAX_CONSTANTS) {
        bc_set_error(cs, "Too many string constants (max %d)", BC_MAX_CONSTANTS);
        return 0xFFFF;
    }

    uint16_t idx = cs->const_count++;
    int copy_len = len;
    if (copy_len > STRINGSIZE - 1) copy_len = STRINGSIZE - 1;
    memcpy(cs->constants[idx].data, data, copy_len);
    cs->constants[idx].data[copy_len] = '\0';
    cs->constants[idx].len = (uint16_t)copy_len;
    return idx;
}

/* ------------------------------------------------------------------ */
/*  SUB/FUNCTION management                                           */
/* ------------------------------------------------------------------ */

int bc_find_subfun(BCCompiler *cs, const char *name, int name_len) {
    for (int i = 0; i < (int)cs->subfun_count; i++) {
        if ((int)strlen(cs->subfuns[i].name) == name_len &&
            strncasecmp(cs->subfuns[i].name, name, name_len) == 0) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Line map                                                          */
/* ------------------------------------------------------------------ */

int bc_add_linemap_entry(BCCompiler *cs, uint16_t lineno, uint32_t offset) {
    if (cs->linemap_count >= BC_MAX_LINEMAP) {
        bc_set_error(cs, "Too many line map entries (max %d)", BC_MAX_LINEMAP);
        return -1;
    }
    cs->linemap[cs->linemap_count].lineno = lineno;
    cs->linemap[cs->linemap_count].offset = offset;
    cs->linemap_count++;
    return 0;
}

uint32_t bc_linemap_lookup(BCCompiler *cs, uint16_t lineno) {
    /* Linear search — linemap is in program order, not sorted by line number
     * (user-provided line numbers can appear in any order). */
    for (int i = 0; i < (int)cs->linemap_count; i++) {
        if (cs->linemap[i].lineno == lineno) {
            return cs->linemap[i].offset;
        }
    }
    return 0xFFFFFFFF;
}

/* ------------------------------------------------------------------ */
/*  Label map (BASIC labels: `name:` at line start, GOTO/GOSUB target)*/
/* ------------------------------------------------------------------ */

static int bc_labelmap_index(const BCCompiler *cs, const char *name) {
    for (uint16_t i = 0; i < cs->labelmap_count; i++) {
        if (strcasecmp(cs->labelmap[i].name, name) == 0) return (int)i;
    }
    return -1;
}

int bc_add_labelmap_entry(BCCompiler *cs, const char *name, uint32_t offset) {
    if (bc_labelmap_index(cs, name) >= 0) {
        bc_set_error(cs, "Duplicate label '%s'", name);
        return -1;
    }
    if (cs->labelmap_count >= BC_MAX_LABELS) {
        bc_set_error(cs, "Too many labels (max %d)", BC_MAX_LABELS);
        return -1;
    }
    BCLabelMap *L = &cs->labelmap[cs->labelmap_count];
    size_t n = strlen(name);
    if (n > BC_MAX_LABEL_NAME) n = BC_MAX_LABEL_NAME;
    memcpy(L->name, name, n);
    L->name[n] = '\0';
    L->offset  = offset;
    cs->labelmap_count++;
    return 0;
}

uint32_t bc_labelmap_lookup(const BCCompiler *cs, const char *name) {
    int i = bc_labelmap_index(cs, name);
    return (i < 0) ? 0xFFFFFFFF : cs->labelmap[i].offset;
}

/* ------------------------------------------------------------------ */
/*  Nesting stack helpers                                             */
/* ------------------------------------------------------------------ */

void bc_nest_push(BCCompiler *cs, BCNestType type) {
    if (cs->nest_depth >= BC_MAX_NEST) {
        bc_set_error(cs, "Nesting too deep (max %d)", BC_MAX_NEST);
        return;
    }
    BCNestEntry *e = &cs->nest_stack[cs->nest_depth];
    memset(e, 0, sizeof(*e));
    e->type = type;
    cs->nest_depth++;
}

BCNestEntry *bc_nest_top(BCCompiler *cs) {
    if (cs->nest_depth <= 0) return NULL;
    return &cs->nest_stack[cs->nest_depth - 1];
}

BCNestEntry *bc_nest_find(BCCompiler *cs, BCNestType type) {
    for (int i = cs->nest_depth - 1; i >= 0; i--) {
        if (cs->nest_stack[i].type == type) {
            return &cs->nest_stack[i];
        }
    }
    return NULL;
}

void bc_nest_pop(BCCompiler *cs) {
    if (cs->nest_depth <= 0) {
        bc_set_error(cs, "Nesting stack underflow");
        return;
    }
    cs->nest_depth--;
}

/* ------------------------------------------------------------------ */
/*  Fixup management                                                  */
/* ------------------------------------------------------------------ */

void bc_add_fixup_line(BCCompiler *cs, uint32_t patch_addr, int target_line,
                       uint8_t size, uint8_t is_relative) {
    if (cs->fixup_count >= BC_MAX_FIXUPS) {
        bc_set_error(cs, "Too many fixups (max %d)", BC_MAX_FIXUPS);
        return;
    }
    BCFixup *f = &cs->fixups[cs->fixup_count++];
    f->patch_addr   = patch_addr;
    f->target_line  = target_line;
    f->target_label = -1;
    f->size         = size;
    f->is_relative  = is_relative;
}

void bc_add_fixup_label(BCCompiler *cs, uint32_t patch_addr, const char *name,
                        uint8_t size, uint8_t is_relative) {
    if (cs->fixup_count >= BC_MAX_FIXUPS) {
        bc_set_error(cs, "Too many fixups (max %d)", BC_MAX_FIXUPS);
        return;
    }
    /* Stash the label name in the labelmap as a placeholder offset
     * 0xFFFFFFFF if not yet defined; bc_resolve_fixups will use the
     * label_idx to look up the resolved offset. */
    int idx = bc_labelmap_index(cs, name);
    if (idx < 0) {
        if (cs->labelmap_count >= BC_MAX_LABELS) {
            bc_set_error(cs, "Too many labels (max %d)", BC_MAX_LABELS);
            return;
        }
        idx = cs->labelmap_count++;
        BCLabelMap *L = &cs->labelmap[idx];
        size_t n = strlen(name);
        if (n > BC_MAX_LABEL_NAME) n = BC_MAX_LABEL_NAME;
        memcpy(L->name, name, n);
        L->name[n] = '\0';
        L->offset  = 0xFFFFFFFF;  /* unresolved */
    }
    BCFixup *f = &cs->fixups[cs->fixup_count++];
    f->patch_addr   = patch_addr;
    f->target_line  = -1;
    f->target_label = idx;
    f->size         = size;
    f->is_relative  = is_relative;
}

void bc_resolve_fixups(BCCompiler *cs) {
    for (uint16_t i = 0; i < cs->fixup_count; i++) {
        BCFixup *f = &cs->fixups[i];

        uint32_t target;
        if (f->target_line >= 0) {
            target = bc_linemap_lookup(cs, (uint16_t)f->target_line);
            if (target == 0xFFFFFFFF) {
                bc_set_error(cs, "Undefined line number %d", f->target_line);
                return;
            }
        } else if (f->target_label >= 0 && f->target_label < cs->labelmap_count) {
            target = cs->labelmap[f->target_label].offset;
            if (target == 0xFFFFFFFF) {
                bc_set_error(cs, "Undefined label '%s'",
                             cs->labelmap[f->target_label].name);
                return;
            }
        } else {
            bc_set_error(cs, "Bad fixup");
            return;
        }

        if (f->is_relative) {
            int32_t offset = (int32_t)target - (int32_t)(f->patch_addr + f->size);
            if (f->size == 2) {
                if (offset < -32768 || offset > 32767) {
                    bc_set_error(cs, "Relative jump out of range (line %d)", f->target_line);
                    return;
                }
                bc_patch_i16(cs, f->patch_addr, (int16_t)offset);
            } else {
                bc_patch_u32(cs, f->patch_addr, (uint32_t)offset);
            }
        } else {
            if (f->size == 4) {
                bc_patch_u32(cs, f->patch_addr, target);
            } else {
                bc_patch_u16(cs, f->patch_addr, (uint16_t)target);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Variable emit helpers                                             */
/* ------------------------------------------------------------------ */

void bc_emit_load_var(BCCompiler *cs, uint16_t slot, uint8_t type, int is_local) {
    if (is_local) {
        switch (type) {
            case T_INT: bc_emit_byte(cs, OP_LOAD_LOCAL_I); break;
            case T_NBR: bc_emit_byte(cs, OP_LOAD_LOCAL_F); break;
            case T_STR: bc_emit_byte(cs, OP_LOAD_LOCAL_S); break;
            default:
                bc_set_error(cs, "Unknown type 0x%02X in load local", type);
                return;
        }
    } else {
        switch (type) {
            case T_INT: bc_emit_byte(cs, OP_LOAD_I); break;
            case T_NBR: bc_emit_byte(cs, OP_LOAD_F); break;
            case T_STR: bc_emit_byte(cs, OP_LOAD_S); break;
            default:
                bc_set_error(cs, "Unknown type 0x%02X in load global", type);
                return;
        }
    }
    bc_emit_u16(cs, slot);
}

void bc_emit_store_var(BCCompiler *cs, uint16_t slot, uint8_t type, int is_local) {
    if (is_local) {
        switch (type) {
            case T_INT: bc_emit_byte(cs, OP_STORE_LOCAL_I); break;
            case T_NBR: bc_emit_byte(cs, OP_STORE_LOCAL_F); break;
            case T_STR: bc_emit_byte(cs, OP_STORE_LOCAL_S); break;
            default:
                bc_set_error(cs, "Unknown type 0x%02X in store local", type);
                return;
        }
    } else {
        switch (type) {
            case T_INT: bc_emit_byte(cs, OP_STORE_I); break;
            case T_NBR: bc_emit_byte(cs, OP_STORE_F); break;
            case T_STR: bc_emit_byte(cs, OP_STORE_S); break;
            default:
                bc_set_error(cs, "Unknown type 0x%02X in store global", type);
                return;
        }
    }
    bc_emit_u16(cs, slot);
}

/* ------------------------------------------------------------------ */
/*  bc_skip_var — skip past a variable reference in tokenized stream  */
/* ------------------------------------------------------------------ */

unsigned char *bc_skip_var(unsigned char *p) {
    /* Skip the name characters */
    if (!isnamestart(*p)) return p;
    while (isnamechar(*p)) p++;

    /* Skip optional type suffix */
    if (*p == '%' || *p == '!' || *p == '$') p++;

    /* If followed by '(' we have array indices — skip balanced parens */
    if (*p == '(') {
        int depth = 0;
        do {
            if (*p == '(') {
                depth++;
            } else if (*p == ')') {
                depth--;
            } else if (*p == '\0') {
                /* Unterminated — bail out to avoid runaway */
                break;
            }
            p++;
        } while (depth > 0);
    }

    return p;
}
