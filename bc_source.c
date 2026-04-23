#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bc_alloc.h"
#include "bc_compiler_internal.h"
#include "bc_source.h"
#include "MMBasic.h"
#include "Memory.h"
#include "Draw.h"
#include "vm_sys_pin.h"
#include "vm_sys_file.h"

/* Maximum lines in an '!ASM block.  Heap-allocated on demand, so this is
 * the cap on buffer size rather than a per-compile cost.  128 is plenty —
 * real '!ASM blocks in this repo are <50 lines. */
#define ASM_MAX_LINES 128
#define ASM_MAX_LINE_LEN 128

typedef struct {
    int line_no;
    int fast_next_loop;   /* set by '!FAST directive, consumed by next loop */

    /* '!ASM block accumulation state.  The line buffers are ~33 KB, which is
     * far larger than the RP2040 core0 stack (2 KB), so they are heap-allocated
     * on first use inside an '!ASM block and freed at '!ENDASM.  Keeping the
     * frontend struct small keeps bc_compile_source stack-safe on rp2040. */
    int asm_active;                                /* 1 while inside '!ASM...'!ENDASM */
    int asm_line_count;
    int asm_start_line;                            /* source line of '!ASM directive */
    char (*asm_lines)[ASM_MAX_LINE_LEN];           /* [ASM_MAX_LINES] — alloc on demand */
    int  *asm_line_nos;                            /* [ASM_MAX_LINES] — alloc on demand */

    /* TYPE…END TYPE state.  In --vm mode PrepareProgramExt doesn't run (the
     * source is compiled straight into bytecode), so the compiler has to
     * populate g_structtbl itself.  We accumulate members into
     * `struct_def_inprogress` as they arrive; on END TYPE we paste the result
     * into g_structtbl[g_structcnt++].  In --interp + compare modes the
     * entries may already exist from PrepareProgramExt — InitBasic resets
     * g_structtbl, and compile-time duplicates are an upstream error. */
    int                  in_type_block;
    struct s_structdef  *struct_def_inprogress;    /* NULL outside TYPE blocks */

    /* FUNCTION foo(...) AS <struct>  (Phase 6) OR
     * SUB/FUNCTION foo(... As <struct>, ...)  (Phase 7)
     * Both cases bridge every call to the interpreter so the VM never
     * executes the body.  The flag is set when we see the opening header
     * and cleared on the matching END SUB / END FUNCTION. */
    int                  in_struct_fn;

    /* Phase 11: set during prescan if the program uses STRUCT SAVE or
     * STRUCT LOAD.  Those commands bridge to the interpreter, which has
     * its own FileTable[] — disjoint from the VM's vm_files[].  To keep
     * the file state coherent, OPEN / CLOSE / SEEK also route through the
     * bridge when this flag is set, so a single table owns all I/O for
     * the program. */
    int                  uses_struct_file_io;

    /* Phase 13: set during prescan if the program uses STRUCT EXTRACT or
     * STRUCT INSERT.  Those commands memcpy into/from a flat array
     * assuming the interpreter's contiguous `(len+1) * N` string layout,
     * but the VM stores `DIM s$(n)` as an array of `BCValue` (pointer +
     * padding) pointing to per-element 256-byte buffers.  When bridged
     * and aliased through `v->val.s = arr->data`, the interpreter walks
     * the pointer array as if it were contiguous strings and corrupts
     * memory.  When this flag is set, DIMs that allocate non-struct
     * string arrays also bridge, so the array lives in g_vartbl with
     * the contiguous layout the bridged EXTRACT/INSERT expects. */
    int                  uses_struct_extract_insert;
} BCSourceFrontend;

static int source_asm_buf_alloc(BCSourceFrontend *fe) {
    if (fe->asm_lines && fe->asm_line_nos) return 0;
    fe->asm_lines    = (char (*)[ASM_MAX_LINE_LEN])BC_ALLOC(sizeof(char) * ASM_MAX_LINES * ASM_MAX_LINE_LEN);
    fe->asm_line_nos = (int *)BC_ALLOC(sizeof(int) * ASM_MAX_LINES);
    if (!fe->asm_lines || !fe->asm_line_nos) {
        if (fe->asm_lines)    { BC_FREE(fe->asm_lines);    fe->asm_lines    = NULL; }
        if (fe->asm_line_nos) { BC_FREE(fe->asm_line_nos); fe->asm_line_nos = NULL; }
        return -1;
    }
    return 0;
}

static void source_asm_buf_free(BCSourceFrontend *fe) {
    if (fe->asm_lines)    { BC_FREE(fe->asm_lines);    fe->asm_lines    = NULL; }
    if (fe->asm_line_nos) { BC_FREE(fe->asm_line_nos); fe->asm_line_nos = NULL; }
}

int bc_opt_level = 1;

static uint8_t source_parse_expression(BCSourceFrontend *fe, BCCompiler *cs, const char **pp);
static void source_compile_statement(BCSourceFrontend *fe, BCCompiler *cs, const char *stmt);
static int source_compile_call_args(BCSourceFrontend *fe, BCCompiler *cs, const char **pp,
                                    int require_parens);
static int source_parse_array_indices(BCSourceFrontend *fe, BCCompiler *cs, const char **pp);
static void source_emit_bridge_for_stmt(BCCompiler *cs, const char *stmt);
static void source_emit_int_conversion(BCCompiler *cs, uint8_t type);
static void source_emit_syscall(BCCompiler *cs, uint16_t sysid, uint8_t argc,
                                const uint8_t *aux, uint8_t auxlen);
static void source_emit_syscall_noaux(BCCompiler *cs, uint16_t sysid, uint8_t argc);

static void source_skip_space(const char **pp) {
    while (**pp == ' ' || **pp == '\t') (*pp)++;
}

static int source_keyword(const char **pp, const char *kw) {
    const char *p = *pp;
    unsigned char next;
    size_t len = strlen(kw);
    if (strncasecmp(p, kw, len) != 0) return 0;
    next = (unsigned char)p[len];
    if (isnamechar(next) || next == '$' || next == '%' || next == '!') return 0;
    *pp = p + len;
    return 1;
}

static int source_line_empty_or_comment(const char *p) {
    source_skip_space(&p);
    return *p == '\0' || *p == '\'' ||
           (strncasecmp(p, "REM", 3) == 0 && !isnamechar((unsigned char)p[3]));
}

static void source_statement_end(BCCompiler *cs, const char *p) {
    source_skip_space(&p);
    if (*p != '\0' && *p != '\'')
        bc_set_error(cs, "Unsupported source syntax near: %.24s", p);
}

static void source_insert_byte(BCCompiler *cs, uint32_t pos, uint8_t b) {
    if (cs->code_len >= BC_MAX_CODE) {
        bc_set_error(cs, "Bytecode overflow (%d bytes)", BC_MAX_CODE);
        return;
    }
    if (pos > cs->code_len) {
        bc_set_error(cs, "Internal source compiler error");
        return;
    }
    memmove(&cs->code[pos + 1], &cs->code[pos], cs->code_len - pos);
    cs->code[pos] = b;
    cs->code_len++;
}

static void source_delete_bytes(BCCompiler *cs, uint32_t pos, uint32_t count) {
    if (count == 0) return;
    if (pos + count > cs->code_len) {
        bc_set_error(cs, "Internal source compiler error");
        return;
    }
    memmove(&cs->code[pos], &cs->code[pos + count], cs->code_len - (pos + count));
    cs->code_len -= count;
}

static int source_power_of_two_bits_i64(int64_t v) {
    int bits = 0;
    if (v <= 0) return -1;
    while ((v & 1) == 0) {
        v >>= 1;
        bits++;
    }
    return (v == 1) ? bits : -1;
}

static int source_try_fuse_mulshr(BCCompiler *cs, uint8_t left, uint8_t right,
                                  uint32_t expr_start, uint32_t right_start) {
    uint32_t mul_pos;
    uint32_t mul_len;
    int64_t divisor;
    int bits;

    if (bc_opt_level < 1) return 0;
    if (left != T_INT || right != T_INT) return 0;
    if (right_start < 1 || right_start + 9 != cs->code_len) return 0;
    if (cs->code[right_start - 1] != OP_MUL_I) return 0;
    if (cs->code[right_start] != OP_PUSH_INT) return 0;

    memcpy(&divisor, &cs->code[right_start + 1], sizeof(divisor));
    bits = source_power_of_two_bits_i64(divisor);
    if (bits < 0) return 0;

    mul_pos = right_start - 1;
    mul_len = mul_pos - expr_start;
    source_delete_bytes(cs, mul_pos, 1);
    if (cs->has_error) return 0;

    divisor = (int64_t)bits;
    cs->code[mul_pos] = OP_PUSH_INT;
    memcpy(&cs->code[mul_pos + 1], &divisor, sizeof(divisor));
    if (mul_len == 6 &&
        cs->code[expr_start] == OP_LOAD_I &&
        cs->code[expr_start + 3] == OP_LOAD_I &&
        memcmp(&cs->code[expr_start + 1], &cs->code[expr_start + 4], 2) == 0) {
        source_delete_bytes(cs, expr_start + 3, 3);
        if (cs->has_error) return 0;
        bc_emit_byte(cs, OP_MATH_SQRSHR);
        return 1;
    }
    if (mul_len == 6 &&
        cs->code[expr_start] == OP_LOAD_LOCAL_I &&
        cs->code[expr_start + 3] == OP_LOAD_LOCAL_I &&
        memcmp(&cs->code[expr_start + 1], &cs->code[expr_start + 4], 2) == 0) {
        source_delete_bytes(cs, expr_start + 3, 3);
        if (cs->has_error) return 0;
        bc_emit_byte(cs, OP_MATH_SQRSHR);
        return 1;
    }
    bc_emit_byte(cs, OP_MATH_MULSHR);
    return 1;
}

static int source_is_same_simple_int_load(BCCompiler *cs,
                                          uint32_t start_a, uint32_t end_a,
                                          uint32_t start_b, uint32_t end_b) {
    uint32_t len_a = end_a - start_a;
    uint32_t len_b = end_b - start_b;

    if (len_a != len_b) return 0;
    if (len_a == 3 &&
        cs->code[start_a] == OP_LOAD_I &&
        cs->code[start_b] == OP_LOAD_I &&
        memcmp(&cs->code[start_a + 1], &cs->code[start_b + 1], 2) == 0)
        return 1;
    if (len_a == 3 &&
        cs->code[start_a] == OP_LOAD_LOCAL_I &&
        cs->code[start_b] == OP_LOAD_LOCAL_I &&
        memcmp(&cs->code[start_a + 1], &cs->code[start_b + 1], 2) == 0)
        return 1;
    return 0;
}

static int source_try_fuse_mulshradd(BCCompiler *cs, uint8_t left, uint8_t right,
                                     uint32_t right_start, char op) {
    if (bc_opt_level < 1) return 0;
    if (op != '+') return 0;
    if (left != T_INT || right != T_INT) return 0;
    if (right_start < 1) return 0;
    if (cs->code[right_start - 1] != OP_MATH_MULSHR)
        return 0;
    source_delete_bytes(cs, right_start - 1, 1);
    if (cs->has_error) return 0;
    bc_emit_byte(cs, OP_MATH_MULSHRADD);
    return 1;
}

static int source_try_fuse_mov_assignment(BCCompiler *cs, uint32_t expr_start,
                                          uint16_t dst_slot, int dst_is_local,
                                          uint8_t vtype, uint8_t etype) {
    uint32_t expr_len = cs->code_len - expr_start;
    uint16_t src_slot;
    uint8_t mov_kind;
    uint16_t src_raw;
    uint16_t dst_raw = dst_is_local ? (uint16_t)(dst_slot | 0x8000u) : dst_slot;
    int src_is_local;

    if (bc_opt_level < 1) return 0;
    if (expr_len != 3) return 0;
    (void)etype;

    if (vtype == T_INT) {
        mov_kind = BC_MOV_INT;
        if (cs->code[expr_start] == OP_LOAD_LOCAL_I) src_is_local = 1;
        else if (cs->code[expr_start] == OP_LOAD_I) src_is_local = 0;
        else return 0;
    } else if (vtype == T_NBR) {
        mov_kind = BC_MOV_FLT;
        if (cs->code[expr_start] == OP_LOAD_LOCAL_F) src_is_local = 1;
        else if (cs->code[expr_start] == OP_LOAD_F) src_is_local = 0;
        else return 0;
    } else if (vtype == T_STR) {
        mov_kind = BC_MOV_STR;
        if (cs->code[expr_start] == OP_LOAD_LOCAL_S) src_is_local = 1;
        else if (cs->code[expr_start] == OP_LOAD_S) src_is_local = 0;
        else return 0;
    } else {
        return 0;
    }

    memcpy(&src_slot, &cs->code[expr_start + 1], sizeof(src_slot));
    src_raw = src_is_local ? (uint16_t)(src_slot | 0x8000u) : src_slot;

    source_delete_bytes(cs, expr_start, expr_len);
    if (cs->has_error) return 0;
    bc_emit_byte(cs, OP_MOV_VAR);
    bc_emit_byte(cs, mov_kind);
    bc_emit_u16(cs, src_raw);
    bc_emit_u16(cs, dst_raw);
    return 1;
}

static uint8_t source_jcmp_relation(uint8_t compare_op, uint8_t branch_op, uint8_t *jcmp_op) {
    if (branch_op != OP_JZ && branch_op != OP_JNZ) return 0;
    switch (compare_op) {
        case OP_EQ_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_EQ : BC_JCMP_NE;
        case OP_NE_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_NE : BC_JCMP_EQ;
        case OP_LT_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_LT : BC_JCMP_GE;
        case OP_GT_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_GT : BC_JCMP_LE;
        case OP_LE_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_LE : BC_JCMP_GT;
        case OP_GE_I: *jcmp_op = OP_JCMP_I; return (branch_op == OP_JNZ) ? BC_JCMP_GE : BC_JCMP_LT;
        case OP_EQ_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_EQ : BC_JCMP_NE;
        case OP_NE_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_NE : BC_JCMP_EQ;
        case OP_LT_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_LT : BC_JCMP_GE;
        case OP_GT_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_GT : BC_JCMP_LE;
        case OP_LE_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_LE : BC_JCMP_GT;
        case OP_GE_F: *jcmp_op = OP_JCMP_F; return (branch_op == OP_JNZ) ? BC_JCMP_GE : BC_JCMP_LT;
        default:      return 0;
    }
}

static uint32_t source_emit_jmp_placeholder(BCCompiler *cs, uint8_t opcode) {
    uint8_t rel;
    uint8_t jcmp_op;
    if (bc_opt_level >= 1 && cs->code_len > 0 &&
        (rel = source_jcmp_relation(cs->code[cs->code_len - 1], opcode, &jcmp_op)) != 0) {
        source_delete_bytes(cs, cs->code_len - 1, 1);
        if (cs->has_error) return 0;
        bc_emit_byte(cs, jcmp_op);
        bc_emit_byte(cs, rel);
        uint32_t patch = cs->code_len;
        bc_emit_i16(cs, 0);
        return patch;
    }
    bc_emit_byte(cs, opcode);
    uint32_t patch = cs->code_len;
    bc_emit_i16(cs, 0);
    return patch;
}

static void source_patch_jmp_here(BCCompiler *cs, uint32_t patch_addr) {
    bc_patch_i16(cs, patch_addr, (int16_t)(cs->code_len - (patch_addr + 2)));
}

static void source_emit_rel_jump(BCCompiler *cs, uint8_t opcode, uint32_t target_addr) {
    uint8_t rel;
    uint8_t jcmp_op;
    if (bc_opt_level >= 1 && cs->code_len > 0 &&
        (rel = source_jcmp_relation(cs->code[cs->code_len - 1], opcode, &jcmp_op)) != 0) {
        source_delete_bytes(cs, cs->code_len - 1, 1);
        if (cs->has_error) return;
        bc_emit_byte(cs, jcmp_op);
        bc_emit_byte(cs, rel);
        bc_emit_i16(cs, (int16_t)(target_addr - (cs->code_len + 2)));
        return;
    }
    bc_emit_byte(cs, opcode);
    bc_emit_i16(cs, (int16_t)(target_addr - (cs->code_len + 2)));
}

static int source_parse_line_number(const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (!isdigit((unsigned char)*p)) return -1;
    int num = 0;
    while (isdigit((unsigned char)*p)) {
        num = num * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return num;
}

static void source_emit_abs_jump(BCCompiler *cs, uint8_t opcode, int lineno) {
    bc_emit_byte(cs, opcode);
    uint32_t patch = cs->code_len;
    bc_emit_u32(cs, 0);
    uint32_t target = bc_linemap_lookup(cs, (uint16_t)lineno);
    if (target != 0xFFFFFFFF)
        bc_patch_u32(cs, patch, target);
    else
        bc_add_fixup_line(cs, patch, lineno, 4, 0);
}

static int source_get_or_create_subfun(BCCompiler *cs, const char *name,
                                       int name_len, uint8_t return_type) {
    int idx = bc_find_subfun(cs, name, name_len);
    if (idx >= 0) {
        cs->subfuns[idx].return_type = return_type;
        return idx;
    }
    if (cs->subfun_count >= BC_MAX_SUBFUNS) {
        bc_set_error(cs, "Too many SUB/FUNCTION definitions");
        return -1;
    }
    idx = cs->subfun_count++;
    int copy_len = name_len > MAXVARLEN ? MAXVARLEN : name_len;
    memcpy(cs->subfuns[idx].name, name, copy_len);
    cs->subfuns[idx].name[copy_len] = '\0';
    cs->subfuns[idx].return_type = return_type;
    return idx;
}

static int source_parse_varname(const char **pp, char *name, int *name_len, uint8_t *type) {
    const char *p = *pp;
    source_skip_space(&p);
    if (!isnamestart((unsigned char)*p)) return 0;

    int len = 0;
    while (isnamechar((unsigned char)p[len]) && len < MAXVARLEN) {
        name[len] = p[len];
        len++;
    }
    while (isnamechar((unsigned char)p[len])) len++;

    *type = 0;
    if (p[len] == '%' || p[len] == '!' || p[len] == '$') {
        *type = bc_type_from_suffix(p[len]);
        if (len < MAXVARLEN) name[len] = p[len];
        len++;
    }

    int copy_len = len > MAXVARLEN ? MAXVARLEN : len;
    name[copy_len] = '\0';
    *name_len = copy_len;
    *pp = p + len;
    return 1;
}

// LHS classifier — walks a struct-reference prefix WITHOUT emitting bytecode.
// Returns 1 if the prefix parses as a whole-struct reference (scalar struct,
// array element of struct, or chain terminating at a T_STRUCT member).  Does
// not advance the caller's parse state.  Used by the assignment dispatcher
// to bridge struct-to-struct stores to the interpreter.
static int source_lhs_is_whole_struct(BCCompiler *cs, const char *stmt_line) {
    const char *p = stmt_line;
    source_skip_space(&p);
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t stype = 0;
    const char *probe = p;
    if (!source_parse_varname(&probe, name, &name_len, &stype)) return 0;

    const char *dot = memchr(name, '.', name_len);
    int baselen = dot ? (int)(dot - name) : name_len;
    if (baselen == 0) return 0;
    uint16_t slot = bc_find_slot(cs, name, baselen);
    if (slot == 0xFFFF) return 0;
    if (cs->slots[slot].type != T_STRUCT) return 0;

    int current_idx = cs->slots[slot].struct_idx;
    if (current_idx < 0 || current_idx >= g_structcnt ||
        g_structtbl[current_idx] == NULL) return 0;

    int terminal_m_type = T_STRUCT;                 // slot-level access is whole-struct
    const char *dtp = dot ? (dot + 1) : NULL;
    int dtp_rem = dot ? (name_len - baselen - 1) : 0;
    const char *q = probe;                          // parse tail position

    // Optional outer `(…)` subscript after slot name (when slot is array-of-struct).
    source_skip_space(&q);
    if (*q == '(' && dtp_rem == 0) {
        int depth = 0;
        do {
            if (*q == '(') depth++;
            else if (*q == ')') depth--;
            else if (*q == 0) return 0;
            q++;
        } while (depth > 0);
        // Terminal is still whole-struct (array element).
    }

    int depth = 0;
    while (1) {
        if (++depth > MAX_STRUCT_NEST_DEPTH) return 0;

        // Consume leading '.' if present.
        unsigned char mname[MAXVARLEN + 1];
        int mlen = 0;
        if (dtp_rem > 0) {
            if (*dtp == '.') { dtp++; dtp_rem--; }
            while (dtp_rem > 0 && mlen < MAXVARLEN) {
                char c = *dtp;
                if (c == '.' || c == '(') break;
                mname[mlen++] = (unsigned char)mytoupper(c);
                dtp++; dtp_rem--;
            }
            if (mlen > 0 && (mname[mlen-1] == '$' || mname[mlen-1] == '%' || mname[mlen-1] == '!'))
                mlen--;
        } else {
            source_skip_space(&q);
            if (*q != '.') break;                   // no more segments
            q++;
            source_skip_space(&q);
            while (isnamechar((unsigned char)*q) && *q != '.' && mlen < MAXVARLEN) {
                mname[mlen++] = (unsigned char)mytoupper(*q++);
            }
            if (*q == '$' || *q == '%' || *q == '!') q++;
        }
        if (mlen == 0) break;
        mname[mlen] = 0;

        int m_type = 0, m_offset = 0, m_size = 0;
        short m_dims[MAXDIM];
        int midx = FindStructMember(current_idx, mname,
                                    &m_type, &m_offset, &m_size, m_dims);
        if (midx < 0) return 0;
        terminal_m_type = m_type;

        // Skip any trailing `(…)` (we don't evaluate; just brace-match).
        source_skip_space(&q);
        if (*q == '(' && m_dims[0] != 0) {
            int d = 0;
            do {
                if (*q == '(') d++;
                else if (*q == ')') d--;
                else if (*q == 0) return 0;
                q++;
            } while (d > 0);
        }

        // More to resolve?
        if (dtp_rem > 0 && *dtp == '.') continue;
        source_skip_space(&q);
        if (*q == '.') continue;
        break;
    }

    source_skip_space(&q);
    if (*q != '=') return 0;
    return terminal_m_type == T_STRUCT;
}

// Struct-chain walker — compiles `a.b.c`, `a(i).b.c`, `a.b(j).c(k)`, and the
// fully-general `a(i).b(j).c(k).d` into the appropriate opcode:
//   * All-scalar chain            → OP_LOAD/STORE_STRUCT_FIELD_*
//   * Outer-index-only chain      → OP_LOAD/STORE_STRUCT_ELEM_*
//   * Any intermediate `(idx)`    → OP_LOAD/STORE_STRUCT_NESTED_*  (Phase 4)
//
// The walker emits each `(idx)` expression inline as it encounters the segment
// so runtime indices end up on the stack in the order the NESTED opcode expects
// (outermost outer index first, then innermost-nested-first for pops).

#define CHAIN_MAX_NESTED 8

typedef struct {
    int    is_indexed;          /* 1 if `(idx)` followed this member */
    int    const_off;            /* offset added to base before indexing */
    int    stride;               /* sizeof(elem) when indexed */
    int    m_type;               /* T_INT/T_NBR/T_STR/T_STRUCT */
    int    m_size;               /* member size (struct-idx for T_STRUCT, len for T_STR) */
} ChainSeg;

// Walk the `.member[(idx)]…` tail starting from `current_struct_idx`.
// Returns 0 on error (bc_set_error called), 1 on success.
// On return:
//   *leaf_type  = terminal member's storage type (T_INT/T_NBR/T_STR)
//   *leaf_size  = terminal member's .size field (string maxlen or struct idx)
//   *nested_nseg = count of indexed segments (emitted index exprs are on stack)
//   nested_off[i], nested_stride[i] = per-nested-segment const+stride (i < *nested_nseg)
//   *scalar_total_offset = offset to add to the base pointer for all-scalar chains
//      (only meaningful when *nested_nseg == 0, else caller uses nested segs + final_offset)
//   *final_offset = trailing scalar offset after the last indexed segment
//      (only meaningful when *nested_nseg > 0)
static int source_walk_struct_tail(BCSourceFrontend *fe, BCCompiler *cs,
                                   int current_struct_idx,
                                   const char *dotted_tail, int dotted_tail_len,
                                   const char **pp,
                                   int *leaf_type, int *leaf_size,
                                   int *nested_nseg,
                                   int nested_off[CHAIN_MAX_NESTED],
                                   int nested_stride[CHAIN_MAX_NESTED],
                                   int *scalar_total_offset,
                                   int *final_offset) {
    const char *dtp = dotted_tail;
    int dtp_rem = dotted_tail_len;
    const char *p = *pp;
    int accum_off = 0;     // accumulating const offset for the current (pre-index) run
    int nseg = 0;
    int depth = 0;
    int term_type = 0;
    int term_size = 0;

    while (1) {
        if (++depth > MAX_STRUCT_NEST_DEPTH) {
            bc_set_error(cs, "Structure nesting too deep");
            return 0;
        }

        // Pull the next member name.  Read first from dotted_tail (the member
        // path captured by the tokenizer — dots included) while characters
        // remain; fall through to the parse stream once it's exhausted.  We
        // always consume the leading '.' here so the "more to resolve" block
        // below can leave the '.' un-consumed and stay symmetric.
        unsigned char mname[MAXVARLEN + 1];
        int mlen = 0;
        if (dtp_rem > 0) {
            if (*dtp == '.') { dtp++; dtp_rem--; }      // eat any leading dot
            while (dtp_rem > 0 && mlen < MAXVARLEN) {
                char c = *dtp;
                if (c == '.' || c == '(') break;
                mname[mlen++] = (unsigned char)mytoupper(c);
                dtp++; dtp_rem--;
            }
            if (mlen > 0 && (mname[mlen-1] == '$' || mname[mlen-1] == '%' || mname[mlen-1] == '!')) {
                mlen--;
            }
        } else {
            source_skip_space(&p);
            if (*p == '.') p++;
            source_skip_space(&p);
            // `isnamechar` also matches `.`, so read one segment only.
            while (isnamechar((unsigned char)*p) && *p != '.' && mlen < MAXVARLEN) {
                mname[mlen++] = (unsigned char)mytoupper(*p++);
            }
            if (*p == '$' || *p == '%' || *p == '!') p++;
        }
        mname[mlen] = 0;
        if (mlen == 0) {
            bc_set_error(cs, "Empty struct member name");
            return 0;
        }

        int m_type = 0, m_offset = 0, m_size = 0;
        short m_dims[MAXDIM];
        int midx = FindStructMember(current_struct_idx, mname,
                                    &m_type, &m_offset, &m_size, m_dims);
        if (midx < 0) {
            bc_set_error(cs, "Unknown struct member: %s", (const char *)mname);
            return 0;
        }

        // Is this member indexed in the source?  Either "(idx)" in the parse
        // stream (most common), or we've exhausted the dotted_tail and the
        // tail now has `(`.
        int indexed = 0;
        {
            const char *q;
            if (dtp_rem > 0 && *dtp == '(') {
                // Shouldn't happen — tokenizer would have stopped before `(`.
                // Guard anyway.
                bc_set_error(cs, "Index in struct path prefix not supported");
                return 0;
            }
            q = p;
            source_skip_space(&q);
            if (*q == '(' && m_dims[0] != 0) indexed = 1;
        }

        if (indexed) {
            if (nseg >= CHAIN_MAX_NESTED) {
                bc_set_error(cs, "Too many nested struct indices (max %d)", CHAIN_MAX_NESTED);
                return 0;
            }
            // Parse and emit index expressions — expected one per dim.
            int expected_dims = 0;
            for (int di = 0; di < MAXDIM && m_dims[di] != 0; di++) expected_dims++;
            if (expected_dims != 1) {
                // Multi-dim nested-array members aren't supported in this phase
                // (acceptance tests 51-54 only use 1-D nested arrays).  Fall
                // through to error so we know if a program needs it.
                bc_set_error(cs, "Multi-dim nested struct member array not yet supported");
                return 0;
            }
            source_skip_space(&p);
            if (*p != '(') {
                bc_set_error(cs, "Expected '(' for struct member array index");
                return 0;
            }
            p++;
            uint8_t ix_type = source_parse_expression(fe, cs, &p);
            if (cs->has_error) return 0;
            source_emit_int_conversion(cs, ix_type);
            source_skip_space(&p);
            if (*p != ')') {
                bc_set_error(cs, "Expected ')' for struct member array index");
                return 0;
            }
            p++;

            int elem_sz;
            if (m_type == T_STRUCT) {
                if (m_size < 0 || m_size >= g_structcnt || g_structtbl[m_size] == NULL) {
                    bc_set_error(cs, "Invalid nested struct type");
                    return 0;
                }
                elem_sz = g_structtbl[m_size]->total_size;
            } else if (m_type == T_STR) {
                elem_sz = m_size + 1;
            } else {
                elem_sz = m_size;
            }
            nested_off[nseg]    = accum_off + m_offset;
            nested_stride[nseg] = elem_sz;
            nseg++;
            accum_off = 0;
        } else {
            accum_off += m_offset;
            if (m_dims[0] != 0 && m_type != T_STRUCT) {
                bc_set_error(cs, "Array member requires index");
                return 0;
            }
        }

        // Decide: continue descending, or stop here?  Leave the '.' in place —
        // the iter-start block at the top of the loop consumes it.
        int more = 0;
        if (dtp_rem > 0 && *dtp == '.') {
            more = 1;
        } else {
            const char *q = p;
            source_skip_space(&q);
            if (*q == '.') {
                p = q;
                more = 1;
            }
        }

        if (more) {
            if (m_type != T_STRUCT) {
                bc_set_error(cs, "Not a nested structure");
                return 0;
            }
            current_struct_idx = m_size;
            continue;
        }

        // Terminal.
        if (m_type == T_STRUCT) {
            bc_set_error(cs, "Whole-struct read/write not yet supported");
            return 0;
        }
        term_type = m_type;
        term_size = m_size;
        break;
    }

    *pp = p;
    *leaf_type = term_type;
    *leaf_size = term_size;
    *nested_nseg = nseg;
    if (nseg == 0) {
        *scalar_total_offset = accum_off;
        *final_offset = 0;
    } else {
        *scalar_total_offset = 0;
        *final_offset = accum_off;
    }
    return 1;
}

// Emit the right load opcode for a struct chain.  `slot` is the base slot,
// outer_ndim is the number of outer-array indices already on the stack (0 if
// outer is a scalar struct).  Returns the terminal member type on success.
static uint8_t source_emit_struct_chain_load(BCSourceFrontend *fe, BCCompiler *cs,
                                             uint16_t slot, int outer_ndim,
                                             const char *dotted_tail, int dotted_tail_len,
                                             const char **pp) {
    int sidx = cs->slots[slot].struct_idx;
    if (sidx < 0 || sidx >= g_structcnt || g_structtbl[sidx] == NULL) {
        bc_set_error(cs, "Invalid struct type on slot");
        return 0;
    }
    int leaf_type = 0, leaf_size = 0, nseg = 0;
    int nested_off[CHAIN_MAX_NESTED];
    int nested_stride[CHAIN_MAX_NESTED];
    int scalar_off = 0, final_off = 0;
    if (!source_walk_struct_tail(fe, cs, sidx, dotted_tail, dotted_tail_len,
                                 pp, &leaf_type, &leaf_size, &nseg,
                                 nested_off, nested_stride,
                                 &scalar_off, &final_off))
        return 0;

    if (nseg == 0 && outer_ndim == 0) {
        uint8_t op = (leaf_type == T_INT) ? OP_LOAD_STRUCT_FIELD_I :
                     (leaf_type == T_NBR) ? OP_LOAD_STRUCT_FIELD_F :
                                            OP_LOAD_STRUCT_FIELD_S;
        bc_emit_byte(cs, op);
        bc_emit_u16(cs, slot);
        bc_emit_u16(cs, (uint16_t)scalar_off);
        return (uint8_t)leaf_type;
    }

    if (nseg == 0) {
        // Outer indexing only — ELEM opcode.
        int elem_size = g_structtbl[sidx]->total_size;
        uint8_t op = (leaf_type == T_INT) ? OP_LOAD_STRUCT_ELEM_I :
                     (leaf_type == T_NBR) ? OP_LOAD_STRUCT_ELEM_F :
                                            OP_LOAD_STRUCT_ELEM_S;
        bc_emit_byte(cs, op);
        bc_emit_u16(cs, slot);
        bc_emit_u16(cs, (uint16_t)scalar_off);
        bc_emit_u16(cs, (uint16_t)elem_size);
        bc_emit_byte(cs, (uint8_t)outer_ndim);
        return (uint8_t)leaf_type;
    }

    // Nested: NESTED opcode.
    int outer_stride = g_structtbl[sidx]->total_size;
    uint8_t op = (leaf_type == T_INT) ? OP_LOAD_STRUCT_NESTED_I :
                 (leaf_type == T_NBR) ? OP_LOAD_STRUCT_NESTED_F :
                                        OP_LOAD_STRUCT_NESTED_S;
    bc_emit_byte(cs, op);
    bc_emit_u16(cs, slot);
    bc_emit_byte(cs, (uint8_t)outer_ndim);
    bc_emit_byte(cs, (uint8_t)nseg);
    bc_emit_u16(cs, (uint16_t)outer_stride);
    for (int i = 0; i < nseg; i++) {
        bc_emit_u16(cs, (uint16_t)nested_off[i]);
        bc_emit_u16(cs, (uint16_t)nested_stride[i]);
    }
    bc_emit_u16(cs, (uint16_t)final_off);
    return (uint8_t)leaf_type;
}

// Emit the right store opcode.  Caller emits the RHS expression AFTER the
// indices so the value is on the top of stack (nested/outer indices below it).
// NOTE: the walker emits index expressions while executing; callers must
// therefore invoke this walker BEFORE evaluating the RHS.
static int source_emit_struct_chain_store(BCSourceFrontend *fe, BCCompiler *cs,
                                          uint16_t slot, int outer_ndim,
                                          const char *dotted_tail, int dotted_tail_len,
                                          const char **pp,
                                          int *leaf_type_out, int *leaf_size_out,
                                          int *scalar_off_out, int *final_off_out,
                                          int *nseg_out,
                                          int nested_off_out[CHAIN_MAX_NESTED],
                                          int nested_stride_out[CHAIN_MAX_NESTED]) {
    int sidx = cs->slots[slot].struct_idx;
    if (sidx < 0 || sidx >= g_structcnt || g_structtbl[sidx] == NULL) {
        bc_set_error(cs, "Invalid struct type on slot");
        return 0;
    }
    int leaf_type = 0, leaf_size = 0, nseg = 0;
    int scalar_off = 0, final_off = 0;
    if (!source_walk_struct_tail(fe, cs, sidx, dotted_tail, dotted_tail_len,
                                 pp, &leaf_type, &leaf_size, &nseg,
                                 nested_off_out, nested_stride_out,
                                 &scalar_off, &final_off))
        return 0;
    *leaf_type_out   = leaf_type;
    *leaf_size_out   = leaf_size;
    *scalar_off_out  = scalar_off;
    *final_off_out   = final_off;
    *nseg_out        = nseg;
    (void)outer_ndim;  // stored by caller when emitting opcode
    (void)slot;
    return 1;
}

// Finalise the store opcode emission after the RHS expression has been parsed
// and pushed to the stack.
static void source_emit_struct_chain_store_finish(BCCompiler *cs, uint16_t slot,
                                                  int outer_ndim, int leaf_type,
                                                  int leaf_size, int scalar_off,
                                                  int final_off, int nseg,
                                                  const int *nested_off,
                                                  const int *nested_stride) {
    if (nseg == 0 && outer_ndim == 0) {
        uint8_t op = (leaf_type == T_INT) ? OP_STORE_STRUCT_FIELD_I :
                     (leaf_type == T_NBR) ? OP_STORE_STRUCT_FIELD_F :
                                            OP_STORE_STRUCT_FIELD_S;
        bc_emit_byte(cs, op);
        bc_emit_u16(cs, slot);
        bc_emit_u16(cs, (uint16_t)scalar_off);
        if (op == OP_STORE_STRUCT_FIELD_S) bc_emit_u16(cs, (uint16_t)leaf_size);
        return;
    }
    int sidx = cs->slots[slot].struct_idx;
    int outer_stride = g_structtbl[sidx]->total_size;
    if (nseg == 0) {
        uint8_t op = (leaf_type == T_INT) ? OP_STORE_STRUCT_ELEM_I :
                     (leaf_type == T_NBR) ? OP_STORE_STRUCT_ELEM_F :
                                            OP_STORE_STRUCT_ELEM_S;
        bc_emit_byte(cs, op);
        bc_emit_u16(cs, slot);
        bc_emit_u16(cs, (uint16_t)scalar_off);
        bc_emit_u16(cs, (uint16_t)outer_stride);
        bc_emit_byte(cs, (uint8_t)outer_ndim);
        if (op == OP_STORE_STRUCT_ELEM_S) bc_emit_u16(cs, (uint16_t)leaf_size);
        return;
    }
    uint8_t op = (leaf_type == T_INT) ? OP_STORE_STRUCT_NESTED_I :
                 (leaf_type == T_NBR) ? OP_STORE_STRUCT_NESTED_F :
                                        OP_STORE_STRUCT_NESTED_S;
    bc_emit_byte(cs, op);
    bc_emit_u16(cs, slot);
    bc_emit_byte(cs, (uint8_t)outer_ndim);
    bc_emit_byte(cs, (uint8_t)nseg);
    bc_emit_u16(cs, (uint16_t)outer_stride);
    for (int i = 0; i < nseg; i++) {
        bc_emit_u16(cs, (uint16_t)nested_off[i]);
        bc_emit_u16(cs, (uint16_t)nested_stride[i]);
    }
    bc_emit_u16(cs, (uint16_t)final_off);
    if (op == OP_STORE_STRUCT_NESTED_S) bc_emit_u16(cs, (uint16_t)leaf_size);
}

// Load-side entry for the scalar-dot path: name contains `.`, no outer indices.
static int source_try_emit_struct_field_load(BCSourceFrontend *fe, BCCompiler *cs,
                                             const char *name, int name_len,
                                             const char **pp, uint8_t *type_out) {
    const char *dot = memchr(name, '.', name_len);
    if (!dot) return 0;
    int baselen  = (int)(dot - name);
    if (baselen == 0) return 0;
    uint16_t slot = bc_find_slot(cs, name, baselen);
    if (slot == 0xFFFF) return 0;
    if (cs->slots[slot].type != T_STRUCT) return 0;

    int tail_len = name_len - baselen - 1;
    uint8_t t = source_emit_struct_chain_load(fe, cs, slot, /*outer_ndim=*/0,
                                              dot + 1, tail_len, pp);
    if (cs->has_error) return 1;
    *type_out = t;
    return 1;
}

// Store-side entry for the scalar-dot path.  Caller has not yet parsed the RHS;
// this walker emits index expressions first, then caller parses RHS, then the
// caller calls the _finish helper.  Returns 2 on walker success, 1 on error
// already reported, 0 if not a struct reference.
static int source_try_begin_struct_field_store(BCSourceFrontend *fe, BCCompiler *cs,
                                               const char *name, int name_len,
                                               const char **pp,
                                               uint16_t *slot_out,
                                               int *leaf_type_out, int *leaf_size_out,
                                               int *scalar_off_out, int *final_off_out,
                                               int *nseg_out,
                                               int nested_off_out[CHAIN_MAX_NESTED],
                                               int nested_stride_out[CHAIN_MAX_NESTED]) {
    const char *dot = memchr(name, '.', name_len);
    if (!dot) return 0;
    int baselen = (int)(dot - name);
    if (baselen == 0) return 0;
    uint16_t slot = bc_find_slot(cs, name, baselen);
    if (slot == 0xFFFF) return 0;
    if (cs->slots[slot].type != T_STRUCT) return 0;

    int tail_len = name_len - baselen - 1;
    if (!source_emit_struct_chain_store(fe, cs, slot, /*outer_ndim=*/0,
                                        dot + 1, tail_len, pp,
                                        leaf_type_out, leaf_size_out,
                                        scalar_off_out, final_off_out,
                                        nseg_out, nested_off_out, nested_stride_out))
        return 1;
    *slot_out = slot;
    return 2;
}

static uint16_t source_resolve_global(BCCompiler *cs, const char *name, int name_len,
                                      uint8_t type, int create) {
    uint16_t slot = bc_find_slot(cs, name, name_len);
    if (slot != 0xFFFF) return slot;
    if (!create) return 0xFFFF;
    return bc_add_slot(cs, name, name_len, type, 0);
}

static uint16_t source_resolve_var(BCCompiler *cs, const char *name, int name_len,
                                   uint8_t type, int create, int *is_local) {
    *is_local = 0;
    if (cs->current_subfun >= 0) {
        int loc = bc_find_local(cs, name, name_len);
        if (loc >= 0) {
            *is_local = 1;
            return (uint16_t)loc;
        }
    }
    return source_resolve_global(cs, name, name_len, type, create);
}

static uint8_t source_default_var_type(BCCompiler *cs, const char *name, int name_len) {
    if (cs->current_subfun >= 0) {
        int loc = bc_find_local(cs, name, name_len);
        if (loc >= 0) return cs->locals[loc].type;
    }
    uint16_t slot = bc_find_slot(cs, name, name_len);
    return slot == 0xFFFF ? T_NBR : cs->slots[slot].type;
}

static uint16_t source_alloc_hidden_slot(BCCompiler *cs, uint8_t type) {
    char buf[MAXVARLEN + 1];
    snprintf(buf, sizeof(buf), "#SRC_%u", (unsigned)cs->next_hidden_slot++);
    return bc_add_slot(cs, buf, (int)strlen(buf), type, 0);
}

/* Allocate a hidden local slot (for FOR limit/step inside SUB/FUNCTION) */
static uint16_t source_alloc_hidden_local(BCCompiler *cs, uint8_t type) {
    char buf[MAXVARLEN + 1];
    snprintf(buf, sizeof(buf), "#SRC_%u", (unsigned)cs->next_hidden_slot++);
    int idx = bc_add_local(cs, buf, (int)strlen(buf), type, 0);
    if (idx < 0) return 0;
    return (uint16_t)idx;
}

static int source_color_name(const char *start, const char *end, int *color) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
#define SOURCE_RGB_NAME(name, value) \
    if (end - start == (int)strlen(name) && strncasecmp(start, name, strlen(name)) == 0) { \
        *color = (int)(value); \
        return 1; \
    }
    SOURCE_RGB_NAME("WHITE", WHITE)
    SOURCE_RGB_NAME("YELLOW", YELLOW)
    SOURCE_RGB_NAME("LILAC", LILAC)
    SOURCE_RGB_NAME("BROWN", BROWN)
    SOURCE_RGB_NAME("FUCHSIA", FUCHSIA)
    SOURCE_RGB_NAME("RUST", RUST)
    SOURCE_RGB_NAME("MAGENTA", MAGENTA)
    SOURCE_RGB_NAME("RED", RED)
    SOURCE_RGB_NAME("CYAN", CYAN)
    SOURCE_RGB_NAME("GREEN", GREEN)
    SOURCE_RGB_NAME("CERULEAN", CERULEAN)
    SOURCE_RGB_NAME("MIDGREEN", MIDGREEN)
    SOURCE_RGB_NAME("COBALT", COBALT)
    SOURCE_RGB_NAME("MYRTLE", MYRTLE)
    SOURCE_RGB_NAME("BLUE", BLUE)
    SOURCE_RGB_NAME("BLACK", BLACK)
    SOURCE_RGB_NAME("GRAY", GRAY)
    SOURCE_RGB_NAME("GREY", GRAY)
    SOURCE_RGB_NAME("LIGHTGRAY", LITEGRAY)
    SOURCE_RGB_NAME("LIGHTGREY", LITEGRAY)
    SOURCE_RGB_NAME("ORANGE", ORANGE)
    SOURCE_RGB_NAME("PINK", PINK)
    SOURCE_RGB_NAME("GOLD", GOLD)
    SOURCE_RGB_NAME("SALMON", SALMON)
    SOURCE_RGB_NAME("BEIGE", BEIGE)
#undef SOURCE_RGB_NAME
    return 0;
}

static int source_parse_setpin_mode(const char **pp, int *mode) {
    const char *p = *pp;
    source_skip_space(&p);
    if (source_keyword(&p, "OFF")) {
        *mode = VM_PIN_MODE_OFF;
    } else if (source_keyword(&p, "DIN")) {
        *mode = VM_PIN_MODE_DIN;
    } else if (source_keyword(&p, "DOUT")) {
        *mode = VM_PIN_MODE_DOUT;
    } else if (source_keyword(&p, "ARAW")) {
        *mode = VM_PIN_MODE_ARAW;
    } else if (source_keyword(&p, "PWM0A")) {
        *mode = VM_PIN_MODE_PWM0A;
    } else if (source_keyword(&p, "PWM0B")) {
        *mode = VM_PIN_MODE_PWM0B;
    } else if (source_keyword(&p, "PWM1A")) {
        *mode = VM_PIN_MODE_PWM1A;
    } else if (source_keyword(&p, "PWM1B")) {
        *mode = VM_PIN_MODE_PWM1B;
    } else if (source_keyword(&p, "PWM2A")) {
        *mode = VM_PIN_MODE_PWM2A;
    } else if (source_keyword(&p, "PWM2B")) {
        *mode = VM_PIN_MODE_PWM2B;
    } else if (source_keyword(&p, "PWM3A")) {
        *mode = VM_PIN_MODE_PWM3A;
    } else if (source_keyword(&p, "PWM3B")) {
        *mode = VM_PIN_MODE_PWM3B;
    } else if (source_keyword(&p, "PWM4A")) {
        *mode = VM_PIN_MODE_PWM4A;
    } else if (source_keyword(&p, "PWM4B")) {
        *mode = VM_PIN_MODE_PWM4B;
    } else if (source_keyword(&p, "PWM5A")) {
        *mode = VM_PIN_MODE_PWM5A;
    } else if (source_keyword(&p, "PWM5B")) {
        *mode = VM_PIN_MODE_PWM5B;
    } else if (source_keyword(&p, "PWM6A")) {
        *mode = VM_PIN_MODE_PWM6A;
    } else if (source_keyword(&p, "PWM6B")) {
        *mode = VM_PIN_MODE_PWM6B;
    } else if (source_keyword(&p, "PWM7A")) {
        *mode = VM_PIN_MODE_PWM7A;
    } else if (source_keyword(&p, "PWM7B")) {
        *mode = VM_PIN_MODE_PWM7B;
    /* PWM8A..PWM11B are rp2350-only hardware slices; the keywords are
     * always accepted so parser / FRUN are portable, but VM setpin
     * errors on rp2040 if the program actually drives them. */
    } else if (source_keyword(&p, "PWM8A")) {
        *mode = VM_PIN_MODE_PWM8A;
    } else if (source_keyword(&p, "PWM8B")) {
        *mode = VM_PIN_MODE_PWM8B;
    } else if (source_keyword(&p, "PWM9A")) {
        *mode = VM_PIN_MODE_PWM9A;
    } else if (source_keyword(&p, "PWM9B")) {
        *mode = VM_PIN_MODE_PWM9B;
    } else if (source_keyword(&p, "PWM10A")) {
        *mode = VM_PIN_MODE_PWM10A;
    } else if (source_keyword(&p, "PWM10B")) {
        *mode = VM_PIN_MODE_PWM10B;
    } else if (source_keyword(&p, "PWM11A")) {
        *mode = VM_PIN_MODE_PWM11A;
    } else if (source_keyword(&p, "PWM11B")) {
        *mode = VM_PIN_MODE_PWM11B;
    } else if (source_keyword(&p, "PWM")) {
        *mode = -1;
    } else if (*p == '0' && !isnamechar((unsigned char)p[1])) {
        *mode = VM_PIN_MODE_OFF;
        p++;
    } else {
        return 0;
    }
    *pp = p;
    return 1;
}

static int source_try_emit_gp_pin(BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (strncasecmp(p, "GP", 2) != 0 || !isdigit((unsigned char)p[2]))
        return 0;

    const char *digits = p + 2;
    char *end = NULL;
    long gpio = strtol(digits, &end, 10);
    if (end == digits || isnamechar((unsigned char)*end))
        return 0;

    bc_emit_byte(cs, OP_PUSH_INT);
    bc_emit_i64(cs, -(int64_t)gpio - 1);
    *pp = end;
    return 1;
}

static void source_compile_pin_operand(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    if (source_try_emit_gp_pin(cs, pp))
        return;
    uint8_t type = source_parse_expression(fe, cs, pp);
    source_emit_int_conversion(cs, type);
}

static void source_emit_store_converted(BCCompiler *cs, uint16_t slot,
                                        uint8_t vtype, uint8_t etype,
                                        int is_local) {
    if ((vtype & T_INT) && (etype & T_NBR)) bc_emit_byte(cs, OP_CVT_F2I);
    else if ((vtype & T_NBR) && (etype & T_INT)) bc_emit_byte(cs, OP_CVT_I2F);
    bc_emit_store_var(cs, slot, vtype, is_local);
}

static void source_emit_int_conversion(BCCompiler *cs, uint8_t type) {
    if (type == T_NBR) bc_emit_byte(cs, OP_CVT_F2I);
    else if (type != T_INT) bc_set_error(cs, "Expected numeric expression");
}

static void source_emit_float_conversion(BCCompiler *cs, uint8_t type) {
    if (type == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
    else if (type != T_NBR) bc_set_error(cs, "Expected numeric expression");
}

static int source_expect_char(BCCompiler *cs, const char **pp, char ch, const char *msg) {
    const char *p = *pp;
    source_skip_space(&p);
    if (*p != ch) {
        bc_set_error(cs, "%s", msg);
        *pp = p;
        return 0;
    }
    *pp = p + 1;
    return 1;
}

static int source_name_eq(const char *name, int name_len, const char *want) {
    int want_len = (int)strlen(want);
    return name_len == want_len && strncasecmp(name, want, want_len) == 0;
}

// Parses an `AS <type>` clause.  Returns T_INT / T_NBR / T_STR / T_STRUCT and
// sets *struct_idx_out (−1 unless type is T_STRUCT).  Returns 0 if no `AS` is
// present or the name after `AS` doesn't resolve to a known type — the caller
// then falls back to the slot's default.  Struct names come from g_structtbl,
// populated by PrepareProgramExt before the compiler runs, so a recognized
// type name here is guaranteed stable at runtime.
static uint8_t source_parse_as_type_clause_ex(const char **pp, int *struct_idx_out) {
    const char *p = *pp;
    *struct_idx_out = -1;
    source_skip_space(&p);
    if (!source_keyword(&p, "AS")) return 0;
    source_skip_space(&p);

    uint8_t type = 0;
    if (strncasecmp(p, "INTEGER", 7) == 0 && !isnamechar((unsigned char)p[7])) {
        type = T_INT;
        p += 7;
    } else if (strncasecmp(p, "FLOAT", 5) == 0 && !isnamechar((unsigned char)p[5])) {
        type = T_NBR;
        p += 5;
    } else if (strncasecmp(p, "STRING", 6) == 0 && !isnamechar((unsigned char)p[6])) {
        type = T_STR;
        p += 6;
    } else if (isnamestart((unsigned char)*p)) {
        int sidx = FindStructType((unsigned char *)p);
        if (sidx < 0) return 0;
        type = T_STRUCT;
        *struct_idx_out = sidx;
        while (isnamechar((unsigned char)*p)) p++;
    } else {
        return 0;
    }

    *pp = p;
    return type;
}

static uint8_t source_parse_as_type_clause(const char **pp) {
    int ignored;
    return source_parse_as_type_clause_ex(pp, &ignored);
}

static uint8_t source_compile_rgb_call(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (*p != '(') {
        bc_set_error(cs, "Expected '(' after RGB");
        *pp = p;
        return 0;
    }
    p++;

    const char *arg_start = p;
    int depth = 1;
    int comma_count = 0;
    const char *arg_end = NULL;
    const char *scan = p;
    while (*scan && depth > 0) {
        if (*scan == '"') {
            scan++;
            while (*scan && *scan != '"') scan++;
        } else if (*scan == '(') {
            depth++;
        } else if (*scan == ')') {
            depth--;
            if (depth == 0) {
                arg_end = scan;
                break;
            }
        } else if (*scan == ',' && depth == 1) {
            comma_count++;
        }
        if (*scan) scan++;
    }
    if (!arg_end) {
        bc_set_error(cs, "Expected ')' after RGB");
        *pp = p;
        return 0;
    }

    if (comma_count == 0) {
        int color = 0;
        if (!source_color_name(arg_start, arg_end, &color)) {
            bc_set_error(cs, "Unknown RGB colour name");
            *pp = p;
            return 0;
        }
        bc_emit_byte(cs, OP_PUSH_INT);
        bc_emit_i64(cs, color);
        *pp = arg_end + 1;
        return T_INT;
    }

    if (comma_count != 2) {
        bc_set_error(cs, "RGB requires one colour name or three components");
        *pp = p;
        return 0;
    }

    uint8_t type = source_parse_expression(fe, cs, &p);
    source_emit_int_conversion(cs, type);
    source_skip_space(&p);
    if (*p != ',') {
        bc_set_error(cs, "Expected ',' in RGB");
        *pp = p;
        return 0;
    }
    p++;

    type = source_parse_expression(fe, cs, &p);
    source_emit_int_conversion(cs, type);
    source_skip_space(&p);
    if (*p != ',') {
        bc_set_error(cs, "Expected ',' in RGB");
        *pp = p;
        return 0;
    }
    p++;

    type = source_parse_expression(fe, cs, &p);
    source_emit_int_conversion(cs, type);
    source_skip_space(&p);
    if (*p != ')') {
        bc_set_error(cs, "Expected ')' after RGB");
        *pp = p;
        return 0;
    }
    bc_emit_byte(cs, OP_SYSCALL);
    bc_emit_u16(cs, BC_SYS_GFX_RGB);
    bc_emit_byte(cs, 3);
    bc_emit_byte(cs, 0);
    *pp = p + 1;
    return T_INT;
}

static int source_parse_array_indices(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (*p != '(') {
        bc_set_error(cs, "Expected '(' for array access");
        *pp = p;
        return 0;
    }
    p++;
    source_skip_space(&p);
    if (*p == ')') {
        *pp = p + 1;
        return 0;
    }

    int ndim = 0;
    while (!cs->has_error) {
        uint8_t itype = source_parse_expression(fe, cs, &p);
        source_emit_int_conversion(cs, itype);
        ndim++;
        source_skip_space(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ')') {
            p++;
            break;
        }
        bc_set_error(cs, "Expected ',' or ')' in array access");
        break;
    }

    *pp = p;
    return ndim;
}

static void source_emit_load_array(BCCompiler *cs, uint16_t slot, uint8_t type,
                                   int is_local, int ndim) {
    uint8_t op;
    if (is_local) {
        op = (type == T_INT) ? OP_LOAD_LOCAL_ARR_I :
             (type == T_STR) ? OP_LOAD_LOCAL_ARR_S :
                               OP_LOAD_LOCAL_ARR_F;
    } else {
        op = (type == T_INT) ? OP_LOAD_ARR_I :
             (type == T_STR) ? OP_LOAD_ARR_S :
                               OP_LOAD_ARR_F;
    }
    bc_emit_byte(cs, op);
    bc_emit_u16(cs, slot);
    bc_emit_byte(cs, (uint8_t)ndim);
}

static void source_emit_store_array(BCCompiler *cs, uint16_t slot, uint8_t type,
                                    int is_local, int ndim) {
    uint8_t op;
    if (is_local) {
        op = (type == T_INT) ? OP_STORE_LOCAL_ARR_I :
             (type == T_STR) ? OP_STORE_LOCAL_ARR_S :
                               OP_STORE_LOCAL_ARR_F;
    } else {
        op = (type == T_INT) ? OP_STORE_ARR_I :
             (type == T_STR) ? OP_STORE_ARR_S :
                               OP_STORE_ARR_F;
    }
    bc_emit_byte(cs, op);
    bc_emit_u16(cs, slot);
    bc_emit_byte(cs, (uint8_t)ndim);
}

static uint8_t source_parse_primary(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    if (*p == '"') {
        p++;
        const char *start = p;
        while (*p && *p != '"') p++;
        if (*p != '"') {
            bc_set_error(cs, "Unterminated string literal");
            *pp = p;
            return 0;
        }
        uint16_t idx = bc_add_constant_string(cs, (const uint8_t *)start, (int)(p - start));
        bc_emit_byte(cs, OP_PUSH_STR);
        bc_emit_u16(cs, idx);
        *pp = p + 1;
        return T_STR;
    }

    if (*p == '(') {
        p++;
        uint8_t type = source_parse_expression(fe, cs, &p);
        source_skip_space(&p);
        if (*p != ')') {
            bc_set_error(cs, "Expected ')'");
            *pp = p;
            return 0;
        }
        *pp = p + 1;
        return type;
    }

    {
        const char *q = p;
        if (source_keyword(&q, "MM.HRES")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_HRES, 0);
            *pp = q;
            return T_INT;
        }
        q = p;
        if (source_keyword(&q, "MM.VRES")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_VRES, 0);
            *pp = q;
            return T_INT;
        }
    }

    if (strncasecmp(p, "MM.INFO", 7) == 0) {
        const char *q = p + 7;
        source_skip_space(&q);
        if (*q != '(') {
            bc_set_error(cs, "Expected '(' after MM.INFO");
            *pp = q;
            return 0;
        }
        q++;
        source_skip_space(&q);
        if (source_keyword(&q, "HRES")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_HRES, 0);
        } else if (source_keyword(&q, "VRES")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_VRES, 0);
        } else if (source_keyword(&q, "FONTWIDTH")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_FONTWIDTH, 0);
        } else if (source_keyword(&q, "FONTHEIGHT")) {
            source_emit_syscall_noaux(cs, BC_SYS_MM_FONTHEIGHT, 0);
        } else {
            bc_set_error(cs, "Unsupported VM function: MM.INFO");
            *pp = q;
            return 0;
        }
        source_skip_space(&q);
        if (*q != ')') {
            bc_set_error(cs, "Expected ')' after MM.INFO");
            *pp = q;
            return 0;
        }
        *pp = q + 1;
        return T_INT;
    }

    if (*p == '&') {
        p++;
        int64_t ival = 0;
        char base = (char)toupper((unsigned char)*p++);
        if (base == 'H') {
            if (!isxdigit((unsigned char)*p)) bc_set_error(cs, "Invalid hexadecimal literal");
            while (isxdigit((unsigned char)*p)) {
                int d = toupper((unsigned char)*p) >= 'A' ? toupper((unsigned char)*p) - 'A' + 10
                                                          : *p - '0';
                ival = (ival << 4) | d;
                p++;
            }
        } else if (base == 'O') {
            if (*p < '0' || *p > '7') bc_set_error(cs, "Invalid octal literal");
            while (*p >= '0' && *p <= '7') {
                ival = (ival << 3) | (*p - '0');
                p++;
            }
        } else if (base == 'B') {
            if (*p != '0' && *p != '1') bc_set_error(cs, "Invalid binary literal");
            while (*p == '0' || *p == '1') {
                ival = (ival << 1) | (*p - '0');
                p++;
            }
        } else {
            bc_set_error(cs, "Invalid number base prefix");
        }
        bc_emit_byte(cs, OP_PUSH_INT);
        bc_emit_i64(cs, ival);
        *pp = p;
        return T_INT;
    }

    if (isdigit((unsigned char)*p) || *p == '.') {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p) {
            bc_set_error(cs, "Invalid number");
            *pp = p;
            return 0;
        }

        int is_float = 0;
        for (const char *q = p; q < end; q++) {
            if (*q == '.' || *q == 'e' || *q == 'E') {
                is_float = 1;
                break;
            }
        }

        if (is_float) {
            bc_emit_byte(cs, OP_PUSH_FLT);
            bc_emit_f64(cs, (MMFLOAT)v);
            *pp = end;
            return T_NBR;
        }

        bc_emit_byte(cs, OP_PUSH_INT);
        bc_emit_i64(cs, (int64_t)strtoll(p, NULL, 10));
        *pp = end;
        return T_INT;
    }

    if (isnamestart((unsigned char)*p)) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t type = 0;
        if (!source_parse_varname(&p, name, &name_len, &type)) {
            bc_set_error(cs, "Expected variable");
            *pp = p;
            return 0;
        }
        uint8_t suffix_type = type;
        if (type == 0) type = source_default_var_type(cs, name, name_len);

        // Struct field read: if `name` is "base.field[.more]" with base a
        // declared T_STRUCT slot, emit the right load opcode.  If base isn't a
        // struct, falls through to normal handling so legacy dotted-name
        // variables still work.  The chain walker also consumes `(idx)` tails
        // in `p` for nested-array members.
        {
            uint8_t ft = 0;
            if (source_try_emit_struct_field_load(fe, cs, name, name_len, &p, &ft)) {
                *pp = p;
                return ft;
            }
        }

        const char *after_name = p;
        source_skip_space(&after_name);
        if (source_name_eq(name, name_len, "INKEY$")) {
            bc_emit_byte(cs, OP_STR_INKEY);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "DATE$")) {
            source_emit_syscall_noaux(cs, BC_SYS_DATE_STR, 0);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "TIME$")) {
            source_emit_syscall_noaux(cs, BC_SYS_TIME_STR, 0);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "TIMER")) {
            bc_emit_byte(cs, OP_TIMER);
            *pp = p;
            return T_NBR;
        }

        if (source_name_eq(name, name_len, "PI")) {
            bc_emit_byte(cs, OP_MATH_PI);
            *pp = p;
            return T_NBR;
        }

        if (source_name_eq(name, name_len, "RND")) {
            /* Interpreter's fun_rnd ignores its argument (Functions.c:950):
             * Rnd, Rnd(), Rnd(n) all produce a fresh random in [0,1).
             * Parse the optional arg for shape, then discard with DROP. */
            if (*after_name == '(') {
                p = after_name + 1;
                source_skip_space(&p);
                if (*p == ')') {
                    p++;
                } else {
                    uint8_t arg_type = source_parse_expression(fe, cs, &p);
                    source_emit_float_conversion(cs, arg_type);
                    bc_emit_byte(cs, OP_POP);
                    if (!source_expect_char(cs, &p, ')', "Expected ')' after RND argument")) return 0;
                }
            }
            bc_emit_byte(cs, OP_RND);
            *pp = p;
            return T_NBR;
        }

        if (source_name_eq(name, name_len, "LEN") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type != T_STR) bc_set_error(cs, "LEN requires a string argument");
            if (!source_expect_char(cs, &p, ')', "Expected ')' after LEN")) return 0;
            bc_emit_byte(cs, OP_STR_LEN);
            *pp = p;
            return T_INT;
        }

        if ((source_name_eq(name, name_len, "LEFT$") ||
             source_name_eq(name, name_len, "RIGHT$")) && *after_name == '(') {
            int is_left = source_name_eq(name, name_len, "LEFT$");
            p = after_name + 1;
            uint8_t str_type = source_parse_expression(fe, cs, &p);
            if (str_type != T_STR) bc_set_error(cs, "String function requires a string argument");
            if (!source_expect_char(cs, &p, ',', "Expected ',' in string function")) return 0;
            uint8_t count_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, count_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after string function")) return 0;
            bc_emit_byte(cs, is_left ? OP_STR_LEFT : OP_STR_RIGHT);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "MID$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t str_type = source_parse_expression(fe, cs, &p);
            if (str_type != T_STR) bc_set_error(cs, "MID$ requires a string argument");
            if (!source_expect_char(cs, &p, ',', "Expected ',' in MID$")) return 0;
            uint8_t start_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, start_type);
            source_skip_space(&p);
            if (*p == ',') {
                p++;
                uint8_t len_type = source_parse_expression(fe, cs, &p);
                source_emit_int_conversion(cs, len_type);
                if (!source_expect_char(cs, &p, ')', "Expected ')' after MID$")) return 0;
                bc_emit_byte(cs, OP_STR_MID3);
            } else {
                if (!source_expect_char(cs, &p, ')', "Expected ')' after MID$")) return 0;
                bc_emit_byte(cs, OP_STR_MID2);
            }
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "VAL") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type != T_STR) bc_set_error(cs, "VAL requires a string argument");
            if (!source_expect_char(cs, &p, ')', "Expected ')' after VAL")) return 0;
            bc_emit_byte(cs, OP_STR_VAL);
            *pp = p;
            return T_NBR;
        }

        if (source_name_eq(name, name_len, "STR$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type == T_STR) bc_set_error(cs, "STR$ requires a numeric argument");
            if (!source_expect_char(cs, &p, ')', "Expected ')' after STR$")) return 0;
            bc_emit_byte(cs, OP_STR_STR);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "INSTR") && *after_name == '(') {
            p = after_name + 1;
            uint8_t first_type = source_parse_expression(fe, cs, &p);
            if (!source_expect_char(cs, &p, ',', "Expected ',' in INSTR")) return 0;
            if (first_type == T_STR) {
                uint8_t needle_type = source_parse_expression(fe, cs, &p);
                if (needle_type != T_STR) bc_set_error(cs, "INSTR requires string arguments");
                if (!source_expect_char(cs, &p, ')', "Expected ')' after INSTR")) return 0;
                bc_emit_byte(cs, OP_STR_INSTR);
                bc_emit_byte(cs, 2);
            } else {
                source_emit_int_conversion(cs, first_type);
                uint8_t haystack_type = source_parse_expression(fe, cs, &p);
                if (haystack_type != T_STR) bc_set_error(cs, "INSTR requires string arguments");
                if (!source_expect_char(cs, &p, ',', "Expected ',' in INSTR")) return 0;
                uint8_t needle_type = source_parse_expression(fe, cs, &p);
                if (needle_type != T_STR) bc_set_error(cs, "INSTR requires string arguments");
                if (!source_expect_char(cs, &p, ')', "Expected ')' after INSTR")) return 0;
                bc_emit_byte(cs, OP_STR_INSTR);
                bc_emit_byte(cs, 3);
            }
            *pp = p;
            return T_INT;
        }

        if ((source_name_eq(name, name_len, "HEX$") ||
             source_name_eq(name, name_len, "OCT$") ||
             source_name_eq(name, name_len, "BIN$")) && *after_name == '(') {
            uint8_t op = source_name_eq(name, name_len, "HEX$") ? OP_STR_HEX :
                         source_name_eq(name, name_len, "OCT$") ? OP_STR_OCT :
                                                                   OP_STR_BIN;
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, arg_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after numeric string function")) return 0;
            bc_emit_byte(cs, op);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "SPACE$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, arg_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after SPACE$")) return 0;
            bc_emit_byte(cs, OP_STR_SPACE);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "STRING$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t count_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, count_type);
            if (!source_expect_char(cs, &p, ',', "Expected ',' in STRING$")) return 0;
            uint8_t char_type = source_parse_expression(fe, cs, &p);
            if (char_type == T_STR) bc_emit_byte(cs, OP_STR_ASC);
            else source_emit_int_conversion(cs, char_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after STRING$")) return 0;
            bc_emit_byte(cs, OP_STR_STRING);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "FIELD$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t src_type = source_parse_expression(fe, cs, &p);
            if (src_type != T_STR) bc_set_error(cs, "FIELD$ requires a string source");
            if (!source_expect_char(cs, &p, ',', "Expected ',' in FIELD$")) return 0;
            uint8_t field_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, field_type);
            if (!source_expect_char(cs, &p, ',', "Expected ',' in FIELD$")) return 0;
            uint8_t delim_type = source_parse_expression(fe, cs, &p);
            if (delim_type != T_STR) bc_set_error(cs, "FIELD$ requires string delimiters");
            if (!source_expect_char(cs, &p, ')', "Expected ')' after FIELD$")) return 0;
            bc_emit_byte(cs, OP_STR_FIELD3);
            *pp = p;
            return T_STR;
        }

        if (source_name_eq(name, name_len, "RGB") && *after_name == '(') {
            p = after_name;
            uint8_t rgb_type = source_compile_rgb_call(fe, cs, &p);
            *pp = p;
            return rgb_type;
        }

        if (source_name_eq(name, name_len, "KEYDOWN") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, arg_type);
            source_skip_space(&p);
            if (*p != ')') bc_set_error(cs, "Expected ')' after KEYDOWN");
            else p++;
            source_emit_syscall_noaux(cs, BC_SYS_KEYDOWN, 1);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "PIN") && *after_name == '(') {
            p = after_name + 1;
            source_compile_pin_operand(fe, cs, &p);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after PIN")) return 0;
            source_emit_syscall_noaux(cs, BC_SYS_PIN_READ, 1);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "PIXEL") && *after_name == '(') {
            p = after_name + 1;
            uint8_t x_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, x_type);
            if (!source_expect_char(cs, &p, ',', "Expected ',' after PIXEL x")) return 0;
            uint8_t y_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, y_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after PIXEL")) return 0;
            source_emit_syscall_noaux(cs, BC_SYS_GFX_PIXEL_READ, 2);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "MULSHR") && *after_name == '(') {
            uint32_t a_start, a_end, b_start, b_end;
            p = after_name + 1;
            a_start = cs->code_len;
            uint8_t a_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, a_type);
            a_end = cs->code_len;
            if (!source_expect_char(cs, &p, ',', "Expected ',' after MULSHR a")) return 0;
            b_start = cs->code_len;
            uint8_t b_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, b_type);
            b_end = cs->code_len;
            if (!source_expect_char(cs, &p, ',', "Expected ',' after MULSHR b")) return 0;
            uint8_t bits_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, bits_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after MULSHR")) return 0;
            if (bc_opt_level >= 1 &&
                source_is_same_simple_int_load(cs, a_start, a_end, b_start, b_end)) {
                source_delete_bytes(cs, b_start, b_end - b_start);
                if (cs->has_error) return 0;
                bc_emit_byte(cs, OP_MATH_SQRSHR);
            } else
                bc_emit_byte(cs, OP_MATH_MULSHR);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "ASC") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type != T_STR) bc_set_error(cs, "ASC requires a string argument");
            source_skip_space(&p);
            if (*p != ')') bc_set_error(cs, "Expected ')' after ASC");
            else p++;
            bc_emit_byte(cs, OP_STR_ASC);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "CHR$") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, arg_type);
            source_skip_space(&p);
            if (*p != ')') bc_set_error(cs, "Expected ')' after CHR$");
            else p++;
            bc_emit_byte(cs, OP_STR_CHR);
            *pp = p;
            return T_STR;
        }

        if ((source_name_eq(name, name_len, "LCASE$") ||
             source_name_eq(name, name_len, "UCASE$")) && *after_name == '(') {
            int is_lcase = source_name_eq(name, name_len, "LCASE$");
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type != T_STR) bc_set_error(cs, "Case conversion requires a string argument");
            source_skip_space(&p);
            if (*p != ')') bc_set_error(cs, "Expected ')' after string function");
            else p++;
            bc_emit_byte(cs, is_lcase ? OP_STR_LCASE : OP_STR_UCASE);
            *pp = p;
            return T_STR;
        }

        if ((source_name_eq(name, name_len, "SIN") ||
             source_name_eq(name, name_len, "COS") ||
             source_name_eq(name, name_len, "TAN") ||
             source_name_eq(name, name_len, "ATN") ||
             source_name_eq(name, name_len, "ASIN") ||
             source_name_eq(name, name_len, "ACOS") ||
             source_name_eq(name, name_len, "SQR") ||
             source_name_eq(name, name_len, "LOG") ||
             source_name_eq(name, name_len, "EXP") ||
             source_name_eq(name, name_len, "RAD") ||
             source_name_eq(name, name_len, "DEG")) && *after_name == '(') {
            uint8_t op =
                source_name_eq(name, name_len, "SIN")  ? OP_MATH_SIN :
                source_name_eq(name, name_len, "COS")  ? OP_MATH_COS :
                source_name_eq(name, name_len, "TAN")  ? OP_MATH_TAN :
                source_name_eq(name, name_len, "ATN")  ? OP_MATH_ATN :
                source_name_eq(name, name_len, "ASIN") ? OP_MATH_ASIN :
                source_name_eq(name, name_len, "ACOS") ? OP_MATH_ACOS :
                source_name_eq(name, name_len, "SQR")  ? OP_MATH_SQR :
                source_name_eq(name, name_len, "LOG")  ? OP_MATH_LOG :
                source_name_eq(name, name_len, "EXP")  ? OP_MATH_EXP :
                source_name_eq(name, name_len, "RAD")  ? OP_MATH_RAD :
                                                        OP_MATH_DEG;
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, arg_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after math function")) return 0;
            bc_emit_byte(cs, op);
            *pp = p;
            return T_NBR;
        }

        if (source_name_eq(name, name_len, "ATAN2") && *after_name == '(') {
            p = after_name + 1;
            uint8_t y_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, y_type);
            if (!source_expect_char(cs, &p, ',', "Expected ',' in ATAN2")) return 0;
            uint8_t x_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, x_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after ATAN2")) return 0;
            bc_emit_byte(cs, OP_MATH_ATAN2);
            *pp = p;
            return T_NBR;
        }

        if ((source_name_eq(name, name_len, "INT") ||
             source_name_eq(name, name_len, "ABS")) && *after_name == '(') {
            int is_int = source_name_eq(name, name_len, "INT");
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type == T_STR) bc_set_error(cs, "Math function requires a numeric argument");
            if (is_int && arg_type == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
            source_skip_space(&p);
            if (*p != ')') bc_set_error(cs, "Expected ')' after math function");
            else p++;
            bc_emit_byte(cs, is_int ? OP_MATH_INT : OP_MATH_ABS);
            *pp = p;
            return is_int ? T_NBR : arg_type;
        }

        if ((source_name_eq(name, name_len, "FIX") ||
             source_name_eq(name, name_len, "CINT")) && *after_name == '(') {
            int is_fix = source_name_eq(name, name_len, "FIX");
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, arg_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after math function")) return 0;
            bc_emit_byte(cs, is_fix ? OP_MATH_FIX : OP_MATH_CINT);
            *pp = p;
            return T_INT;
        }

        if (source_name_eq(name, name_len, "SGN") && *after_name == '(') {
            p = after_name + 1;
            uint8_t arg_type = source_parse_expression(fe, cs, &p);
            if (arg_type == T_STR) bc_set_error(cs, "SGN requires a numeric argument");
            if (!source_expect_char(cs, &p, ')', "Expected ')' after SGN")) return 0;
            bc_emit_byte(cs, OP_MATH_SGN);
            *pp = p;
            return T_INT;
        }

        if ((source_name_eq(name, name_len, "MIN") ||
             source_name_eq(name, name_len, "MAX")) && *after_name == '(') {
            int is_min = source_name_eq(name, name_len, "MIN");
            p = after_name + 1;
            uint8_t arg1_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, arg1_type);
            if (!source_expect_char(cs, &p, ',', "Expected ',' in MIN/MAX")) return 0;
            uint8_t arg2_type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, arg2_type);
            if (!source_expect_char(cs, &p, ')', "Expected ')' after MIN/MAX")) return 0;
            bc_emit_byte(cs, is_min ? OP_MATH_MIN : OP_MATH_MAX);
            *pp = p;
            return T_NBR;
        }

        int sf_name_len = (suffix_type != 0 && name_len > 0) ? name_len - 1 : name_len;
        int sf_idx = bc_find_subfun(cs, name, sf_name_len);
        if (sf_idx >= 0 && cs->subfuns[sf_idx].return_type != 0 &&
            !cs->subfuns[sf_idx].bridged && *after_name == '(') {
            p = after_name;
            int nargs = source_compile_call_args(fe, cs, &p, 1);
            if (cs->has_error) {
                *pp = p;
                return 0;
            }
            bc_emit_byte(cs, OP_CALL_FUN);
            bc_emit_u16(cs, (uint16_t)sf_idx);
            bc_emit_byte(cs, (uint8_t)nargs);
            *pp = p;
            return cs->subfuns[sf_idx].return_type;
        }

        /* Check if this is a built-in interpreter function we should bridge.
         * Search tokentbl for T_FUN match (name with '(') and T_FNA match (name without). */
        {
            unsigned char tok_lookup[MAXVARLEN + 4];
            int tl_len = 0;
            int tok_idx = -1;
            uint8_t tok_flags = 0;

            /* Try T_FUN match: "NAME$(" — note `name` already includes
             * any `$`/`%`/`!` suffix (source_parse_varname keeps it). */
            if (*after_name == '(') {
                memcpy(tok_lookup, name, name_len);
                tl_len = name_len;
                tok_lookup[tl_len++] = '(';
                tok_lookup[tl_len] = 0;
                for (int ti = 0; ti < TokenTableSize - 1; ti++) {
                    if (str_equal(tok_lookup, tokentbl[ti].name)) {
                        tok_idx = ti;
                        tok_flags = tokentbl[ti].type;
                        break;
                    }
                }
            }

            /* Try T_FNA match: "NAME$" (no-argument function) */
            if (tok_idx < 0) {
                memcpy(tok_lookup, name, name_len);
                tl_len = name_len;
                tok_lookup[tl_len] = 0;
                for (int ti = 0; ti < TokenTableSize - 1; ti++) {
                    if ((tokentbl[ti].type & T_FNA) &&
                        str_equal(tok_lookup, tokentbl[ti].name)) {
                        tok_idx = ti;
                        tok_flags = tokentbl[ti].type;
                        break;
                    }
                }
            }

            if (tok_idx >= 0) {
                /* Determine return type and bridge opcode */
                uint8_t ret_type;
                uint8_t bridge_op;
                if (tok_flags & T_STR) { ret_type = T_STR; bridge_op = OP_BRIDGE_FUN_S; }
                else if (tok_flags & T_INT) { ret_type = T_INT; bridge_op = OP_BRIDGE_FUN_I; }
                else { ret_type = T_NBR; bridge_op = OP_BRIDGE_FUN_F; }

                uint16_t tok_len = 0;
                unsigned char saved_inpbuf[STRINGSIZE];
                unsigned char saved_tknbuf[STRINGSIZE];

                if (tok_flags & T_FUN) {
                    /* Function with arguments: scan to matching ')' in source */
                    const char *paren = after_name; /* points to '(' */
                    int depth = 1;
                    const char *scan = paren + 1;
                    while (*scan && depth > 0) {
                        if (*scan == '(') depth++;
                        else if (*scan == ')') depth--;
                        scan++;
                    }
                    if (depth != 0) {
                        bc_set_error(cs, "Unmatched parenthesis in bridged function");
                        *pp = p;
                        return 0;
                    }

                    /* Tokenize the arguments (between parens, exclusive).
                     * Prepend "?" so tokenise() enters non-firstnonwhite mode,
                     * allowing nested function tokens to be recognized.
                     * The "?" tokenizes as a 2-byte PRINT command prefix we skip. */
                    char call_text[STRINGSIZE];
                    call_text[0] = '?';
                    size_t args_len = (size_t)(scan - 1 - (paren + 1)); /* exclude outer ( and ) */
                    if (args_len >= STRINGSIZE - 2) args_len = STRINGSIZE - 2;
                    memcpy(call_text + 1, paren + 1, args_len);
                    call_text[1 + args_len] = 0;

                    memcpy(saved_inpbuf, inpbuf, STRINGSIZE);
                    memcpy(saved_tknbuf, tknbuf, STRINGSIZE);
                    memcpy(inpbuf, call_text, args_len + 2);
                    tokenise(1);

                    /* tknbuf: PRINT_cmd(2 bytes) + tokenized_args + 0x00
                     * Skip the 2-byte PRINT command prefix. */
                    unsigned char *tp = tknbuf + 2;
                    unsigned char *tp_start = tp;
                    while (*tp) {
                        if (*tp == T_LINENBR) { tp += 3; continue; }
                        tp++;
                    }
                    tok_len = (uint16_t)(tp - tp_start);

                    bc_emit_byte(cs, bridge_op);
                    bc_emit_u16(cs, (uint16_t)tok_idx);
                    bc_emit_u16(cs, tok_len);
                    for (uint16_t i = 0; i < tok_len; i++)
                        bc_emit_byte(cs, tp_start[i]);

                    memcpy(inpbuf, saved_inpbuf, STRINGSIZE);
                    memcpy(tknbuf, saved_tknbuf, STRINGSIZE);
                    p = scan; /* advance past closing ')' */
                } else {
                    /* T_FNA: no arguments */
                    bc_emit_byte(cs, bridge_op);
                    bc_emit_u16(cs, (uint16_t)tok_idx);
                    bc_emit_u16(cs, 0);
                    /* p stays where it was — no args to consume */
                }

                *pp = p;
                return ret_type;
            }
        }

        if (*after_name == '(') {
            p = after_name;
            int ndim = source_parse_array_indices(fe, cs, &p);
            if (cs->has_error) {
                *pp = p;
                return 0;
            }
            int is_local = 0;
            uint16_t slot = source_resolve_var(cs, name, name_len, type, 1, &is_local);
            if (!is_local && slot != 0xFFFF) cs->slots[slot].is_array = 1;

            /* Struct array element field access — `points(i).x` up through
             * `points(i).b(j).c(k)` — the chain walker resolves each `.name`
             * and emits inner index expressions, then emits FIELD / ELEM /
             * NESTED based on chain shape.  Outer indices are already on the
             * stack from source_parse_array_indices above. */
            if (!is_local && slot != 0xFFFF && cs->slots[slot].type == T_STRUCT) {
                const char *after_idx = p;
                source_skip_space(&after_idx);
                if (*after_idx == '.') {
                    uint8_t t = source_emit_struct_chain_load(fe, cs, slot, ndim,
                                                              NULL, 0, &after_idx);
                    if (cs->has_error) { *pp = after_idx; return 0; }
                    *pp = after_idx;
                    return t;
                }
                /* Whole-element read (Phase 5) — not supported yet. */
                bc_set_error(cs, "Whole-struct element read not yet supported");
                *pp = p;
                return 0;
            }

            source_emit_load_array(cs, slot, type, is_local, ndim);
            *pp = p;
            return type;
        }

        int is_local = 0;
        uint16_t slot = source_resolve_var(cs, name, name_len, type, 1, &is_local);
        if (slot == 0xFFFF) {
            *pp = p;
            return 0;
        }
        /* Inline known constants — emit literal instead of slot load */
        if (!is_local && slot < cs->slot_count && cs->slots[slot].is_const) {
            if (cs->slots[slot].type & T_INT) {
                bc_emit_byte(cs, OP_PUSH_INT);
                bc_emit_i64(cs, cs->slots[slot].const_ival);
                *pp = p;
                return T_INT;
            } else if (cs->slots[slot].type & T_NBR) {
                bc_emit_byte(cs, OP_PUSH_FLT);
                bc_emit_f64(cs, cs->slots[slot].const_fval);
                *pp = p;
                return T_NBR;
            }
        }
        bc_emit_load_var(cs, slot, type, is_local);
        *pp = p;
        return type;
    }

    bc_set_error(cs, "Unsupported expression near: %.24s", p);
    *pp = p;
    return 0;
}

static uint8_t source_parse_unary(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (source_keyword(&p, "NOT")) {
        *pp = p;
        uint8_t type = source_parse_unary(fe, cs, pp);
        source_emit_int_conversion(cs, type);
        bc_emit_byte(cs, OP_NOT);
        return T_INT;
    }
    if (source_keyword(&p, "INV")) {
        *pp = p;
        uint8_t type = source_parse_unary(fe, cs, pp);
        source_emit_int_conversion(cs, type);
        bc_emit_byte(cs, OP_INV);
        return T_INT;
    }
    if (*p == '+') {
        p++;
        *pp = p;
        return source_parse_unary(fe, cs, pp);
    }
    if (*p == '-') {
        p++;
        *pp = p;
        uint8_t type = source_parse_unary(fe, cs, pp);
        if (cs->has_error) return 0;
        if (type == T_INT) bc_emit_byte(cs, OP_NEG_I);
        else if (type == T_NBR) bc_emit_byte(cs, OP_NEG_F);
        else bc_set_error(cs, "Unary '-' requires a numeric expression");
        return type;
    }
    return source_parse_primary(fe, cs, pp);
}

static uint8_t source_parse_power_expr(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    uint8_t left = source_parse_unary(fe, cs, pp);
    if (cs->has_error) return 0;

    while (1) {
        const char *p = *pp;
        source_skip_space(&p);
        if (*p != '^') break;
        p++;

        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_unary(fe, cs, &p);
        if (cs->has_error) return 0;
        if (left == T_INT) source_insert_byte(cs, right_start, OP_CVT_I2F);
        else if (left != T_NBR) bc_set_error(cs, "Power requires numeric expressions");
        if (right == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
        else if (right != T_NBR) bc_set_error(cs, "Power requires numeric expressions");
        bc_emit_byte(cs, OP_POW_F);
        left = T_NBR;
        *pp = p;
    }

    return left;
}

static uint8_t source_emit_numeric_binary(BCCompiler *cs, uint8_t left, uint8_t right,
                                          uint32_t right_start, char op) {
    if ((left & T_STR) || (right & T_STR)) {
        if (op == '+' && (left & T_STR) && (right & T_STR)) {
            bc_emit_byte(cs, OP_ADD_S);
            return T_STR;
        }
        bc_set_error(cs, "Invalid string operator");
        return 0;
    }

    if (op == '/') {
        if (left == T_INT) source_insert_byte(cs, right_start, OP_CVT_I2F);
        if (right == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
        bc_emit_byte(cs, OP_DIV_F);
        return T_NBR;
    }

    if (op == '\\') {
        if (left == T_NBR) source_insert_byte(cs, right_start, OP_CVT_F2I);
        if (right == T_NBR) bc_emit_byte(cs, OP_CVT_F2I);
        bc_emit_byte(cs, OP_IDIV_I);
        return T_INT;
    }

    if (op == 'm') {
        if (left == T_NBR) source_insert_byte(cs, right_start, OP_CVT_F2I);
        if (right == T_NBR) bc_emit_byte(cs, OP_CVT_F2I);
        bc_emit_byte(cs, OP_MOD_I);
        return T_INT;
    }

    if (left == T_NBR || right == T_NBR) {
        if (left == T_INT) source_insert_byte(cs, right_start, OP_CVT_I2F);
        if (right == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
        switch (op) {
            case '+': bc_emit_byte(cs, OP_ADD_F); break;
            case '-': bc_emit_byte(cs, OP_SUB_F); break;
            case '*': bc_emit_byte(cs, OP_MUL_F); break;
            default:  bc_set_error(cs, "Unsupported numeric operator"); return 0;
        }
        return T_NBR;
    }

    switch (op) {
        case '+': bc_emit_byte(cs, OP_ADD_I); break;
        case '-': bc_emit_byte(cs, OP_SUB_I); break;
        case '*': bc_emit_byte(cs, OP_MUL_I); break;
        default:  bc_set_error(cs, "Unsupported numeric operator"); return 0;
    }
    return T_INT;
}

static int source_match_compare(const char **pp, char *op) {
    const char *p = *pp;
    source_skip_space(&p);
    if (p[0] == '<' && p[1] == '>') {
        *op = 'n';
        *pp = p + 2;
        return 1;
    }
    if (p[0] == '<' && p[1] == '=') {
        *op = 'l';
        *pp = p + 2;
        return 1;
    }
    if (p[0] == '>' && p[1] == '=') {
        *op = 'g';
        *pp = p + 2;
        return 1;
    }
    if (*p == '=') {
        *op = '=';
        *pp = p + 1;
        return 1;
    }
    if (*p == '<') {
        *op = '<';
        *pp = p + 1;
        return 1;
    }
    if (*p == '>') {
        *op = '>';
        *pp = p + 1;
        return 1;
    }
    return 0;
}

static uint8_t source_emit_compare(BCCompiler *cs, uint8_t left, uint8_t right,
                                   uint32_t right_start, char op) {
    if ((left & T_STR) || (right & T_STR)) {
        if (!(left & T_STR) || !(right & T_STR)) {
            bc_set_error(cs, "Cannot compare string with numeric expression");
            return 0;
        }
        switch (op) {
            case '=': bc_emit_byte(cs, OP_EQ_S); break;
            case 'n': bc_emit_byte(cs, OP_NE_S); break;
            case '<': bc_emit_byte(cs, OP_LT_S); break;
            case '>': bc_emit_byte(cs, OP_GT_S); break;
            case 'l': bc_emit_byte(cs, OP_LE_S); break;
            case 'g': bc_emit_byte(cs, OP_GE_S); break;
            default:  bc_set_error(cs, "Unsupported comparison"); return 0;
        }
        return T_INT;
    }

    if (left == T_NBR || right == T_NBR) {
        if (left == T_INT) source_insert_byte(cs, right_start, OP_CVT_I2F);
        if (right == T_INT) bc_emit_byte(cs, OP_CVT_I2F);
        switch (op) {
            case '=': bc_emit_byte(cs, OP_EQ_F); break;
            case 'n': bc_emit_byte(cs, OP_NE_F); break;
            case '<': bc_emit_byte(cs, OP_LT_F); break;
            case '>': bc_emit_byte(cs, OP_GT_F); break;
            case 'l': bc_emit_byte(cs, OP_LE_F); break;
            case 'g': bc_emit_byte(cs, OP_GE_F); break;
            default:  bc_set_error(cs, "Unsupported comparison"); return 0;
        }
        return T_INT;
    }

    switch (op) {
        case '=': bc_emit_byte(cs, OP_EQ_I); break;
        case 'n': bc_emit_byte(cs, OP_NE_I); break;
        case '<': bc_emit_byte(cs, OP_LT_I); break;
        case '>': bc_emit_byte(cs, OP_GT_I); break;
        case 'l': bc_emit_byte(cs, OP_LE_I); break;
        case 'g': bc_emit_byte(cs, OP_GE_I); break;
        default:  bc_set_error(cs, "Unsupported comparison"); return 0;
    }
    return T_INT;
}

static uint8_t source_emit_int_binary(BCCompiler *cs, uint8_t left, uint8_t right,
                                      uint32_t right_start, uint8_t op) {
    if (left == T_NBR) source_insert_byte(cs, right_start, OP_CVT_F2I);
    else if (left != T_INT) bc_set_error(cs, "Expected numeric expression");
    if (right == T_NBR) bc_emit_byte(cs, OP_CVT_F2I);
    else if (right != T_INT) bc_set_error(cs, "Expected numeric expression");
    bc_emit_byte(cs, op);
    return T_INT;
}

static uint8_t source_parse_mul_expr(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    uint32_t expr_start = cs->code_len;
    uint8_t left = source_parse_power_expr(fe, cs, pp);
    if (cs->has_error) return 0;

    while (1) {
        const char *p = *pp;
        source_skip_space(&p);
        char op = 0;
        if (*p == '*' || *p == '/' || *p == '\\') {
            op = *p++;
        } else {
            const char *q = p;
            if (source_keyword(&q, "MOD")) {
                op = 'm';
                p = q;
            } else {
                break;
            }
        }
        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_power_expr(fe, cs, &p);
        if (cs->has_error) return 0;
        if (op == '\\' && source_try_fuse_mulshr(cs, left, right, expr_start, right_start)) {
            left = T_INT;
            *pp = p;
            continue;
        }
        left = source_emit_numeric_binary(cs, left, right, right_start, op);
        *pp = p;
    }

    return left;
}

static uint8_t source_parse_add_expr(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    uint32_t expr_start = cs->code_len;
    uint8_t left = source_parse_mul_expr(fe, cs, pp);
    if (cs->has_error) return 0;

    while (1) {
        const char *p = *pp;
        source_skip_space(&p);
        if (*p != '+' && *p != '-') break;
        char op = *p++;
        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_mul_expr(fe, cs, &p);
        if (cs->has_error) return 0;
        if (source_try_fuse_mulshradd(cs, left, right, right_start, op)) {
            left = T_INT;
            *pp = p;
            continue;
        }
        left = source_emit_numeric_binary(cs, left, right, right_start, op);
        *pp = p;
    }

    return left;
}

static uint8_t source_parse_shift_expr(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    uint8_t left = source_parse_add_expr(fe, cs, pp);
    if (cs->has_error) return 0;

    while (1) {
        const char *p = *pp;
        source_skip_space(&p);
        uint8_t op = 0;
        if (p[0] == '<' && p[1] == '<') {
            op = OP_SHL;
            p += 2;
        } else if (p[0] == '>' && p[1] == '>') {
            op = OP_SHR;
            p += 2;
        } else {
            break;
        }
        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_add_expr(fe, cs, &p);
        if (cs->has_error) return 0;
        left = source_emit_int_binary(cs, left, right, right_start, op);
        *pp = p;
    }

    return left;
}

static uint8_t source_parse_expression(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    uint8_t left = source_parse_shift_expr(fe, cs, pp);
    if (cs->has_error) return 0;

    const char *p = *pp;
    char op = 0;
    if (source_match_compare(&p, &op)) {
        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_shift_expr(fe, cs, &p);
        if (cs->has_error) return 0;
        left = source_emit_compare(cs, left, right, right_start, op);
        *pp = p;
    }

    while (1) {
        p = *pp;
        source_skip_space(&p);
        uint8_t bool_op = 0;
        const char *q = p;
        if (source_keyword(&q, "AND")) {
            bool_op = OP_AND;
            p = q;
        } else if (source_keyword(&q, "OR")) {
            bool_op = OP_OR;
            p = q;
        } else if (source_keyword(&q, "XOR")) {
            bool_op = OP_XOR;
            p = q;
        } else {
            break;
        }

        uint32_t right_start = cs->code_len;
        uint8_t right = source_parse_shift_expr(fe, cs, &p);
        if (cs->has_error) return 0;

        char cmp_op = 0;
        if (source_match_compare(&p, &cmp_op)) {
            uint32_t cmp_right_start = cs->code_len;
            uint8_t cmp_right = source_parse_shift_expr(fe, cs, &p);
            if (cs->has_error) return 0;
            right = source_emit_compare(cs, right, cmp_right, cmp_right_start, cmp_op);
        }

        left = source_emit_int_binary(cs, left, right, right_start, bool_op);
        *pp = p;
    }

    return left;
}

static void source_compile_assignment(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;

    // Phase 5: whole-struct assignments (scalar struct / struct array element /
    // struct-member chain ending at a T_STRUCT node) don't have native opcodes;
    // we bridge the whole statement to the interpreter's cmd_let.  This also
    // handles the RHS regardless of shape (scalar, array-elem, nested member,
    // function return) because the interpreter resolves it via findvar.
    if (source_lhs_is_whole_struct(cs, p)) {
        /* Advance pp past the statement so the caller's statement_end is OK,
         * but emit the original raw text as a BRIDGE_CMD. */
        const char *line_end = p;
        while (*line_end && *line_end != ':' && *line_end != '\'') line_end++;
        size_t len = (size_t)(line_end - p);
        if (len >= STRINGSIZE) len = STRINGSIZE - 1;
        char tmp[STRINGSIZE];
        memcpy(tmp, p, len);
        tmp[len] = 0;
        source_emit_bridge_for_stmt(cs, tmp);
        *pp = line_end;
        return;
    }

    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t vtype = 0;
    if (!source_parse_varname(&p, name, &name_len, &vtype)) {
        bc_set_error(cs, "Expected variable");
        *pp = p;
        return;
    }

    if (source_name_eq(name, name_len, "PIN")) {
        uint8_t type;
        source_skip_space(&p);
        if (*p != '(') {
            bc_set_error(cs, "Expected '(' after PIN");
            *pp = p;
            return;
        }
        p++;
        source_compile_pin_operand(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        if (!source_expect_char(cs, &p, ')', "Expected ')' after PIN")) {
            *pp = p;
            return;
        }
        source_skip_space(&p);
        if (*p != '=') {
            bc_set_error(cs, "Expected '='");
            *pp = p;
            return;
        }
        p++;
        type = source_parse_expression(fe, cs, &p);
        source_emit_int_conversion(cs, type);
        bc_emit_byte(cs, OP_PIN_WRITE);
        *pp = p;
        return;
    }

    if (vtype == 0) vtype = source_default_var_type(cs, name, name_len);

    // Struct-member store: pt.x = expr / a.b.c = expr / a.b(j).c = expr.
    // Fallthrough behaviour matches the read side — non-struct names take the
    // normal path and legacy dotted names are unaffected.
    source_skip_space(&p);
    if (memchr(name, '.', name_len)) {
        uint16_t tmp_slot = 0;
        int leaf_type = 0, leaf_size = 0, scalar_off = 0, final_off = 0, nseg = 0;
        int nested_off[CHAIN_MAX_NESTED];
        int nested_stride[CHAIN_MAX_NESTED];
        int r = source_try_begin_struct_field_store(fe, cs, name, name_len, &p,
                                                    &tmp_slot, &leaf_type, &leaf_size,
                                                    &scalar_off, &final_off, &nseg,
                                                    nested_off, nested_stride);
        if (r == 2) {
            // Walker already emitted any nested-index exprs; RHS goes next.
            source_skip_space(&p);
            if (*p != '=') {
                bc_set_error(cs, "Expected '='");
                *pp = p; return;
            }
            p++;
            uint8_t etype = source_parse_expression(fe, cs, &p);
            if (cs->has_error) { *pp = p; return; }
            if ((leaf_type == T_STR) && !(etype & T_STR)) {
                bc_set_error(cs, "Cannot assign numeric expression to string member");
                *pp = p; return;
            }
            if ((leaf_type != T_STR) && (etype & T_STR)) {
                bc_set_error(cs, "Cannot assign string expression to numeric member");
                *pp = p; return;
            }
            if ((leaf_type == T_INT) && (etype & T_NBR)) bc_emit_byte(cs, OP_CVT_F2I);
            else if ((leaf_type == T_NBR) && (etype & T_INT)) bc_emit_byte(cs, OP_CVT_I2F);
            source_emit_struct_chain_store_finish(cs, tmp_slot, /*outer_ndim=*/0,
                                                  leaf_type, leaf_size,
                                                  scalar_off, final_off,
                                                  nseg, nested_off, nested_stride);
            *pp = p;
            return;
        }
        if (r == 1) { *pp = p; return; }                   // error already reported
    }

    int is_local = 0;
    uint16_t slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
    source_skip_space(&p);
    if (*p == '(') {
        int ndim = source_parse_array_indices(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        if (!is_local && slot != 0xFFFF) cs->slots[slot].is_array = 1;
        source_skip_space(&p);

        /* Struct array element field store — `points(i).x = ...` through
         * `points(i).b(j).c = ...`.  The chain walker emits nested-index
         * expressions while it walks; we then parse the RHS (also pushed) and
         * call the finish helper to emit the right opcode. */
        if (!is_local && slot != 0xFFFF && cs->slots[slot].type == T_STRUCT && *p == '.') {
            int sidx = cs->slots[slot].struct_idx;
            if (sidx < 0 || sidx >= g_structcnt || g_structtbl[sidx] == NULL) {
                bc_set_error(cs, "Invalid struct type on slot");
                *pp = p; return;
            }
            int leaf_type = 0, leaf_size = 0, scalar_off = 0, final_off = 0, nseg = 0;
            int nested_off[CHAIN_MAX_NESTED];
            int nested_stride[CHAIN_MAX_NESTED];
            if (!source_emit_struct_chain_store(fe, cs, slot, ndim,
                                                NULL, 0, &p,
                                                &leaf_type, &leaf_size,
                                                &scalar_off, &final_off, &nseg,
                                                nested_off, nested_stride)) {
                *pp = p; return;
            }
            source_skip_space(&p);
            if (*p != '=') {
                bc_set_error(cs, "Expected '='");
                *pp = p; return;
            }
            p++;
            uint8_t etype = source_parse_expression(fe, cs, &p);
            if (cs->has_error) { *pp = p; return; }
            if ((leaf_type == T_STR) && !(etype & T_STR)) {
                bc_set_error(cs, "Cannot assign numeric expression to string member");
                *pp = p; return;
            }
            if ((leaf_type != T_STR) && (etype & T_STR)) {
                bc_set_error(cs, "Cannot assign string expression to numeric member");
                *pp = p; return;
            }
            if ((leaf_type == T_INT) && (etype & T_NBR)) bc_emit_byte(cs, OP_CVT_F2I);
            else if ((leaf_type == T_NBR) && (etype & T_INT)) bc_emit_byte(cs, OP_CVT_I2F);
            source_emit_struct_chain_store_finish(cs, slot, ndim,
                                                  leaf_type, leaf_size,
                                                  scalar_off, final_off,
                                                  nseg, nested_off, nested_stride);
            *pp = p;
            return;
        }

        if (*p != '=') {
            bc_set_error(cs, "Expected '='");
            *pp = p;
            return;
        }
        p++;

        uint8_t etype = source_parse_expression(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        if ((vtype & T_STR) && !(etype & T_STR)) {
            bc_set_error(cs, "Cannot assign numeric expression to string array");
            *pp = p;
            return;
        }
        if (!(vtype & T_STR) && (etype & T_STR)) {
            bc_set_error(cs, "Cannot assign string expression to numeric array");
            *pp = p;
            return;
        }
        if ((vtype & T_INT) && (etype & T_NBR)) bc_emit_byte(cs, OP_CVT_F2I);
        else if ((vtype & T_NBR) && (etype & T_INT)) bc_emit_byte(cs, OP_CVT_I2F);
        source_emit_store_array(cs, slot, vtype, is_local, ndim);
        *pp = p;
        return;
    }

    if (*p != '=') {
        bc_set_error(cs, "Expected '='");
        *pp = p;
        return;
    }
    p++;

    uint32_t expr_start = cs->code_len;
    uint8_t etype = source_parse_expression(fe, cs, &p);
    if (cs->has_error) {
        *pp = p;
        return;
    }

    if ((vtype & T_STR) && !(etype & T_STR)) {
        bc_set_error(cs, "Cannot assign numeric expression to string variable");
        *pp = p;
        return;
    }
    if (!(vtype & T_STR) && (etype & T_STR)) {
        bc_set_error(cs, "Cannot assign string expression to numeric variable");
        *pp = p;
        return;
    }
    if (source_try_fuse_mov_assignment(cs, expr_start, slot, is_local, vtype, etype)) {
        *pp = p;
        return;
    }
    source_emit_store_converted(cs, slot, vtype, etype, is_local);

    *pp = p;
}

static void source_compile_for(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t vtype = 0;
    if (!source_parse_varname(&p, name, &name_len, &vtype)) {
        bc_set_error(cs, "Expected variable after FOR");
        *pp = p;
        return;
    }
    if (vtype == 0) {
        vtype = source_default_var_type(cs, name, name_len);
    }

    int is_local = 0;
    source_skip_space(&p);
    if (*p != '=') {
        bc_set_error(cs, "Expected '=' in FOR");
        *pp = p;
        return;
    }
    p++;

    uint16_t var_slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
    uint8_t start_type = source_parse_expression(fe, cs, &p);
    if (cs->has_error) {
        *pp = p;
        return;
    }
    source_emit_store_converted(cs, var_slot, vtype, start_type, is_local);

    source_skip_space(&p);
    if (!source_keyword(&p, "TO")) {
        bc_set_error(cs, "Expected TO in FOR");
        *pp = p;
        return;
    }

    int lim_is_local = (cs->current_subfun >= 0);
    uint16_t lim_slot, step_slot;
    if (lim_is_local) {
        lim_slot = source_alloc_hidden_local(cs, vtype);
        step_slot = source_alloc_hidden_local(cs, vtype);
    } else {
        lim_slot = source_alloc_hidden_slot(cs, vtype);
        step_slot = source_alloc_hidden_slot(cs, vtype);
    }

    uint8_t limit_type = source_parse_expression(fe, cs, &p);
    if (cs->has_error) {
        *pp = p;
        return;
    }
    source_emit_store_converted(cs, lim_slot, vtype, limit_type, lim_is_local);

    source_skip_space(&p);
    if (source_keyword(&p, "STEP")) {
        uint8_t step_type = source_parse_expression(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        source_emit_store_converted(cs, step_slot, vtype, step_type, lim_is_local);
    } else {
        if (vtype == T_INT) bc_emit_byte(cs, OP_PUSH_ONE);
        else {
            bc_emit_byte(cs, OP_PUSH_FLT);
            bc_emit_f64(cs, 1.0);
        }
        bc_emit_store_var(cs, step_slot, vtype, lim_is_local);
    }

    uint16_t enc_var = var_slot | (is_local ? 0x8000 : 0);
    uint16_t enc_lim = lim_slot | (lim_is_local ? 0x8000 : 0);
    uint16_t enc_step = step_slot | (lim_is_local ? 0x8000 : 0);

    bc_emit_byte(cs, (vtype == T_INT) ? OP_FOR_INIT_I : OP_FOR_INIT_F);
    bc_emit_u16(cs, enc_var);
    bc_emit_u16(cs, enc_lim);
    bc_emit_u16(cs, enc_step);
    uint32_t exit_patch = cs->code_len;
    bc_emit_i16(cs, 0);

    uint32_t loop_top = cs->code_len;
    bc_nest_push(cs, NEST_FOR);
    BCNestEntry *ne = bc_nest_top(cs);
    if (ne) {
        ne->addr1 = loop_top;
        ne->addr2 = exit_patch;
        ne->var_slot = enc_var;
        ne->lim_slot = enc_lim;
        ne->step_slot = enc_step;
        ne->var_type = vtype;
    }

    *pp = p;
}

static void source_compile_next(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    if (fe->fast_next_loop) {
        fe->fast_next_loop = 0;
        bc_set_error(cs, "'!FAST not yet supported for FOR loops (use DO WHILE instead)");
        return;
    }
    BCNestEntry *ne = bc_nest_find(cs, NEST_FOR);
    if (!ne) {
        bc_set_error(cs, "NEXT without matching FOR");
        return;
    }

    bc_emit_byte(cs, (ne->var_type == T_INT) ? OP_FOR_NEXT_I : OP_FOR_NEXT_F);
    bc_emit_u16(cs, ne->var_slot);
    bc_emit_u16(cs, ne->lim_slot);
    bc_emit_u16(cs, ne->step_slot);
    bc_emit_i16(cs, (int16_t)(ne->addr1 - (cs->code_len + 2)));

    bc_patch_i16(cs, ne->addr2, (int16_t)(cs->code_len - (ne->addr2 + 2)));
    for (int i = 0; i < ne->exit_fixup_count; i++)
        source_patch_jmp_here(cs, ne->exit_fixups[i]);
    bc_nest_pop(cs);

    const char *p = *pp;
    source_skip_space(&p);
    if (isnamestart((unsigned char)*p)) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t type = 0;
        source_parse_varname(&p, name, &name_len, &type);
    }
    *pp = p;
}

static int source_parse_file_number(BCCompiler *cs, const char **pp, int *fnbr) {
    const char *p = *pp;
    char *end = NULL;
    long value;

    source_skip_space(&p);
    if (*p != '#') {
        bc_set_error(cs, "Expected file number");
        *pp = p;
        return 0;
    }
    p++;
    source_skip_space(&p);
    if (!isdigit((unsigned char)*p)) {
        bc_set_error(cs, "Expected file number");
        *pp = p;
        return 0;
    }

    value = strtol(p, &end, 10);
    if (value < 1 || value > MAXOPENFILES) {
        bc_set_error(cs, "File number");
        *pp = end;
        return 0;
    }
    *fnbr = (int)value;
    *pp = end;
    return 1;
}

static void source_emit_file_print_expr(BCSourceFrontend *fe, BCCompiler *cs,
                                        const char **pp, int fnbr) {
    uint8_t type = source_parse_expression(fe, cs, pp);
    uint16_t sysid;
    uint8_t aux[2];

    if (cs->has_error) return;
    switch (type & (T_INT | T_NBR | T_STR)) {
        case T_INT: sysid = BC_SYS_FILE_PRINT_INT; break;
        case T_NBR: sysid = BC_SYS_FILE_PRINT_FLT; break;
        case T_STR: sysid = BC_SYS_FILE_PRINT_STR; break;
        default:
            bc_set_error(cs, "Invalid PRINT # expression");
            return;
    }
    aux[0] = (uint8_t)(fnbr & 0xFF);
    aux[1] = (uint8_t)(fnbr >> 8);
    source_emit_syscall(cs, sysid, 1, aux, 2);
}

static void source_compile_file_print(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int fnbr = 0;
    int suppress_newline = 0;

    if (!source_parse_file_number(cs, &p, &fnbr)) {
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (*p == ',' || *p == ';') p++;

    while (*p && *p != '\'') {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') break;
        if (*p == ';') {
            suppress_newline = 1;
            p++;
            continue;
        }
        if (*p == ',') {
            uint16_t tab = bc_add_constant_string(cs, (const uint8_t *)"\t", 1);
            bc_emit_byte(cs, OP_PUSH_STR);
            bc_emit_u16(cs, tab);
            {
                uint8_t aux[2] = {(uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
                source_emit_syscall(cs, BC_SYS_FILE_PRINT_STR, 1, aux, 2);
            }
            suppress_newline = 1;
            p++;
            continue;
        }

        suppress_newline = 0;
        source_emit_file_print_expr(fe, cs, &p, fnbr);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        source_skip_space(&p);
        if (*p == ',' || *p == ';') continue;
        break;
    }

    if (!suppress_newline) {
        uint8_t aux[2] = {(uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
        source_emit_syscall(cs, BC_SYS_FILE_PRINT_NEWLINE, 0, aux, 2);
    }
    *pp = p;
}

static void source_compile_print(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int suppress_newline = 0;

    source_skip_space(&p);
    if (*p == '#') {
        source_compile_file_print(fe, cs, &p);
        *pp = p;
        return;
    }

    while (*p && *p != '\'') {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') break;
        if (*p == ';') {
            suppress_newline = 1;
            p++;
            continue;
        }
        if (*p == ',') {
            bc_emit_byte(cs, OP_PRINT_TAB);
            suppress_newline = 1;
            p++;
            continue;
        }

        /* @(x,y) — move the text cursor to pixel (x,y). PRINT-only
         * syntax; fun_at on the interpreter side rejects it elsewhere.
         * Emit a PRINT_AT syscall and loop back — @ doesn't contribute
         * output, it just has the side effect of setting CurrentX/Y so
         * the next argument's glyphs land there. Optional third arg
         * (PrintPixelMode) is accepted and ignored: the VM's canvas
         * path doesn't implement inverse/underline modes yet, but we
         * swallow it so programs written for the device still compile. */
        if (*p == '@') {
            p++;
            source_skip_space(&p);
            if (*p != '(') {
                bc_set_error(cs, "Expected '(' after @");
                *pp = p;
                return;
            }
            p++;
            uint8_t xt = source_parse_expression(fe, cs, &p);
            if (cs->has_error) { *pp = p; return; }
            source_emit_int_conversion(cs, xt);
            source_skip_space(&p);
            if (*p != ',') {
                bc_set_error(cs, "Expected ',' after x in @(x,y)");
                *pp = p;
                return;
            }
            p++;
            uint8_t yt = source_parse_expression(fe, cs, &p);
            if (cs->has_error) { *pp = p; return; }
            source_emit_int_conversion(cs, yt);
            source_skip_space(&p);
            if (*p == ',') {
                /* PrintPixelMode — parse and discard (see above) */
                p++;
                uint8_t mt = source_parse_expression(fe, cs, &p);
                if (cs->has_error) { *pp = p; return; }
                source_emit_int_conversion(cs, mt);
                bc_emit_byte(cs, OP_POP);
                source_skip_space(&p);
            }
            if (*p != ')') {
                bc_set_error(cs, "Expected ')' in @(x,y)");
                *pp = p;
                return;
            }
            p++;
            source_emit_syscall_noaux(cs, BC_SYS_PRINT_AT, 0);
            suppress_newline = 1;  /* @ alone shouldn't emit a newline */
            source_skip_space(&p);
            if (*p == ',' || *p == ';') continue;
            /* No separator after @(x,y) — the next token is the value
             * to print at that position. Fall through to the normal
             * expression path, which reads it on the next loop turn. */
            continue;
        }

        suppress_newline = 0;
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }

        uint8_t op;
        switch (type & (T_INT | T_NBR | T_STR)) {
            case T_INT: op = OP_PRINT_INT; break;
            case T_NBR: op = OP_PRINT_FLT; break;
            case T_STR: op = OP_PRINT_STR; break;
            default:    op = OP_PRINT_INT; break;
        }
        bc_emit_byte(cs, op);
        bc_emit_byte(cs, PRINT_NO_NEWLINE);
        source_skip_space(&p);
        if (*p == ',' || *p == ';') continue;
        break;
    }

    if (!suppress_newline) bc_emit_byte(cs, OP_PRINT_NEWLINE);
    *pp = p;
}

/* Helper for GOTO / GOSUB: parse either a numeric line number or a
 * BASIC label name. Emits the jump opcode + a 4-byte absolute offset
 * patched in place if the target is already known, otherwise queues a
 * fixup resolved at end-of-compile. */
static int source_emit_jump_to_target(BCCompiler *cs, const char **pp,
                                      uint8_t opcode, const char *what) {
    const char *p = *pp;
    source_skip_space(&p);
    if (isdigit((unsigned char)*p)) {
        int lineno = source_parse_line_number(&p);
        if (lineno < 0) {
            bc_set_error(cs, "Expected line number after %s", what);
            *pp = p;
            return 0;
        }
        source_emit_abs_jump(cs, opcode, lineno);
        *pp = p;
        return 1;
    }
    if (isalpha((unsigned char)*p) || *p == '_') {
        char name[BC_MAX_LABEL_NAME + 1];
        int n = 0;
        while ((isalnum((unsigned char)*p) || *p == '_' || *p == '.') &&
               n < BC_MAX_LABEL_NAME) {
            name[n++] = *p++;
        }
        name[n] = '\0';
        bc_emit_byte(cs, opcode);
        uint32_t patch = cs->code_len;
        bc_emit_u32(cs, 0);
        uint32_t target = bc_labelmap_lookup(cs, name);
        if (target != 0xFFFFFFFF) {
            bc_patch_u32(cs, patch, target);
        } else {
            bc_add_fixup_label(cs, patch, name, 4, 0);
        }
        *pp = p;
        return 1;
    }
    bc_set_error(cs, "Expected line number or label after %s", what);
    *pp = p;
    return 0;
}

static void source_compile_goto(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    (void)fe;
    source_emit_jump_to_target(cs, pp, OP_JMP_ABS, "GOTO");
}

static void source_compile_gosub(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    (void)fe;
    source_emit_jump_to_target(cs, pp, OP_GOSUB, "GOSUB");
}

static void source_compile_const(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;

    while (!cs->has_error) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t vtype = 0;
        if (!source_parse_varname(&p, name, &name_len, &vtype)) {
            bc_set_error(cs, "Expected name in CONST");
            *pp = p;
            return;
        }

        source_skip_space(&p);
        if (*p != '=') {
            bc_set_error(cs, "Expected = in CONST");
            *pp = p;
            return;
        }
        p++;

        uint32_t expr_start = cs->code_len;
        uint8_t etype = source_parse_expression(fe, cs, &p);
        if (cs->has_error) {
            *pp = p;
            return;
        }
        if (vtype == 0) vtype = etype;

        if ((vtype & T_STR) && !(etype & T_STR)) {
            bc_set_error(cs, "Cannot assign numeric expression to string constant");
            *pp = p;
            return;
        }
        if (!(vtype & T_STR) && (etype & T_STR)) {
            bc_set_error(cs, "Cannot assign string expression to numeric constant");
            *pp = p;
            return;
        }
        uint16_t slot = source_resolve_global(cs, name, name_len, vtype, 1);

        /* Constant inlining: if the expression is a simple literal, record
         * the value in the slot and elide the store.  Every subsequent
         * LOAD of this slot will emit PUSH_INT/PUSH_FLT instead. */
        uint32_t expr_len = cs->code_len - expr_start;
        if (expr_len == 9 && cs->code[expr_start] == OP_PUSH_INT &&
            (vtype & T_INT)) {
            int64_t val;
            memcpy(&val, &cs->code[expr_start + 1], sizeof(val));
            cs->slots[slot].is_const = 1;
            cs->slots[slot].const_ival = val;
            cs->code_len = expr_start;  /* roll back — no runtime code needed */
        } else if (expr_len == 9 && cs->code[expr_start] == OP_PUSH_FLT &&
                   (vtype & T_NBR)) {
            double val;
            memcpy(&val, &cs->code[expr_start + 1], sizeof(val));
            cs->slots[slot].is_const = 1;
            cs->slots[slot].const_fval = val;
            cs->code_len = expr_start;
        } else {
            source_emit_store_converted(cs, slot, vtype, etype, 0);
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    *pp = p;
}

// Peek ahead on a DIM-arg sub-expression to see if its AS clause references
// a registered struct type.  Returns struct_idx on hit, -1 otherwise.  Does
// not consume p.  Used so source_compile_dim can decide whether to bridge.
static int source_peek_dim_struct_type(const char *p) {
    while (*p && *p != ',' && *p != '\'' && *p != '\n') {
        if ((p[0] == 'A' || p[0] == 'a') && (p[1] == 'S' || p[1] == 's') &&
            !isnamechar((unsigned char)p[2])) {
            const char *q = p + 2;
            while (*q == ' ' || *q == '\t') q++;
            if (strncasecmp(q, "INTEGER", 7) == 0 && !isnamechar((unsigned char)q[7])) return -1;
            if (strncasecmp(q, "FLOAT",   5) == 0 && !isnamechar((unsigned char)q[5])) return -1;
            if (strncasecmp(q, "STRING",  6) == 0 && !isnamechar((unsigned char)q[6])) return -1;
            if (isnamestart((unsigned char)*q)) return FindStructType((unsigned char *)q);
            return -1;
        }
        p++;
    }
    return -1;
}

// Tokenize a DIM/other statement through MMBasic's tokeniser and emit a
// BRIDGE_CMD with the resulting bytes.  Used to route DIM-of-struct through
// the interpreter's cmd_dim so struct memory is allocated in g_vartbl (and
// the post-bridge sync picks it up for subsequent VM field accesses).
static void source_emit_bridge_for_stmt(BCCompiler *cs, const char *stmt) {
    unsigned char saved_inpbuf[STRINGSIZE];
    unsigned char saved_tknbuf[STRINGSIZE];
    memcpy(saved_inpbuf, inpbuf, STRINGSIZE);
    memcpy(saved_tknbuf, tknbuf, STRINGSIZE);

    size_t slen = strlen(stmt);
    if (slen >= STRINGSIZE) slen = STRINGSIZE - 1;
    memcpy(inpbuf, stmt, slen);
    inpbuf[slen] = 0;

    tokenise(1);

    unsigned char *tp = tknbuf;
    while (*tp) {
        if (*tp == T_LINENBR) { tp += 3; continue; }
        tp++;
    }
    uint16_t tok_len = (uint16_t)(tp - tknbuf);

    if (tok_len < 2) {
        bc_set_error(cs, "Unable to tokenise DIM statement");
    } else {
        bc_emit_byte(cs, OP_BRIDGE_CMD);
        bc_emit_u16(cs, tok_len);
        for (uint16_t i = 0; i < tok_len; i++) bc_emit_byte(cs, tknbuf[i]);
    }

    memcpy(inpbuf, saved_inpbuf, STRINGSIZE);
    memcpy(tknbuf, saved_tknbuf, STRINGSIZE);
}

// Phase 13: DIM arg contains `(` sized array but no `AS <struct>` and no
// `LENGTH`.  Used with fe->uses_struct_extract_insert to bridge these DIMs
// so their storage is contiguous g_vartbl memory, matching what cmd_struct
// EXTRACT/INSERT assumes when it memcpys into/out of the destination array.
static int source_peek_dim_has_sized_array(const char *p) {
    while (*p && *p != ',' && *p != '\'' && *p != '\n') {
        if (*p == '(') {
            const char *q = p + 1;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != ')') return 1;     // non-empty parens → sized
            return 0;
        }
        p++;
    }
    return 0;
}

static void source_compile_dim(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    uint8_t forced_type = 0;

    source_skip_space(&p);
    if (strncasecmp(p, "INTEGER", 7) == 0 && !isnamechar((unsigned char)p[7])) {
        forced_type = T_INT;
        p += 7;
    } else if (strncasecmp(p, "FLOAT", 5) == 0 && !isnamechar((unsigned char)p[5])) {
        forced_type = T_NBR;
        p += 5;
    } else if (strncasecmp(p, "STRING", 6) == 0 && !isnamechar((unsigned char)p[6])) {
        forced_type = T_STR;
        p += 6;
    }

    // If any DIM-arg uses `AS <structname>`, delegate the whole statement to
    // the interpreter: we still have to register the slot in cs->slots (so
    // later pt.x accesses resolve) but the actual allocation and initialisation
    // happens via cmd_dim under BRIDGE_CMD.  Phase 1 doesn't handle mixed-kind
    // DIMs (scalar + struct on one line) — bridge still works in that case,
    // but the non-struct slots won't get the native OP_DIM_ARR_* fast path.
    {
        const char *peek = p;
        int needs_bridge = 0;
        while (*peek && *peek != '\n') {
            if (source_peek_dim_struct_type(peek) >= 0) { needs_bridge = 1; break; }
            while (*peek && *peek != ',') peek++;
            if (*peek == ',') peek++;
        }
        /* Phase 13: DIM with an explicit LENGTH clause (`DIM s$(n) LENGTH m`)
         * isn't modelled by our native DIM compiler — bridge the whole line
         * to cmd_dim which handles LENGTH natively.  Substring scan is fine
         * since `LENGTH` never appears inside a struct type name. */
        if (!needs_bridge) {
            const char *scan = p;
            int in_str = 0;
            while (*scan && *scan != '\n' && *scan != '\'') {
                if (*scan == '"') { in_str = !in_str; scan++; continue; }
                if (!in_str && (scan[0] == 'L' || scan[0] == 'l') &&
                    strncasecmp(scan, "LENGTH", 6) == 0 &&
                    (scan == p || !isnamechar((unsigned char)scan[-1])) &&
                    !isnamechar((unsigned char)scan[6])) {
                    needs_bridge = 1;
                    break;
                }
                scan++;
            }
        }
        /* Phase 13: if the program uses STRUCT EXTRACT or STRUCT INSERT, any
         * DIM that allocates a non-struct sized array goes through the bridge
         * so the storage lives in g_vartbl's contiguous layout — the VM's
         * native BCValue[] layout for T_STR arrays is incompatible with
         * cmd_struct's memcpy assumption.  Blunt for non-string types too,
         * since the bridge path is safe for int/float arrays and the prescan
         * flag is specific to programs that already bridge struct commands. */
        if (!needs_bridge && fe && fe->uses_struct_extract_insert) {
            const char *peek_arr = p;
            while (*peek_arr && *peek_arr != '\n') {
                if (source_peek_dim_has_sized_array(peek_arr)) {
                    needs_bridge = 1;
                    break;
                }
                while (*peek_arr && *peek_arr != ',') peek_arr++;
                if (*peek_arr == ',') peek_arr++;
            }
        }

        if (needs_bridge) {
            // Register each arg's slot with T_STRUCT + struct_idx (or skip
            // through non-struct args; they just get compile-time resolution
            // later when referenced).  We purposely don't emit any stores
            // here — cmd_dim handles initialisation on the bridge side.
            const char *walk = p;
            while (*walk && *walk != '\n') {
                char name[MAXVARLEN + 1];
                int  name_len = 0;
                uint8_t suffix_type = 0;
                if (!source_parse_varname(&walk, name, &name_len, &suffix_type)) break;
                source_skip_space(&walk);
                // Skip over any array subscript `(...)` before looking for AS
                // so `DIM points(5) AS Point` registers the slot correctly.
                int is_array = 0;
                int arr_ndim = 0;
                if (*walk == '(') {
                    is_array = 1;
                    arr_ndim = 1;
                    walk++;
                    int depth = 1;
                    while (*walk && depth > 0) {
                        if (*walk == '(') depth++;
                        else if (*walk == ')') { depth--; if (!depth) break; }
                        else if (*walk == ',' && depth == 1) arr_ndim++;
                        walk++;
                    }
                    if (*walk == ')') walk++;
                    source_skip_space(&walk);
                }
                int sidx = -1;
                uint8_t as_type = source_parse_as_type_clause_ex(&walk, &sidx);
                uint8_t vtype = suffix_type ? suffix_type :
                                (as_type ? as_type :
                                 (forced_type ? forced_type : T_NBR));
                if (vtype == T_STRUCT) {
                    uint16_t slot = source_resolve_global(cs, name, name_len, vtype, 1);
                    if (slot != 0xFFFF) {
                        cs->slots[slot].type = T_STRUCT;
                        cs->slots[slot].struct_idx = (int16_t)sidx;
                        cs->slots[slot].is_array = (uint8_t)is_array;
                        cs->slots[slot].ndims = (uint8_t)arr_ndim;
                    }
                } else if (is_array) {
                    /* Phase 13: a bridged DIM that allocates a non-struct
                     * array (e.g. `DIM s$(n) LENGTH m`) still needs the slot
                     * registered so later VM references emit the right
                     * OP_LOAD_ARR_* opcode.  cmd_dim owns the allocation on
                     * the bridge side; here we just claim the slot and tag
                     * it as an array of the right type. */
                    uint16_t slot = source_resolve_global(cs, name, name_len, vtype, 1);
                    if (slot != 0xFFFF) {
                        cs->slots[slot].type = vtype;
                        cs->slots[slot].is_array = 1;
                        cs->slots[slot].ndims = (uint8_t)arr_ndim;
                    }
                }
                while (*walk && *walk != ',' && *walk != '\n') walk++;
                if (*walk == ',') walk++;
            }

            // Build the bridge statement "DIM <original args>" and emit.
            char buf[STRINGSIZE];
            int n = snprintf(buf, sizeof(buf), "DIM %s", *pp);
            if (n > 0 && (size_t)n < sizeof(buf)) {
                // Trim trailing comment/newline.
                char *eol = buf;
                while (*eol && *eol != '\n' && *eol != '\'') eol++;
                *eol = 0;
                source_emit_bridge_for_stmt(cs, buf);
            }

            // Advance pp past the DIM args.
            while (*p && *p != '\n' && *p != '\'') p++;
            *pp = p;
            return;
        }
    }

    while (!cs->has_error) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t suffix_type = 0;
        if (!source_parse_varname(&p, name, &name_len, &suffix_type)) {
            bc_set_error(cs, "Expected name in DIM");
            *pp = p;
            return;
        }
        uint8_t vtype = suffix_type ? suffix_type : (forced_type ? forced_type : T_NBR);
        source_skip_space(&p);

        if (*p == '(') {
            p++;
            int ndim = 0;
            while (!cs->has_error) {
                uint8_t dtype = source_parse_expression(fe, cs, &p);
                source_emit_int_conversion(cs, dtype);
                ndim++;
                source_skip_space(&p);
                if (*p != ',') break;
                p++;
            }
            if (*p != ')') {
                bc_set_error(cs, "Expected ')' in DIM");
                *pp = p;
                return;
            }
            p++;
            uint8_t as_type = source_parse_as_type_clause(&p);
            if (as_type && !suffix_type) vtype = as_type;

            uint16_t slot = source_resolve_global(cs, name, name_len, vtype, 1);
            if (slot != 0xFFFF) cs->slots[slot].is_array = 1;
            uint8_t dim_op = (vtype == T_INT) ? OP_DIM_ARR_I :
                             (vtype == T_STR) ? OP_DIM_ARR_S :
                                                OP_DIM_ARR_F;
            bc_emit_byte(cs, dim_op);
            bc_emit_u16(cs, slot);
            bc_emit_byte(cs, (uint8_t)ndim);
        } else {
            uint8_t as_type = source_parse_as_type_clause(&p);
            if (as_type && !suffix_type) vtype = as_type;
            uint16_t slot = source_resolve_global(cs, name, name_len, vtype, 1);

            source_skip_space(&p);
            if (*p == '=') {
                p++;
                uint8_t etype = source_parse_expression(fe, cs, &p);
                if ((vtype & T_STR) && !(etype & T_STR)) {
                    bc_set_error(cs, "Cannot assign numeric expression to string variable");
                    *pp = p;
                    return;
                }
                if (!(vtype & T_STR) && (etype & T_STR)) {
                    bc_set_error(cs, "Cannot assign string expression to numeric variable");
                    *pp = p;
                    return;
                }
                source_emit_store_converted(cs, slot, vtype, etype, 0);
            }
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    *pp = p;
}

static void source_compile_data(BCCompiler *cs, const char **pp) {
    const char *p = *pp;

    while (!cs->has_error) {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') break;
        if (cs->data_count >= BC_MAX_DATA_ITEMS) {
            bc_set_error(cs, "Too many DATA items");
            break;
        }

        BCDataItem *item = &cs->data_pool[cs->data_count];
        if (*p == '"') {
            p++;
            const char *start = p;
            while (*p && *p != '"') p++;
            uint16_t cidx = bc_add_constant_string(cs, (const uint8_t *)start, (int)(p - start));
            if (*p == '"') p++;
            item->value.i = cidx;
            item->type = T_STR;
            cs->data_count++;
        } else if (*p == '+' || *p == '-' || *p == '.' || isdigit((unsigned char)*p)) {
            char *end = NULL;
            double v = strtod(p, &end);
            if (end == p) {
                bc_set_error(cs, "Invalid DATA value");
                break;
            }
            int is_float = 0;
            for (const char *q = p; q < end; q++) {
                if (*q == '.' || *q == 'e' || *q == 'E') {
                    is_float = 1;
                    break;
                }
            }
            if (is_float) {
                item->value.f = (MMFLOAT)v;
                item->type = T_NBR;
            } else {
                item->value.i = (int64_t)strtoll(p, NULL, 10);
                item->type = T_INT;
            }
            p = end;
            cs->data_count++;
        } else {
            const char *start = p;
            while (*p && *p != ',' && *p != '\'') p++;
            const char *end = p;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
            uint16_t cidx = bc_add_constant_string(cs, (const uint8_t *)start, (int)(end - start));
            item->value.i = cidx;
            item->type = T_STR;
            cs->data_count++;
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    while (*p && *p != '\'') p++;
    *pp = p;
}

static void source_compile_read(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;

    while (!cs->has_error) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t vtype = 0;
        source_skip_space(&p);
        if (!source_parse_varname(&p, name, &name_len, &vtype)) {
            bc_set_error(cs, "Expected variable in READ");
            *pp = p;
            return;
        }
        if (vtype == 0) vtype = source_default_var_type(cs, name, name_len);

        const char *after_name = p;
        source_skip_space(&after_name);
        if (*after_name == '(') {
            p = after_name;
            int ndim = source_parse_array_indices(fe, cs, &p);
            int is_local = 0;
            uint16_t slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
            if (!is_local && slot != 0xFFFF) cs->slots[slot].is_array = 1;
            bc_emit_byte(cs, (vtype == T_INT) ? OP_READ_I :
                             (vtype == T_STR) ? OP_READ_S : OP_READ_F);
            source_emit_store_array(cs, slot, vtype, is_local, ndim);
        } else {
            p = after_name;
            int is_local = 0;
            uint16_t slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
            bc_emit_byte(cs, (vtype == T_INT) ? OP_READ_I :
                             (vtype == T_STR) ? OP_READ_S : OP_READ_F);
            bc_emit_store_var(cs, slot, vtype, is_local);
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    *pp = p;
}

static void source_compile_inc(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t vtype = 0;
    if (!source_parse_varname(&p, name, &name_len, &vtype)) {
        bc_set_error(cs, "Expected variable in INC");
        *pp = p;
        return;
    }
    if (vtype == 0) vtype = source_default_var_type(cs, name, name_len);
    if (vtype == T_STR) {
        bc_set_error(cs, "Unsupported source command: INC string");
        *pp = p;
        return;
    }

    int is_local = 0;
    uint16_t slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
    bc_emit_load_var(cs, slot, vtype, is_local);

    source_skip_space(&p);
    uint32_t right_start = cs->code_len;
    uint8_t amount_type;
    if (*p == ',') {
        p++;
        amount_type = source_parse_expression(fe, cs, &p);
    } else {
        bc_emit_byte(cs, OP_PUSH_ONE);
        amount_type = T_INT;
    }

    uint8_t result_type = source_emit_numeric_binary(cs, vtype, amount_type, right_start, '+');
    source_emit_store_converted(cs, slot, vtype, result_type, is_local);
    *pp = p;
}

// Lightweight pre-scan — returns 1 if any parameter in the `(...)` list
// starting at `p` is declared `As <structtype>` (scalar or array).  Used
// by the SUB / FUNCTION compilers to decide whether to skip native
// compilation of the body and bridge call sites through the interpreter.
static int source_params_contain_struct(const char *p) {
    source_skip_space(&p);
    if (*p != '(') return 0;
    p++;
    while (*p && *p != ')') {
        source_skip_space(&p);
        while (isnamechar((unsigned char)*p)) p++;   // name (may include dots)
        if (*p == '$' || *p == '%' || *p == '!') p++;
        source_skip_space(&p);
        if (*p == '(') {                              // `name()` — array param
            int d = 0;
            do {
                if (*p == '(') d++;
                else if (*p == ')') d--;
                else if (*p == 0) return 0;
                p++;
            } while (d > 0);
            source_skip_space(&p);
        }
        // Optional `AS <type>` clause.  Only struct types trigger bridging.
        if ((p[0] == 'A' || p[0] == 'a') && (p[1] == 'S' || p[1] == 's') &&
            !isnamechar((unsigned char)p[2])) {
            p += 2;
            source_skip_space(&p);
            if (strncasecmp(p, "INTEGER", 7) != 0 &&
                strncasecmp(p, "FLOAT", 5) != 0 &&
                strncasecmp(p, "STRING", 6) != 0 &&
                isnamestart((unsigned char)*p)) {
                // A type name that's not a scalar keyword — check g_structtbl.
                if (FindStructType((unsigned char *)p) >= 0) return 1;
            }
            while (isnamechar((unsigned char)*p)) p++;     // skip type name
            source_skip_space(&p);
        }
        if (*p == ',') { p++; continue; }
        break;
    }
    return 0;
}

static int source_parse_params(BCCompiler *cs, const char **pp, int sf_idx) {
    const char *p = *pp;
    int nparams = 0;
    source_skip_space(&p);
    if (*p != '(') {
        *pp = p;
        return 0;
    }
    p++;

    while (!cs->has_error) {
        source_skip_space(&p);
        if (*p == ')') {
            p++;
            break;
        }
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t ptype = 0;
        if (!source_parse_varname(&p, name, &name_len, &ptype)) {
            bc_set_error(cs, "Expected parameter name");
            *pp = p;
            return nparams;
        }

        int is_array = 0;
        source_skip_space(&p);
        if (*p == '(') {
            const char *q = p + 1;
            source_skip_space(&q);
            if (*q == ')') {
                is_array = 1;
                p = q + 1;
            }
        }

        uint8_t as_type = source_parse_as_type_clause(&p);
        if (as_type != 0) ptype = as_type;
        if (ptype == 0) ptype = T_NBR;

        bc_add_local(cs, name, name_len, ptype, is_array);
        if (nparams < BC_MAX_PARAMS) {
            cs->subfuns[sf_idx].param_types[nparams] = ptype;
            cs->subfuns[sf_idx].param_is_array[nparams] = (uint8_t)is_array;
        }
        nparams++;

        source_skip_space(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ')') {
            p++;
            break;
        }
        bc_set_error(cs, "Expected ',' or ')' in parameter list");
        break;
    }

    *pp = p;
    return nparams;
}

static int source_compile_call_args(BCSourceFrontend *fe, BCCompiler *cs, const char **pp,
                                    int require_parens) {
    const char *p = *pp;
    int nargs = 0;
    source_skip_space(&p);

    int has_parens = (*p == '(');
    if (has_parens) p++;
    else if (require_parens) {
        bc_set_error(cs, "Expected '(' in function call");
        *pp = p;
        return 0;
    } else if (*p == '\0' || *p == '\'') {
        *pp = p;
        return 0;
    }

    while (!cs->has_error) {
        source_skip_space(&p);
        if (has_parens && *p == ')') {
            p++;
            break;
        }
        if (!has_parens && (*p == '\0' || *p == '\'')) break;

        (void)source_parse_expression(fe, cs, &p);
        if (cs->has_error) break;
        nargs++;

        source_skip_space(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (has_parens) {
            if (*p == ')') {
                p++;
                break;
            }
            bc_set_error(cs, "Expected ',' or ')' in argument list");
        }
        break;
    }

    *pp = p;
    return nargs;
}

static void source_compile_local(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    (void)fe;
    const char *p = *pp;
    if (cs->current_subfun < 0) {
        bc_set_error(cs, "LOCAL outside SUB/FUNCTION");
        *pp = p;
        return;
    }

    uint8_t forced_type = 0;
    source_skip_space(&p);
    if (strncasecmp(p, "INTEGER", 7) == 0 && !isnamechar((unsigned char)p[7])) {
        forced_type = T_INT;
        p += 7;
    } else if (strncasecmp(p, "FLOAT", 5) == 0 && !isnamechar((unsigned char)p[5])) {
        forced_type = T_NBR;
        p += 5;
    } else if (strncasecmp(p, "STRING", 6) == 0 && !isnamechar((unsigned char)p[6])) {
        forced_type = T_STR;
        p += 6;
    }

    while (!cs->has_error) {
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t vtype = 0;
        if (!source_parse_varname(&p, name, &name_len, &vtype)) {
            bc_set_error(cs, "Expected name in LOCAL");
            *pp = p;
            return;
        }

        int is_array = 0;
        source_skip_space(&p);
        if (*p == '(') {
            const char *q = p + 1;
            source_skip_space(&q);
            if (*q == ')') {
                is_array = 1;
                p = q + 1;
            }
        }

        uint8_t as_type = source_parse_as_type_clause(&p);
        if (as_type != 0) vtype = as_type;
        if (vtype == 0) vtype = forced_type ? forced_type : T_NBR;
        int local_idx = bc_add_local(cs, name, name_len, vtype, is_array);

        /* Optional `= expr` initialiser. The interpreter accepts
         * `LOCAL INTEGER x = 5` so the VM compiler must too. Mirrors
         * source_compile_dim's `=` handling but stores into the local
         * slot just allocated, not a global. Arrays don't take an
         * initialiser here (matches DIM). */
        source_skip_space(&p);
        if (!is_array && local_idx >= 0 && *p == '=') {
            p++;
            uint8_t etype = source_parse_expression(fe, cs, &p);
            if ((vtype & T_STR) && !(etype & T_STR)) {
                bc_set_error(cs, "Cannot assign numeric expression to string variable");
                *pp = p;
                return;
            }
            if (!(vtype & T_STR) && (etype & T_STR)) {
                bc_set_error(cs, "Cannot assign string expression to numeric variable");
                *pp = p;
                return;
            }
            source_emit_store_converted(cs, (uint16_t)local_idx, vtype, etype, 1);
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    *pp = p;
}

static void source_compile_sub(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    const char *name_start = p;
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t unused_type = 0;
    if (!source_parse_varname(&p, name, &name_len, &unused_type)) {
        bc_set_error(cs, "Expected SUB name");
        *pp = p;
        return;
    }
    if (unused_type != 0) {
        bc_set_error(cs, "SUB name cannot have a type suffix");
        *pp = p;
        return;
    }

    /* Phase 7/8: if any parameter is `As <struct>`, or the predeclare body
     * scan already flagged this SUB as bridged (Phase 8: body contains
     * `LOCAL … AS <struct>`), skip body compilation and let the interpreter
     * own the sub.  The predeclare pass populates cs->subfuns[] before the
     * main compile pass runs, so .bridged is reliable here. */
    int predecl_idx = bc_find_subfun(cs, name_start, name_len);
    int predecl_bridged = (predecl_idx >= 0 && cs->subfuns[predecl_idx].bridged);
    if (predecl_bridged || source_params_contain_struct(p)) {
        fe->in_struct_fn = 1;
        if (predecl_idx >= 0) cs->subfuns[predecl_idx].bridged = 1;
        source_skip_space(&p);
        if (*p == '(') {
            int d = 0;
            do {
                if (*p == '(') d++;
                else if (*p == ')') d--;
                else if (*p == 0) break;
                p++;
            } while (d > 0);
        }
        *pp = p;
        return;
    }

    int sf_idx = source_get_or_create_subfun(cs, name_start, name_len, 0);
    if (sf_idx < 0) {
        *pp = p;
        return;
    }

    uint32_t skip_patch = source_emit_jmp_placeholder(cs, OP_JMP);
    cs->subfuns[sf_idx].entry_addr = cs->code_len;
    cs->current_subfun = sf_idx;
    cs->local_count = 0;

    cs->subfuns[sf_idx].nparams = (uint8_t)source_parse_params(cs, &p, sf_idx);

    bc_emit_byte(cs, OP_ENTER_FRAME);
    uint32_t nlocals_patch = cs->code_len;
    bc_emit_u16(cs, 0);

    bc_nest_push(cs, NEST_SUB);
    BCNestEntry *ne = bc_nest_top(cs);
    if (ne) {
        ne->addr1 = skip_patch;
        ne->addr2 = nlocals_patch;
    }
    *pp = p;
}

static void source_compile_end_sub(BCCompiler *cs) {
    BCNestEntry *ne = bc_nest_top(cs);
    if (!ne || ne->type != NEST_SUB) {
        bc_set_error(cs, "END SUB without matching SUB");
        return;
    }
    bc_patch_u16(cs, ne->addr2, cs->local_count);
    if (cs->current_subfun >= 0) {
        cs->subfuns[cs->current_subfun].nlocals = cs->local_count;
        bc_commit_locals(cs, cs->current_subfun);
    }
    bc_emit_byte(cs, OP_LEAVE_FRAME);
    bc_emit_byte(cs, OP_RET_SUB);
    source_patch_jmp_here(cs, ne->addr1);
    cs->current_subfun = -1;
    cs->local_count = 0;
    bc_nest_pop(cs);
}

static void source_compile_function(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    const char *name_start = p;
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t ret_type = 0;
    if (!source_parse_varname(&p, name, &name_len, &ret_type)) {
        bc_set_error(cs, "Expected FUNCTION name");
        *pp = p;
        return;
    }
    int has_suffix = (ret_type != 0);
    int sf_name_len = has_suffix ? name_len - 1 : name_len;
    if (ret_type == 0) ret_type = T_NBR;

    /* Peek at the param list + `AS <type>` clause WITHOUT committing.  If
     * either the return type or any parameter is `AS <struct>`, skip body
     * compilation — the interpreter owns the function and call sites
     * bridge (struct-return goes through source_lhs_is_whole_struct;
     * struct-param subs/fns drop through to the statement-bridge). */
    {
        int has_struct_param = source_params_contain_struct(p);
        const char *peek = p;
        source_skip_space(&peek);
        if (*peek == '(') {
            int d = 0;
            do {
                if (*peek == '(') d++;
                else if (*peek == ')') d--;
                else if (*peek == 0) break;
                peek++;
            } while (d > 0);
        }
        int sidx = -1;
        uint8_t peek_type = source_parse_as_type_clause_ex(&peek, &sidx);
        int predecl_idx = bc_find_subfun(cs, name_start, sf_name_len);
        int predecl_bridged = (predecl_idx >= 0 && cs->subfuns[predecl_idx].bridged);
        if (predecl_bridged || has_struct_param || peek_type == T_STRUCT) {
            fe->in_struct_fn = 1;
            if (predecl_idx >= 0) cs->subfuns[predecl_idx].bridged = 1;
            p = peek;
            *pp = p;
            return;
        }
    }

    int sf_idx = source_get_or_create_subfun(cs, name_start, sf_name_len, ret_type);
    if (sf_idx < 0) {
        *pp = p;
        return;
    }

    uint32_t skip_patch = source_emit_jmp_placeholder(cs, OP_JMP);
    cs->subfuns[sf_idx].entry_addr = cs->code_len;
    cs->current_subfun = sf_idx;
    cs->local_count = 0;

    bc_add_local(cs, name_start, has_suffix ? name_len : sf_name_len, ret_type, 0);
    cs->subfuns[sf_idx].nparams = (uint8_t)source_parse_params(cs, &p, sf_idx);
    uint8_t as_type = source_parse_as_type_clause(&p);
    if (as_type != 0 && !has_suffix) {
        ret_type = as_type;
        cs->subfuns[sf_idx].return_type = ret_type;
        cs->locals[0].type = ret_type;
    }

    bc_emit_byte(cs, OP_ENTER_FRAME);
    uint32_t nlocals_patch = cs->code_len;
    bc_emit_u16(cs, 0);

    bc_nest_push(cs, NEST_FUNCTION);
    BCNestEntry *ne = bc_nest_top(cs);
    if (ne) {
        ne->addr1 = skip_patch;
        ne->addr2 = nlocals_patch;
        ne->var_slot = 0;
        ne->var_type = ret_type;
    }
    *pp = p;
}

static void source_compile_end_function(BCCompiler *cs) {
    BCNestEntry *ne = bc_nest_top(cs);
    if (!ne || ne->type != NEST_FUNCTION) {
        bc_set_error(cs, "END FUNCTION without matching FUNCTION");
        return;
    }
    bc_patch_u16(cs, ne->addr2, cs->local_count);
    if (cs->current_subfun >= 0) {
        cs->subfuns[cs->current_subfun].nlocals = cs->local_count;
        bc_commit_locals(cs, cs->current_subfun);
    }
    bc_emit_load_var(cs, ne->var_slot, ne->var_type, 1);
    bc_emit_byte(cs, OP_LEAVE_FRAME);
    bc_emit_byte(cs, OP_RET_FUN);
    source_patch_jmp_here(cs, ne->addr1);
    cs->current_subfun = -1;
    cs->local_count = 0;
    bc_nest_pop(cs);
}

/* ======================================================================
 * '!FAST loop converter — stack bytecode to register micro-ops
 * ====================================================================== */

/* Converter state */
typedef struct {
    uint8_t  rop[4096];          /* micro-op output buffer */
    uint32_t rop_len;

    /* Simulated stack — each entry is a register index */
    uint8_t  sim[64];
    int      sim_sp;

    /* Register allocation */
    int      nlocals;            /* regs 0..nlocals-1 = local frame slots */

    /* Global register map */
    struct { uint16_t slot; } globals[32];
    int      nglobals;

    /* Constant register map */
    struct { int64_t ival; double fval; uint8_t type; } consts[32];
    int      nconsts;

    /* Array reference map (for 1D array access in fast loop) */
    struct { uint8_t is_local; uint16_t slot; } arrays[16];
    int      narrays;

    int      temp_base;          /* first temp register index */
    int      temp_next;          /* next available temp */
    int      max_regs;           /* high-water mark */

    /* Bytecode-to-ROP offset mapping (for jump resolution) */
    uint16_t bc_to_rop[4096];    /* indexed by bc offset within loop */
    uint32_t bc_len;             /* length of loop bytecode being converted */

    /* Forward jump fixups */
    struct { uint32_t rop_addr; uint32_t bc_target; } fixups[64];
    int      fixup_count;

    /* Last line number seen (for error reporting) */
    uint16_t last_line;
} FastConv;

static void fc_emit(FastConv *fc, uint8_t b) {
    if (fc->rop_len < sizeof(fc->rop)) fc->rop[fc->rop_len++] = b;
}

static void fc_emit_u16(FastConv *fc, uint16_t v) {
    fc_emit(fc, v & 0xFF);
    fc_emit(fc, (v >> 8) & 0xFF);
}

static void fc_emit_i16(FastConv *fc, int16_t v) {
    fc_emit_u16(fc, (uint16_t)v);
}

static void fc_emit_i64(FastConv *fc, int64_t v) {
    for (int i = 0; i < 8; i++) fc_emit(fc, (v >> (i * 8)) & 0xFF);
}

static void fc_emit_f64(FastConv *fc, double v) {
    int64_t bits; memcpy(&bits, &v, 8); fc_emit_i64(fc, bits);
}

/* Push a register index onto the simulated stack */
static void fc_sim_push(FastConv *fc, uint8_t reg) {
    if (fc->sim_sp < 63) fc->sim[++fc->sim_sp] = reg;
}

static uint8_t fc_sim_pop(FastConv *fc) {
    return (fc->sim_sp >= 0) ? fc->sim[fc->sim_sp--] : 0;
}

/* Allocate a temporary register */
static uint8_t fc_alloc_temp(FastConv *fc) {
    uint8_t r = (uint8_t)fc->temp_next++;
    if (fc->temp_next > fc->max_regs) fc->max_regs = fc->temp_next;
    return r;
}

/* Reset temp allocator at statement boundary */
static void fc_reset_temps(FastConv *fc) {
    fc->temp_next = fc->temp_base;
}

/* Find or create a global register mapping */
static uint8_t fc_global_reg(FastConv *fc, uint16_t slot) {
    for (int i = 0; i < fc->nglobals; i++) {
        if (fc->globals[i].slot == slot)
            return (uint8_t)(fc->nlocals + i);
    }
    if (fc->nglobals >= 32) return 0; /* overflow — should error */
    int idx = fc->nglobals++;
    fc->globals[idx].slot = slot;
    /* Adjust temp_base and temp_next */
    fc->temp_base = fc->nlocals + fc->nglobals + fc->nconsts;
    fc->temp_next = fc->temp_base;
    return (uint8_t)(fc->nlocals + idx);
}

/* Find or create a constant register mapping */
static uint8_t fc_const_reg_i(FastConv *fc, int64_t val) {
    int base = fc->nlocals + fc->nglobals;
    for (int i = 0; i < fc->nconsts; i++) {
        if (fc->consts[i].type == T_INT && fc->consts[i].ival == val)
            return (uint8_t)(base + i);
    }
    if (fc->nconsts >= 32) return 0;
    int idx = fc->nconsts++;
    fc->consts[idx].type = T_INT;
    fc->consts[idx].ival = val;
    fc->temp_base = fc->nlocals + fc->nglobals + fc->nconsts;
    fc->temp_next = fc->temp_base;
    return (uint8_t)(base + idx);
}

static uint8_t fc_const_reg_f(FastConv *fc, double val) {
    int base = fc->nlocals + fc->nglobals;
    for (int i = 0; i < fc->nconsts; i++) {
        if (fc->consts[i].type == T_NBR) {
            double d; memcpy(&d, &fc->consts[i].fval, sizeof(d));
            if (d == val) return (uint8_t)(base + i);
        }
    }
    if (fc->nconsts >= 32) return 0;
    int idx = fc->nconsts++;
    fc->consts[idx].type = T_NBR;
    fc->consts[idx].fval = val;
    fc->temp_base = fc->nlocals + fc->nglobals + fc->nconsts;
    fc->temp_next = fc->temp_base;
    return (uint8_t)(base + idx);
}

/* Find or create an array reference */
static uint8_t fc_array_ref(FastConv *fc, uint8_t is_local, uint16_t slot) {
    for (int i = 0; i < fc->narrays; i++) {
        if (fc->arrays[i].is_local == is_local && fc->arrays[i].slot == slot)
            return (uint8_t)i;
    }
    if (fc->narrays >= 16) return 0;
    int idx = fc->narrays++;
    fc->arrays[idx].is_local = is_local;
    fc->arrays[idx].slot = slot;
    return (uint8_t)idx;
}

/* Emit a 3-register op: [op][dst][s1][s2] */
static void fc_emit_3reg(FastConv *fc, uint8_t op, uint8_t dst, uint8_t s1, uint8_t s2) {
    fc_emit(fc, op); fc_emit(fc, dst); fc_emit(fc, s1); fc_emit(fc, s2);
}

/* Emit a 2-register op: [op][dst][src] */
static void fc_emit_2reg(FastConv *fc, uint8_t op, uint8_t dst, uint8_t src) {
    fc_emit(fc, op); fc_emit(fc, dst); fc_emit(fc, src);
}

/* Try to patch the previous micro-op's destination to avoid a MOV.
 * Returns 1 if successful. */
static int fc_patch_prev_dst(FastConv *fc, uint8_t old_dst, uint8_t new_dst) {
    if (fc->rop_len < 3) return 0;
    /* The previous op should have its dst at rop[prev_start + 1] */
    uint8_t prev_op = fc->rop[fc->rop_len - 3]; /* for 2-reg ops, check -2 too */
    /* For 4-byte ops (3-reg): dst is at [-3] */
    if (fc->rop_len >= 4) {
        uint8_t op4 = fc->rop[fc->rop_len - 4];
        /* Check if it's a 4-byte arithmetic/comparison op */
        if ((op4 >= ROP_ADD_I && op4 <= ROP_DIV_F) ||
            (op4 >= ROP_AND && op4 <= ROP_SHR) ||
            (op4 >= ROP_EQ_I && op4 <= ROP_GE_I) ||
            op4 == ROP_SQRSHR) {
            if (fc->rop[fc->rop_len - 3] == old_dst) {
                fc->rop[fc->rop_len - 3] = new_dst;
                return 1;
            }
        }
    }
    /* 5-byte ops (MULSHR): dst at [-4] */
    if (fc->rop_len >= 5) {
        uint8_t op5 = fc->rop[fc->rop_len - 5];
        if (op5 == ROP_MULSHR) {
            if (fc->rop[fc->rop_len - 4] == old_dst) {
                fc->rop[fc->rop_len - 4] = new_dst;
                return 1;
            }
        }
    }
    /* 6-byte ops (MULSHRADD): dst at [-5] */
    if (fc->rop_len >= 6) {
        uint8_t op6 = fc->rop[fc->rop_len - 6];
        if (op6 == ROP_MULSHRADD) {
            if (fc->rop[fc->rop_len - 5] == old_dst) {
                fc->rop[fc->rop_len - 5] = new_dst;
                return 1;
            }
        }
    }
    /* 3-byte ops (unary/mov/cvt): dst at [-2] */
    if (fc->rop_len >= 3) {
        uint8_t op3 = fc->rop[fc->rop_len - 3];
        if ((op3 >= ROP_NEG_I && op3 <= ROP_INV) ||
            op3 == ROP_MOV || op3 == ROP_CVT_I2F || op3 == ROP_CVT_F2I) {
            if (fc->rop[fc->rop_len - 2] == old_dst) {
                fc->rop[fc->rop_len - 2] = new_dst;
                return 1;
            }
        }
    }
    return 0;
}

/* Record a forward jump fixup */
static void fc_add_fixup(FastConv *fc, uint32_t rop_addr, uint32_t bc_target) {
    if (fc->fixup_count < 64) {
        fc->fixups[fc->fixup_count].rop_addr = rop_addr;
        fc->fixups[fc->fixup_count].bc_target = bc_target;
        fc->fixup_count++;
    }
}

/*
 * Convert loop bytecode [loop_start, loop_end) to register micro-ops.
 * Returns 1 on success, 0 on failure (sets cs->error_msg).
 */
static int source_convert_fast_loop(BCCompiler *cs, uint32_t loop_start,
                                     uint32_t loop_end) {
    /* Heap-allocate: FastConv is ~12KB, too large for the 4KB device stack */
    FastConv *fcp = (FastConv *)BC_COMPILER_ALLOC(sizeof(FastConv));
    if (!fcp) { bc_set_error(cs, "'!FAST: out of memory"); return 0; }
    memset(fcp, 0, sizeof(*fcp));
    #define fc (*fcp)
    fc.sim_sp = -1;
    fc.nlocals = (cs->current_subfun >= 0) ? cs->local_count : 0;
    fc.temp_base = fc.nlocals;
    fc.temp_next = fc.temp_base;
    fc.max_regs = fc.nlocals;
    fc.bc_len = loop_end - loop_start;

    int fc_result = 0; /* used by FC_FAIL */
    #define FC_FAIL do { fc_result = 0; goto fc_cleanup; } while(0)
    #define FC_OK   do { fc_result = 1; goto fc_cleanup; } while(0)

    if (fc.bc_len > sizeof(fc.bc_to_rop)) {
        bc_set_error(cs, "'!FAST loop too large");
        FC_FAIL;
    }

    /* Initialize bc_to_rop mapping with sentinel values */
    memset(fc.bc_to_rop, 0xFF, sizeof(fc.bc_to_rop));

    uint8_t *base = &cs->code[loop_start];
    uint8_t *end = &cs->code[loop_end];

    /* Fast loops only work inside subs/functions (locals only).
     * Module-scope globals have a register allocation collision with constants. */
    if (fc.nlocals == 0) {
        bc_set_error(cs, "'!FAST requires loop to be inside a SUB or FUNCTION");
        FC_FAIL;
    }

    /* Pre-scan: register all constants so temp registers never overlap with them.
     * Without this, temps allocated before a constant is first seen can collide. */
    {
        uint8_t *scan = base;
        while (scan < end) {
            uint8_t sop = *scan++;
            switch (sop) {
            case OP_PUSH_INT: { int64_t v; memcpy(&v, scan, 8); scan += 8; fc_const_reg_i(&fc, v); break; }
            case OP_PUSH_FLT: { double v; memcpy(&v, scan, 8); scan += 8; fc_const_reg_f(&fc, v); break; }
            case OP_PUSH_ZERO: fc_const_reg_i(&fc, 0); break;
            case OP_PUSH_ONE:  fc_const_reg_i(&fc, 1); break;
            /* Skip operand bytes for known opcodes */
            case OP_LOAD_LOCAL_I: case OP_LOAD_LOCAL_F: case OP_LOAD_LOCAL_S:
            case OP_STORE_LOCAL_I: case OP_STORE_LOCAL_F: case OP_STORE_LOCAL_S:
            case OP_LOAD_I: case OP_LOAD_F: case OP_LOAD_S:
            case OP_STORE_I: case OP_STORE_F: case OP_STORE_S:
            case OP_INC_I: case OP_LINE:
                scan += 2; break;
            case OP_LOAD_ARR_I: case OP_LOAD_ARR_F: case OP_LOAD_ARR_S:
            case OP_STORE_ARR_I: case OP_STORE_ARR_F: case OP_STORE_ARR_S:
                scan += 3; break; /* slot:16 + ndim:8 */
            case OP_JMP: case OP_JZ: case OP_JNZ:
                scan += 2; break;
            case OP_JCMP_I: case OP_JCMP_F:
                scan += 3; break; /* rel:8 + off:16 */
            case OP_MOV_VAR:
                scan += 5; break; /* kind:8 src:16 dst:16 */
            default:
                break; /* zero-operand opcodes (arithmetic, etc) */
            }
        }
    }

    uint8_t *pc = base;

    while (pc < end) {
        uint32_t bc_off = (uint32_t)(pc - base);
        fc.bc_to_rop[bc_off] = (uint16_t)fc.rop_len;

        uint8_t op = *pc++;
        switch (op) {

        /* --- Loads --- */
        case OP_LOAD_LOCAL_I:
        case OP_LOAD_LOCAL_F: {
            uint16_t off; memcpy(&off, pc, 2); pc += 2;
            fc_sim_push(&fc, (uint8_t)off);
            break;
        }
        case OP_LOAD_I:
        case OP_LOAD_F: {
            uint16_t slot; memcpy(&slot, pc, 2); pc += 2;
            uint8_t r = fc_global_reg(&fc, slot);
            fc_sim_push(&fc, r);
            break;
        }

        /* --- Stores --- */
        case OP_STORE_LOCAL_I:
        case OP_STORE_LOCAL_F: {
            uint16_t off; memcpy(&off, pc, 2); pc += 2;
            uint8_t src = fc_sim_pop(&fc);
            if (src != (uint8_t)off) {
                if (!fc_patch_prev_dst(&fc, src, (uint8_t)off))
                    fc_emit_2reg(&fc, ROP_MOV, (uint8_t)off, src);
            }
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }
        case OP_STORE_I:
        case OP_STORE_F: {
            uint16_t slot; memcpy(&slot, pc, 2); pc += 2;
            uint8_t dst = fc_global_reg(&fc, slot);
            uint8_t src = fc_sim_pop(&fc);
            if (src != dst) {
                if (!fc_patch_prev_dst(&fc, src, dst))
                    fc_emit_2reg(&fc, ROP_MOV, dst, src);
            }
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }

        /* --- Constants --- */
        case OP_PUSH_INT: {
            int64_t val; memcpy(&val, pc, 8); pc += 8;
            uint8_t r = fc_const_reg_i(&fc, val);
            fc_sim_push(&fc, r);
            break;
        }
        case OP_PUSH_FLT: {
            double val; memcpy(&val, pc, 8); pc += 8;
            uint8_t r = fc_const_reg_f(&fc, val);
            fc_sim_push(&fc, r);
            break;
        }
        case OP_PUSH_ZERO: {
            uint8_t r = fc_const_reg_i(&fc, 0);
            fc_sim_push(&fc, r);
            break;
        }
        case OP_PUSH_ONE: {
            uint8_t r = fc_const_reg_i(&fc, 1);
            fc_sim_push(&fc, r);
            break;
        }

        /* --- Integer binary arithmetic --- */
        case OP_ADD_I: case OP_SUB_I: case OP_MUL_I:
        case OP_IDIV_I: case OP_MOD_I: {
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            uint8_t rop;
            switch (op) {
                case OP_ADD_I:  rop = ROP_ADD_I;  break;
                case OP_SUB_I:  rop = ROP_SUB_I;  break;
                case OP_MUL_I:  rop = ROP_MUL_I;  break;
                case OP_IDIV_I: rop = ROP_IDIV_I; break;
                default:        rop = ROP_MOD_I;   break;
            }
            fc_emit_3reg(&fc, rop, dst, a, b);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Float binary arithmetic --- */
        case OP_ADD_F: case OP_SUB_F: case OP_MUL_F: case OP_DIV_F: {
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            uint8_t rop;
            switch (op) {
                case OP_ADD_F: rop = ROP_ADD_F; break;
                case OP_SUB_F: rop = ROP_SUB_F; break;
                case OP_MUL_F: rop = ROP_MUL_F; break;
                default:       rop = ROP_DIV_F; break;
            }
            fc_emit_3reg(&fc, rop, dst, a, b);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Unary --- */
        case OP_NEG_I: case OP_NEG_F: case OP_NOT: case OP_INV: {
            uint8_t src = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            uint8_t rop;
            switch (op) {
                case OP_NEG_I: rop = ROP_NEG_I; break;
                case OP_NEG_F: rop = ROP_NEG_F; break;
                case OP_NOT:   rop = ROP_NOT;   break;
                default:       rop = ROP_INV;   break;
            }
            fc_emit_2reg(&fc, rop, dst, src);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Bitwise --- */
        case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR: {
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            uint8_t rop;
            switch (op) {
                case OP_AND: rop = ROP_AND; break;
                case OP_OR:  rop = ROP_OR;  break;
                case OP_XOR: rop = ROP_XOR; break;
                case OP_SHL: rop = ROP_SHL; break;
                default:     rop = ROP_SHR; break;
            }
            fc_emit_3reg(&fc, rop, dst, a, b);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Integer comparisons (produce 0/1) --- */
        case OP_EQ_I: case OP_NE_I: case OP_LT_I:
        case OP_GT_I: case OP_LE_I: case OP_GE_I: {
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            uint8_t rop;
            switch (op) {
                case OP_EQ_I: rop = ROP_EQ_I; break;
                case OP_NE_I: rop = ROP_NE_I; break;
                case OP_LT_I: rop = ROP_LT_I; break;
                case OP_GT_I: rop = ROP_GT_I; break;
                case OP_LE_I: rop = ROP_LE_I; break;
                default:      rop = ROP_GE_I; break;
            }
            fc_emit_3reg(&fc, rop, dst, a, b);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Type conversion --- */
        case OP_CVT_I2F: {
            uint8_t src = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit_2reg(&fc, ROP_CVT_I2F, dst, src);
            fc_sim_push(&fc, dst);
            break;
        }
        case OP_CVT_F2I: {
            uint8_t src = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit_2reg(&fc, ROP_CVT_F2I, dst, src);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- Fused fixed-point --- */
        case OP_MATH_SQRSHR: {
            uint8_t bits = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit(&fc, ROP_SQRSHR);
            fc_emit(&fc, dst); fc_emit(&fc, a); fc_emit(&fc, bits);
            fc_sim_push(&fc, dst);
            break;
        }
        case OP_MATH_MULSHR: {
            uint8_t bits = fc_sim_pop(&fc);
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit(&fc, ROP_MULSHR);
            fc_emit(&fc, dst); fc_emit(&fc, a); fc_emit(&fc, b); fc_emit(&fc, bits);
            fc_sim_push(&fc, dst);
            break;
        }
        case OP_MATH_MULSHRADD: {
            uint8_t c = fc_sim_pop(&fc);
            uint8_t bits = fc_sim_pop(&fc);
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit(&fc, ROP_MULSHRADD);
            fc_emit(&fc, dst); fc_emit(&fc, a); fc_emit(&fc, b);
            fc_emit(&fc, bits); fc_emit(&fc, c);
            fc_sim_push(&fc, dst);
            break;
        }

        /* --- JCMP_I (fused compare+jump) --- */
        case OP_JCMP_I: {
            uint8_t rel = *pc++;
            int16_t off; memcpy(&off, pc, 2); pc += 2;
            uint8_t b = fc_sim_pop(&fc);
            uint8_t a = fc_sim_pop(&fc);
            uint8_t rop;
            switch (rel) {
                case BC_JCMP_EQ: rop = ROP_JCMP_EQ_I; break;
                case BC_JCMP_NE: rop = ROP_JCMP_NE_I; break;
                case BC_JCMP_LT: rop = ROP_JCMP_LT_I; break;
                case BC_JCMP_GT: rop = ROP_JCMP_GT_I; break;
                case BC_JCMP_LE: rop = ROP_JCMP_LE_I; break;
                case BC_JCMP_GE: rop = ROP_JCMP_GE_I; break;
                default:
                    bc_set_error(cs, "'!FAST: unknown JCMP relation %d", rel);
                    return 0;
            }
            /* Compute bytecode target (absolute within loop) */
            uint32_t bc_here = (uint32_t)(pc - base);
            uint32_t bc_target = (uint32_t)((int32_t)bc_here + off);

            fc_emit(&fc, rop);
            fc_emit(&fc, a); fc_emit(&fc, b);
            uint32_t fixup_addr = fc.rop_len;

            if (bc_target <= bc_off) {
                /* Backward jump — target already mapped */
                int16_t rop_off = (int16_t)((int32_t)fc.bc_to_rop[bc_target] - (int32_t)(fc.rop_len + 2));
                fc_emit_i16(&fc, rop_off);
            } else {
                /* Forward jump — add fixup */
                fc_emit_i16(&fc, 0); /* placeholder */
                fc_add_fixup(&fc, fixup_addr, bc_target);
            }
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }

        /* --- JZ / JNZ --- */
        case OP_JZ: case OP_JNZ: {
            int16_t off; memcpy(&off, pc, 2); pc += 2;
            uint8_t src = fc_sim_pop(&fc);
            uint8_t rop = (op == OP_JZ) ? ROP_JZ : ROP_JNZ;
            uint32_t bc_here = (uint32_t)(pc - base);
            uint32_t bc_target = (uint32_t)((int32_t)bc_here + off);

            fc_emit(&fc, rop);
            fc_emit(&fc, src);
            uint32_t fixup_addr = fc.rop_len;

            if (bc_target <= bc_off) {
                int16_t rop_off = (int16_t)((int32_t)fc.bc_to_rop[bc_target] - (int32_t)(fc.rop_len + 2));
                fc_emit_i16(&fc, rop_off);
            } else {
                fc_emit_i16(&fc, 0);
                fc_add_fixup(&fc, fixup_addr, bc_target);
            }
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }

        /* --- JMP --- */
        case OP_JMP: {
            int16_t off; memcpy(&off, pc, 2); pc += 2;
            uint32_t bc_here = (uint32_t)(pc - base);
            uint32_t bc_target = (uint32_t)((int32_t)bc_here + off);

            /* If jumping past loop end, this is an exit */
            if (bc_target >= fc.bc_len) {
                fc_emit(&fc, ROP_EXIT);
            } else {
                fc_emit(&fc, ROP_JMP);
                uint32_t fixup_addr = fc.rop_len;
                if (bc_target <= bc_off) {
                    int16_t rop_off = (int16_t)((int32_t)fc.bc_to_rop[bc_target] - (int32_t)(fc.rop_len + 2));
                    fc_emit_i16(&fc, rop_off);
                } else {
                    fc_emit_i16(&fc, 0);
                    fc_add_fixup(&fc, fixup_addr, bc_target);
                }
            }
            break;
        }

        /* --- INC_I (increment variable) --- */
        case OP_INC_I: {
            uint16_t raw_slot; memcpy(&raw_slot, pc, 2); pc += 2;
            int is_local = (raw_slot & 0x8000u) != 0;
            uint16_t slot = raw_slot & 0x7FFFu;
            uint8_t delta = fc_sim_pop(&fc);
            uint8_t dst = is_local ? (uint8_t)slot : fc_global_reg(&fc, slot);
            fc_emit_3reg(&fc, ROP_ADD_I, dst, dst, delta);
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }

        /* --- MOV_VAR --- */
        case OP_MOV_VAR: {
            uint8_t kind = *pc++;
            uint16_t src_raw; memcpy(&src_raw, pc, 2); pc += 2;
            uint16_t dst_raw; memcpy(&dst_raw, pc, 2); pc += 2;
            int src_local = (src_raw & 0x8000u) != 0;
            int dst_local = (dst_raw & 0x8000u) != 0;
            uint16_t src_slot = src_raw & 0x7FFFu;
            uint16_t dst_slot = dst_raw & 0x7FFFu;
            (void)kind; /* type doesn't matter for int/float move */
            uint8_t src_r = src_local ? (uint8_t)src_slot : fc_global_reg(&fc, src_slot);
            uint8_t dst_r = dst_local ? (uint8_t)dst_slot : fc_global_reg(&fc, dst_slot);
            if (src_r != dst_r)
                fc_emit_2reg(&fc, ROP_MOV, dst_r, src_r);
            break;
        }

        /* --- 1D Array access --- */
        case OP_LOAD_LOCAL_ARR_I: case OP_LOAD_LOCAL_ARR_F:
        case OP_LOAD_ARR_I: case OP_LOAD_ARR_F: {
            uint16_t slot; memcpy(&slot, pc, 2); pc += 2;
            uint8_t ndim = *pc++;
            if (ndim != 1) {
                bc_set_error(cs, "'!FAST: only 1D arrays supported (got %dD)", ndim);
                return 0;
            }
            uint8_t is_local = (op == OP_LOAD_LOCAL_ARR_I || op == OP_LOAD_LOCAL_ARR_F);
            uint8_t is_float = (op == OP_LOAD_LOCAL_ARR_F || op == OP_LOAD_ARR_F);
            uint8_t arr_idx = fc_array_ref(&fc, is_local, slot);
            uint8_t idx_reg = fc_sim_pop(&fc);
            uint8_t dst = fc_alloc_temp(&fc);
            fc_emit(&fc, is_float ? ROP_LOAD_ARR_F : ROP_LOAD_ARR_I);
            fc_emit(&fc, dst); fc_emit(&fc, arr_idx); fc_emit(&fc, idx_reg);
            fc_sim_push(&fc, dst);
            break;
        }
        case OP_STORE_LOCAL_ARR_I: case OP_STORE_LOCAL_ARR_F:
        case OP_STORE_ARR_I: case OP_STORE_ARR_F: {
            uint16_t slot; memcpy(&slot, pc, 2); pc += 2;
            uint8_t ndim = *pc++;
            if (ndim != 1) {
                bc_set_error(cs, "'!FAST: only 1D arrays supported (got %dD)", ndim);
                return 0;
            }
            uint8_t is_local = (op == OP_STORE_LOCAL_ARR_I || op == OP_STORE_LOCAL_ARR_F);
            uint8_t is_float = (op == OP_STORE_LOCAL_ARR_F || op == OP_STORE_ARR_F);
            uint8_t val_reg = fc_sim_pop(&fc);
            uint8_t idx_reg = fc_sim_pop(&fc);
            uint8_t arr_idx = fc_array_ref(&fc, is_local, slot);
            fc_emit(&fc, is_float ? ROP_STORE_ARR_F : ROP_STORE_ARR_I);
            fc_emit(&fc, val_reg); fc_emit(&fc, arr_idx); fc_emit(&fc, idx_reg);
            if (fc.sim_sp < 0) fc_reset_temps(&fc);
            break;
        }

        /* --- Housekeeping (skip or convert) --- */
        case OP_LINE: {
            uint16_t line; memcpy(&line, pc, 2); pc += 2;
            fc.last_line = line;
            break; /* skip — no micro-op needed */
        }
        case OP_CHECKINT:
            fc_emit(&fc, ROP_CHECKINT);
            break;

        /* --- Unsupported — fail conversion --- */
        default:
            bc_set_error(cs, "'!FAST: unsupported opcode 0x%02X at line %d", op, fc.last_line);
            FC_FAIL;
        }
    }

    /* Record final bc_to_rop mapping entry (for forward jumps to loop end) */
    fc.bc_to_rop[fc.bc_len] = (uint16_t)fc.rop_len;

    /* Patch forward jump fixups */
    for (int i = 0; i < fc.fixup_count; i++) {
        uint32_t rop_addr = fc.fixups[i].rop_addr;
        uint32_t bc_target = fc.fixups[i].bc_target;
        uint16_t rop_target;
        if (bc_target >= fc.bc_len) {
            /* Target is past loop end — point to EXIT at the end */
            rop_target = (uint16_t)fc.rop_len;
        } else {
            rop_target = fc.bc_to_rop[bc_target];
            if (rop_target == 0xFFFF) {
                bc_set_error(cs, "'!FAST: jump target offset %u not mapped", bc_target);
                FC_FAIL;
            }
        }
        int16_t rel = (int16_t)((int32_t)rop_target - (int32_t)(rop_addr + 2));
        fc.rop[rop_addr] = rel & 0xFF;
        fc.rop[rop_addr + 1] = (rel >> 8) & 0xFF;
    }

    /* Append EXIT at the end (for forward jumps that target loop exit) */
    fc_emit(&fc, ROP_EXIT);

    if (fc.max_regs > MAX_FAST_REGS) {
        bc_set_error(cs, "'!FAST: too many registers needed (%d > %d)", fc.max_regs, MAX_FAST_REGS);
        FC_FAIL;
    }

    /* --- Replace loop bytecode with OP_FAST_LOOP --- */
    cs->code_len = loop_start;

    /* Calculate total payload size */
    uint32_t global_map_size = fc.nglobals * 2;
    uint32_t array_map_size = fc.narrays * 3; /* is_local:8 + slot:16 per array */
    uint32_t const_data_size = fc.nconsts * 9; /* type:8 + value:64 per const */
    uint32_t total_payload = 5 + global_map_size + array_map_size + const_data_size + fc.rop_len;
    /* header: nregs:8 nlocals:8 nglobals:8 nconsts:8 narrays:8 = 5 bytes */

    bc_emit_byte(cs, OP_FAST_LOOP);
    bc_emit_u16(cs, (uint16_t)total_payload);
    bc_emit_byte(cs, (uint8_t)fc.max_regs);
    bc_emit_byte(cs, (uint8_t)fc.nlocals);
    bc_emit_byte(cs, (uint8_t)fc.nglobals);
    bc_emit_byte(cs, (uint8_t)fc.nconsts);
    bc_emit_byte(cs, (uint8_t)fc.narrays);

    /* Global register map */
    for (int i = 0; i < fc.nglobals; i++)
        bc_emit_u16(cs, fc.globals[i].slot);

    /* Array reference map */
    for (int i = 0; i < fc.narrays; i++) {
        bc_emit_byte(cs, fc.arrays[i].is_local);
        bc_emit_u16(cs, fc.arrays[i].slot);
    }

    /* Constant data */
    for (int i = 0; i < fc.nconsts; i++) {
        bc_emit_byte(cs, fc.consts[i].type);
        if (fc.consts[i].type == T_INT)
            bc_emit_i64(cs, fc.consts[i].ival);
        else {
            MMFLOAT fv = fc.consts[i].fval;
            bc_emit_f64(cs, fv);
        }
    }

    /* Micro-ops */
    for (uint32_t i = 0; i < fc.rop_len; i++)
        bc_emit_byte(cs, fc.rop[i]);

    FC_OK;

fc_cleanup:
    #undef fc
    #undef FC_FAIL
    #undef FC_OK
    BC_COMPILER_FREE(fcp);
    return fc_result;
}

/* ======================================================================
 * '!ASM inline assembler — text to register micro-ops
 * ====================================================================== */

/* Assembler state */
typedef struct {
    uint8_t  rop[4096];          /* micro-op output buffer */
    uint32_t rop_len;

    int      nlocals;            /* regs 0..nlocals-1 = local frame slots */

    /* Constant pool */
    struct { int64_t ival; double fval; uint8_t type; } consts[32];
    int      nconsts;

    /* Array reference map */
    struct { uint8_t is_local; uint16_t slot; } arrays[16];
    int      narrays;

    int      max_regs;           /* high-water mark */

    /* Labels */
    struct { char name[32]; uint32_t rop_addr; int defined; } labels[64];
    int      nlabels;

    /* Forward jump fixups (label-based) */
    struct { uint32_t rop_addr; int label_idx; } fixups[128];
    int      fixup_count;
} AsmCtx;

static void asm_emit(AsmCtx *ctx, uint8_t b) {
    if (ctx->rop_len < sizeof(ctx->rop)) ctx->rop[ctx->rop_len++] = b;
}

static void asm_emit_i16(AsmCtx *ctx, int16_t v) {
    asm_emit(ctx, (uint8_t)(v & 0xFF));
    asm_emit(ctx, (uint8_t)((v >> 8) & 0xFF));
}

/* Find or create a label entry. Returns index. */
static int asm_find_label(AsmCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->nlabels; i++) {
        if (strncasecmp(ctx->labels[i].name, name, 31) == 0) return i;
    }
    if (ctx->nlabels >= 64) return -1;
    int idx = ctx->nlabels++;
    strncpy(ctx->labels[idx].name, name, 31);
    ctx->labels[idx].name[31] = '\0';
    /* Lowercase for case-insensitive matching */
    for (char *c = ctx->labels[idx].name; *c; c++) *c = tolower((unsigned char)*c);
    ctx->labels[idx].rop_addr = 0;
    ctx->labels[idx].defined = 0;
    return idx;
}

/* Find or create a constant register (integer) */
static uint8_t asm_const_reg_i(AsmCtx *ctx, int64_t val) {
    int base = ctx->nlocals;
    for (int i = 0; i < ctx->nconsts; i++) {
        if (ctx->consts[i].type == T_INT && ctx->consts[i].ival == val)
            return (uint8_t)(base + i);
    }
    if (ctx->nconsts >= 32) return 0;
    int idx = ctx->nconsts++;
    ctx->consts[idx].type = T_INT;
    ctx->consts[idx].ival = val;
    int total = ctx->nlocals + ctx->nconsts;
    if (total > ctx->max_regs) ctx->max_regs = total;
    return (uint8_t)(base + idx);
}

/* Find or create a constant register (float) */
static uint8_t asm_const_reg_f(AsmCtx *ctx, double val) {
    int base = ctx->nlocals;
    for (int i = 0; i < ctx->nconsts; i++) {
        if (ctx->consts[i].type == T_NBR) {
            int64_t a, b;
            memcpy(&a, &ctx->consts[i].fval, 8);
            memcpy(&b, &val, 8);
            if (a == b) return (uint8_t)(base + i);
        }
    }
    if (ctx->nconsts >= 32) return 0;
    int idx = ctx->nconsts++;
    ctx->consts[idx].type = T_NBR;
    ctx->consts[idx].fval = val;
    int total = ctx->nlocals + ctx->nconsts;
    if (total > ctx->max_regs) ctx->max_regs = total;
    return (uint8_t)(base + idx);
}

/* Emit a jump to a label (forward or backward). Adds fixup if forward. */
static void asm_emit_jump_to_label(AsmCtx *ctx, int label_idx) {
    if (ctx->labels[label_idx].defined) {
        /* Backward reference — compute relative offset */
        int16_t rel = (int16_t)((int32_t)ctx->labels[label_idx].rop_addr -
                                (int32_t)(ctx->rop_len + 2));
        asm_emit_i16(ctx, rel);
    } else {
        /* Forward reference — fixup later */
        if (ctx->fixup_count < 128) {
            ctx->fixups[ctx->fixup_count].rop_addr = ctx->rop_len;
            ctx->fixups[ctx->fixup_count].label_idx = label_idx;
            ctx->fixup_count++;
        }
        asm_emit_i16(ctx, 0); /* placeholder */
    }
}

/* Skip whitespace and comments in an assembly line */
static void asm_skip_ws(const char **pp) {
    while (**pp == ' ' || **pp == '\t') (*pp)++;
}

/* Parse an identifier (alphanumeric + underscore + dot for mnemonics, case-insensitive) */
static int asm_parse_ident(const char **pp, char *buf, int bufsz) {
    const char *p = *pp;
    int len = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '.') && len < bufsz - 1) {
        buf[len++] = tolower((unsigned char)*p);
        p++;
    }
    buf[len] = '\0';
    *pp = p;
    return len;
}

/*
 * Resolve an operand name:
 *  1. Check .const names
 *  2. Check local variable names (suffix-stripped)
 * Returns register index, or -1 if not found.
 */
typedef struct {
    char name[32];
    uint8_t reg;
    int is_const;     /* 1 if this is a .const entry */
} AsmName;

static int asm_resolve_operand(AsmCtx *ctx, AsmName *names, int nnames,
                               BCCompiler *cs, const char *ident, int *is_const_out) {
    *is_const_out = 0;

    /* 1. Check .const names first */
    for (int i = 0; i < nnames; i++) {
        if (names[i].is_const && strncasecmp(names[i].name, ident, 31) == 0) {
            *is_const_out = 1;
            return names[i].reg;
        }
    }

    /* 2. Check local variables (suffix-stripped) */
    for (int i = 0; i < nnames; i++) {
        if (!names[i].is_const && strncasecmp(names[i].name, ident, 31) == 0) {
            return names[i].reg;
        }
    }

    return -1;
}

/*
 * Assemble lines and emit OP_FAST_LOOP block.
 */
static void source_assemble_block(BCSourceFrontend *fe, BCCompiler *cs) {
    if (cs->current_subfun < 0) {
        bc_set_error(cs, "'!ASM must be inside a SUB or FUNCTION");
        return;
    }

    /* Heap-allocate: AsmCtx is large */
    AsmCtx *ctx = (AsmCtx *)BC_COMPILER_ALLOC(sizeof(AsmCtx));
    if (!ctx) { bc_set_error(cs, "'!ASM: out of memory"); return; }
    memset(ctx, 0, sizeof(*ctx));

    ctx->nlocals = cs->local_count;
    ctx->max_regs = ctx->nlocals;

    /* Build name table from locals (params + LOCAL vars, suffix-stripped) */
    AsmName names[MAX_FAST_REGS];
    int nnames = 0;

    for (int i = 0; i < (int)cs->local_count && nnames < MAX_FAST_REGS; i++) {
        char stripped[MAXVARLEN + 1];
        int slen = (int)strlen(cs->locals[i].name);
        /* Strip type suffix */
        if (slen > 0) {
            char last = cs->locals[i].name[slen - 1];
            if (last == '%' || last == '!' || last == '$') slen--;
        }
        /* Strip () for arrays */
        if (slen >= 2 && cs->locals[i].name[slen - 2] == '(' && cs->locals[i].name[slen - 1] == ')') slen -= 2;
        if (slen > 31) slen = 31;
        memcpy(stripped, cs->locals[i].name, slen);
        stripped[slen] = '\0';

        strncpy(names[nnames].name, stripped, 31);
        names[nnames].name[31] = '\0';
        names[nnames].reg = (uint8_t)i;
        names[nnames].is_const = 0;
        nnames++;
    }

    /* Two-pass: first pass processes .const and .array directives + label definitions.
     * Second pass assembles instructions. */

    /* --- Pass 1: directives and labels --- */
    for (int ln = 0; ln < fe->asm_line_count; ln++) {
        const char *p = fe->asm_lines[ln];
        asm_skip_ws(&p);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == ';') continue;

        /* .const directive */
        if (*p == '.' && strncasecmp(p + 1, "const", 5) == 0 && !isalnum((unsigned char)p[6])) {
            p += 6;
            asm_skip_ws(&p);

            char cname[32];
            int clen = asm_parse_ident(&p, cname, sizeof(cname));
            if (clen == 0) {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: expected constant name after .const");
                goto asm_cleanup;
            }

            asm_skip_ws(&p);
            if (*p == ',') p++;
            asm_skip_ws(&p);

            /* Parse value (integer or float) */
            int is_negative = 0;
            if (*p == '-') { is_negative = 1; p++; }

            /* Check if it's a float (contains '.') */
            const char *vstart = p;
            int has_dot = 0;
            while (*p && *p != ';' && *p != ',' && *p != ' ' && *p != '\t') {
                if (*p == '.') has_dot = 1;
                p++;
            }
            char vbuf[64];
            int vlen = (int)(p - vstart);
            if (vlen > 63) vlen = 63;
            memcpy(vbuf, vstart, vlen);
            vbuf[vlen] = '\0';

            uint8_t reg;
            if (has_dot) {
                double fv = strtod(vbuf, NULL);
                if (is_negative) fv = -fv;
                reg = asm_const_reg_f(ctx, fv);
            } else {
                int64_t iv = strtoll(vbuf, NULL, 10);
                if (is_negative) iv = -iv;
                reg = asm_const_reg_i(ctx, iv);
            }

            /* Add to name table */
            if (nnames < MAX_FAST_REGS) {
                strncpy(names[nnames].name, cname, 31);
                names[nnames].name[31] = '\0';
                names[nnames].reg = reg;
                names[nnames].is_const = 1;
                nnames++;
            }
            continue;
        }

        /* .array directive */
        if (*p == '.' && strncasecmp(p + 1, "array", 5) == 0 && !isalnum((unsigned char)p[6])) {
            p += 6;
            asm_skip_ws(&p);

            /* Parse full BASIC name including suffix and parens: e.g. buf%() */
            char fullname[MAXVARLEN + 1];
            int flen = 0;
            while (*p && *p != ';' && *p != ' ' && *p != '\t' && flen < MAXVARLEN) {
                fullname[flen++] = *p++;
            }
            fullname[flen] = '\0';

            /* Must end with () — strip them to get the lookup name */
            if (flen < 3 || fullname[flen-2] != '(' || fullname[flen-1] != ')') {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: .array name must include type suffix and (), e.g. buf%%()");
                goto asm_cleanup;
            }

            /* Find the array in locals or globals */
            /* The full name with suffix but without () is used for lookup */
            char lookup_name[MAXVARLEN + 1];
            memcpy(lookup_name, fullname, flen - 2);
            lookup_name[flen - 2] = '\0';
            int lookup_len = flen - 2;

            /* Also need the name with () for array matching */
            int is_local = 0;
            uint16_t slot = 0xFFFF;

            /* Check locals first */
            if (cs->current_subfun >= 0) {
                for (int i = 0; i < (int)cs->local_count; i++) {
                    /* locals include suffix in name, check with parens stripped */
                    if (strncasecmp(cs->locals[i].name, fullname, flen) == 0 &&
                        cs->locals[i].name[flen] == '\0') {
                        is_local = 1;
                        slot = (uint16_t)i;
                        break;
                    }
                    /* Also try matching without parens (local name might not have them) */
                    if (strncasecmp(cs->locals[i].name, lookup_name, lookup_len) == 0 &&
                        (cs->locals[i].name[lookup_len] == '\0' ||
                         (cs->locals[i].name[lookup_len] == '(' && cs->locals[i].name[lookup_len+1] == ')')) &&
                        cs->locals[i].is_array) {
                        is_local = 1;
                        slot = (uint16_t)i;
                        break;
                    }
                }
            }

            if (slot == 0xFFFF) {
                /* Check globals */
                slot = bc_find_slot(cs, fullname, flen);
                if (slot == 0xFFFF) {
                    /* Try without parens */
                    slot = bc_find_slot(cs, lookup_name, lookup_len);
                }
                if (slot == 0xFFFF) {
                    cs->current_line = fe->asm_line_nos[ln];
                    bc_set_error(cs, "'!ASM: array '%s' not found", fullname);
                    goto asm_cleanup;
                }
            }

            /* Add to array map */
            if (ctx->narrays >= 16) {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: too many arrays (max 16)");
                goto asm_cleanup;
            }
            ctx->arrays[ctx->narrays].is_local = (uint8_t)is_local;
            ctx->arrays[ctx->narrays].slot = slot;
            ctx->narrays++;

            continue;
        }

        /* Label definition: .name: */
        if (*p == '.') {
            const char *lstart = p + 1;
            const char *lend = lstart;
            while (*lend && (isalnum((unsigned char)*lend) || *lend == '_')) lend++;
            if (*lend == ':') {
                /* It's a label definition — just record it (positions filled in pass 2) */
                /* We don't record positions here since we haven't assembled yet */
                continue;
            }
        }

        /* Everything else is an instruction — skip in pass 1 */
    }

    /* --- Pass 2: assemble instructions --- */
    for (int ln = 0; ln < fe->asm_line_count; ln++) {
        const char *p = fe->asm_lines[ln];
        asm_skip_ws(&p);

        if (*p == '\0' || *p == ';') continue;

        /* Skip .const and .array directives */
        if (*p == '.' && strncasecmp(p + 1, "const", 5) == 0 && !isalnum((unsigned char)p[6])) continue;
        if (*p == '.' && strncasecmp(p + 1, "array", 5) == 0 && !isalnum((unsigned char)p[6])) continue;

        /* Label definition */
        if (*p == '.') {
            const char *lstart = p + 1;
            const char *lend = lstart;
            while (*lend && (isalnum((unsigned char)*lend) || *lend == '_')) lend++;
            if (*lend == ':') {
                char lname[32];
                int ll = (int)(lend - lstart);
                if (ll > 31) ll = 31;
                for (int i = 0; i < ll; i++) lname[i] = tolower((unsigned char)lstart[i]);
                lname[ll] = '\0';

                int idx = asm_find_label(ctx, lname);
                if (idx < 0) {
                    cs->current_line = fe->asm_line_nos[ln];
                    bc_set_error(cs, "'!ASM: too many labels");
                    goto asm_cleanup;
                }
                if (ctx->labels[idx].defined) {
                    cs->current_line = fe->asm_line_nos[ln];
                    bc_set_error(cs, "'!ASM: duplicate label '.%s'", lname);
                    goto asm_cleanup;
                }
                ctx->labels[idx].defined = 1;
                ctx->labels[idx].rop_addr = ctx->rop_len;
                continue;
            }
        }

        /* Parse mnemonic */
        char mnemonic[16];
        int mlen = asm_parse_ident(&p, mnemonic, sizeof(mnemonic));
        if (mlen == 0) {
            cs->current_line = fe->asm_line_nos[ln];
            bc_set_error(cs, "'!ASM: expected instruction mnemonic");
            goto asm_cleanup;
        }
        asm_skip_ws(&p);

        /* Helper: parse one operand (name, literal, or label) */
        #define ASM_MAX_OPERANDS 6
        char operands[ASM_MAX_OPERANDS][32];
        int op_is_label[ASM_MAX_OPERANDS];
        int nops = 0;

        while (*p && *p != ';' && nops < ASM_MAX_OPERANDS) {
            asm_skip_ws(&p);
            if (*p == '\0' || *p == ';') break;

            op_is_label[nops] = 0;

            if (*p == '.') {
                /* Label reference */
                p++; /* skip '.' */
                char lname[32];
                int ll = 0;
                while (*p && (isalnum((unsigned char)*p) || *p == '_') && ll < 31) {
                    lname[ll++] = tolower((unsigned char)*p);
                    p++;
                }
                lname[ll] = '\0';
                strncpy(operands[nops], lname, 31);
                operands[nops][31] = '\0';
                op_is_label[nops] = 1;
                nops++;
            } else if (*p == '-' || isdigit((unsigned char)*p)) {
                /* Integer or float literal */
                char litbuf[64];
                int llen = 0;
                if (*p == '-') litbuf[llen++] = *p++;
                int has_dot = 0;
                while (*p && (isdigit((unsigned char)*p) || *p == '.') && llen < 63) {
                    if (*p == '.') has_dot = 1;
                    litbuf[llen++] = *p++;
                }
                litbuf[llen] = '\0';

                uint8_t reg;
                if (has_dot) {
                    reg = asm_const_reg_f(ctx, strtod(litbuf, NULL));
                } else {
                    reg = asm_const_reg_i(ctx, strtoll(litbuf, NULL, 10));
                }
                snprintf(operands[nops], 32, "%d", (int)reg);
                /* Mark as already resolved (negative = raw register) */
                op_is_label[nops] = -1; /* special: raw register index */
                nops++;
            } else if (isalpha((unsigned char)*p) || *p == '_') {
                /* Name (variable or constant) */
                char name[32];
                asm_parse_ident(&p, name, sizeof(name));
                strncpy(operands[nops], name, 31);
                operands[nops][31] = '\0';
                nops++;
            } else {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: unexpected character '%c'", *p);
                goto asm_cleanup;
            }

            asm_skip_ws(&p);
            if (*p == ',') { p++; asm_skip_ws(&p); }
        }

        /* Resolve name operands to register indices */
        uint8_t regs[ASM_MAX_OPERANDS];
        int     reg_is_const[ASM_MAX_OPERANDS];
        int     label_indices[ASM_MAX_OPERANDS];
        memset(regs, 0, sizeof(regs));
        memset(reg_is_const, 0, sizeof(reg_is_const));
        memset(label_indices, -1, sizeof(label_indices));

        for (int i = 0; i < nops; i++) {
            if (op_is_label[i] == 1) {
                /* Label — find/create label entry */
                label_indices[i] = asm_find_label(ctx, operands[i]);
                if (label_indices[i] < 0) {
                    cs->current_line = fe->asm_line_nos[ln];
                    bc_set_error(cs, "'!ASM: too many labels");
                    goto asm_cleanup;
                }
            } else if (op_is_label[i] == -1) {
                /* Raw register index (from literal) */
                regs[i] = (uint8_t)atoi(operands[i]);
                reg_is_const[i] = 1; /* literals are constants */
            } else {
                /* Name — resolve */
                int is_const = 0;
                int reg = asm_resolve_operand(ctx, names, nnames, cs, operands[i], &is_const);
                if (reg < 0) {
                    cs->current_line = fe->asm_line_nos[ln];
                    bc_set_error(cs, "'!ASM: unknown name '%s'", operands[i]);
                    goto asm_cleanup;
                }
                regs[i] = (uint8_t)reg;
                reg_is_const[i] = is_const;
            }
        }

        /* Resolve array operands for array instructions */
        /* Find array index by bare name (match against .array declarations) */
        #define ASM_RESOLVE_ARRAY(op_idx) do { \
            int found = -1; \
            /* Search for the bare name among declared arrays */ \
            for (int ai = 0; ai < ctx->narrays; ai++) { \
                /* Match bare name: strip suffix from locals[slot].name or slots[slot].name */ \
                uint16_t aslot = ctx->arrays[ai].slot; \
                const char *aname = NULL; \
                if (ctx->arrays[ai].is_local) { \
                    aname = cs->locals[aslot].name; \
                } else { \
                    aname = cs->slots[aslot].name; \
                } \
                /* Extract bare name (no suffix, no parens) */ \
                char bare[32]; \
                int bl = 0; \
                while (aname[bl] && isalnum((unsigned char)aname[bl]) && bl < 31) { \
                    bare[bl] = tolower((unsigned char)aname[bl]); \
                    bl++; \
                } \
                bare[bl] = '\0'; \
                if (strncasecmp(bare, operands[op_idx], 31) == 0) { \
                    found = ai; break; \
                } \
            } \
            if (found < 0) { \
                cs->current_line = fe->asm_line_nos[ln]; \
                bc_set_error(cs, "'!ASM: array '%s' not declared with .array", operands[op_idx]); \
                goto asm_cleanup; \
            } \
            regs[op_idx] = (uint8_t)found; \
        } while(0)

        /* Check destination is not a constant */
        #define ASM_CHECK_DST(idx) do { \
            if (reg_is_const[idx]) { \
                cs->current_line = fe->asm_line_nos[ln]; \
                bc_set_error(cs, "'!ASM: cannot write to constant '%s'", operands[idx]); \
                goto asm_cleanup; \
            } \
        } while(0)

        /* Instruction dispatch */
        /* 3-register integer arithmetic: addi, subi, muli, divi, modi */
        if (strcmp(mnemonic, "addi") == 0 || strcmp(mnemonic, "subi") == 0 ||
            strcmp(mnemonic, "muli") == 0 || strcmp(mnemonic, "divi") == 0 ||
            strcmp(mnemonic, "modi") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 3 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "addi") == 0) rop = ROP_ADD_I;
            else if (strcmp(mnemonic, "subi") == 0) rop = ROP_SUB_I;
            else if (strcmp(mnemonic, "muli") == 0) rop = ROP_MUL_I;
            else if (strcmp(mnemonic, "divi") == 0) rop = ROP_IDIV_I;
            else                                     rop = ROP_MOD_I;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        /* 3-register float arithmetic: addf, subf, mulf, divf */
        else if (strcmp(mnemonic, "addf") == 0 || strcmp(mnemonic, "subf") == 0 ||
                 strcmp(mnemonic, "mulf") == 0 || strcmp(mnemonic, "divf") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 3 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "addf") == 0) rop = ROP_ADD_F;
            else if (strcmp(mnemonic, "subf") == 0) rop = ROP_SUB_F;
            else if (strcmp(mnemonic, "mulf") == 0) rop = ROP_MUL_F;
            else                                     rop = ROP_DIV_F;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        /* Unary: negi, negf, not, inv */
        else if (strcmp(mnemonic, "negi") == 0 || strcmp(mnemonic, "negf") == 0 ||
                 strcmp(mnemonic, "not") == 0  || strcmp(mnemonic, "inv") == 0) {
            if (nops != 2) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 2 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "negi") == 0) rop = ROP_NEG_I;
            else if (strcmp(mnemonic, "negf") == 0) rop = ROP_NEG_F;
            else if (strcmp(mnemonic, "not") == 0)  rop = ROP_NOT;
            else                                     rop = ROP_INV;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]);
        }
        /* Bitwise: and, or, xor, shl, shr */
        else if (strcmp(mnemonic, "and") == 0 || strcmp(mnemonic, "or") == 0 ||
                 strcmp(mnemonic, "xor") == 0 || strcmp(mnemonic, "shl") == 0 ||
                 strcmp(mnemonic, "shr") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 3 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "and") == 0) rop = ROP_AND;
            else if (strcmp(mnemonic, "or") == 0)  rop = ROP_OR;
            else if (strcmp(mnemonic, "xor") == 0) rop = ROP_XOR;
            else if (strcmp(mnemonic, "shl") == 0) rop = ROP_SHL;
            else                                    rop = ROP_SHR;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        /* Move / convert: mov, cvtif, cvtfi */
        else if (strcmp(mnemonic, "mov") == 0 || strcmp(mnemonic, "cvtif") == 0 ||
                 strcmp(mnemonic, "cvtfi") == 0) {
            if (nops != 2) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 2 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "mov") == 0)    rop = ROP_MOV;
            else if (strcmp(mnemonic, "cvtif") == 0)  rop = ROP_CVT_I2F;
            else                                       rop = ROP_CVT_F2I;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]);
        }
        /* Fused fixed-point: sqrshr, mulshr, mulshradd */
        else if (strcmp(mnemonic, "sqrshr") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: sqrshr requires 3 operands (dst, a, bits)"); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            asm_emit(ctx, ROP_SQRSHR); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        else if (strcmp(mnemonic, "mulshr") == 0) {
            if (nops != 4) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: mulshr requires 4 operands (dst, a, b, bits)"); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            asm_emit(ctx, ROP_MULSHR); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]); asm_emit(ctx, regs[3]);
        }
        else if (strcmp(mnemonic, "mulshradd") == 0) {
            if (nops != 5) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: mulshradd requires 5 operands (dst, a, b, bits, c)"); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            asm_emit(ctx, ROP_MULSHRADD); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]); asm_emit(ctx, regs[3]); asm_emit(ctx, regs[4]);
        }
        /* Integer comparisons: eqi, nei, lti, gti, lei, gei */
        else if (strcmp(mnemonic, "eqi") == 0 || strcmp(mnemonic, "nei") == 0 ||
                 strcmp(mnemonic, "lti") == 0 || strcmp(mnemonic, "gti") == 0 ||
                 strcmp(mnemonic, "lei") == 0 || strcmp(mnemonic, "gei") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires 3 operands", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            uint8_t rop;
            if      (strcmp(mnemonic, "eqi") == 0) rop = ROP_EQ_I;
            else if (strcmp(mnemonic, "nei") == 0) rop = ROP_NE_I;
            else if (strcmp(mnemonic, "lti") == 0) rop = ROP_LT_I;
            else if (strcmp(mnemonic, "gti") == 0) rop = ROP_GT_I;
            else if (strcmp(mnemonic, "lei") == 0) rop = ROP_LE_I;
            else                                    rop = ROP_GE_I;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        /* Fused compare-and-jump: jeq, jne, jlt, jgt, jle, jge */
        else if (strcmp(mnemonic, "jeq") == 0 || strcmp(mnemonic, "jne") == 0 ||
                 strcmp(mnemonic, "jlt") == 0 || strcmp(mnemonic, "jgt") == 0 ||
                 strcmp(mnemonic, "jle") == 0 || strcmp(mnemonic, "jge") == 0) {
            if (nops != 3 || label_indices[2] < 0) {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: %s requires src1, src2, .label", mnemonic);
                goto asm_cleanup;
            }
            uint8_t rop;
            if      (strcmp(mnemonic, "jeq") == 0) rop = ROP_JCMP_EQ_I;
            else if (strcmp(mnemonic, "jne") == 0) rop = ROP_JCMP_NE_I;
            else if (strcmp(mnemonic, "jlt") == 0) rop = ROP_JCMP_LT_I;
            else if (strcmp(mnemonic, "jgt") == 0) rop = ROP_JCMP_GT_I;
            else if (strcmp(mnemonic, "jle") == 0) rop = ROP_JCMP_LE_I;
            else                                    rop = ROP_JCMP_GE_I;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]);
            asm_emit_jump_to_label(ctx, label_indices[2]);
        }
        /* Conditional jump: jz, jnz */
        else if (strcmp(mnemonic, "jz") == 0 || strcmp(mnemonic, "jnz") == 0) {
            if (nops != 2 || label_indices[1] < 0) {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: %s requires src, .label", mnemonic);
                goto asm_cleanup;
            }
            uint8_t rop = (strcmp(mnemonic, "jz") == 0) ? ROP_JZ : ROP_JNZ;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]);
            asm_emit_jump_to_label(ctx, label_indices[1]);
        }
        /* 1D Array access: loadi.a, storei.a, loadf.a, storef.a */
        else if (strcmp(mnemonic, "loadi.a") == 0 || strcmp(mnemonic, "loadf.a") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires reg, array, idx", mnemonic); goto asm_cleanup; }
            ASM_CHECK_DST(0);
            ASM_RESOLVE_ARRAY(1);
            uint8_t rop = (strcmp(mnemonic, "loadi.a") == 0) ? ROP_LOAD_ARR_I : ROP_LOAD_ARR_F;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        else if (strcmp(mnemonic, "storei.a") == 0 || strcmp(mnemonic, "storef.a") == 0) {
            if (nops != 3) { cs->current_line = fe->asm_line_nos[ln]; bc_set_error(cs, "'!ASM: %s requires reg, array, idx", mnemonic); goto asm_cleanup; }
            ASM_RESOLVE_ARRAY(1);
            uint8_t rop = (strcmp(mnemonic, "storei.a") == 0) ? ROP_STORE_ARR_I : ROP_STORE_ARR_F;
            asm_emit(ctx, rop); asm_emit(ctx, regs[0]); asm_emit(ctx, regs[1]); asm_emit(ctx, regs[2]);
        }
        /* Control flow */
        else if (strcmp(mnemonic, "jmp") == 0) {
            if (nops != 1 || label_indices[0] < 0) {
                cs->current_line = fe->asm_line_nos[ln];
                bc_set_error(cs, "'!ASM: jmp requires .label");
                goto asm_cleanup;
            }
            asm_emit(ctx, ROP_JMP);
            asm_emit_jump_to_label(ctx, label_indices[0]);
        }
        else if (strcmp(mnemonic, "exit") == 0) {
            asm_emit(ctx, ROP_EXIT);
        }
        else if (strcmp(mnemonic, "checkint") == 0) {
            asm_emit(ctx, ROP_CHECKINT);
        }
        else {
            cs->current_line = fe->asm_line_nos[ln];
            bc_set_error(cs, "'!ASM: unknown instruction '%s'", mnemonic);
            goto asm_cleanup;
        }

        #undef ASM_MAX_OPERANDS
    }

    /* Append implicit ROP_EXIT */
    asm_emit(ctx, ROP_EXIT);

    /* Resolve forward jump fixups */
    for (int i = 0; i < ctx->fixup_count; i++) {
        int li = ctx->fixups[i].label_idx;
        if (!ctx->labels[li].defined) {
            bc_set_error(cs, "'!ASM: undefined label '.%s'", ctx->labels[li].name);
            goto asm_cleanup;
        }
        uint32_t rop_addr = ctx->fixups[i].rop_addr;
        int16_t rel = (int16_t)((int32_t)ctx->labels[li].rop_addr - (int32_t)(rop_addr + 2));
        ctx->rop[rop_addr] = (uint8_t)(rel & 0xFF);
        ctx->rop[rop_addr + 1] = (uint8_t)((rel >> 8) & 0xFF);
    }

    /* Check register limit */
    if (ctx->max_regs > MAX_FAST_REGS) {
        bc_set_error(cs, "'!ASM: too many registers (%d > %d)", ctx->max_regs, MAX_FAST_REGS);
        goto asm_cleanup;
    }

    /* --- Emit OP_FAST_LOOP --- */
    cs->current_line = fe->asm_start_line;
    bc_add_linemap_entry(cs, (uint16_t)fe->asm_start_line, cs->code_len);
    bc_emit_byte(cs, OP_LINE);
    bc_emit_u16(cs, (uint16_t)fe->asm_start_line);

    uint32_t array_map_size = ctx->narrays * 3;
    uint32_t const_data_size = ctx->nconsts * 9;
    uint32_t total_payload = 5 + array_map_size + const_data_size + ctx->rop_len;

    bc_emit_byte(cs, OP_FAST_LOOP);
    bc_emit_u16(cs, (uint16_t)total_payload);
    bc_emit_byte(cs, (uint8_t)ctx->max_regs);
    bc_emit_byte(cs, (uint8_t)ctx->nlocals);
    bc_emit_byte(cs, 0);  /* nglobals = 0, ASM only supports locals */
    bc_emit_byte(cs, (uint8_t)ctx->nconsts);
    bc_emit_byte(cs, (uint8_t)ctx->narrays);

    /* Array reference map */
    for (int i = 0; i < ctx->narrays; i++) {
        bc_emit_byte(cs, ctx->arrays[i].is_local);
        bc_emit_u16(cs, ctx->arrays[i].slot);
    }

    /* Constant data */
    for (int i = 0; i < ctx->nconsts; i++) {
        bc_emit_byte(cs, ctx->consts[i].type);
        if (ctx->consts[i].type == T_INT)
            bc_emit_i64(cs, ctx->consts[i].ival);
        else {
            MMFLOAT fv = ctx->consts[i].fval;
            bc_emit_f64(cs, fv);
        }
    }

    /* Micro-ops */
    for (uint32_t i = 0; i < ctx->rop_len; i++)
        bc_emit_byte(cs, ctx->rop[i]);

asm_cleanup:
    #undef ASM_CHECK_DST
    #undef ASM_RESOLVE_ARRAY
    BC_COMPILER_FREE(ctx);
}

static void source_compile_do(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    uint32_t loop_top = cs->code_len;
    bc_nest_push(cs, NEST_DO);
    BCNestEntry *ne = bc_nest_top(cs);
    if (ne) {
        ne->addr1 = loop_top;
        ne->addr2 = 0xFFFFFFFF;
    }

    if (source_keyword(&p, "WHILE")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (type == T_STR) bc_set_error(cs, "DO WHILE requires a numeric condition");
        if (ne) ne->addr2 = source_emit_jmp_placeholder(cs, OP_JZ);
    } else if (source_keyword(&p, "UNTIL")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (type == T_STR) bc_set_error(cs, "DO UNTIL requires a numeric condition");
        if (ne) ne->addr2 = source_emit_jmp_placeholder(cs, OP_JNZ);
    }

    *pp = p;
}

static void source_compile_loop(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    BCNestEntry *ne = bc_nest_find(cs, NEST_DO);
    if (!ne) ne = bc_nest_find(cs, NEST_WHILE);
    if (!ne) {
        bc_set_error(cs, "LOOP without matching DO or WHILE");
        return;
    }

    uint32_t loop_start = ne->addr1;
    int do_fast = fe->fast_next_loop;
    fe->fast_next_loop = 0;

    const char *p = *pp;
    source_skip_space(&p);

    if (ne->type == NEST_WHILE) {
        bc_emit_byte(cs, OP_JMP);
        bc_emit_i16(cs, (int16_t)(ne->addr1 - (cs->code_len + 2)));
        source_patch_jmp_here(cs, ne->addr2);
        for (int i = 0; i < ne->exit_fixup_count; i++)
            source_patch_jmp_here(cs, ne->exit_fixups[i]);
        bc_nest_pop(cs);
        if (!cs->has_error) {
            if (!source_convert_fast_loop(cs, loop_start, cs->code_len) && !do_fast) {
                /* Auto-optimization failed — silently keep normal bytecode */
                cs->has_error = 0;
                cs->error_msg[0] = '\0';
            }
        }
        *pp = p;
        return;
    }

    if (source_keyword(&p, "WHILE")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (type == T_STR) bc_set_error(cs, "LOOP WHILE requires a numeric condition");
        source_emit_rel_jump(cs, OP_JNZ, ne->addr1);
    } else if (source_keyword(&p, "UNTIL")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (type == T_STR) bc_set_error(cs, "LOOP UNTIL requires a numeric condition");
        source_emit_rel_jump(cs, OP_JZ, ne->addr1);
    } else {
        bc_emit_byte(cs, OP_JMP);
        bc_emit_i16(cs, (int16_t)(ne->addr1 - (cs->code_len + 2)));
    }

    if (ne->addr2 != 0xFFFFFFFF) source_patch_jmp_here(cs, ne->addr2);
    for (int i = 0; i < ne->exit_fixup_count; i++)
        source_patch_jmp_here(cs, ne->exit_fixups[i]);
    bc_nest_pop(cs);
    if (!cs->has_error) {
        if (!source_convert_fast_loop(cs, loop_start, cs->code_len) && !do_fast) {
            cs->has_error = 0;
            cs->error_msg[0] = '\0';
        }
    }
    *pp = p;
}

static void source_compile_exit(BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    if (source_keyword(&p, "DO")) {
        BCNestEntry *ne = bc_nest_find(cs, NEST_DO);
        if (!ne) {
            bc_set_error(cs, "EXIT DO without matching DO");
            *pp = p;
            return;
        }
        uint32_t patch = source_emit_jmp_placeholder(cs, OP_JMP);
        if (ne->exit_fixup_count < 64) ne->exit_fixups[ne->exit_fixup_count++] = patch;
        *pp = p;
        return;
    }

    if (source_keyword(&p, "FOR")) {
        BCNestEntry *ne = bc_nest_find(cs, NEST_FOR);
        if (!ne) {
            bc_set_error(cs, "EXIT FOR without matching FOR");
            *pp = p;
            return;
        }
        uint32_t patch = source_emit_jmp_placeholder(cs, OP_JMP);
        if (ne->exit_fixup_count < 64) ne->exit_fixups[ne->exit_fixup_count++] = patch;
        *pp = p;
        return;
    }

    if (source_keyword(&p, "FUNCTION")) {
        BCNestEntry *ne = bc_nest_find(cs, NEST_FUNCTION);
        if (!ne) {
            bc_set_error(cs, "EXIT FUNCTION without matching FUNCTION");
            *pp = p;
            return;
        }
        bc_emit_load_var(cs, ne->var_slot, ne->var_type, 1);
        bc_emit_byte(cs, OP_LEAVE_FRAME);
        bc_emit_byte(cs, OP_RET_FUN);
        *pp = p;
        return;
    }

    if (source_keyword(&p, "SUB")) {
        BCNestEntry *ne = bc_nest_find(cs, NEST_SUB);
        if (!ne) {
            bc_set_error(cs, "EXIT SUB without matching SUB");
            *pp = p;
            return;
        }
        bc_emit_byte(cs, OP_LEAVE_FRAME);
        bc_emit_byte(cs, OP_RET_SUB);
        *pp = p;
        return;
    }

    bc_set_error(cs, "Expected DO, FOR, FUNCTION or SUB after EXIT");
    *pp = p;
}

static void source_compile_fastgfx(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    if (source_keyword(&p, "CREATE")) {
        source_emit_syscall_noaux(cs, BC_SYS_FASTGFX_CREATE, 0);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "CLOSE")) {
        source_emit_syscall_noaux(cs, BC_SYS_FASTGFX_CLOSE, 0);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "SWAP")) {
        source_emit_syscall_noaux(cs, BC_SYS_FASTGFX_SWAP, 0);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "SYNC")) {
        source_emit_syscall_noaux(cs, BC_SYS_FASTGFX_SYNC, 0);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "FPS")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        source_emit_int_conversion(cs, type);
        source_emit_syscall_noaux(cs, BC_SYS_FASTGFX_FPS, 1);
        *pp = p;
        return;
    }

    bc_set_error(cs, "Unsupported FASTGFX command");
    *pp = p;
}

static int source_parse_framebuffer_target(const char **pp, char *target_out) {
    const char *p = *pp;
    source_skip_space(&p);
    if (*p == '"') {
        p++;
        if ((*p == 'N' || *p == 'n' || *p == 'F' || *p == 'f' || *p == 'L' || *p == 'l') &&
            p[1] == '"') {
            *target_out = (char)toupper((unsigned char)*p);
            p += 2;
            *pp = p;
            return 1;
        }
        return 0;
    }
    if (*p == 'N' || *p == 'n' || *p == 'F' || *p == 'f' || *p == 'L' || *p == 'l') {
        *target_out = (char)toupper((unsigned char)*p);
        p++;
        *pp = p;
        return 1;
    }
    return 0;
}

static int source_parse_framebuffer_merge_mode(const char **pp, uint8_t *mode_out) {
    const char *p = *pp;
    source_skip_space(&p);
    if (*p == '"') {
        p++;
        if ((*p == 'A' || *p == 'a' || *p == 'B' || *p == 'b' || *p == 'R' || *p == 'r') &&
            p[1] == '"') {
            char mode = (char)toupper((unsigned char)*p);
            *mode_out = (mode == 'A') ? BC_FB_MERGE_MODE_A :
                        (mode == 'B') ? BC_FB_MERGE_MODE_B : BC_FB_MERGE_MODE_R;
            p += 2;
            *pp = p;
            return 1;
        }
        return 0;
    }
    if (*p == 'A' || *p == 'a' || *p == 'B' || *p == 'b' || *p == 'R' || *p == 'r') {
        char mode = (char)toupper((unsigned char)*p);
        *mode_out = (mode == 'A') ? BC_FB_MERGE_MODE_A :
                    (mode == 'B') ? BC_FB_MERGE_MODE_B : BC_FB_MERGE_MODE_R;
        p++;
        *pp = p;
        return 1;
    }
    return 0;
}

static void source_compile_framebuffer(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    uint8_t aux[4];
    source_skip_space(&p);

    if (source_keyword(&p, "CREATE")) {
        uint8_t create_flags = BC_FB_CREATE_NORMAL;
        source_skip_space(&p);
        if (source_keyword(&p, "FAST")) {
            create_flags = BC_FB_CREATE_FAST;
        }
        aux[0] = BC_FB_OP_CREATE;
        aux[1] = create_flags;
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, aux, 2);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "LAYER")) {
        source_skip_space(&p);
        if (source_keyword(&p, "TOP")) {
            bc_set_error(cs, "Unsupported FRAMEBUFFER LAYER TOP");
            *pp = p;
            return;
        }
        if (*p && *p != '\'') {
            uint8_t type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, type);
            aux[0] = BC_FB_OP_LAYER;
            aux[1] = 1;
            source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 1, aux, 2);
        } else {
            aux[0] = BC_FB_OP_LAYER;
            aux[1] = 0;
            source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, aux, 2);
        }
        *pp = p;
        return;
    }
    if (source_keyword(&p, "WRITE")) {
        char target = 0;
        if (!source_parse_framebuffer_target(&p, &target)) {
            bc_set_error(cs, "Unsupported FRAMEBUFFER WRITE target");
            *pp = p;
            return;
        }
        aux[0] = BC_FB_OP_WRITE;
        aux[1] = (uint8_t)target;
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, aux, 2);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "CLOSE")) {
        char target = BC_FB_TARGET_DEFAULT;
        source_parse_framebuffer_target(&p, &target);
        aux[0] = BC_FB_OP_CLOSE;
        aux[1] = (uint8_t)target;
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, aux, 2);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "MERGE")) {
        int argc = 0;
        uint8_t mode = BC_FB_MERGE_MODE_NOW;
        int has_colour = 0;
        int has_rate = 0;

        source_skip_space(&p);
        if (*p && *p != '\'') {
            uint8_t type = source_parse_expression(fe, cs, &p);
            source_emit_int_conversion(cs, type);
            argc++;
            has_colour = 1;
            source_skip_space(&p);
            if (*p == ',') {
                p++;
                if (!source_parse_framebuffer_merge_mode(&p, &mode)) {
                    bc_set_error(cs, "Unsupported FRAMEBUFFER MERGE mode");
                    *pp = p;
                    return;
                }
                source_skip_space(&p);
                if (*p == ',') {
                    p++;
                    {
                        uint8_t type2 = source_parse_expression(fe, cs, &p);
                        source_emit_int_conversion(cs, type2);
                        argc++;
                        has_rate = 1;
                    }
                }
            }
        }

        aux[0] = BC_FB_OP_MERGE;
        aux[1] = mode;
        aux[2] = (uint8_t)has_colour;
        aux[3] = (uint8_t)has_rate;
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, (uint8_t)argc, aux, 4);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "SYNC")) {
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, (uint8_t[]){BC_FB_OP_SYNC}, 1);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "WAIT")) {
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, (uint8_t[]){BC_FB_OP_WAIT}, 1);
        *pp = p;
        return;
    }
    if (source_keyword(&p, "COPY")) {
        char from = 0;
        char to = 0;
        uint8_t background = 0;

        if (!source_parse_framebuffer_target(&p, &from)) {
            bc_set_error(cs, "Unsupported FRAMEBUFFER COPY source");
            *pp = p;
            return;
        }
        source_skip_space(&p);
        if (*p != ',') {
            bc_set_error(cs, "Expected ','");
            *pp = p;
            return;
        }
        p++;
        if (!source_parse_framebuffer_target(&p, &to)) {
            bc_set_error(cs, "Unsupported FRAMEBUFFER COPY destination");
            *pp = p;
            return;
        }
        source_skip_space(&p);
        if (*p == ',') {
            p++;
            source_skip_space(&p);
            if (*p == '"') {
                p++;
                if ((*p == 'B' || *p == 'b') && p[1] == '"') {
                    background = 1;
                    p += 2;
                } else {
                    bc_set_error(cs, "Unsupported FRAMEBUFFER COPY mode");
                    *pp = p;
                    return;
                }
            } else if (*p == 'B' || *p == 'b') {
                background = 1;
                p++;
            } else {
                bc_set_error(cs, "Unsupported FRAMEBUFFER COPY mode");
                *pp = p;
                return;
            }
        }
        aux[0] = BC_FB_OP_COPY;
        aux[1] = (uint8_t)from;
        aux[2] = (uint8_t)to;
        aux[3] = background;
        source_emit_syscall(cs, BC_SYS_GFX_FRAMEBUFFER, 0, aux, 4);
        *pp = p;
        return;
    }

    bc_set_error(cs, "Unsupported FRAMEBUFFER command");
    *pp = p;
}

/* PLAY intentionally has no native compile path. It falls through to
 * the OP_BRIDGE_CMD fallback at the bottom of source_compile_statement,
 * so both the interpreter and the VM land in Audio.c's cmd_play — one
 * subcommand-parsing implementation shared across paths. */

static void source_compile_pwm(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);

    if (source_keyword(&p, "SYNC")) {
        uint16_t present = 0;
        for (int i = 0; i < 12; i++) {
            source_skip_space(&p);
            if (*p != ',' && *p != '\0' && *p != '\'') {
                uint8_t type = source_parse_expression(fe, cs, &p);
                source_emit_float_conversion(cs, type);
                present |= (uint16_t)(1u << i);
            }
            source_skip_space(&p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        {
            uint8_t aux[2] = {(uint8_t)(present & 0xFF), (uint8_t)(present >> 8)};
            source_emit_syscall(cs, BC_SYS_PWM_SYNC, (uint8_t)__builtin_popcount((unsigned)present), aux, 2);
        }
        *pp = p;
        return;
    }

    {
        uint8_t type = source_parse_expression(fe, cs, &p);
        uint8_t present = 0;
        source_emit_int_conversion(cs, type);
        if (!source_expect_char(cs, &p, ',', "Expected comma in PWM")) {
            *pp = p;
            return;
        }
        source_skip_space(&p);
        if (source_keyword(&p, "OFF")) {
            source_emit_syscall_noaux(cs, BC_SYS_PWM_OFF, 1);
            *pp = p;
            return;
        }

        type = source_parse_expression(fe, cs, &p);
        source_emit_float_conversion(cs, type);
        if (!source_expect_char(cs, &p, ',', "Expected duty cycle after PWM frequency")) {
            *pp = p;
            return;
        }
        for (int slot = 0; slot < 4; slot++) {
            source_skip_space(&p);
            if (*p != ',' && *p != '\0' && *p != '\'') {
                type = source_parse_expression(fe, cs, &p);
                if (slot < 2)
                    source_emit_float_conversion(cs, type);
                else
                    source_emit_int_conversion(cs, type);
                present |= (uint8_t)(1u << slot);
            }
            source_skip_space(&p);
            if (slot == 3 || *p != ',')
                break;
            p++;
        }
        source_emit_syscall(cs, BC_SYS_PWM_CONFIG, (uint8_t)(2 + ((present & 0x01) ? 1 : 0) +
                                                             ((present & 0x02) ? 1 : 0) +
                                                             ((present & 0x04) ? 1 : 0) +
                                                             ((present & 0x08) ? 1 : 0)),
                            &present, 1);
        *pp = p;
    }
}

static void source_compile_servo(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    uint8_t present = 0;
    uint8_t type = source_parse_expression(fe, cs, &p);
    source_emit_int_conversion(cs, type);
    if (!source_expect_char(cs, &p, ',', "Expected comma in SERVO")) {
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (source_keyword(&p, "OFF")) {
        source_emit_syscall_noaux(cs, BC_SYS_PWM_OFF, 1);
        *pp = p;
        return;
    }
    for (int slot = 0; slot < 2; slot++) {
        source_skip_space(&p);
        if (*p != ',' && *p != '\0' && *p != '\'') {
            type = source_parse_expression(fe, cs, &p);
            source_emit_float_conversion(cs, type);
            present |= (uint8_t)(1u << slot);
        }
        source_skip_space(&p);
        if (slot == 1 || *p != ',')
            break;
        p++;
    }
    source_emit_syscall(cs, BC_SYS_SERVO, (uint8_t)(1 + ((present & 0x01) ? 1 : 0) +
                                                   ((present & 0x02) ? 1 : 0)),
                        &present, 1);
    *pp = p;
}

static void source_compile_setpin(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int mode = 0;
    int option = VM_PIN_OPT_NONE;

    source_compile_pin_operand(fe, cs, &p);
    if (cs->has_error) {
        *pp = p;
        return;
    }
    if (!source_expect_char(cs, &p, ',', "Expected comma in SETPIN")) {
        *pp = p;
        return;
    }
    if (!source_parse_setpin_mode(&p, &mode)) {
        bc_set_error(cs, "Unsupported SETPIN mode");
        *pp = p;
        return;
    }

    source_skip_space(&p);
    if (*p == ',') {
        p++;
        source_skip_space(&p);
        if (source_keyword(&p, "PULLUP")) {
            option = VM_PIN_OPT_PULLUP;
        } else if (source_keyword(&p, "PULLDOWN")) {
            option = VM_PIN_OPT_PULLDOWN;
        } else {
            bc_set_error(cs, "Unsupported SETPIN option");
            *pp = p;
            return;
        }
    }

    {
        uint8_t aux[4];
        aux[0] = (uint8_t)(mode & 0xFF);
        aux[1] = (uint8_t)(((uint16_t)mode >> 8) & 0xFF);
        aux[2] = (uint8_t)(option & 0xFF);
        aux[3] = (uint8_t)(((uint16_t)option >> 8) & 0xFF);
        source_emit_syscall(cs, BC_SYS_SETPIN, 1, aux, 4);
    }
    *pp = p;
}

static void source_compile_open(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int fnbr = 0;
    int mode = 0;
    uint8_t type;

    type = source_parse_expression(fe, cs, &p);
    if (cs->has_error) {
        *pp = p;
        return;
    }
    if (type != T_STR) {
        bc_set_error(cs, "OPEN requires string filename");
        *pp = p;
        return;
    }

    source_skip_space(&p);
    if (!source_keyword(&p, "FOR")) {
        bc_set_error(cs, "Expected FOR in OPEN");
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (source_keyword(&p, "INPUT")) {
        mode = VM_FILE_MODE_INPUT;
    } else if (source_keyword(&p, "OUTPUT")) {
        mode = VM_FILE_MODE_OUTPUT;
    } else if (source_keyword(&p, "APPEND")) {
        mode = VM_FILE_MODE_APPEND;
    } else if (source_keyword(&p, "RANDOM")) {
        mode = VM_FILE_MODE_RANDOM;
    } else {
        bc_set_error(cs, "Unsupported OPEN mode");
        *pp = p;
        return;
    }

    source_skip_space(&p);
    if (!source_keyword(&p, "AS")) {
        bc_set_error(cs, "Expected AS in OPEN");
        *pp = p;
        return;
    }
    if (!source_parse_file_number(cs, &p, &fnbr)) {
        *pp = p;
        return;
    }

    {
        uint8_t aux[3] = {(uint8_t)mode, (uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
        source_emit_syscall(cs, BC_SYS_FILE_OPEN, 1, aux, 3);
    }
    *pp = p;
}

static void source_compile_close(BCCompiler *cs, const char **pp) {
    const char *p = *pp;

    while (!cs->has_error) {
        int fnbr = 0;
        if (!source_parse_file_number(cs, &p, &fnbr)) {
            *pp = p;
            return;
        }
        {
            uint8_t aux[2] = {(uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
            source_emit_syscall(cs, BC_SYS_FILE_CLOSE, 0, aux, 2);
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    *pp = p;
}

static int source_parse_string_expression(BCSourceFrontend *fe, BCCompiler *cs, const char **pp,
                                          const char *msg) {
    uint8_t type = source_parse_expression(fe, cs, pp);
    if (cs->has_error) return 0;
    if (type != T_STR) {
        bc_set_error(cs, "%s", msg);
        return 0;
    }
    return 1;
}

static void source_compile_drive(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    if (!source_parse_string_expression(fe, cs, &p, "DRIVE requires string argument")) {
        *pp = p;
        return;
    }
    source_emit_syscall_noaux(cs, BC_SYS_FILE_DRIVE, 1);
    *pp = p;
}

static void source_compile_seek(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int fnbr = 0;
    uint8_t type;

    if (!source_parse_file_number(cs, &p, &fnbr)) {
        *pp = p;
        return;
    }
    if (!source_expect_char(cs, &p, ',', "Expected ',' in SEEK")) {
        *pp = p;
        return;
    }
    type = source_parse_expression(fe, cs, &p);
    source_emit_int_conversion(cs, type);
    {
        uint8_t aux[2] = {(uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
        source_emit_syscall(cs, BC_SYS_FILE_SEEK, 1, aux, 2);
    }
    *pp = p;
}

static void source_compile_file_path_command(BCSourceFrontend *fe, BCCompiler *cs, const char **pp,
                                             uint16_t sysid, const char *msg) {
    const char *p = *pp;
    if (!source_parse_string_expression(fe, cs, &p, msg)) {
        *pp = p;
        return;
    }
    source_emit_syscall_noaux(cs, sysid, 1);
    *pp = p;
}

static void source_compile_rename(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    if (!source_parse_string_expression(fe, cs, &p, "RENAME requires string source")) {
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (!source_keyword(&p, "AS")) {
        bc_set_error(cs, "Expected AS in RENAME");
        *pp = p;
        return;
    }
    if (!source_parse_string_expression(fe, cs, &p, "RENAME requires string destination")) {
        *pp = p;
        return;
    }
    source_emit_syscall_noaux(cs, BC_SYS_FILE_RENAME, 2);
    *pp = p;
}

static void source_compile_copy(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    uint8_t mode = 0;

    source_skip_space(&p);
    if (source_keyword(&p, "A2A")) mode = 1;
    else if (source_keyword(&p, "A2B")) mode = 2;
    else if (source_keyword(&p, "B2A")) mode = 3;
    else if (source_keyword(&p, "B2B")) mode = 4;

    if (!source_parse_string_expression(fe, cs, &p, "COPY requires string source")) {
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (!source_keyword(&p, "TO")) {
        bc_set_error(cs, "Expected TO in COPY");
        *pp = p;
        return;
    }
    if (!source_parse_string_expression(fe, cs, &p, "COPY requires string destination")) {
        *pp = p;
        return;
    }
    source_emit_syscall(cs, BC_SYS_FILE_COPY, 2, &mode, 1);
    *pp = p;
}

/* FILES intentionally has no native compile path. It falls through to
 * the OP_BRIDGE_CMD fallback at the bottom of source_compile_statement,
 * so both the interpreter and the VM land in FileIO.c's cmd_files —
 * one formatted-listing implementation shared across paths. */

static void source_compile_line_input(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    int fnbr = 0;
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t vtype = 0;
    int is_local = 0;
    uint16_t slot;
    (void)fe;

    if (!source_parse_file_number(cs, &p, &fnbr)) {
        *pp = p;
        return;
    }
    if (!source_expect_char(cs, &p, ',', "Expected comma in LINE INPUT")) {
        *pp = p;
        return;
    }
    if (!source_parse_varname(&p, name, &name_len, &vtype)) {
        bc_set_error(cs, "Expected string variable in LINE INPUT");
        *pp = p;
        return;
    }
    if (vtype == 0) vtype = source_default_var_type(cs, name, name_len);
    if (vtype != T_STR) {
        bc_set_error(cs, "LINE INPUT requires string variable");
        *pp = p;
        return;
    }
    source_skip_space(&p);
    if (*p == '(') {
        bc_set_error(cs, "Unsupported LINE INPUT array target");
        *pp = p;
        return;
    }

    slot = source_resolve_var(cs, name, name_len, vtype, 1, &is_local);
    {
        uint8_t aux[5] = {(uint8_t)is_local,
                          (uint8_t)(slot & 0xFF), (uint8_t)(slot >> 8),
                          (uint8_t)(fnbr & 0xFF), (uint8_t)(fnbr >> 8)};
        source_emit_syscall(cs, BC_SYS_FILE_LINE_INPUT, 0, aux, 5);
    }
    *pp = p;
}

typedef struct {
    int present;
    uint8_t kind;
    uint8_t type;
    uint16_t slot;
} SourceGfxArg;

static int source_compile_expr_slice(BCSourceFrontend *fe, BCCompiler *cs,
                                     const char *start, const char *end,
                                     uint8_t *type_out) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (end <= start) return 0;
    if ((size_t)(end - start) > STRINGSIZE) {
        bc_set_error(cs, "Expression too long");
        return -1;
    }

    char expr[STRINGSIZE + 1];
    memcpy(expr, start, (size_t)(end - start));
    expr[end - start] = '\0';
    const char *p = expr;
    *type_out = source_parse_expression(fe, cs, &p);
    source_statement_end(cs, p);
    return cs->has_error ? -1 : 1;
}

static int source_compile_text_just_literal(BCCompiler *cs, const char *start, const char *end) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (end <= start || (size_t)(end - start) >= STRINGSIZE) return 0;
    for (const char *p = start; p < end; p++) {
        if (!isalpha((unsigned char)*p) && *p != ' ') return 0;
    }
    uint16_t idx = bc_add_constant_string(cs, (const uint8_t *)start, (int)(end - start));
    bc_emit_byte(cs, OP_PUSH_STR);
    bc_emit_u16(cs, idx);
    return 1;
}

static int source_try_parse_gfx_array_ref(BCCompiler *cs, SourceGfxArg *arg,
                                          const char *cmd_name,
                                          const char *start, const char *end) {
    char name[MAXVARLEN + 1];
    int name_len = 0;
    uint8_t type_hint = 0;
    uint8_t type = 0;
    uint16_t slot = 0xFFFF;
    int is_local = 0;
    int symbol_is_array = 0;

    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (start >= end || !isnamestart((unsigned char)*start)) return 0;

    const char *p = start;
    if (!source_parse_varname(&p, name, &name_len, &type_hint)) return 0;
    source_skip_space(&p);
    if (p < end) {
        if (*p != '(') return 0;
        p++;
        source_skip_space(&p);
        if (p >= end || *p != ')') return 0;
        p++;
        source_skip_space(&p);
        if (p != end) return 0;
    }

    if (cs->current_subfun >= 0) {
        int loc = bc_find_local(cs, name, name_len);
        if (loc >= 0) {
            slot = (uint16_t)loc;
            type = cs->locals[loc].type;
            symbol_is_array = cs->locals[loc].is_array;
            is_local = 1;
        }
    }

    if (!is_local) {
        slot = bc_find_slot(cs, name, name_len);
        if (slot == 0xFFFF) {
            if (type_hint == 0) return 0;
            slot = bc_add_slot(cs, name, name_len, type_hint, 1);
            if (slot == 0xFFFF) return -1;
        }
        type = cs->slots[slot].type;
        symbol_is_array = cs->slots[slot].is_array;
    }

    if (!symbol_is_array) return 0;
    if ((type & (T_INT | T_NBR)) == 0 || (type & T_STR)) {
        bc_set_error(cs, "%s requires numeric array arguments", cmd_name);
        return -1;
    }

    arg->present = 1;
    arg->slot = slot;
    arg->type = type;
    if (is_local)
        arg->kind = (type == T_INT) ? BC_BOX_ARG_LOCAL_ARR_I : BC_BOX_ARG_LOCAL_ARR_F;
    else
        arg->kind = (type == T_INT) ? BC_BOX_ARG_GLOBAL_ARR_I : BC_BOX_ARG_GLOBAL_ARR_F;
    return 1;
}

static void source_emit_syscall(BCCompiler *cs, uint16_t sysid, uint8_t argc,
                                const uint8_t *aux, uint8_t auxlen) {
    bc_emit_byte(cs, OP_SYSCALL);
    bc_emit_u16(cs, sysid);
    bc_emit_byte(cs, argc);
    bc_emit_byte(cs, auxlen);
    for (uint8_t i = 0; i < auxlen; i++) {
        bc_emit_byte(cs, aux[i]);
    }
}

static void source_emit_syscall_noaux(BCCompiler *cs, uint16_t sysid, uint8_t argc) {
    source_emit_syscall(cs, sysid, argc, NULL, 0);
}

static uint8_t source_gfx_stack_argc(int max_args, SourceGfxArg *args) {
    uint8_t argc = 0;
    for (int i = 0; i < max_args; i++) {
        if (args[i].kind == BC_BOX_ARG_STACK) argc++;
    }
    return argc;
}

static void source_emit_gfx_native(BCCompiler *cs, uint16_t sysid, int max_args,
                                   int field_count, SourceGfxArg *args) {
    uint8_t argc = source_gfx_stack_argc(max_args, args);
    uint8_t aux[1 + BC_TEXT_ARG_COUNT * 3];
    int auxlen = 0;

    aux[auxlen++] = (uint8_t)field_count;
    for (int i = 0; i < max_args; i++) {
        aux[auxlen++] = args[i].kind;
        if (args[i].kind == BC_BOX_ARG_GLOBAL_ARR_I ||
            args[i].kind == BC_BOX_ARG_GLOBAL_ARR_F ||
            args[i].kind == BC_BOX_ARG_LOCAL_ARR_I ||
            args[i].kind == BC_BOX_ARG_LOCAL_ARR_F) {
            aux[auxlen++] = (uint8_t)(args[i].slot & 0xFF);
            aux[auxlen++] = (uint8_t)(args[i].slot >> 8);
        }
    }
    source_emit_syscall(cs, sysid, argc, aux, (uint8_t)auxlen);
}

static void source_compile_gfx_args(BCSourceFrontend *fe, BCCompiler *cs, const char **pp,
                                    const char *cmd_name, uint16_t sysid,
                                    int min_args, int max_args, int text_mode) {
    SourceGfxArg args[BC_TEXT_ARG_COUNT];
    const char *p = *pp;
    int field_count = 0;
    int idx = 0;

    memset(args, 0, sizeof(args));
    for (int i = 0; i < BC_TEXT_ARG_COUNT; i++) args[i].kind = BC_BOX_ARG_EMPTY;

    while (idx < max_args) {
        source_skip_space(&p);
        const char *start = p;
        int in_string = 0;
        int depth = 0;
        while (*p) {
            if (*p == '"') in_string = !in_string;
            else if (!in_string && *p == '(') depth++;
            else if (!in_string && *p == ')') depth--;
            else if (!in_string && depth == 0 && (*p == ',' || *p == '\'')) break;
            p++;
        }
        const char *end = p;

        uint8_t type = 0;
        int present = 0;
        int array_rc = source_try_parse_gfx_array_ref(cs, &args[idx], cmd_name, start, end);
        if (array_rc < 0) return;
        if (array_rc > 0) {
            present = 1;
            type = args[idx].type;
        }
        if (text_mode && idx == 3) {
            int literal = source_compile_text_just_literal(cs, start, end);
            if (literal < 0) return;
            if (literal > 0) {
                present = 1;
                type = T_STR;
            }
        }
        if (!present) {
            int rc = source_compile_expr_slice(fe, cs, start, end, &type);
            if (rc < 0) return;
            present = rc > 0;
        }

        if (present) {
            int wants_string = text_mode && (idx == 2 || idx == 3);
            int wants_numeric = !wants_string;
            if (wants_string && type != T_STR) {
                bc_set_error(cs, "%s requires string arguments", cmd_name);
                return;
            }
            if (wants_numeric && (((type & (T_INT | T_NBR)) == 0) || (type & T_STR))) {
                bc_set_error(cs, "%s requires numeric arguments", cmd_name);
                return;
            }
            args[idx].present = 1;
            if (args[idx].kind == BC_BOX_ARG_EMPTY) args[idx].kind = BC_BOX_ARG_STACK;
            args[idx].type = type;
        }
        field_count = idx + 1;
        idx++;

        if (*p == ',') {
            p++;
            continue;
        }
        break;
    }

    if (field_count < min_args || field_count > max_args) {
        bc_set_error(cs, "Invalid %s argument count", cmd_name);
        *pp = p;
        return;
    }
    source_emit_gfx_native(cs, sysid, max_args, field_count, args);
    *pp = p;
}

static void source_compile_polygon(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    SourceGfxArg args[BC_POLYGON_ARG_COUNT];
    const char *p = *pp;
    int field_count = 0;

    memset(args, 0, sizeof(args));
    for (int i = 0; i < BC_POLYGON_ARG_COUNT; i++) args[i].kind = BC_BOX_ARG_EMPTY;

    for (int idx = 0; idx < BC_POLYGON_ARG_COUNT; idx++) {
        source_skip_space(&p);
        const char *start = p;
        int in_string = 0;
        int depth = 0;
        while (*p) {
            if (*p == '"') in_string = !in_string;
            else if (!in_string && *p == '(') depth++;
            else if (!in_string && *p == ')') depth--;
            else if (!in_string && depth == 0 && (*p == ',' || *p == '\'')) break;
            p++;
        }
        const char *end = p;
        uint8_t type = 0;
        int present = 0;
        int array_rc = source_try_parse_gfx_array_ref(cs, &args[idx], "POLYGON", start, end);
        if (array_rc < 0) return;
        if (idx == 1 || idx == 2) {
            if (array_rc <= 0) {
                bc_set_error(cs, "POLYGON requires numeric array arguments");
                return;
            }
            present = 1;
            type = args[idx].type;
        } else if (array_rc > 0) {
            present = 1;
            type = args[idx].type;
        } else {
            int rc = source_compile_expr_slice(fe, cs, start, end, &type);
            if (rc < 0) return;
            present = rc > 0;
        }

        if (!present) {
            if (idx < 3) {
                bc_set_error(cs, "Argument count");
                return;
            }
            break;
        }
        if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR)) {
            bc_set_error(cs, "POLYGON requires numeric arguments");
            return;
        }
        args[idx].present = 1;
        if (args[idx].kind == BC_BOX_ARG_EMPTY) args[idx].kind = BC_BOX_ARG_STACK;
        args[idx].type = type;
        field_count = idx + 1;

        if (*p == ',') {
            p++;
            continue;
        }
        break;
    }

    if (field_count < 3 || field_count > BC_POLYGON_ARG_COUNT) {
        bc_set_error(cs, "Argument count");
        *pp = p;
        return;
    }
    source_emit_gfx_native(cs, BC_SYS_GFX_POLYGON, BC_POLYGON_ARG_COUNT, field_count, args);
    *pp = p;
}

static void source_compile_cls(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    int has_arg = 0;
    if (*p && *p != '\'') {
        uint8_t type = source_parse_expression(fe, cs, &p);
        if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR))
            bc_set_error(cs, "CLS requires numeric arguments");
        has_arg = 1;
    }
    {
        uint8_t aux = (uint8_t)has_arg;
        source_emit_syscall(cs, BC_SYS_GFX_CLS, (uint8_t)has_arg, &aux, 1);
    }
    *pp = p;
}

static void source_compile_select_case(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    source_skip_space(&p);
    if (!source_keyword(&p, "CASE")) {
        bc_set_error(cs, "Expected CASE after SELECT");
        *pp = p;
        return;
    }
    uint8_t type = source_parse_expression(fe, cs, &p);
    uint16_t slot = source_alloc_hidden_slot(cs, type);
    source_emit_store_converted(cs, slot, type, type, 0);

    bc_nest_push(cs, NEST_SELECT);
    BCNestEntry *ne = bc_nest_top(cs);
    if (ne) {
        ne->select_slot = slot;
        ne->select_type = type;
        ne->addr1 = 0xFFFFFFFF;
    }
    *pp = p;
}

static void source_compile_case(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    BCNestEntry *ne = bc_nest_find(cs, NEST_SELECT);
    if (!ne) {
        bc_set_error(cs, "CASE without matching SELECT CASE");
        return;
    }

    if (ne->addr1 != 0xFFFFFFFF) {
        uint32_t end_jmp = source_emit_jmp_placeholder(cs, OP_JMP);
        if (ne->case_end_count < 32) ne->case_end_fixups[ne->case_end_count++] = end_jmp;
        source_patch_jmp_here(cs, ne->addr1);
        ne->addr1 = 0xFFFFFFFF;
    }

    const char *p = *pp;
    source_skip_space(&p);
    if (source_keyword(&p, "ELSE")) {
        ne->has_else = 1;
        *pp = p;
        return;
    }

    uint32_t body_patches[32];
    int body_patch_count = 0;
    while (!cs->has_error) {
        bc_emit_load_var(cs, ne->select_slot, ne->select_type, 0);
        uint32_t right_start = cs->code_len;
        uint8_t rhs = source_parse_expression(fe, cs, &p);
        source_skip_space(&p);
        if (source_keyword(&p, "TO")) {
            source_emit_compare(cs, ne->select_type, rhs, right_start, 'g');
            uint32_t low_fail = source_emit_jmp_placeholder(cs, OP_JZ);

            bc_emit_load_var(cs, ne->select_slot, ne->select_type, 0);
            uint32_t high_start = cs->code_len;
            uint8_t high = source_parse_expression(fe, cs, &p);
            source_emit_compare(cs, ne->select_type, high, high_start, 'l');
            if (body_patch_count < 32)
                body_patches[body_patch_count++] = source_emit_jmp_placeholder(cs, OP_JNZ);
            source_patch_jmp_here(cs, low_fail);
        } else {
            source_emit_compare(cs, ne->select_type, rhs, right_start, '=');
            if (body_patch_count < 32)
                body_patches[body_patch_count++] = source_emit_jmp_placeholder(cs, OP_JNZ);
        }

        source_skip_space(&p);
        if (*p != ',') break;
        p++;
    }

    ne->addr1 = source_emit_jmp_placeholder(cs, OP_JMP);
    for (int i = 0; i < body_patch_count; i++)
        source_patch_jmp_here(cs, body_patches[i]);
    *pp = p;
}

static void source_compile_end_select(BCCompiler *cs) {
    BCNestEntry *ne = bc_nest_find(cs, NEST_SELECT);
    if (!ne) {
        bc_set_error(cs, "END SELECT without matching SELECT CASE");
        return;
    }
    if (ne->addr1 != 0xFFFFFFFF) source_patch_jmp_here(cs, ne->addr1);
    for (int i = 0; i < ne->case_end_count; i++)
        source_patch_jmp_here(cs, ne->case_end_fixups[i]);
    bc_nest_pop(cs);
}

static const char *source_find_keyword_outside_string(const char *p, const char *kw) {
    const char *base = p;
    int in_string = 0;
    size_t len = strlen(kw);
    for (; *p; p++) {
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (strncasecmp(p, kw, len) == 0 &&
            (p == base || !isnamechar((unsigned char)p[-1])) &&
            !isnamechar((unsigned char)p[len])) {
            return p;
        }
    }
    return NULL;
}

static void source_compile_statement_list(BCSourceFrontend *fe, BCCompiler *cs, const char *text) {
    const char *p = text;
    while (*p && !cs->has_error) {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') break;

        const char *start = p;
        const char *kw_probe = p;
        int if_statement = source_keyword(&kw_probe, "IF");
        int in_string = 0;
        while (*p) {
            if (*p == '"') in_string = !in_string;
            if (!in_string && ((!if_statement && *p == ':') || *p == '\'')) break;
            p++;
        }

        char stmt[STRINGSIZE + 1];
        size_t len = (size_t)(p - start);
        if (len > STRINGSIZE) len = STRINGSIZE;
        memcpy(stmt, start, len);
        stmt[len] = '\0';
        source_compile_statement(fe, cs, stmt);

        if (*p == ':') {
            p++;
            continue;
        }
        break;
    }
}

static void source_compile_if(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    const char *then_kw = source_find_keyword_outside_string(p, "THEN");
    if (!then_kw) {
        bc_set_error(cs, "IF without THEN");
        *pp = p;
        return;
    }

    char cond[STRINGSIZE + 1];
    size_t cond_len = (size_t)(then_kw - p);
    if (cond_len > STRINGSIZE) cond_len = STRINGSIZE;
    memcpy(cond, p, cond_len);
    cond[cond_len] = '\0';
    const char *cond_p = cond;
    uint8_t cond_type = source_parse_expression(fe, cs, &cond_p);
    if (cs->has_error) {
        *pp = p;
        return;
    }
    if (cond_type == T_STR) {
        bc_set_error(cs, "IF requires a numeric condition");
        *pp = p;
        return;
    }
    source_statement_end(cs, cond_p);
    if (cs->has_error) {
        *pp = p;
        return;
    }

    uint32_t false_patch = source_emit_jmp_placeholder(cs, OP_JZ);

    const char *then_start = then_kw + 4;
    if (source_line_empty_or_comment(then_start)) {
        bc_nest_push(cs, NEST_IF);
        BCNestEntry *ne = bc_nest_top(cs);
        if (ne) {
            ne->addr1 = false_patch;
            ne->addr2 = 0xFFFFFFFF;
        }
        *pp = then_start + strlen(then_start);
        return;
    }

    const char *else_kw = source_find_keyword_outside_string(then_start, "ELSE");
    char then_stmt[STRINGSIZE + 1];
    size_t then_len = else_kw ? (size_t)(else_kw - then_start) : strlen(then_start);
    if (then_len > STRINGSIZE) then_len = STRINGSIZE;
    memcpy(then_stmt, then_start, then_len);
    then_stmt[then_len] = '\0';
    source_compile_statement_list(fe, cs, then_stmt);
    if (cs->has_error) {
        *pp = then_start;
        return;
    }

    if (else_kw) {
        uint32_t end_patch = source_emit_jmp_placeholder(cs, OP_JMP);
        source_patch_jmp_here(cs, false_patch);
        source_compile_statement_list(fe, cs, else_kw + 4);
        source_patch_jmp_here(cs, end_patch);
        *pp = else_kw + strlen(else_kw);
        return;
    }

    source_patch_jmp_here(cs, false_patch);
    *pp = then_start + strlen(then_start);
}

static void source_compile_elseif(BCSourceFrontend *fe, BCCompiler *cs, const char **pp) {
    BCNestEntry *ne = bc_nest_top(cs);
    if (!ne || ne->type != NEST_IF) {
        bc_set_error(cs, "ELSEIF without matching IF");
        return;
    }

    uint32_t end_patch = source_emit_jmp_placeholder(cs, OP_JMP);
    if (ne->exit_fixup_count < 64) ne->exit_fixups[ne->exit_fixup_count++] = end_patch;
    if (ne->addr1 != 0xFFFFFFFF) source_patch_jmp_here(cs, ne->addr1);

    const char *p = *pp;
    const char *then_kw = source_find_keyword_outside_string(p, "THEN");
    if (!then_kw) {
        bc_set_error(cs, "ELSEIF without THEN");
        *pp = p;
        return;
    }

    char cond[STRINGSIZE + 1];
    size_t cond_len = (size_t)(then_kw - p);
    if (cond_len > STRINGSIZE) cond_len = STRINGSIZE;
    memcpy(cond, p, cond_len);
    cond[cond_len] = '\0';
    const char *cond_p = cond;
    uint8_t cond_type = source_parse_expression(fe, cs, &cond_p);
    if (cond_type == T_STR) bc_set_error(cs, "ELSEIF requires a numeric condition");
    source_statement_end(cs, cond_p);
    if (cs->has_error) {
        *pp = p;
        return;
    }

    ne->addr1 = source_emit_jmp_placeholder(cs, OP_JZ);
    const char *then_start = then_kw + 4;
    if (source_line_empty_or_comment(then_start)) {
        *pp = then_start + strlen(then_start);
        return;
    }

    source_compile_statement(fe, cs, then_start);
    *pp = then_start + strlen(then_start);
}

static void source_compile_else(BCCompiler *cs, const char **pp) {
    BCNestEntry *ne = bc_nest_top(cs);
    if (!ne || ne->type != NEST_IF) {
        bc_set_error(cs, "ELSE without matching IF");
        return;
    }
    uint32_t end_patch = source_emit_jmp_placeholder(cs, OP_JMP);
    if (ne->exit_fixup_count < 64) ne->exit_fixups[ne->exit_fixup_count++] = end_patch;
    if (ne->addr1 != 0xFFFFFFFF) source_patch_jmp_here(cs, ne->addr1);
    ne->addr1 = 0xFFFFFFFF;
    ne->has_else = 1;
    const char *p = *pp;
    source_skip_space(&p);
    *pp = p;
}

static void source_compile_endif(BCCompiler *cs) {
    BCNestEntry *ne = bc_nest_top(cs);
    if (!ne || ne->type != NEST_IF) {
        bc_set_error(cs, "ENDIF without matching IF");
        return;
    }
    if (ne->addr1 != 0xFFFFFFFF) source_patch_jmp_here(cs, ne->addr1);
    for (int i = 0; i < ne->exit_fixup_count; i++)
        source_patch_jmp_here(cs, ne->exit_fixups[i]);
    bc_nest_pop(cs);
}

// Phase 7: does the statement text reference any user-defined SUB or
// FUNCTION we have flagged as `.bridged`?  If so, native statement
// compilation can't express it — any arg evaluation would try to load a
// struct slot — so we bridge the whole line.  Scans identifier-shaped
// tokens and checks against cs->subfuns[].
static int source_stmt_references_bridged_subfun(BCCompiler *cs, const char *stmt) {
    const char *p = stmt;
    int in_str = 0;
    while (*p) {
        if (*p == '\"') { in_str = !in_str; p++; continue; }
        if (in_str) { p++; continue; }
        if (!isnamestart((unsigned char)*p)) { p++; continue; }
        const char *nstart = p;
        while (isnamechar((unsigned char)*p) && *p != '.') p++;
        if (*p == '$' || *p == '%' || *p == '!') p++;
        int nlen = (int)(p - nstart);
        if (nlen == 0) continue;
        /* Ignore trailing suffix for sub-name match — BCSubFun stores names
         * without type suffix. */
        int match_len = nlen;
        unsigned char last = (unsigned char)nstart[nlen-1];
        if (last == '$' || last == '%' || last == '!') match_len--;
        int sf_idx = bc_find_subfun(cs, nstart, match_len);
        if (sf_idx >= 0 && cs->subfuns[sf_idx].bridged) return 1;
    }
    return 0;
}

/* Detect a `name:` label definition at *pp, register it pointing at
 * cs->code_len, advance *pp past the colon, and return 1. Otherwise
 * leave *pp untouched and return 0.  Reserved-keyword check skips
 * obvious statements that can't be labels.  Used both in the line
 * scanner (before the `:` statement separator gets stripped) and in
 * source_compile_statement (for the inline `: mylabel:` case after an
 * earlier statement on the same line). */
static int source_try_register_label(BCCompiler *cs, const char **pp) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (!(*p) || (!isalpha((unsigned char)*p) && *p != '_')) return 0;
    const char *q = p;
    while (isalnum((unsigned char)*q) || *q == '_' || *q == '.') q++;
    int name_len = (int)(q - p);
    const char *cs_q = q;
    while (*cs_q == ' ' || *cs_q == '\t') cs_q++;
    if (*cs_q != ':' || cs_q[1] == '=') return 0;
    if (name_len <= 0 || name_len > BC_MAX_LABEL_NAME) return 0;

    static const char *reserved[] = {
        "IF", "FOR", "DO", "WHILE", "SUB", "FUNCTION",
        "SELECT", "END", "EXIT", "NEXT", "LOOP", "RETURN",
        "GOTO", "GOSUB", "DIM", "LOCAL", "STATIC", "CONST",
        "PRINT", "INPUT", "OPEN", "CLOSE", "DATA", "READ",
        "RESTORE", "REM", "OPTION", "ON", "ELSE", "ELSEIF",
        "THEN", "TO", "STEP", "AS", "CASE", "DEFAULT",
        NULL
    };
    for (int i = 0; reserved[i]; i++) {
        size_t rl = strlen(reserved[i]);
        if ((size_t)name_len == rl && strncasecmp(p, reserved[i], rl) == 0)
            return 0;
    }

    char buf[BC_MAX_LABEL_NAME + 1];
    memcpy(buf, p, name_len);
    buf[name_len] = '\0';

    /* Resolve forward refs (label was used by GOTO before it was
     * defined): patch the placeholder entry in-place instead of
     * adding a duplicate. */
    int hit = -1;
    for (uint16_t i = 0; i < cs->labelmap_count; i++) {
        if (strcasecmp(cs->labelmap[i].name, buf) == 0) {
            hit = (int)i; break;
        }
    }
    if (hit >= 0) {
        if (cs->labelmap[hit].offset != 0xFFFFFFFF) {
            bc_set_error(cs, "Duplicate label '%s'", buf);
            return 0;
        }
        cs->labelmap[hit].offset = cs->code_len;
    } else {
        bc_add_labelmap_entry(cs, buf, cs->code_len);
    }
    *pp = cs_q + 1;
    return 1;
}

static void source_compile_statement(BCSourceFrontend *fe, BCCompiler *cs, const char *stmt) {
    const char *p = stmt;
    source_skip_space(&p);

    /* BASIC label definition (also handled in source_compile_line —
     * this branch covers labels that appear after an inline `:`
     * separator, e.g. `PRINT 1 : mylabel: PRINT 2`). The line-level
     * scanner strips the trailing `:` before handing the statement
     * here, so this looks for the *bare identifier* form when called
     * from the line-level loop's reentry path. */
    if (source_try_register_label(cs, &p)) {
        if (*p == '\0' || *p == '\'') return;
        source_skip_space(&p);
        stmt = p;
    }

    /* Phase 7 early-bridge: if the statement touches a user sub/fun with a
     * struct parameter or struct return, bridge the whole line.  Native
     * arg evaluation can't load struct slots.  Skip for SUB / FUNCTION /
     * END declarations — the name there is the definition, not a call. */
    {
        const char *probe = stmt;
        source_skip_space(&probe);
        int is_def = (strncasecmp(probe, "SUB", 3) == 0 && !isnamechar((unsigned char)probe[3])) ||
                     (strncasecmp(probe, "FUNCTION", 8) == 0 && !isnamechar((unsigned char)probe[8])) ||
                     (strncasecmp(probe, "END", 3) == 0 && !isnamechar((unsigned char)probe[3])) ||
                     (strncasecmp(probe, "EXIT", 4) == 0 && !isnamechar((unsigned char)probe[4])) ||
                     (strncasecmp(probe, "LOCAL", 5) == 0 && !isnamechar((unsigned char)probe[5])) ||
                     (strncasecmp(probe, "STATIC", 6) == 0 && !isnamechar((unsigned char)probe[6]));
        if (!is_def && source_stmt_references_bridged_subfun(cs, stmt)) {
            const char *line_end = stmt;
            while (*line_end && *line_end != ':' && *line_end != '\'') line_end++;
            size_t len = (size_t)(line_end - stmt);
            if (len >= STRINGSIZE) len = STRINGSIZE - 1;
            char tmp[STRINGSIZE];
            memcpy(tmp, stmt, len);
            tmp[len] = 0;
            source_emit_bridge_for_stmt(cs, tmp);
            return;
        }

        /* Phase 11: if the program uses STRUCT SAVE / STRUCT LOAD (which
         * bridge to the interpreter's FileTable[]), keep all file state in
         * the same table by bridging OPEN / CLOSE / SEEK too.  The VM's
         * vm_files[] would otherwise be invisible to the bridged struct
         * commands.  Scoped to the few file statements — PRINT # / INPUT #
         * still compile natively but won't see interpreter-opened files,
         * which is documented as the tradeoff. */
        if (fe->uses_struct_file_io &&
            ((strncasecmp(probe, "OPEN",  4) == 0 && !isnamechar((unsigned char)probe[4])) ||
             (strncasecmp(probe, "CLOSE", 5) == 0 && !isnamechar((unsigned char)probe[5])) ||
             (strncasecmp(probe, "SEEK",  4) == 0 && !isnamechar((unsigned char)probe[4])))) {
            const char *line_end = stmt;
            while (*line_end && *line_end != ':' && *line_end != '\'') line_end++;
            size_t len = (size_t)(line_end - stmt);
            if (len >= STRINGSIZE) len = STRINGSIZE - 1;
            char tmp[STRINGSIZE];
            memcpy(tmp, stmt, len);
            tmp[len] = 0;
            source_emit_bridge_for_stmt(cs, tmp);
            return;
        }

        /* Variable file numbers: source_parse_file_number() and the
         * native PRINT/OPEN/CLOSE/SEEK syscalls all bake the file
         * number into a compile-time aux byte. The interpreter accepts
         * `#fnbr` where fnbr is a runtime variable, so any file
         * statement we see with `#<non-digit>` gets bridged whole.
         * Cheap to detect: scan the statement for an unquoted '#' and
         * peek the next non-space char. Skip over string literals to
         * avoid matching '#' inside text. */
        {
            int has_var_fnbr = 0;
            const char *scan = stmt;
            int in_str = 0;
            while (*scan && *scan != '\n' && *scan != '\'') {
                char c = *scan;
                if (c == '"') { in_str = !in_str; scan++; continue; }
                if (!in_str && c == '#') {
                    const char *q = scan + 1;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q && !isdigit((unsigned char)*q)) {
                        has_var_fnbr = 1;
                        break;
                    }
                }
                scan++;
            }
            if (has_var_fnbr) {
                const char *line_end = stmt;
                int s_in_str = 0;
                while (*line_end && *line_end != '\n') {
                    if (*line_end == '"') s_in_str = !s_in_str;
                    if (!s_in_str && (*line_end == ':' || *line_end == '\'')) break;
                    line_end++;
                }
                size_t len = (size_t)(line_end - stmt);
                if (len >= STRINGSIZE) len = STRINGSIZE - 1;
                char tmp[STRINGSIZE];
                memcpy(tmp, stmt, len);
                tmp[len] = 0;
                source_emit_bridge_for_stmt(cs, tmp);
                return;
            }
        }
    }

    if (*p == '?') {
        p++;
        source_compile_print(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "PRINT")) {
        source_compile_print(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "OPTION")) {
        return;
    }

    if (source_keyword(&p, "TYPE")) {
        // TYPE <name> … END TYPE — record the shape into g_structtbl so DIM
        // with `AS <name>` can resolve struct_idx at compile time.  --interp
        // also runs PrepareProgramExt, but InitBasic zeros g_structtbl and
        // PrepareProgram frees stale entries on every program load, so the
        // double-population is either identical or a clean no-op.  In --vm
        // mode this is the only writer.  Block alignment padding is applied
        // on END TYPE via GetStructAlignment inside PrepareProgramExt — we
        // mirror that when finalizing below.
        source_skip_space(&p);
        unsigned char tname[MAXVARLEN + 1];
        int tlen = 0;
        while (isnamechar((unsigned char)*p) && tlen < MAXVARLEN) {
            tname[tlen++] = (unsigned char)mytoupper(*p);
            p++;
        }
        tname[tlen] = 0;
        if (tlen == 0) {
            bc_set_error(cs, "Invalid TYPE name");
            return;
        }
        if (FindStructType(tname) >= 0) {
            // Already registered (e.g. by PrepareProgramExt in compare mode).
            // Skip members silently; the existing shape still matches.
            fe->in_type_block = 1;
            fe->struct_def_inprogress = NULL;
            return;
        }
        if (g_structcnt >= MAX_STRUCT_TYPES) {
            bc_set_error(cs, "Too many structure types");
            return;
        }
        struct s_structdef *sd = (struct s_structdef *)GetMemory(sizeof(struct s_structdef));
        memset(sd, 0, sizeof(*sd));
        memcpy(sd->name, tname, tlen + 1);
        fe->struct_def_inprogress = sd;
        fe->in_type_block = 1;
        return;
    }

    if (source_keyword(&p, "CONST")) {
        source_compile_const(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "DIM")) {
        source_compile_dim(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "DATA")) {
        source_compile_data(cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "READ")) {
        source_compile_read(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RESTORE")) {
        bc_emit_byte(cs, OP_RESTORE);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "INC")) {
        source_compile_inc(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "FASTGFX")) {
        source_compile_fastgfx(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "FRAMEBUFFER")) {
        source_compile_framebuffer(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    /* SAVE forms (SAVE IMAGE, SAVE COMPRESSED IMAGE, SAVE DATA, bare SAVE)
     * drop through to the OP_BRIDGE_CMD fallback at the bottom of this
     * function. The interpreter's cmd_save handles every variant and
     * FileIO.c now routes its raw f_write calls through POSIX on host,
     * so SAVE IMAGE under FRUN writes real BMP files just like RUN. */

    if (source_keyword(&p, "RANDOMIZE")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        source_emit_int_conversion(cs, type);
        bc_emit_byte(cs, OP_RANDOMIZE);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "PAUSE")) {
        uint8_t type = source_parse_expression(fe, cs, &p);
        source_emit_int_conversion(cs, type);
        bc_emit_byte(cs, OP_PAUSE);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "SETPIN")) {
        source_compile_setpin(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "PWM")) {
        source_compile_pwm(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "SERVO")) {
        source_compile_servo(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "ERROR")) {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') {
            bc_emit_byte(cs, OP_ERROR_EMPTY);
        } else {
            uint8_t type = source_parse_expression(fe, cs, &p);
            if (type != T_STR) bc_set_error(cs, "ERROR requires a string argument");
            bc_emit_byte(cs, OP_ERROR_S);
        }
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "OPEN")) {
        source_compile_open(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "DRIVE")) {
        source_compile_drive(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "CLOSE")) {
        source_compile_close(cs, &p);
        source_statement_end(cs, p);
        return;
    }

    /* FILES: dropped through to OP_BRIDGE_CMD fallback (see comment above
     * source_compile_files). Interpreter and VM share FileIO.c's cmd_files. */

    if (source_keyword(&p, "SEEK")) {
        source_compile_seek(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "MKDIR")) {
        source_compile_file_path_command(fe, cs, &p, BC_SYS_FILE_MKDIR, "MKDIR requires string path");
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "CHDIR")) {
        source_compile_file_path_command(fe, cs, &p, BC_SYS_FILE_CHDIR, "CHDIR requires string path");
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RMDIR")) {
        source_compile_file_path_command(fe, cs, &p, BC_SYS_FILE_RMDIR, "RMDIR requires string path");
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "KILL")) {
        source_compile_file_path_command(fe, cs, &p, BC_SYS_FILE_KILL, "KILL requires string path");
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RENAME")) {
        source_compile_rename(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "COPY")) {
        source_compile_copy(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RUN")) {
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') {
            bc_set_error(cs, "RUN requires a filename");
        } else {
            if (!source_parse_string_expression(fe, cs, &p, "RUN requires string filename")) {
                /* error already set */
            } else {
                source_emit_syscall(cs, BC_SYS_RUN, 1, NULL, 0);
            }
        }
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "CLS")) {
        source_compile_cls(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "COLOUR") || source_keyword(&p, "COLOR")) {
        uint8_t type;
        int argc = 0;
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') {
            bc_set_error(cs, "Argument count");
            return;
        }
        type = source_parse_expression(fe, cs, &p);
        if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR)) {
            bc_set_error(cs, "COLOUR requires numeric arguments");
            return;
        }
        argc = 1;
        source_skip_space(&p);
        if (*p == ',') {
            p++;
            type = source_parse_expression(fe, cs, &p);
            if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR)) {
                bc_set_error(cs, "COLOUR requires numeric arguments");
                return;
            }
            argc = 2;
        }
        source_emit_syscall_noaux(cs, BC_SYS_GFX_COLOUR, (uint8_t)argc);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "FONT")) {
        uint8_t type;
        int argc = 0;
        source_skip_space(&p);
        if (*p == '#') p++;
        if (*p == '\0' || *p == '\'') {
            bc_set_error(cs, "Argument count");
            return;
        }
        type = source_parse_expression(fe, cs, &p);
        if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR)) {
            bc_set_error(cs, "FONT requires numeric arguments");
            return;
        }
        argc = 1;
        source_skip_space(&p);
        if (*p == ',') {
            p++;
            type = source_parse_expression(fe, cs, &p);
            if (((type & (T_INT | T_NBR)) == 0) || (type & T_STR)) {
                bc_set_error(cs, "FONT requires numeric arguments");
                return;
            }
            argc = 2;
        }
        source_emit_syscall_noaux(cs, BC_SYS_GFX_FONT, (uint8_t)argc);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "BOX")) {
        source_compile_gfx_args(fe, cs, &p, "BOX", BC_SYS_GFX_BOX,
                                4, BC_BOX_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RBOX")) {
        source_compile_gfx_args(fe, cs, &p, "RBOX", BC_SYS_GFX_RBOX,
                                4, BC_BOX_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "ARC")) {
        source_compile_gfx_args(fe, cs, &p, "ARC", BC_SYS_GFX_ARC,
                                6, BC_BOX_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "TRIANGLE")) {
        source_compile_gfx_args(fe, cs, &p, "TRIANGLE", BC_SYS_GFX_TRIANGLE,
                                6, BC_TRIANGLE_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "POLYGON")) {
        source_compile_polygon(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "CIRCLE")) {
        source_compile_gfx_args(fe, cs, &p, "CIRCLE", BC_SYS_GFX_CIRCLE,
                                3, BC_BOX_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    {
        const char *q = p;
        if (source_keyword(&q, "LINE")) {
            const char *r = q;
            source_skip_space(&r);
            if (source_keyword(&r, "INPUT")) {
                p = r;
                source_compile_line_input(fe, cs, &p);
                source_statement_end(cs, p);
                return;
            }
        }
    }

    if (source_keyword(&p, "LINE")) {
        source_compile_gfx_args(fe, cs, &p, "LINE", BC_SYS_GFX_LINE,
                                2, BC_LINE_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "PIXEL")) {
        source_compile_gfx_args(fe, cs, &p, "PIXEL", BC_SYS_GFX_PIXEL,
                                2, BC_PIXEL_ARG_COUNT, 0);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "TEXT")) {
        source_compile_gfx_args(fe, cs, &p, "TEXT", BC_SYS_GFX_TEXT,
                                3, BC_TEXT_ARG_COUNT, 1);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "SELECT")) {
        source_compile_select_case(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "CASE")) {
        source_compile_case(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "LOCAL") || source_keyword(&p, "STATIC")) {
        source_compile_local(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "SUB")) {
        source_compile_sub(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "FUNCTION")) {
        source_compile_function(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "ELSEIF")) {
        source_compile_elseif(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "ELSE")) {
        const char *q = p;
        source_skip_space(&q);
        if (source_keyword(&q, "IF")) {
            source_compile_elseif(fe, cs, &q);
            source_statement_end(cs, q);
            return;
        }
        source_compile_else(cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "ENDIF") || source_keyword(&p, "END IF")) {
        source_compile_endif(cs);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "END")) {
        source_skip_space(&p);
        if (source_keyword(&p, "SUB")) {
            source_compile_end_sub(cs);
            source_statement_end(cs, p);
            return;
        }
        if (source_keyword(&p, "FUNCTION")) {
            source_compile_end_function(cs);
            source_statement_end(cs, p);
            return;
        }
        if (source_keyword(&p, "SELECT")) {
            source_compile_end_select(cs);
            source_statement_end(cs, p);
            return;
        }
        bc_emit_byte(cs, OP_END);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "DO")) {
        source_compile_do(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "LOOP")) {
        source_compile_loop(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "EXIT")) {
        source_compile_exit(cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "IF")) {
        source_compile_if(fe, cs, &p);
        return;
    }

    if (source_keyword(&p, "FOR")) {
        source_compile_for(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "NEXT")) {
        source_compile_next(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "GOTO")) {
        source_compile_goto(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "GOSUB")) {
        source_compile_gosub(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "RETURN")) {
        bc_emit_byte(cs, OP_RETURN);
        source_statement_end(cs, p);
        return;
    }

    if (source_keyword(&p, "LET")) {
        source_compile_assignment(fe, cs, &p);
        source_statement_end(cs, p);
        return;
    }

    if (isnamestart((unsigned char)*p)) {
        const char *probe = p;
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t type = 0;
        if (source_parse_varname(&probe, name, &name_len, &type)) {
            source_skip_space(&probe);
            if (*probe == '=') {
                source_compile_assignment(fe, cs, &p);
                source_statement_end(cs, p);
                return;
            }
            const char *after_name = probe;
            if (*probe == '(') {
                int depth = 0;
                do {
                    if (*probe == '(') depth++;
                    else if (*probe == ')') depth--;
                    else if (*probe == '\0') break;
                    probe++;
                } while (depth > 0);
                source_skip_space(&probe);
                if (*probe == '=') {
                    source_compile_assignment(fe, cs, &p);
                    source_statement_end(cs, p);
                    return;
                }
            }

            int sf_idx = bc_find_subfun(cs, name, name_len);
            if (sf_idx >= 0 && cs->subfuns[sf_idx].return_type == 0 &&
                !cs->subfuns[sf_idx].bridged) {
                p = after_name;
                int nargs = source_compile_call_args(fe, cs, &p, 0);
                if (!cs->has_error) {
                    bc_emit_byte(cs, OP_CALL_SUB);
                    bc_emit_u16(cs, (uint16_t)sf_idx);
                    bc_emit_byte(cs, (uint8_t)nargs);
                }
                source_statement_end(cs, p);
                return;
            }
            /* Fall through to OP_BRIDGE_CMD at the bottom — either no such
             * user-defined sub, or the sub takes a struct param and the
             * interpreter owns it (Phase 7). */
        }
    }

    /* Unsupported command: bridge to interpreter.
     * Tokenize the source statement and embed the tokenized form
     * in the bytecode stream so the VM can hand it to the interpreter. */
    {
        unsigned char saved_inpbuf[STRINGSIZE];
        unsigned char saved_tknbuf[STRINGSIZE];
        memcpy(saved_inpbuf, inpbuf, STRINGSIZE);
        memcpy(saved_tknbuf, tknbuf, STRINGSIZE);

        /* Copy statement into inpbuf for tokenise() */
        size_t slen = strlen(stmt);
        if (slen >= STRINGSIZE) slen = STRINGSIZE - 1;
        memcpy(inpbuf, stmt, slen);
        inpbuf[slen] = 0;

        tokenise(1);  /* console mode: no T_NEWLINE prefix */

        /* tknbuf now has: cmd_token(2 bytes) + tokenized args + 0x00 terminator.
         * Find the length of the tokenized form. */
        unsigned char *tp = tknbuf;
        while (*tp) {
            if (*tp == T_LINENBR) { tp += 3; continue; }
            tp++;
        }
        uint16_t tok_len = (uint16_t)(tp - tknbuf);

        if (tok_len < 2) {
            bc_set_error(cs, "Unsupported source command near: %.24s", p);
        } else {
            bc_emit_byte(cs, OP_BRIDGE_CMD);
            bc_emit_u16(cs, tok_len);
            for (uint16_t i = 0; i < tok_len; i++)
                bc_emit_byte(cs, tknbuf[i]);
        }

        memcpy(inpbuf, saved_inpbuf, STRINGSIZE);
        memcpy(tknbuf, saved_tknbuf, STRINGSIZE);
    }
}

static void source_compile_line(BCSourceFrontend *fe, BCCompiler *cs, const char *line) {
    bc_crash_checkpoint(BC_CK_LINE_ENTER, "line: enter");
    bc_crash_snapshot_cs(cs);
    const char *p = line;
    source_skip_space(&p);

    /* Inside a FUNCTION foo() AS <struct> body — skip every line until we
     * see `END FUNCTION`.  The interpreter owns such functions (calls reach
     * them via bridged cmd_let); the VM doesn't emit any bytecode for the
     * body. */
    if (fe->in_struct_fn) {
        const char *cp = p;
        if (isdigit((unsigned char)*cp)) {
            char *end = NULL;
            (void)strtol(cp, &end, 10);
            if (end != cp) { cp = end; source_skip_space(&cp); }
        }
        if ((strncasecmp(cp, "END FUNCTION", 12) == 0 && !isnamechar((unsigned char)cp[12])) ||
            (strncasecmp(cp, "END SUB", 7) == 0 && !isnamechar((unsigned char)cp[7]))) {
            fe->in_struct_fn = 0;
        }
        return;
    }

    /* Inside a TYPE block — consume member lines silently until END TYPE.
     * We also register members into `fe->struct_def_inprogress` so --vm mode
     * (which skips PrepareProgramExt) still ends up with a populated
     * g_structtbl.  Skip any leading line-number prefix so tests that use
     * numbered lines still terminate on `999 END TYPE`. */
    if (fe->in_type_block) {
        const char *cp = p;
        if (isdigit((unsigned char)*cp)) {
            char *end = NULL;
            (void)strtol(cp, &end, 10);
            if (end != cp) { cp = end; source_skip_space(&cp); }
        }
        if (strncasecmp(cp, "END TYPE", 8) == 0 && !isnamechar((unsigned char)cp[8])) {
            struct s_structdef *sd = fe->struct_def_inprogress;
            if (sd) {
                if (sd->num_members > 0 && g_structcnt < MAX_STRUCT_TYPES) {
                    // Pad total_size to natural alignment so arrays of structs
                    // stay aligned (upstream PrepareProgramExt does the same).
                    int align = GetStructAlignment(sd);
                    if (align > 1 && (sd->total_size % align) != 0)
                        sd->total_size = ((sd->total_size + align - 1) / align) * align;
                    g_structtbl[g_structcnt++] = sd;
                } else {
                    int empty = (sd->num_members == 0);
                    FreeMemory((unsigned char *)sd);
                    if (empty) bc_set_error(cs, "TYPE has no members");
                }
                fe->struct_def_inprogress = NULL;
            }
            fe->in_type_block = 0;
            return;
        }
        if (*cp == 0 || *cp == '\'') return;                        // blank / comment line
        if (fe->struct_def_inprogress) {
            const char *merr = ParseStructMember((unsigned char *)cp,
                                                 fe->struct_def_inprogress);
            if (merr) bc_set_error(cs, "%s", merr);
        }
        return;
    }

    /* If inside '!ASM block, accumulate lines until '!ENDASM */
    if (fe->asm_active) {
        const char *cp = p;
        source_skip_space(&cp);
        /* Check for line number prefix */
        if (isdigit((unsigned char)*cp)) {
            char *end = NULL;
            (void)strtol(cp, &end, 10);
            if (end != cp) { cp = end; source_skip_space(&cp); }
        }
        /* Check for '!ENDASM */
        if (*cp == '\'' && strncasecmp(cp, "'!ENDASM", 8) == 0) {
            fe->asm_active = 0;
            source_assemble_block(fe, cs);
            fe->asm_line_count = 0;
            source_asm_buf_free(fe);
            return;
        }
        /* Strip comment-only prefix if line starts with ' — it's ASM content */
        /* Accumulate the raw line content (after any ' prefix) */
        const char *content = cp;
        if (*content == '\'') content++; /* strip leading ' if present */
        if (fe->asm_line_count < ASM_MAX_LINES) {
            strncpy(fe->asm_lines[fe->asm_line_count], content, ASM_MAX_LINE_LEN - 1);
            fe->asm_lines[fe->asm_line_count][ASM_MAX_LINE_LEN - 1] = '\0';
            fe->asm_line_nos[fe->asm_line_count] = fe->line_no;
            fe->asm_line_count++;
        } else {
            bc_set_error(cs, "'!ASM block too large (max %d lines)", ASM_MAX_LINES);
        }
        return;
    }

    int explicit_line = 0;
    if (isdigit((unsigned char)*p)) {
        char *end = NULL;
        long n = strtol(p, &end, 10);
        if (end != p) {
            explicit_line = (int)n;
            p = end;
            source_skip_space(&p);
        }
    }

    if (source_line_empty_or_comment(p)) {
        /* Check for '!FAST compiler directive */
        const char *cp = p;
        source_skip_space(&cp);
        if (*cp == '\'' && strncasecmp(cp, "'!FAST", 6) == 0) {
            fe->fast_next_loop = 1;
        }
        /* Check for '!ASM compiler directive */
        if (*cp == '\'' && strncasecmp(cp, "'!ASM", 5) == 0 &&
            (cp[5] == '\0' || cp[5] == ' ' || cp[5] == '\t' || cp[5] == '\r' || cp[5] == '\n')) {
            if (source_asm_buf_alloc(fe) != 0) {
                bc_set_error(cs, "Not enough memory for '!ASM buffer");
                return;
            }
            fe->asm_active = 1;
            fe->asm_line_count = 0;
            fe->asm_start_line = fe->line_no;
        }
        return;
    }

    int line_no = explicit_line > 0 ? explicit_line : fe->line_no;
    cs->current_line = line_no;
    bc_crash_checkpoint(BC_CK_LINE_LINEMAP, "line: add_linemap");
    bc_add_linemap_entry(cs, (uint16_t)line_no, cs->code_len);
    bc_crash_checkpoint(BC_CK_LINE_EMIT_OP_LINE, "line: emit OP_LINE");
    bc_emit_byte(cs, OP_LINE);
    bc_emit_u16(cs, (uint16_t)line_no);
    bc_crash_snapshot_cs(cs);

    while (*p && !cs->has_error) {
        bc_crash_checkpoint(BC_CK_LINE_STMT_LOOP, "line: stmt loop");
        source_skip_space(&p);
        if (*p == '\0' || *p == '\'') break;

        /* `name:` label definition before any statement. Register
         * pointing at the current code address (right after OP_LINE)
         * and continue. Multiple labels on one line are unusual but
         * legal — the loop just runs again. Pure-label lines fall
         * through and the outer `while` exits. */
        if (source_try_register_label(cs, &p)) continue;

        const char *start = p;
        const char *kw_probe = p;
        int if_statement = source_keyword(&kw_probe, "IF");
        int in_string = 0;
        while (*p) {
            if (*p == '"') in_string = !in_string;
            if (!in_string && ((!if_statement && *p == ':') || *p == '\'')) break;
            p++;
        }

        char stmt[STRINGSIZE + 1];
        size_t len = (size_t)(p - start);
        if (len > STRINGSIZE) len = STRINGSIZE;
        memcpy(stmt, start, len);
        stmt[len] = '\0';
        bc_crash_checkpoint(BC_CK_LINE_STMT_CALL, "line: compile_statement");
        source_compile_statement(fe, cs, stmt);
        bc_crash_checkpoint(BC_CK_LINE_STMT_DONE, "line: stmt returned");

        if (*p == ':') {
            p++;
            continue;
        }
        break;
    }
}

static void source_skip_parenthesized(const char **pp) {
    const char *p = *pp;
    if (*p != '(') return;

    int depth = 0;
    int in_string = 0;
    while (*p) {
        if (*p == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '(') depth++;
            else if (*p == ')') {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            }
        }
        p++;
    }
    *pp = p;
}

static void source_predeclare_line(BCCompiler *cs, const char *line, int line_no) {
    const char *p = line;
    source_skip_space(&p);

    if (isdigit((unsigned char)*p)) {
        char *end = NULL;
        (void)strtol(p, &end, 10);
        if (end != p) {
            p = end;
            source_skip_space(&p);
        }
    }
    if (source_line_empty_or_comment(p)) return;

    cs->current_line = line_no;
    if (source_keyword(&p, "SUB")) {
        source_skip_space(&p);
        const char *name_start = p;
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t type = 0;
        if (source_parse_varname(&p, name, &name_len, &type) && type == 0) {
            /* Phase 7: subs with any `As <struct>` parameter are owned by the
             * interpreter — don't register here, so the statement dispatcher
             * drops calls through to the OP_BRIDGE_CMD fallback. */
            /* Phase 7: subs with `As <struct>` params are interpreter-owned,
             * but we still register them so source_compile_statement's
             * early-bridge scan can detect `.bridged` and route the whole
             * line through OP_BRIDGE_CMD. */
            int has_struct = source_params_contain_struct(p);
            int sf = source_get_or_create_subfun(cs, name_start, name_len, 0);
            if (sf >= 0 && has_struct) cs->subfuns[sf].bridged = 1;
        }
        return;
    }

    if (source_keyword(&p, "FUNCTION")) {
        source_skip_space(&p);
        const char *name_start = p;
        char name[MAXVARLEN + 1];
        int name_len = 0;
        uint8_t ret_type = 0;
        if (!source_parse_varname(&p, name, &name_len, &ret_type)) return;

        int has_suffix = (ret_type != 0);
        int sf_name_len = has_suffix ? name_len - 1 : name_len;
        if (ret_type == 0) ret_type = T_NBR;

        /* Phase 6/7: functions whose params or return type involve a struct
         * are interpreter-owned, but still register with .bridged = 1 so
         * source_compile_statement's early-bridge check can see them and
         * bridge the enclosing line. */
        int has_struct_param = source_params_contain_struct(p);
        source_skip_space(&p);
        if (*p == '(') source_skip_parenthesized(&p);
        uint8_t as_type = source_parse_as_type_clause(&p);
        if (as_type != 0 && !has_suffix) ret_type = as_type;

        int sf = source_get_or_create_subfun(cs, name_start, sf_name_len, ret_type);
        if (sf >= 0 && (has_struct_param || ret_type == T_STRUCT))
            cs->subfuns[sf].bridged = 1;
    }
}

static void source_update_continuation_setting(const char *line, unsigned char *continuation) {
    const char *p = line;

    if (!continuation) return;
    source_skip_space(&p);
    if (isdigit((unsigned char)*p)) {
        char *end = NULL;
        (void)strtol(p, &end, 10);
        if (end != p) {
            p = end;
            source_skip_space(&p);
        }
    }
    if (!source_keyword(&p, "OPTION")) return;
    source_skip_space(&p);
    if (!source_keyword(&p, "CONTINUATION")) return;
    source_skip_space(&p);
    if (!source_keyword(&p, "LINES")) return;
    source_skip_space(&p);
    if (source_keyword(&p, "ON") || source_keyword(&p, "ENABLE")) {
        *continuation = '_';
    } else if (source_keyword(&p, "OFF") || source_keyword(&p, "DISABLE")) {
        *continuation = 0;
    }
}

static int source_read_logical_line(const char **pp, char *line, size_t line_cap,
                                    int *physical_line_io, int *line_no_out,
                                    unsigned char *continuation) {
    const char *p = *pp;
    size_t out_len = 0;
    int line_no = *physical_line_io;

    if (*p == '\0') return 0;
    line[0] = '\0';

    while (*p) {
        const char *start = p;
        size_t len;
        while (*p && *p != '\n' && *p != '\r') p++;
        len = (size_t)(p - start);
        if (out_len + len > line_cap - 1) len = (line_cap - 1) - out_len;
        memcpy(line + out_len, start, len);
        out_len += len;
        line[out_len] = '\0';

        if (*p == '\r' && p[1] == '\n') p += 2;
        else if (*p == '\n' || *p == '\r') p++;

        (*physical_line_io)++;
        if (*continuation && out_len >= 2 &&
            line[out_len - 2] == ' ' && line[out_len - 1] == *continuation) {
            out_len -= 2;
            line[out_len] = '\0';
            continue;
        }
        break;
    }

    *pp = p;
    *line_no_out = line_no;
    source_update_continuation_setting(line, continuation);
    return 1;
}

// Phase 7 helper: scan the program for `TYPE … END TYPE` blocks and populate
// g_structtbl BEFORE source_predeclare_subfuns runs.  Without this pass,
// predeclare can't tell a SUB/FUN apart from one whose `As <struct>` param
// makes it interpreter-owned, and call sites emit OP_CALL_SUB for a sub
// whose body we later flag `.bridged`.  Mirrors source_compile_line's TYPE
// block handling but without any bytecode emission.
static void source_prescan_types(BCCompiler *cs, const char *source) {
    (void)cs;
    int physical_line = 1;
    int line_no = 1;
    unsigned char continuation = 0;
    const char *p = source;
    char line[STRINGSIZE + 1];
    struct s_structdef *sd = NULL;
    int in_asm = 0;

    while (source_read_logical_line(&p, line, sizeof(line), &physical_line, &line_no, &continuation)) {
        const char *lp = line;
        source_skip_space(&lp);
        if (isdigit((unsigned char)*lp)) {
            char *end = NULL;
            (void)strtol(lp, &end, 10);
            if (end != lp) { lp = end; source_skip_space(&lp); }
        }
        if (in_asm) {
            if (*lp == '\'' && strncasecmp(lp, "'!ENDASM", 8) == 0) in_asm = 0;
            continue;
        }
        if (*lp == '\'' && strncasecmp(lp, "'!ASM", 5) == 0 &&
            (lp[5] == '\0' || lp[5] == ' ' || lp[5] == '\t' || lp[5] == '\r' || lp[5] == '\n')) {
            in_asm = 1;
            continue;
        }

        if (sd) {
            if (strncasecmp(lp, "END TYPE", 8) == 0 && !isnamechar((unsigned char)lp[8])) {
                if (sd->num_members > 0 && g_structcnt < MAX_STRUCT_TYPES) {
                    int align = GetStructAlignment(sd);
                    if (align > 1 && (sd->total_size % align) != 0)
                        sd->total_size = ((sd->total_size + align - 1) / align) * align;
                    g_structtbl[g_structcnt++] = sd;
                } else {
                    FreeMemory((unsigned char *)sd);
                }
                sd = NULL;
                continue;
            }
            if (*lp == 0 || *lp == '\'') continue;
            (void)ParseStructMember((unsigned char *)lp, sd);
            continue;
        }

        if (strncasecmp(lp, "TYPE", 4) == 0 && !isnamechar((unsigned char)lp[4])) {
            const char *tp = lp + 4;
            source_skip_space(&tp);
            unsigned char tname[MAXVARLEN + 1];
            int tlen = 0;
            while (isnamechar((unsigned char)*tp) && tlen < MAXVARLEN) {
                tname[tlen++] = (unsigned char)mytoupper(*tp);
                tp++;
            }
            tname[tlen] = 0;
            if (tlen == 0) continue;
            if (FindStructType(tname) >= 0) continue;      // already registered
            if (g_structcnt >= MAX_STRUCT_TYPES) continue;
            sd = (struct s_structdef *)GetMemory(sizeof(struct s_structdef));
            memset(sd, 0, sizeof(*sd));
            memcpy(sd->name, tname, tlen + 1);
        }
    }
    if (sd) FreeMemory((unsigned char *)sd);
}

/* Phase 8 helper: does this LOCAL/STATIC declarator line have an `AS <name>`
 * clause where <name> resolves to a registered struct type?  Returns 1 if so.
 * Walks identifier-shaped tokens, skipping quoted strings and tail comments,
 * so spurious hits inside `"AS Point"` string literals are ignored. */
static int source_local_line_has_struct_as(const char *line) {
    const char *p = line;
    int in_str = 0;
    while (*p && *p != '\'') {
        if (*p == '"') { in_str = !in_str; p++; continue; }
        if (in_str) { p++; continue; }
        if (!isnamestart((unsigned char)*p)) { p++; continue; }
        const char *ns = p;
        while (isnamechar((unsigned char)*p)) p++;
        int nlen = (int)(p - ns);
        if (nlen == 2 && (ns[0] | 0x20) == 'a' && (ns[1] | 0x20) == 's') {
            source_skip_space(&p);
            if (isnamestart((unsigned char)*p)) {
                unsigned char tname[MAXVARLEN + 1];
                int tl = 0;
                while (isnamechar((unsigned char)*p) && tl < MAXVARLEN) {
                    tname[tl++] = (unsigned char)mytoupper(*p);
                    p++;
                }
                tname[tl] = 0;
                if (FindStructType(tname) >= 0) return 1;
            }
        }
    }
    return 0;
}

static void source_predeclare_subfuns(BCCompiler *cs, const char *source) {
    int physical_line = 1;
    int line_no = 1;
    unsigned char continuation = 0;
    const char *p = source;
    char line[STRINGSIZE + 1];
    int in_asm = 0;
    /* Phase 8: track the enclosing SUB/FUNCTION across lines.  A body-level
     * `LOCAL … AS <struct>` (or STATIC) can't be compiled natively — the VM
     * frame only knows scalar slots — so we mark the sub bridged here, and
     * source_compile_statement routes any call to it through OP_BRIDGE_CMD. */
    int current_sf = -1;

    while (!cs->has_error &&
           source_read_logical_line(&p, line, sizeof(line), &physical_line, &line_no, &continuation)) {
        /* Skip lines inside '!ASM blocks */
        const char *lp = line;
        source_skip_space(&lp);
        /* Skip line number prefix */
        if (isdigit((unsigned char)*lp)) {
            char *end = NULL;
            (void)strtol(lp, &end, 10);
            if (end != lp) { lp = end; source_skip_space(&lp); }
        }
        if (in_asm) {
            if (*lp == '\'' && strncasecmp(lp, "'!ENDASM", 8) == 0)
                in_asm = 0;
            continue;
        }
        if (*lp == '\'' && strncasecmp(lp, "'!ASM", 5) == 0 &&
            (lp[5] == '\0' || lp[5] == ' ' || lp[5] == '\t' || lp[5] == '\r' || lp[5] == '\n')) {
            in_asm = 1;
            continue;
        }
        source_predeclare_line(cs, line, line_no);

        /* Phase 8 body-scan: update current_sf on SUB/FUNCTION boundaries and
         * mark the sub bridged when its body declares a struct-typed LOCAL. */
        if (strncasecmp(lp, "SUB", 3) == 0 && !isnamechar((unsigned char)lp[3])) {
            const char *np = lp + 3;
            source_skip_space(&np);
            const char *ns = np;
            char sn[MAXVARLEN + 1];
            int snl = 0;
            uint8_t stype = 0;
            current_sf = (source_parse_varname(&np, sn, &snl, &stype) && stype == 0)
                             ? bc_find_subfun(cs, ns, snl)
                             : -1;
            continue;
        }
        if (strncasecmp(lp, "FUNCTION", 8) == 0 && !isnamechar((unsigned char)lp[8])) {
            const char *np = lp + 8;
            source_skip_space(&np);
            const char *ns = np;
            char sn[MAXVARLEN + 1];
            int snl = 0;
            uint8_t stype = 0;
            if (source_parse_varname(&np, sn, &snl, &stype)) {
                int match_len = (stype != 0) ? snl - 1 : snl;
                current_sf = bc_find_subfun(cs, ns, match_len);
            } else {
                current_sf = -1;
            }
            continue;
        }
        if ((strncasecmp(lp, "END SUB", 7) == 0 && !isnamechar((unsigned char)lp[7])) ||
            (strncasecmp(lp, "END FUNCTION", 12) == 0 && !isnamechar((unsigned char)lp[12]))) {
            current_sf = -1;
            continue;
        }
        if (current_sf < 0 || cs->subfuns[current_sf].bridged) continue;

        int is_local  = (strncasecmp(lp, "LOCAL",  5) == 0 && !isnamechar((unsigned char)lp[5]));
        int is_static = (strncasecmp(lp, "STATIC", 6) == 0 && !isnamechar((unsigned char)lp[6]));
        if (is_local || is_static) {
            const char *decls = lp + (is_local ? 5 : 6);
            if (source_local_line_has_struct_as(decls)) {
                cs->subfuns[current_sf].bridged = 1;
                continue;
            }
        }

        /* If the body uses `#<varname>` for any file op, the
         * statement-level early bridge in source_compile_statement
         * tokenises the line and hands it to the interpreter — but
         * the interpreter looks variables up in g_vartbl and can't
         * see VM-frame locals. Bridge the whole sub instead so the
         * interp can run it natively against its own locals. Cheap
         * substring scan; false positives just bridge a sub
         * unnecessarily, doesn't affect correctness. */
        {
            const char *q = lp;
            int in_str = 0;
            while (*q && *q != '\n') {
                if (*q == '"') in_str = !in_str;
                if (!in_str && *q == '\'') break;
                if (!in_str && *q == '#') {
                    const char *r = q + 1;
                    while (*r == ' ' || *r == '\t') r++;
                    if (*r && !isdigit((unsigned char)*r)) {
                        cs->subfuns[current_sf].bridged = 1;
                        break;
                    }
                }
                q++;
            }
        }
    }
}

/* Phase 11/13: detect whether the program uses any STRUCT subcommand in
 * `wanted` (NULL-terminated list of uppercase names).  Substring scan — both
 * tokens are unique enough that false positives in comments/strings only
 * cause a bit of extra bridging, not incorrect behaviour. */
static int source_program_uses_struct_subcmd(const char *source, const char *const *wanted) {
    const char *p = source;
    while (*p) {
        if ((*p == 's' || *p == 'S') &&
            strncasecmp(p, "STRUCT", 6) == 0 &&
            (p == source || !isnamechar((unsigned char)p[-1])) &&
            !isnamechar((unsigned char)p[6])) {
            const char *q = p + 6;
            while (*q == ' ' || *q == '\t') q++;
            for (const char *const *w = wanted; *w; w++) {
                int wl = (int)strlen(*w);
                if (strncasecmp(q, *w, wl) == 0 && !isnamechar((unsigned char)q[wl]))
                    return 1;
            }
        }
        p++;
    }
    return 0;
}

static int source_program_uses_struct_file_io(const char *source) {
    static const char *const wanted[] = {"SAVE", "LOAD", NULL};
    return source_program_uses_struct_subcmd(source, wanted);
}

static int source_program_uses_struct_extract_insert(const char *source) {
    static const char *const wanted[] = {"EXTRACT", "INSERT", NULL};
    return source_program_uses_struct_subcmd(source, wanted);
}

int bc_compile_source(BCCompiler *cs, const char *source, const char *source_name) {
    BCSourceFrontend fe;
    memset(&fe, 0, sizeof(fe));
    (void)source_name;
    fe.line_no = 1;

    bc_crash_checkpoint(BC_CK_COMPILE_PREDECLARE, "predeclare subfuns");
    /* Populate g_structtbl up front so predeclare can recognise
     * `SUB foo(p As Point)` as interpreter-owned before the main compile
     * pass reaches either the TYPE block or the SUB body. */
    source_prescan_types(cs, source);
    source_predeclare_subfuns(cs, source);
    fe.uses_struct_file_io = source_program_uses_struct_file_io(source);
    fe.uses_struct_extract_insert = source_program_uses_struct_extract_insert(source);
    if (cs->has_error) { source_asm_buf_free(&fe); return -1; }

    const char *p = source;
    int physical_line = 1;
    unsigned char continuation = 0;
    while (!cs->has_error) {
        char line[STRINGSIZE + 1];
        if (!source_read_logical_line(&p, line, sizeof(line), &physical_line, &fe.line_no, &continuation))
            break;
        /* Per-line checkpoint so a crash points to the logical line being compiled. */
        char _ck_lbl[32];
        snprintf(_ck_lbl, sizeof(_ck_lbl), "compile line %d", fe.line_no);
        bc_crash_checkpoint(BC_CK_COMPILE_LINE, _ck_lbl);
        source_compile_line(&fe, cs, line);
    }

    if (cs->has_error) {
        if (cs->error_line == 0) cs->error_line = fe.line_no;
        source_asm_buf_free(&fe);
        return -1;
    }

    bc_crash_checkpoint(BC_CK_COMPILE_EMIT_END, "emit OP_END");
    bc_emit_byte(cs, OP_END);
    bc_crash_checkpoint(BC_CK_COMPILE_FIXUPS, "resolve fixups");
    bc_resolve_fixups(cs);
    bc_crash_checkpoint(BC_CK_COMPILE_DONE, "compile done");
    source_asm_buf_free(&fe);  /* no-op if never allocated */
    return cs->has_error ? -1 : 0;
}
