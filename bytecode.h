/*
 * bytecode.h - Bytecode VM definitions
 *
 * Defines the instruction set, compiler state, and VM state for the
 * MMBasic bytecode compiler and virtual machine.
 *
 * All variable types must be explicit (%, !, $) — no implicit typing.
 */

#ifndef __BYTECODE_H
#define __BYTECODE_H

#include "vm_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opcode definitions — variable-length encoding: [opcode:8][operands...]
 * Multi-byte operands are little-endian (ARM native).
 */
typedef enum {
    /* Stack / Value Operations */
    OP_NOP          = 0x00,
    OP_PUSH_INT     = 0x01,  /* i64 (8 bytes) */
    OP_PUSH_FLT     = 0x02,  /* f64 (8 bytes) */
    OP_PUSH_STR     = 0x03,  /* idx:16 — string from constant pool */
    OP_PUSH_ZERO    = 0x04,  /* — push integer 0 */
    OP_PUSH_ONE     = 0x05,  /* — push integer 1 */
    OP_LOAD_I       = 0x06,  /* slot:16 — load integer global */
    OP_LOAD_F       = 0x07,  /* slot:16 — load float global */
    OP_LOAD_S       = 0x08,  /* slot:16 — load string global */
    OP_STORE_I      = 0x09,  /* slot:16 — pop → integer global */
    OP_STORE_F      = 0x0A,  /* slot:16 — pop → float global */
    OP_STORE_S      = 0x0B,  /* slot:16 — pop → string global */
    OP_LOAD_ARR_I   = 0x0C,  /* slot:16, ndim:8 — load int array elem */
    OP_LOAD_ARR_F   = 0x0D,  /* slot:16, ndim:8 — load float array elem */
    OP_LOAD_ARR_S   = 0x0E,  /* slot:16, ndim:8 — load string array elem */
    OP_STORE_ARR_I  = 0x0F,  /* slot:16, ndim:8 — store int array elem */
    OP_STORE_ARR_F  = 0x10,  /* slot:16, ndim:8 — store float array elem */
    OP_STORE_ARR_S  = 0x11,  /* slot:16, ndim:8 — store string array elem */
    OP_POP          = 0x12,  /* — discard TOS */
    OP_DUP          = 0x13,  /* — duplicate TOS */
    OP_CVT_I2F      = 0x14,  /* — convert TOS int → float */
    OP_CVT_F2I      = 0x15,  /* — convert TOS float → int */

    /* Integer Arithmetic (pop 2, push 1) */
    OP_ADD_I        = 0x20,
    OP_SUB_I        = 0x21,
    OP_MUL_I        = 0x22,
    OP_IDIV_I       = 0x23,  /* integer divide (\) */
    OP_MOD_I        = 0x24,

    /* Float Arithmetic (pop 2, push 1) */
    OP_ADD_F        = 0x28,
    OP_SUB_F        = 0x29,
    OP_MUL_F        = 0x2A,
    OP_DIV_F        = 0x2B,
    OP_POW_F        = 0x2C,
    OP_MOD_F        = 0x2D,

    /* String Operations */
    OP_ADD_S        = 0x30,  /* concatenate (pop 2, push 1) */

    /* Unary (pop 1, push 1) */
    OP_NEG_I        = 0x38,
    OP_NEG_F        = 0x39,
    OP_NOT          = 0x3A,  /* logical NOT (integer) */
    OP_INV          = 0x3B,  /* bitwise NOT ~ (integer) */

    /* Bitwise / Logical (pop 2, push 1) */
    OP_AND          = 0x40,
    OP_OR           = 0x41,
    OP_XOR          = 0x42,
    OP_SHL          = 0x43,  /* << */
    OP_SHR          = 0x44,  /* >> */

    /* Integer Comparison (pop 2, push int 0 or 1) */
    OP_EQ_I         = 0x48,
    OP_NE_I         = 0x49,
    OP_LT_I         = 0x4A,
    OP_GT_I         = 0x4B,
    OP_LE_I         = 0x4C,
    OP_GE_I         = 0x4D,

    /* Float Comparison (pop 2, push int 0 or 1) */
    OP_EQ_F         = 0x50,
    OP_NE_F         = 0x51,
    OP_LT_F         = 0x52,
    OP_GT_F         = 0x53,
    OP_LE_F         = 0x54,
    OP_GE_F         = 0x55,

    /* String Comparison (pop 2, push int 0 or 1) */
    OP_EQ_S         = 0x58,
    OP_NE_S         = 0x59,
    OP_LT_S         = 0x5A,
    OP_GT_S         = 0x5B,
    OP_LE_S         = 0x5C,
    OP_GE_S         = 0x5D,

    /* Control Flow */
    OP_JMP          = 0x60,  /* offset:16 — relative jump */
    OP_JMP_ABS      = 0x61,  /* addr:32 — absolute jump */
    OP_JZ           = 0x62,  /* offset:16 — jump if TOS == 0 */
    OP_JNZ          = 0x63,  /* offset:16 — jump if TOS != 0 */
    OP_GOSUB        = 0x64,  /* addr:32 — push return, jump */
    OP_RETURN       = 0x65,

    /* FOR loop compound opcodes */
    OP_FOR_INIT_I   = 0x66,  /* var:16, lim:16, step:16, exit_off:16 */
    OP_FOR_NEXT_I   = 0x67,  /* var:16, lim:16, step:16, loop_off:16 */
    OP_FOR_INIT_F   = 0x68,  /* var:16, lim:16, step:16, exit_off:16 */
    OP_FOR_NEXT_F   = 0x69,  /* var:16, lim:16, step:16, loop_off:16 */

    /* SUB / FUNCTION */
    OP_CALL_SUB     = 0x70,  /* idx:16, nargs:8 */
    OP_CALL_FUN     = 0x71,  /* idx:16, nargs:8 */
    OP_RET_SUB      = 0x72,
    OP_RET_FUN      = 0x73,  /* result on stack */
    OP_ENTER_FRAME  = 0x74,  /* nlocals:16 */
    OP_LEAVE_FRAME  = 0x75,
    OP_LOAD_LOCAL_I = 0x76,  /* offset:16 */
    OP_LOAD_LOCAL_F = 0x77,  /* offset:16 */
    OP_LOAD_LOCAL_S = 0x78,  /* offset:16 */
    OP_STORE_LOCAL_I= 0x79,  /* offset:16 */
    OP_STORE_LOCAL_F= 0x7A,  /* offset:16 */
    OP_STORE_LOCAL_S= 0x7B,  /* offset:16 */
    OP_LOAD_LOCAL_ARR_I  = 0x7C, /* offset:16, ndim:8 */
    OP_LOAD_LOCAL_ARR_F  = 0x7D, /* offset:16, ndim:8 */
    OP_LOAD_LOCAL_ARR_S  = 0x7E, /* offset:16, ndim:8 */
    OP_STORE_LOCAL_ARR_I = 0x7F, /* offset:16, ndim:8 */
    OP_BRIDGE_CMD   = 0x80,  /* len:16 tokenized_bytes... — bridge to interpreter command handler */
    OP_BRIDGE_FUN_I = 0x81,  /* fun_idx:16 arg_len:16 tokenized_args... — bridge to interpreter function (int result) */
    OP_BRIDGE_FUN_F = 0x82,  /* fun_idx:16 arg_len:16 tokenized_args... — bridge to interpreter function (float result) */
    OP_BRIDGE_FUN_S = 0x83,  /* fun_idx:16 arg_len:16 tokenized_args... — bridge to interpreter function (string result) */
    OP_STORE_LOCAL_ARR_F = 0x84, /* offset:16, ndim:8 */
    OP_STORE_LOCAL_ARR_S = 0x85, /* offset:16, ndim:8 */
    OP_FAST_LOOP    = 0x86,  /* Register micro-op tight loop — see RegOp enum */

    /* PRINT */
    OP_PRINT_INT    = 0x88,  /* flags:8 (bit0=no newline, bit1=tab after) */
    OP_PRINT_FLT    = 0x89,  /* flags:8 */
    OP_PRINT_STR    = 0x8A,  /* flags:8 */
    OP_PRINT_NEWLINE= 0x8B,  /* — emit CR/LF */
    OP_PRINT_TAB    = 0x8C,  /* — emit tab */

    /* DIM arrays */
    OP_DIM_ARR_I    = 0x90,  /* slot:16, ndim:8 — sizes on stack */
    OP_DIM_ARR_F    = 0x91,  /* slot:16, ndim:8 */
    OP_DIM_ARR_S    = 0x92,  /* slot:16, ndim:8 */

    /* TYPE / STRUCT — field access; DIM still routes via OP_BRIDGE_CMD in Phase 1. */
    OP_LOAD_STRUCT_FIELD_I  = 0x93,  /* slot:16, offset:16 — push int64 from struct buffer */
    OP_LOAD_STRUCT_FIELD_F  = 0x94,  /* slot:16, offset:16 — push float */
    OP_LOAD_STRUCT_FIELD_S  = 0x95,  /* slot:16, offset:16 — push MMBasic string pointer */
    OP_STORE_STRUCT_FIELD_I = 0x96,  /* slot:16, offset:16 — pop int64, store */
    OP_STORE_STRUCT_FIELD_F = 0x97,  /* slot:16, offset:16 — pop float, store */
    OP_STORE_STRUCT_FIELD_S = 0x98,  /* slot:16, offset:16, size:16 — pop string, store */

    /* Struct-array element field access — pops ndim indices off the stack, uses
     * vm->arrays[slot].dims[] for multi-dim linear index calc, then addresses
     * base + linear_index * elem_size + offset.  elem_size is baked in at compile
     * time from g_structtbl[struct_idx]->total_size. */
    OP_LOAD_STRUCT_ELEM_I   = 0x99,  /* slot:16, offset:16, elem_size:16, ndim:8 */
    OP_LOAD_STRUCT_ELEM_F   = 0x9A,  /* same layout as above */
    OP_LOAD_STRUCT_ELEM_S   = 0x9B,
    OP_STORE_STRUCT_ELEM_I  = 0x9C,  /* slot:16, offset:16, elem_size:16, ndim:8 */
    OP_STORE_STRUCT_ELEM_F  = 0x9D,
    OP_STORE_STRUCT_ELEM_S  = 0x9E,  /* slot:16, offset:16, elem_size:16, ndim:8, maxstrlen:16 */

    /* Phase 4 — chained struct access with intermediate array indexing.
     * Used for expressions like `a.b(j).c` or `a(i).b(j).c(k)` where at least
     * one nested member is indexed at runtime.  All-scalar chains (`a.b.c`)
     * and outer-index-only chains (`a(i).b.c`) still use the FIELD / ELEM
     * opcodes above with an accumulated compile-time offset.
     *
     * Encoding:
     *   slot:16, outer_ndim:8, n_nested:8, outer_stride:16,
     *   per nested seg: const_offset:16, stride:16,
     *   final_offset:16,
     *   (store_s only) maxstrlen:16
     *
     * Stack at entry:  ... outer_indices ... nested_indices ... [value_for_store]
     * Innermost nested index is on top.  Store opcodes pop the value FIRST,
     * then all indices. */
    OP_LOAD_STRUCT_NESTED_I  = 0x9F,
    OP_LOAD_STRUCT_NESTED_F  = 0x16,
    OP_LOAD_STRUCT_NESTED_S  = 0x17,
    OP_STORE_STRUCT_NESTED_I = 0x18,
    OP_STORE_STRUCT_NESTED_F = 0x19,
    OP_STORE_STRUCT_NESTED_S = 0x1A,

    /* Native string functions (compiled arguments) */
    OP_STR_LEN      = 0xA0,  /* pop str, push int len */
    OP_STR_LEFT     = 0xA1,  /* pop int n, pop str, push str LEFT$(s,n) */
    OP_STR_RIGHT    = 0xA2,  /* pop int n, pop str, push str RIGHT$(s,n) */
    OP_STR_MID2     = 0xA3,  /* pop int start, pop str, push str MID$(s,start) */
    OP_STR_MID3     = 0xA4,  /* pop int len, pop int start, pop str, push str MID$(s,start,len) */
    OP_STR_UCASE    = 0xA5,  /* pop str, push str UCASE$(s) */
    OP_STR_LCASE    = 0xA6,  /* pop str, push str LCASE$(s) */
    OP_STR_VAL      = 0xA7,  /* pop str, push float VAL(s) */
    OP_STR_STR      = 0xA8,  /* pop float, push str STR$(n) */
    OP_STR_CHR      = 0xA9,  /* pop int, push str CHR$(n) */
    OP_STR_ASC      = 0xAA,  /* pop str, push int ASC(s) */
    OP_STR_INSTR    = 0xAB,  /* nargs:8 — INSTR([start%,] haystack$, needle$) */
    OP_STR_HEX      = 0xAC,  /* pop int, push str HEX$(n) */
    OP_STR_OCT      = 0xAD,  /* pop int, push str OCT$(n) */
    OP_STR_BIN      = 0xAE,  /* pop int, push str BIN$(n) */

    /* Native math functions (compiled arguments) */
    OP_MATH_SIN     = 0xB0,  /* pop float, push float SIN(x) */
    OP_MATH_COS     = 0xB1,  /* pop float, push float COS(x) */
    OP_MATH_TAN     = 0xB2,  /* pop float, push float TAN(x) */
    OP_MATH_ATN     = 0xB3,  /* pop float, push float ATN(x) */
    OP_MATH_SQR     = 0xB4,  /* pop float, push float SQR(x) */
    OP_MATH_LOG     = 0xB5,  /* pop float, push float LOG(x) */
    OP_MATH_EXP     = 0xB6,  /* pop float, push float EXP(x) */
    OP_MATH_ABS     = 0xB7,  /* pop num, push num ABS(x) — preserves type */
    OP_MATH_SGN     = 0xB8,  /* pop num, push int SGN(x) */
    OP_MATH_INT     = 0xB9,  /* pop float, push float INT(x) — floor */
    OP_MATH_FIX     = 0xBA,  /* pop float, push int FIX(x) — truncate */
    OP_MATH_CINT    = 0xBB,  /* pop float, push int CINT(x) — round */
    OP_MATH_RAD     = 0xBC,  /* pop float, push float RAD(x) */
    OP_MATH_DEG     = 0xBD,  /* pop float, push float DEG(x) */
    OP_MATH_PI      = 0xBE,  /* push float PI */
    OP_MATH_MAX     = 0xBF,  /* pop float b, pop float a, push float MAX */
    OP_MATH_MIN     = 0xC0,  /* pop float b, pop float a, push float MIN */

    /* DATA / READ / RESTORE */
    OP_READ_I       = 0xC1,  /* — push next data item as int */
    OP_READ_F       = 0xC2,  /* — push next data item as float */
    OP_READ_S       = 0xC3,  /* — push next data item as string */
    OP_RESTORE      = 0xC4,  /* — reset data pointer to 0 */
    OP_RESTORE_DATA = 0x87,  /* data_index:u16 — set data pointer to absolute index */

    /* Additional string functions */
    OP_STR_SPACE    = 0xC5,  /* pop int n, push str SPACE$(n) */
    OP_STR_STRING   = 0xC6,  /* pop int char, pop int n, push str STRING$(n,c) */
    OP_STR_INKEY    = 0xC7,  /* — push str INKEY$ */

    /* Additional numeric functions */
    OP_RND          = 0xC8,  /* — push float RND */

    /* Additional statements */
    OP_INC_I        = 0xC9,  /* raw_slot:16 — pop int delta, add into int var (high bit = local) */
    OP_INC_F        = 0xCA,  /* raw_slot:16 — pop float delta, add into float var (high bit = local) */
    OP_RANDOMIZE    = 0xCB,  /* — pop int seed, RANDOMIZE */
    OP_ERROR_S      = 0xCC,  /* — pop string, raise error */
    OP_ERROR_EMPTY  = 0xCD,  /* — raise empty error */
    OP_CLEAR        = 0xCE,  /* — CLEAR all variables */
    OP_FASTGFX_SWAP = 0xCF,  /* — FASTGFX SWAP */
    OP_FASTGFX_SYNC = 0xD0,  /* — FASTGFX SYNC */
    OP_BOX          = 0xD1,  /* — native BOX */
    OP_RGB          = 0xD2,  /* — pop b,g,r ints, push RGB(r,g,b) */
    OP_CIRCLE       = 0xD3,  /* — native CIRCLE */
    OP_DRAW_LINE    = 0xD4,  /* — native LINE */
    OP_TEXT         = 0xD5,  /* — native TEXT */
    OP_CLS          = 0xD6,  /* — native CLS */
    OP_PIXEL        = 0xD7,  /* — native PIXEL */
    OP_FASTGFX_CREATE = 0xD8, /* — FASTGFX CREATE */
    OP_FASTGFX_CLOSE  = 0xD9, /* — FASTGFX CLOSE */
    OP_FASTGFX_FPS    = 0xDA, /* — pop int fps, FASTGFX FPS */
    OP_MATH_ASIN      = 0xDB, /* pop float, push float ASIN(x) */
    OP_MATH_ACOS      = 0xDC, /* pop float, push float ACOS(x) */
    OP_MATH_ATAN2     = 0xDD, /* pop float y, pop float x, push float ATAN2(y,x) */
    OP_TIMER          = 0xDE, /* — push float TIMER */
    OP_MM_HRES        = 0xDF, /* — push int MM.HRES */
    OP_MM_VRES        = 0xE0, /* — push int MM.VRES */
    OP_STR_FIELD3     = 0xE1, /* pop delim str, pop field int, pop source str, push FIELD$ */
    OP_COLOUR         = 0xE2, /* native COLOR/COLOUR */
    OP_PAUSE          = 0xE3, /* pop numeric ms, PAUSE */
    OP_STR_DATE       = 0xE4, /* — push str DATE$ */
    OP_STR_TIME       = 0xE5, /* — push str TIME$ */
    OP_KEYDOWN        = 0xE6, /* pop int n, push int KEYDOWN(n) */
    OP_MATH_MULDIV    = 0xE7, /* pop bits,b,a; push int ((a*b) wrapped) \ 2^bits */
    OP_MATH_SQRDIV    = 0xE8, /* pop bits,a; push int ((a*a) wrapped) \ 2^bits */
    OP_SETPIN         = 0xE9, /* mode:16 option:16 — pop pin, SETPIN pin, mode[, option] */
    OP_PIN_READ       = 0xEA, /* pop pin, push int PIN(pin) */
    OP_PIN_WRITE      = 0xEB, /* pop value, pop pin, PIN(pin)=value */
    OP_FILE           = 0xEC, /* subop:8 — native file syscalls */
    OP_PIXEL_READ     = 0xED, /* pop y, pop x, push int PIXEL(x,y) */
    OP_MATH_MULSHR    = 0xEE, /* pop bits, pop b, pop a, push int trunc((a*b)/2^bits) */
    OP_RBOX           = 0xEF, /* — native RBOX */
    OP_ARC            = 0xF2, /* — native ARC */
    OP_TRIANGLE       = 0xF3, /* — native TRIANGLE */
    OP_FONT           = 0xF4, /* argc:8 — FONT [#]n[, scale] */
    OP_POLYGON        = 0xF5, /* — native POLYGON */
    OP_PWM            = 0xF6, /* subop:8 — native PWM */
    OP_SERVO          = 0xF7, /* present:8 — native SERVO */
    OP_SYSCALL        = 0xF8, /* sysid:16 argc:8 auxlen:8 aux... — generic VM syscall/intrinsic */
    OP_MATH_SQRSHR    = 0xF9, /* pop bits, pop a, push int trunc((a*a)/2^bits) */
    OP_MATH_MULSHRADD = 0xFA, /* pop c, pop bits, pop b, pop a, push int trunc((a*b)/2^bits)+c */
    OP_JCMP_I         = 0xFB, /* rel:8 off:16 — pop b,a, jump if integer relation holds */
    OP_JCMP_F         = 0xFC, /* rel:8 off:16 — pop b,a, jump if float relation holds */
    OP_MOV_VAR        = 0xFD, /* kind:8 src_raw:16 dst_raw:16 — direct typed variable copy */

    /* Housekeeping */
    OP_LINE         = 0xF0,  /* lineno:16 — for errors/trace */
    OP_CHECKINT     = 0xF1,  /* — check CTRL-C / interrupts */
    OP_END          = 0xFE,
    OP_HALT         = 0xFF,  /* STOP statement */
} BCOpcode;

/*
 * Register micro-op opcodes for OP_FAST_LOOP.
 *
 * OP_FAST_LOOP encoding:
 *   [OP_FAST_LOOP:8] [total_len:16] [nregs:8] [nlocals:8]
 *   [nglobals:8] [nconsts:8]
 *   [global_map: nglobals * (slot:16)]
 *   [const_data: nconsts * (type:8 value:64)]
 *   [micro_ops: until total_len]
 *
 * Register file:
 *   regs[0..nlocals-1]                  = local frame variables
 *   regs[nlocals..nlocals+nglobals-1]   = global variables
 *   regs[nlocals+nglobals..+nconsts-1]  = constants
 *   regs[above..]                       = temporaries
 *
 * On entry: locals and globals copied into registers, constants loaded.
 * On exit: locals and globals copied back.
 */
typedef enum {
    /* Control */
    ROP_END         = 0,   /* 1 byte — end of micro-op block */
    ROP_EXIT        = 1,   /* 1 byte — exit fast loop */
    ROP_JMP         = 2,   /* [op][off:16] = 3 bytes — relative jump */
    ROP_CHECKINT    = 3,   /* 1 byte — poll CTRL-C */

    /* Integer arithmetic: [op][dst][src1][src2] = 4 bytes */
    ROP_ADD_I       = 10,
    ROP_SUB_I       = 11,
    ROP_MUL_I       = 12,
    ROP_IDIV_I      = 13,
    ROP_MOD_I       = 14,

    /* Float arithmetic: [op][dst][src1][src2] = 4 bytes */
    ROP_ADD_F       = 15,
    ROP_SUB_F       = 16,
    ROP_MUL_F       = 17,
    ROP_DIV_F       = 18,

    /* Unary: [op][dst][src] = 3 bytes */
    ROP_NEG_I       = 20,
    ROP_NEG_F       = 21,
    ROP_NOT         = 22,
    ROP_INV         = 23,

    /* Bitwise: [op][dst][src1][src2] = 4 bytes */
    ROP_AND         = 25,
    ROP_OR          = 26,
    ROP_XOR         = 27,
    ROP_SHL         = 28,
    ROP_SHR         = 29,

    /* Move/convert: [op][dst][src] = 3 bytes */
    ROP_MOV         = 30,
    ROP_CVT_I2F     = 31,
    ROP_CVT_F2I     = 32,

    /* Load immediate: [op][dst][value:64] = 10 bytes */
    ROP_LOAD_IMM_I  = 35,
    ROP_LOAD_IMM_F  = 36,

    /* Fused BASIC integer semantics: wrapped multiply, then integer divide */
    ROP_SQRDIV      = 37,
    ROP_MULDIV      = 38,

    /* Fused fixed-point ops */
    ROP_SQRSHR      = 40,  /* [op][dst][a][bits] = 4 bytes — (a*a)>>bits */
    ROP_MULSHR      = 41,  /* [op][dst][a][b][bits] = 5 bytes — (a*b)>>bits */
    ROP_MULSHRADD   = 42,  /* [op][dst][a][b][bits][c] = 6 bytes — (a*b)>>bits + c */

    /* Integer comparison → 0/1: [op][dst][src1][src2] = 4 bytes */
    ROP_EQ_I        = 45,
    ROP_NE_I        = 46,
    ROP_LT_I        = 47,
    ROP_GT_I        = 48,
    ROP_LE_I        = 49,
    ROP_GE_I        = 50,

    /* Conditional jump (integer): [op][src1][src2][off:16] = 5 bytes */
    ROP_JCMP_EQ_I   = 55,
    ROP_JCMP_NE_I   = 56,
    ROP_JCMP_LT_I   = 57,
    ROP_JCMP_GT_I   = 58,
    ROP_JCMP_LE_I   = 59,
    ROP_JCMP_GE_I   = 60,

    /* Conditional jump on zero/nonzero: [op][src][off:16] = 4 bytes */
    ROP_JZ          = 65,
    ROP_JNZ         = 66,

    /* 1D array access: [op][dst/val][arr_idx][idx_reg] = 4 bytes */
    ROP_LOAD_ARR_I  = 70,  /* dst = array[arr_idx][regs[idx_reg]] */
    ROP_STORE_ARR_I = 71,  /* array[arr_idx][regs[idx_reg]] = regs[val] */
    ROP_LOAD_ARR_F  = 72,
    ROP_STORE_ARR_F = 73,
} RegOp;

#define MAX_FAST_REGS 64

/* Print flags */
#define PRINT_NO_NEWLINE  0x01
#define PRINT_TAB_AFTER   0x02
#define PRINT_SEMICOLON   0x04

/* Native file syscall sub-operations for OP_FILE */
#define BC_FILE_OPEN          1  /* mode:8 fn:16, pop filename$ */
#define BC_FILE_CLOSE         2  /* fn:16 */
#define BC_FILE_PRINT_INT     3  /* fn:16, pop integer */
#define BC_FILE_PRINT_FLT     4  /* fn:16, pop float */
#define BC_FILE_PRINT_STR     5  /* fn:16, pop string */
#define BC_FILE_PRINT_NEWLINE 6  /* fn:16 */
#define BC_FILE_LINE_INPUT    7  /* is_local:8 slot:16 fn:16 */
#define BC_FILE_DRIVE         9  /* pop drive$ */
#define BC_FILE_SEEK         10  /* fn:16, pop position */
#define BC_FILE_MKDIR        11  /* pop path$ */
#define BC_FILE_CHDIR        12  /* pop path$ */
#define BC_FILE_RMDIR        13  /* pop path$ */
#define BC_FILE_KILL         14  /* pop path$ */
#define BC_FILE_RENAME       15  /* pop new$, pop old$ */
#define BC_FILE_COPY         16  /* mode:8, pop to$, pop from$ */

/* Native PWM sub-operations for OP_PWM */
#define BC_PWM_CONFIG         1  /* present:8, pop defer/phase/dutyB/dutyA/freq/slice */
#define BC_PWM_SYNC           2  /* present:16, pop optional counts[0..11] */
#define BC_PWM_OFF            3  /* pop slice */

/* Integer relation codes for OP_JCMP_I */
#define BC_JCMP_EQ 1
#define BC_JCMP_NE 2
#define BC_JCMP_LT 3
#define BC_JCMP_GT 4
#define BC_JCMP_LE 5
#define BC_JCMP_GE 6

/* Typed move kinds for OP_MOV_VAR */
#define BC_MOV_INT 1
#define BC_MOV_FLT 2
#define BC_MOV_STR 3

/*
 * Generic VM syscall/intrinsic ids for OP_SYSCALL.
 *
 * Arguments are pushed on the VM stack in source order using the standard VM
 * expression rules.  OP_SYSCALL carries:
 *   - syscall id
 *   - argc (stack arguments consumed by the handler)
 *   - aux length
 *   - aux payload bytes (optional metadata such as presence masks, slots, or
 *     argument-kind descriptors for array/vector forms)
 *
 * The generic syscall path is the default ABI. Dedicated opcodes remain
 * available only for hot primitives where profiling or code-size measurements
 * justify them.
 */
typedef enum {
    BC_SYS_FASTGFX_CREATE = 1,
    BC_SYS_FASTGFX_CLOSE,
    BC_SYS_FASTGFX_SWAP,
    BC_SYS_FASTGFX_SYNC,
    BC_SYS_FASTGFX_FPS,
    BC_SYS_GFX_BOX,
    BC_SYS_GFX_RGB,
    BC_SYS_GFX_CIRCLE,
    BC_SYS_GFX_LINE,
    BC_SYS_GFX_TEXT,
    BC_SYS_GFX_CLS,
    BC_SYS_GFX_PIXEL,
    BC_SYS_GFX_PIXEL_READ,
    BC_SYS_GFX_COLOUR,
    BC_SYS_GFX_FONT,
    BC_SYS_GFX_RBOX,
    BC_SYS_GFX_ARC,
    BC_SYS_GFX_TRIANGLE,
    BC_SYS_GFX_POLYGON,
    BC_SYS_GFX_FRAMEBUFFER,
    BC_SYS_MM_HRES,
    BC_SYS_MM_VRES,
    BC_SYS_MM_FONTWIDTH,
    BC_SYS_MM_FONTHEIGHT,
    BC_SYS_PRINT_AT,
    BC_SYS_PAUSE,
    BC_SYS_DATE_STR,
    BC_SYS_TIME_STR,
    BC_SYS_KEYDOWN,
    BC_SYS_SETPIN,
    BC_SYS_PIN_READ,
    BC_SYS_PIN_WRITE,
    BC_SYS_PWM_CONFIG,
    BC_SYS_PWM_SYNC,
    BC_SYS_PWM_OFF,
    BC_SYS_SERVO,
    BC_SYS_FILE_OPEN,
    BC_SYS_FILE_CLOSE,
    BC_SYS_FILE_PRINT_INT,
    BC_SYS_FILE_PRINT_FLT,
    BC_SYS_FILE_PRINT_STR,
    BC_SYS_FILE_PRINT_NEWLINE,
    BC_SYS_FILE_LINE_INPUT,
    BC_SYS_FILE_DRIVE,
    BC_SYS_FILE_SEEK,
    BC_SYS_FILE_MKDIR,
    BC_SYS_FILE_CHDIR,
    BC_SYS_FILE_RMDIR,
    BC_SYS_FILE_KILL,
    BC_SYS_FILE_RENAME,
    BC_SYS_FILE_COPY,
    BC_SYS_RUN,
} BCSyscallId;

typedef enum {
    BC_FB_OP_CREATE = 1,
    BC_FB_OP_LAYER,
    BC_FB_OP_WRITE,
    BC_FB_OP_CLOSE,
    BC_FB_OP_MERGE,
    BC_FB_OP_SYNC,
    BC_FB_OP_WAIT,
    BC_FB_OP_COPY,
} BCFramebufferOp;

#define BC_FB_CREATE_NORMAL   0
#define BC_FB_CREATE_FAST     1

#define BC_FB_TARGET_DEFAULT  0
#define BC_FB_TARGET_N        'N'
#define BC_FB_TARGET_F        'F'
#define BC_FB_TARGET_L        'L'

#define BC_FB_MERGE_MODE_NOW  0
#define BC_FB_MERGE_MODE_B    1
#define BC_FB_MERGE_MODE_R    2
#define BC_FB_MERGE_MODE_A    3

/* Native BOX argument kinds */
#define BC_BOX_ARG_COUNT         7
#define BC_TRIANGLE_ARG_COUNT    8
#define BC_POLYGON_ARG_COUNT     5
#define BC_BOX_ARG_EMPTY         0
#define BC_BOX_ARG_STACK         1
#define BC_BOX_ARG_GLOBAL_ARR_I  2
#define BC_BOX_ARG_GLOBAL_ARR_F  3
#define BC_BOX_ARG_LOCAL_ARR_I   4
#define BC_BOX_ARG_LOCAL_ARR_F   5

#define BC_LINE_ARG_COUNT        6
#define BC_TEXT_ARG_COUNT        8
#define BC_PIXEL_ARG_COUNT       3

/*
 * Compiler-table sizes are supplied by each port's port_config.h —
 * typically by including one of ports/bc_tables_{rp2040,rp2350,host}.h.
 * bytecode.h just consumes the values to size the BCCompiler arrays
 * that bc_compiler_alloc() heap-allocates.
 */
#if !defined(BC_MAX_CODE) || !defined(BC_MAX_CONSTANTS) ||  \
    !defined(BC_MAX_SLOTS) || !defined(BC_MAX_SUBFUNS) ||   \
    !defined(BC_MAX_FIXUPS) || !defined(BC_MAX_LINEMAP) ||  \
    !defined(BC_MAX_LOCALS) || !defined(BC_MAX_PARAMS) ||   \
    !defined(BC_MAX_LOCAL_META) || !defined(BC_MAX_NEST) || \
    !defined(BC_MAX_DATA_ITEMS)
#error "Port's port_config.h must define BC_MAX_* (see ports/bc_tables_*.h)"
#endif

/*
 * Variable slot — compile-time record
 */
typedef struct {
    char     name[MAXVARLEN + 1];
    uint8_t  type;              /* T_INT, T_NBR, T_STR, T_STRUCT */
    uint8_t  is_array;
    uint8_t  is_const;          /* 1 if Const — value inlined, no slot load */
    uint8_t  ndims;
    int      dims[MAXDIM];      /* array dimension sizes, 0 if unknown at compile time */
    int64_t  const_ival;        /* integer value if is_const && type==T_INT */
    double   const_fval;        /* float value if is_const && type==T_NBR */
    int16_t  struct_idx;        /* g_structtbl index if type == T_STRUCT, else -1 */
} BCSlot;

/*
 * SUB/FUNCTION record
 */
typedef struct {
    char     name[MAXVARLEN + 1];
    uint32_t entry_addr;        /* bytecode offset of ENTER_FRAME */
    uint8_t  nparams;
    uint8_t  param_types[BC_MAX_PARAMS]; /* T_INT, T_NBR, T_STR for each param */
    uint8_t  param_is_array[BC_MAX_PARAMS]; /* 1 if param is array (passed by ref) */
    uint8_t  return_type;       /* 0 for SUB, T_INT/T_NBR/T_STR for FUNCTION */
    uint8_t  bridged;           /* 1 = owned by interpreter, call via OP_BRIDGE_CMD */
    uint16_t nlocals;           /* total local slots (params + LOCAL vars) */
} BCSubFun;

/*
 * String constant pool entry
 */
typedef struct {
    uint8_t  data[STRINGSIZE];
    uint16_t len;
} BCConstant;

/*
 * Forward reference fixup
 */
typedef struct {
    uint32_t patch_addr;        /* offset in code[] to patch */
    int      target_line;       /* line number to resolve to (for GOTO/GOSUB) */
    int      target_label;      /* -1 if using line number */
    uint8_t  size;              /* 2 or 4 byte patch */
    uint8_t  is_relative;       /* 1 = relative offset, 0 = absolute addr */
    uint8_t  is_data_index;     /* 1 = patch with labelmap[].data_index (size must be 2) */
} BCFixup;

/*
 * Line number → bytecode offset mapping
 */
typedef struct {
    uint16_t lineno;
    uint32_t offset;
} BCLineMap;

/*
 * Label name → bytecode offset mapping.  GOTO/GOSUB <name> resolve to
 * an offset via this table at compile-end (bc_resolve_fixups).
 */
#ifndef BC_MAX_LABEL_NAME
#define BC_MAX_LABEL_NAME 31
#endif
#ifndef BC_MAX_LABELS
/* GOTO/GOSUB target labels. Real programs have a handful, not hundreds.
 * Sized for typical use (32 × ~36 B = ~1 KB). Override per-build if a
 * pathologically label-heavy program shows up. */
#define BC_MAX_LABELS 32
#endif
typedef struct {
    char     name[BC_MAX_LABEL_NAME + 1];
    uint32_t offset;
    uint16_t data_index;   /* cs->data_count at the label site; 0xFFFF until resolved */
} BCLabelMap;

/*
 * Control flow nesting stack (used during compilation)
 */
typedef enum {
    NEST_IF,
    NEST_FOR,
    NEST_DO,
    NEST_WHILE,
    NEST_SELECT,
    NEST_SUB,
    NEST_FUNCTION,
} BCNestType;

typedef struct {
    BCNestType type;
    uint32_t   addr1;           /* for IF: addr of JZ patch; FOR: loop top; DO: loop top */
    uint32_t   addr2;           /* for IF: addr of JMP-to-endif patch */
    uint32_t   addr3;           /* extra (e.g., SELECT temp slot) */
    uint16_t   var_slot;        /* FOR: loop variable slot */
    uint16_t   lim_slot;        /* FOR: limit hidden slot */
    uint16_t   step_slot;       /* FOR: step hidden slot */
    uint8_t    var_type;        /* FOR: T_INT or T_NBR */
    uint8_t    has_else;        /* IF: whether ELSE was seen */

    /* For SELECT CASE */
    uint16_t   select_slot;     /* hidden variable slot for select expr */
    uint8_t    select_type;     /* type of select expression */
    uint32_t   case_end_fixups[32]; /* fixup addrs for JMP to END SELECT */
    int        case_end_count;

    /* For EXIT FOR/DO — patch locations to fill in when we reach NEXT/LOOP */
    uint32_t   exit_fixups[64];
    int        exit_fixup_count;
} BCNestEntry;

/*
 * DATA pool item — one per value in DATA statements
 * Uses raw union instead of BCValue to avoid forward-declaration issues.
 */
typedef struct {
    union {
        MMFLOAT f;
        int64_t i;
    } value;
    uint8_t  type;    /* T_INT, T_NBR, or T_STR (for T_STR, .i = const pool index) */
} BCDataItem;

/*
 * Persisted local variable metadata for native VM call frames.
 */
typedef struct {
    char    name[MAXVARLEN + 1];
    uint8_t type;
    uint8_t is_array;
} BCLocalMeta;

/*
 * Local variable record (used during compilation)
 */
typedef struct {
    char    name[MAXVARLEN + 1];
    uint8_t type;
    uint8_t is_array;
} BCLocalVar;

/*
 * Compiler state
 *
 * All large arrays are dynamically allocated via bc_compiler_alloc().
 * On host: calloc/free.  On device: the VM-owned allocator in bc_alloc.c.
 */
typedef struct {
    /* Output bytecode (allocated: BC_MAX_CODE bytes) */
    uint8_t    *code;
    uint32_t   code_len;

    /* Constant pool (allocated: BC_MAX_CONSTANTS entries) */
    BCConstant *constants;
    uint16_t   const_count;

    /* Global variable slots (allocated: BC_MAX_SLOTS entries) */
    BCSlot     *slots;
    uint16_t   slot_count;
    uint16_t   next_hidden_slot;   /* for compiler-generated temporaries */

    /* SUB/FUNCTION table (allocated: BC_MAX_SUBFUNS entries) */
    BCSubFun   *subfuns;
    uint16_t   subfun_count;
    uint16_t   *subfun_locals_base;

    /* Forward reference fixups (allocated: BC_MAX_FIXUPS entries) */
    BCFixup    *fixups;
    uint16_t   fixup_count;

    /* Line map (allocated: BC_MAX_LINEMAP entries) */
    BCLineMap  *linemap;
    uint16_t   linemap_count;

    /* Label map (allocated: BC_MAX_LABELS entries) */
    BCLabelMap *labelmap;
    uint16_t   labelmap_count;

    /* Control flow nesting stack (allocated: BC_MAX_NEST entries) */
    BCNestEntry *nest_stack;
    int         nest_depth;

    /* Current context */
    int        current_subfun;     /* -1 if not in SUB/FUNCTION */
    uint16_t   current_line;

    /* Local variable tracking (allocated: BC_MAX_LOCALS entries) */
    BCLocalVar *locals;
    uint16_t   local_count;

    /* Persisted local metadata for all compiled SUB/FUNCTIONs */
    BCLocalMeta *local_meta;
    uint16_t    local_meta_count;

    /* DATA pool (allocated: BC_MAX_DATA_ITEMS entries) */
    BCDataItem *data_pool;
    uint16_t   data_count;

    /* Error state */
    int        error_line;
    char       error_msg[128];
    int        has_error;
} BCCompiler;


/*
 * VM runtime value
 */
typedef union {
    MMFLOAT     f;
    int64_t     i;
    uint8_t    *s;       /* MMBasic format string (length prefix) */
} BCValue;

/* Internal-only VM stack tags for array-by-reference parameter passing. */
#define BC_STK_GARR_I  0x80
#define BC_STK_GARR_F  0x81
#define BC_STK_GARR_S  0x82
#define BC_STK_LARR_I  0x83
#define BC_STK_LARR_F  0x84
#define BC_STK_LARR_S  0x85

/*
 * VM call stack frame
 */
typedef struct {
    uint8_t    *return_pc;
    int         frame_base;     /* index into locals[] */
    int         locals_top;     /* caller's live local extent */
    int         for_sp;         /* caller's FOR stack depth */
    int         saved_sp;
    uint16_t    nlocals;        /* number of locals in this frame */
    uint16_t    subfun_idx;     /* active SUB/FUNCTION metadata */
} BCCallFrame;

/*
 * VM FOR stack entry
 */
typedef struct {
    uint16_t    var_slot;
    uint16_t    lim_slot;
    uint16_t    step_slot;
    uint8_t    *loop_top;      /* PC of loop body start */
    uint8_t     is_local;      /* variable is local (not global) */
    uint8_t     var_type;      /* T_INT or T_NBR */
} BCForEntry;

/*
 * VM array storage
 */
typedef struct {
    BCValue    *data;           /* allocated array of BCValues */
    int         dims[MAXDIM];   /* dimension sizes */
    uint8_t     ndims;
    uint8_t     elem_type;      /* T_INT, T_NBR, T_STR */
    uint8_t     data_external;  /* 1 = data is aliased into g_vartbl
                                 *     (bridged REDIM rebound the buffer);
                                 *     bc_array_release must NOT bc_free it.
                                 *     On device MMHeap is shared so the
                                 *     distinction is cosmetic, but on host
                                 *     bc_alloc/bc_free use libc malloc
                                 *     while g_vartbl uses the MMHeap
                                 *     simulator — mixing them crashes. */
    uint32_t    total_elements;
} BCArray;

/*
 * VM state
 */
#define VM_STACK_SIZE    256
#define VM_MAX_CALL      64
#define VM_MAX_FOR       32
#define VM_MAX_GOSUB     64

/* VM_MAX_LOCALS must track the device heap budget, not the host build.
 * On WASM with an RP2040-sized dropdown (128 KB heap) the simulator
 * needs device-sized locals or bc_vm_alloc burns ~57 KB just for
 * vm->locals/local_types/local_arrays — eating the heap budget and
 * letting programs that OOM on real RP2040 succeed in the simulator.
 * Gate on BC_SIM_RP2040 / BC_SIM_RP2350 (the Makefile.wasm and
 * host/Makefile set these to match the device being simulated). */
#if defined(BC_SIM_RP2040)
  #define VM_MAX_LOCALS   256    /* supports ~4 recursion levels * 64 locals */
#elif defined(BC_SIM_RP2350)
  #define VM_MAX_LOCALS   512
#elif defined(MMBASIC_HOST)
  #define VM_MAX_LOCALS   1024   /* native host with no device sim — generous */
#else
  #define VM_MAX_LOCALS   256    /* default device build (RP2040) */
#endif

/*
 * VM state
 *
 * Large arrays (globals, arrays, locals, local_arrays) are dynamically
 * allocated via bc_vm_alloc().  Small fixed-size arrays stay inline.
 */
typedef struct {
    uint8_t    *pc;             /* program counter into bytecode */

    /* Operand stack for expression evaluation (inline — small, fixed) */
    BCValue     stack[VM_STACK_SIZE];
    uint8_t     stack_types[VM_STACK_SIZE];
    int         sp;             /* stack pointer (-1 = empty) */

    /* Global variable storage (allocated: BC_MAX_SLOTS entries) */
    BCValue    *globals;
    uint8_t    *global_types;   /* tracks what's stored */

    /* Array storage for globals (allocated: BC_MAX_SLOTS entries) */
    BCArray    *arrays;         /* parallel to globals[], used if is_array */

    /* Call stack (inline — small, fixed) */
    BCCallFrame call_stack[VM_MAX_CALL];
    int         csp;            /* call stack pointer */

    /* GOSUB stack (inline — small, fixed) */
    struct {
        uint8_t *return_pc;
    } gosub_stack[VM_MAX_GOSUB];
    int         gsp;            /* gosub stack pointer */

    /* Local variable frames (allocated: VM_MAX_LOCALS entries) */
    BCValue    *locals;
    uint8_t    *local_types;
    int         frame_base;     /* current frame base in locals[] */
    int         locals_top;     /* next free slot in locals[] */

    /* Local array storage (allocated: VM_MAX_LOCALS entries) */
    BCArray    *local_arrays;   /* parallel to locals[] */
    uint8_t     local_array_is_alias[VM_MAX_LOCALS];

    /* FOR loop stack (inline — small, fixed) */
    BCForEntry  for_stack[VM_MAX_FOR];
    int         fsp;

    /* Bytecode and metadata (pointers to compiler output) */
    uint8_t    *bytecode;
    uint32_t    bytecode_len;
    BCCompiler *compiler;       /* for constant pool, linemap, etc. */

    /* DATA read pointer */
    uint16_t    data_ptr;       /* current position in compiler->data_pool[] */

    /* Error reporting */
    uint16_t    current_line;

    /* String temp storage (inline — small, fixed) */
    uint8_t     str_temp[4][STRINGSIZE];
    int         str_temp_idx;

    /* Output capture (for FTEST comparison) */
    char       *capture_buf;    /* if non-NULL, PRINT writes here instead of console */
    int         capture_len;
    int         capture_cap;
} BCVMState;


/*
 * Public API
 */

/* Compiler */
int  bc_compiler_alloc(BCCompiler *cs);   /* allocate all dynamic arrays */
void bc_compiler_free(BCCompiler *cs);    /* free all dynamic arrays */
int  bc_compiler_compact(BCCompiler *cs); /* shrink to actual size after compile */
void bc_compiler_init(BCCompiler *cs);    /* reset state (arrays must be allocated) */
int  bc_compile_source(BCCompiler *cs, const char *source, const char *source_name);

/* VM */
int  bc_vm_alloc(BCVMState *vm);    /* allocate dynamic arrays */
void bc_vm_free(BCVMState *vm);     /* free dynamic arrays */
void bc_vm_init(BCVMState *vm, BCCompiler *cs);
void bc_vm_execute(BCVMState *vm);
void bc_vm_error(BCVMState *vm, const char *msg, ...);
void bc_vm_release_arrays(BCVMState *vm);

/* Commands */
void bc_run_source_string(const char *source, const char *source_name);
int  bc_try_compile_line(const char *line);
void bc_run_immediate(const char *line);
void bc_run_file(const char *filename);

/* Bridge */
void bc_bridge_call_cmd(BCVMState *vm, const uint8_t *tok, uint16_t len);
void bc_bridge_call_fun(BCVMState *vm, uint16_t fun_idx, const uint8_t *args, uint16_t arg_len, uint8_t ret_type);
void bc_bridge_reset_sync(void);

/* Tokenise `source` into a RAM side-buffer and populate subfun[] (and
 * funtbl[] on rp2350) so bridged FindSubFun lookups can resolve
 * user-defined SUB/FUNCTION names. Call before VM execution; pair with
 * bc_bridge_release_subfun_buffer() after execution completes.
 * Returns 0 on success, non-zero on OOM. */
int  bc_bridge_prepare_subfun(const char *source);
void bc_bridge_release_subfun_buffer(void);
/* Returns the bridge prog buffer (tokenised program). NULL when no
 * FRUN session is active. Used by bc_run_source_string_ex to point
 * ProgMemory at the tokenised source so bridged interp commands that
 * scan the program (findlabel, findline, RESTORE label, …) see it
 * even though FRUN never wrote it through PrepareProgram. */
unsigned char *bc_bridge_get_prog_buf(void);

/* Helpers */
uint16_t bc_find_slot(BCCompiler *cs, const char *name, int name_len);
uint16_t bc_add_slot(BCCompiler *cs, const char *name, int name_len, uint8_t type, int is_array);
int      bc_find_subfun(BCCompiler *cs, const char *name, int name_len);
uint16_t bc_add_constant_string(BCCompiler *cs, const uint8_t *data, int len);
int      bc_add_linemap_entry(BCCompiler *cs, uint16_t lineno, uint32_t offset);
int      bc_commit_locals(BCCompiler *cs, int sf_idx);
uint32_t bc_linemap_lookup(BCCompiler *cs, uint16_t lineno);

/* Output capture API (for FTEST comparison) */
void bc_vm_start_capture(BCVMState *vm, char *buf, int capacity);
void bc_vm_capture_write(BCVMState *vm, const char *text, int len);
void bc_vm_capture_char(BCVMState *vm, char c);
void bc_vm_capture_string(BCVMState *vm, const char *s);

/* Bytecode emission helpers */
void bc_emit_byte(BCCompiler *cs, uint8_t b);
void bc_emit_u16(BCCompiler *cs, uint16_t v);
void bc_emit_i16(BCCompiler *cs, int16_t v);
void bc_emit_u32(BCCompiler *cs, uint32_t v);
void bc_emit_ptr(BCCompiler *cs, const void *ptr);
void bc_emit_i64(BCCompiler *cs, int64_t v);
void bc_emit_f64(BCCompiler *cs, MMFLOAT v);
void bc_patch_u16(BCCompiler *cs, uint32_t addr, uint16_t v);
void bc_patch_i16(BCCompiler *cs, uint32_t addr, int16_t v);
void bc_patch_u32(BCCompiler *cs, uint32_t addr, uint32_t v);

/* Debug / diagnostic tools */
extern int bc_debug_enabled;       /* set to 1 to dump stats+disassembly on VM run */
extern int bc_opt_level;           /* host/device frontend optimization level (default 1) */
void bc_disassemble(BCCompiler *cs);

/* Native FASTGFX helpers implemented by the platform runtime. */
void bc_fastgfx_swap(void);
void bc_fastgfx_sync(void);
void bc_fastgfx_create(void);
void bc_fastgfx_close(void);
void bc_fastgfx_reset(void);
void bc_fastgfx_set_fps(int fps);
void bc_dump_stats(BCCompiler *cs);
void bc_dump_vm_state(BCVMState *vm);

/* ------------------------------------------------------------------ */
/* Crash diagnostic breadcrumb — survives soft reset via                */
/* __uninitialized_ram on device                                       */
/* ------------------------------------------------------------------ */
#define BC_CRASH_MAGIC 0xDEADC0DE

#define BC_CRASH_TRAIL_LEN 12

typedef struct {
    uint32_t magic;         /* BC_CRASH_MAGIC if valid crash data present */
    uint32_t checkpoint;    /* last checkpoint ID reached */
    uint32_t sp;            /* ARM stack pointer at last checkpoint */
    uint32_t cfsr;          /* ARM CFSR (fault status) */
    uint32_t hfsr;          /* ARM HFSR (hard fault status) */
    uint32_t bfar;          /* ARM BFAR (bus fault address) */
    uint32_t mmfar;         /* ARM MMFAR (mem-manage fault address) */
    uint32_t sp_lowest;     /* lowest SP ever seen (deepest stack use) */
    uint32_t heap_used;     /* snapshot of bc_alloc heap bytes used */
    uint32_t heap_capacity; /* snapshot of bc_alloc heap capacity */
    /* Compiler state snapshot (captured on each checkpoint) */
    uint32_t cs_code;           /* address of cs->code buffer */
    uint32_t cs_code_len;
    uint32_t cs_code_capacity;
    uint32_t cs_linemap;        /* address of cs->linemap */
    uint32_t cs_linemap_count;
    uint32_t cs_current_line;
    uint32_t cs_has_error;
    /* Ring buffer of recent checkpoint IDs (oldest→newest) */
    uint8_t  trail[BC_CRASH_TRAIL_LEN];
    uint8_t  trail_head;    /* index where next write goes */
    uint8_t  trail_count;   /* how many valid entries (capped at TRAIL_LEN) */
    uint8_t  _pad;
    char     label[32];     /* latest checkpoint description string */
    char     prev_label[32];/* previous checkpoint label (extra context) */
} BCCrashInfo;

/* Checkpoint stage IDs for VM program execution */
#define BC_CK_VM_ENTRY            1
#define BC_CK_VM_ALLOC_CS         2
#define BC_CK_VM_ALLOC_VM         3
#define BC_CK_VM_COMP_ALLOC       4
#define BC_CK_VM_ALLOC            5
#define BC_CK_VM_COMPILE          6
#define BC_CK_VM_INIT             7
#define BC_CK_VM_EXECUTE          8
#define BC_CK_VM_CLEANUP          9
/* Compile subphases (inside bc_compile_source) */
#define BC_CK_COMPILE_PREDECLARE  10
#define BC_CK_COMPILE_LINE        11
#define BC_CK_COMPILE_EMIT_END    12
#define BC_CK_COMPILE_FIXUPS      13
#define BC_CK_COMPILE_DONE        14
/* Inside source_compile_line */
#define BC_CK_LINE_ENTER          20
#define BC_CK_LINE_LINEMAP        21
#define BC_CK_LINE_EMIT_OP_LINE   22
#define BC_CK_LINE_STMT_LOOP      23
#define BC_CK_LINE_STMT_CALL      24
#define BC_CK_LINE_STMT_DONE      25

void bc_crash_checkpoint(int stage, const char *label);
void bc_crash_snapshot_cs(BCCompiler *cs);  /* snapshot compiler state */
void bc_crash_save_fault(void);    /* called from sigbus() to capture ARM regs */
void bc_crash_dump_if_any(void);   /* called at boot to print crash report */
void bc_crash_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* __BYTECODE_H */
