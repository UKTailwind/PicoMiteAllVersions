/*
 * bc_debug.c -- Bytecode disassembler and diagnostic tools
 *
 * Provides human-readable dump of compiled bytecode and compiler statistics.
 * Used for debugging when VM execution produces incorrect output or crashes.
 *
 * Works on both host (printf to stdout) and device (MMPrintString to console).
 * On device, costs ~8 KB flash via XIP (zero RAM when not running).
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "bytecode.h"
#include "bc_alloc.h"
#include "vm_device_support.h"

/* Global debug flag - when set, VM execution dumps stats + disassembly */
int bc_debug_enabled = 0;

/* Output helper: works on both host and device */
static char dbg_buf[256];

static void dbg_print(const char * fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dbg_buf, sizeof(dbg_buf), fmt, ap);
    va_end(ap);
    MMPrintString(dbg_buf);
}

/* ======================================================================
 * Opcode name table
 * ====================================================================== */
static const char * opcode_name(uint8_t op) {
    switch (op) {
    case OP_NOP:
        return "NOP";
    case OP_PUSH_INT:
        return "PUSH_INT";
    case OP_PUSH_FLT:
        return "PUSH_FLT";
    case OP_PUSH_STR:
        return "PUSH_STR";
    case OP_PUSH_ZERO:
        return "PUSH_ZERO";
    case OP_PUSH_ONE:
        return "PUSH_ONE";
    case OP_LOAD_I:
        return "LOAD_I";
    case OP_LOAD_F:
        return "LOAD_F";
    case OP_LOAD_S:
        return "LOAD_S";
    case OP_STORE_I:
        return "STORE_I";
    case OP_STORE_F:
        return "STORE_F";
    case OP_STORE_S:
        return "STORE_S";
    case OP_LOAD_ARR_I:
        return "LOAD_ARR_I";
    case OP_LOAD_ARR_F:
        return "LOAD_ARR_F";
    case OP_LOAD_ARR_S:
        return "LOAD_ARR_S";
    case OP_STORE_ARR_I:
        return "STORE_ARR_I";
    case OP_STORE_ARR_F:
        return "STORE_ARR_F";
    case OP_STORE_ARR_S:
        return "STORE_ARR_S";
    case OP_POP:
        return "POP";
    case OP_DUP:
        return "DUP";
    case OP_CVT_I2F:
        return "CVT_I2F";
    case OP_CVT_F2I:
        return "CVT_F2I";

    case OP_ADD_I:
        return "ADD_I";
    case OP_SUB_I:
        return "SUB_I";
    case OP_MUL_I:
        return "MUL_I";
    case OP_IDIV_I:
        return "IDIV_I";
    case OP_MOD_I:
        return "MOD_I";

    case OP_ADD_F:
        return "ADD_F";
    case OP_SUB_F:
        return "SUB_F";
    case OP_MUL_F:
        return "MUL_F";
    case OP_DIV_F:
        return "DIV_F";
    case OP_POW_F:
        return "POW_F";
    case OP_MOD_F:
        return "MOD_F";

    case OP_ADD_S:
        return "ADD_S";

    case OP_NEG_I:
        return "NEG_I";
    case OP_NEG_F:
        return "NEG_F";
    case OP_NOT:
        return "NOT";
    case OP_INV:
        return "INV";

    case OP_AND:
        return "AND";
    case OP_OR:
        return "OR";
    case OP_XOR:
        return "XOR";
    case OP_SHL:
        return "SHL";
    case OP_SHR:
        return "SHR";

    case OP_EQ_I:
        return "EQ_I";
    case OP_NE_I:
        return "NE_I";
    case OP_LT_I:
        return "LT_I";
    case OP_GT_I:
        return "GT_I";
    case OP_LE_I:
        return "LE_I";
    case OP_GE_I:
        return "GE_I";

    case OP_EQ_F:
        return "EQ_F";
    case OP_NE_F:
        return "NE_F";
    case OP_LT_F:
        return "LT_F";
    case OP_GT_F:
        return "GT_F";
    case OP_LE_F:
        return "LE_F";
    case OP_GE_F:
        return "GE_F";

    case OP_EQ_S:
        return "EQ_S";
    case OP_NE_S:
        return "NE_S";
    case OP_LT_S:
        return "LT_S";
    case OP_GT_S:
        return "GT_S";
    case OP_LE_S:
        return "LE_S";
    case OP_GE_S:
        return "GE_S";

    case OP_JMP:
        return "JMP";
    case OP_JMP_ABS:
        return "JMP_ABS";
    case OP_JZ:
        return "JZ";
    case OP_JNZ:
        return "JNZ";
    case OP_GOSUB:
        return "GOSUB";
    case OP_RETURN:
        return "RETURN";

    case OP_FOR_INIT_I:
        return "FOR_INIT_I";
    case OP_FOR_NEXT_I:
        return "FOR_NEXT_I";
    case OP_FOR_INIT_F:
        return "FOR_INIT_F";
    case OP_FOR_NEXT_F:
        return "FOR_NEXT_F";

    case OP_CALL_SUB:
        return "CALL_SUB";
    case OP_CALL_FUN:
        return "CALL_FUN";
    case OP_RET_SUB:
        return "RET_SUB";
    case OP_RET_FUN:
        return "RET_FUN";
    case OP_ENTER_FRAME:
        return "ENTER_FRAME";
    case OP_LEAVE_FRAME:
        return "LEAVE_FRAME";
    case OP_LOAD_LOCAL_I:
        return "LOAD_LOCAL_I";
    case OP_LOAD_LOCAL_F:
        return "LOAD_LOCAL_F";
    case OP_LOAD_LOCAL_S:
        return "LOAD_LOCAL_S";
    case OP_STORE_LOCAL_I:
        return "STORE_LOCAL_I";
    case OP_STORE_LOCAL_F:
        return "STORE_LOCAL_F";
    case OP_STORE_LOCAL_S:
        return "STORE_LOCAL_S";
    case OP_LOAD_LOCAL_ARR_I:
        return "LD_LARR_I";
    case OP_LOAD_LOCAL_ARR_F:
        return "LD_LARR_F";
    case OP_LOAD_LOCAL_ARR_S:
        return "LD_LARR_S";
    case OP_STORE_LOCAL_ARR_I:
        return "ST_LARR_I";
    case OP_STORE_LOCAL_ARR_F:
        return "ST_LARR_F";
    case OP_STORE_LOCAL_ARR_S:
        return "ST_LARR_S";

    case OP_PRINT_INT:
        return "PRINT_INT";
    case OP_PRINT_FLT:
        return "PRINT_FLT";
    case OP_PRINT_STR:
        return "PRINT_STR";
    case OP_PRINT_NEWLINE:
        return "PRINT_NEWLINE";
    case OP_PRINT_TAB:
        return "PRINT_TAB";

    case OP_DIM_ARR_I:
        return "DIM_ARR_I";
    case OP_DIM_ARR_F:
        return "DIM_ARR_F";
    case OP_DIM_ARR_S:
        return "DIM_ARR_S";

    case OP_STR_LEN:
        return "STR_LEN";
    case OP_STR_LEFT:
        return "STR_LEFT";
    case OP_STR_RIGHT:
        return "STR_RIGHT";
    case OP_STR_MID2:
        return "STR_MID2";
    case OP_STR_MID3:
        return "STR_MID3";
    case OP_STR_UCASE:
        return "STR_UCASE";
    case OP_STR_LCASE:
        return "STR_LCASE";
    case OP_STR_VAL:
        return "STR_VAL";
    case OP_STR_STR:
        return "STR_STR";
    case OP_STR_CHR:
        return "STR_CHR";
    case OP_STR_ASC:
        return "STR_ASC";
    case OP_STR_INSTR:
        return "STR_INSTR";
    case OP_STR_HEX:
        return "STR_HEX";
    case OP_STR_OCT:
        return "STR_OCT";
    case OP_STR_BIN:
        return "STR_BIN";
    case OP_STR_SPACE:
        return "STR_SPACE";
    case OP_STR_STRING:
        return "STR_STRING";
    case OP_STR_INKEY:
        return "STR_INKEY";

    case OP_MATH_SIN:
        return "MATH_SIN";
    case OP_MATH_COS:
        return "MATH_COS";
    case OP_MATH_TAN:
        return "MATH_TAN";
    case OP_MATH_ATN:
        return "MATH_ATN";
    case OP_MATH_SQR:
        return "MATH_SQR";
    case OP_MATH_LOG:
        return "MATH_LOG";
    case OP_MATH_EXP:
        return "MATH_EXP";
    case OP_MATH_ABS:
        return "MATH_ABS";
    case OP_MATH_SGN:
        return "MATH_SGN";
    case OP_MATH_INT:
        return "MATH_INT";
    case OP_MATH_FIX:
        return "MATH_FIX";
    case OP_MATH_CINT:
        return "MATH_CINT";
    case OP_MATH_RAD:
        return "MATH_RAD";
    case OP_MATH_DEG:
        return "MATH_DEG";
    case OP_MATH_PI:
        return "MATH_PI";
    case OP_MATH_MAX:
        return "MATH_MAX";
    case OP_MATH_MIN:
        return "MATH_MIN";

    case OP_READ_I:
        return "READ_I";
    case OP_READ_F:
        return "READ_F";
    case OP_READ_S:
        return "READ_S";
    case OP_RESTORE:
        return "RESTORE";
    case OP_RESTORE_DATA:
        return "RESTORE_DATA";
    case OP_RND:
        return "RND";

    case OP_INC_I:
        return "INC_I";
    case OP_INC_F:
        return "INC_F";
    case OP_RANDOMIZE:
        return "RANDOMIZE";
    case OP_ERROR_S:
        return "ERROR_S";
    case OP_ERROR_EMPTY:
        return "ERROR_EMPTY";
    case OP_CLEAR:
        return "CLEAR";
    case OP_FASTGFX_SWAP:
        return "FASTGFX_SWAP";
    case OP_FASTGFX_SYNC:
        return "FASTGFX_SYNC";
    case OP_FASTGFX_CREATE:
        return "FASTGFX_CREATE";
    case OP_FASTGFX_CLOSE:
        return "FASTGFX_CLOSE";
    case OP_FASTGFX_FPS:
        return "FASTGFX_FPS";
    case OP_BOX:
        return "BOX";
    case OP_RGB:
        return "RGB";
    case OP_CIRCLE:
        return "CIRCLE";
    case OP_DRAW_LINE:
        return "DRAW_LINE";
    case OP_TEXT:
        return "TEXT";
    case OP_CLS:
        return "CLS";
    case OP_PIXEL:
        return "PIXEL";
    case OP_COLOUR:
        return "COLOUR";
    case OP_PAUSE:
        return "PAUSE";
    case OP_STR_DATE:
        return "STR_DATE";
    case OP_STR_TIME:
        return "STR_TIME";
    case OP_KEYDOWN:
        return "KEYDOWN";
    case OP_MATH_MULDIV:
        return "MATH_MULDIV";
    case OP_MATH_SQRDIV:
        return "MATH_SQRDIV";
    case OP_SETPIN:
        return "SETPIN";
    case OP_PIN_READ:
        return "PIN_READ";
    case OP_PIN_WRITE:
        return "PIN_WRITE";
    case OP_PWM:
        return "PWM";
    case OP_SERVO:
        return "SERVO";
    case OP_MATH_MULSHR:
        return "MATH_MULSHR";
    case OP_MATH_SQRSHR:
        return "MATH_SQRSHR";
    case OP_MATH_MULSHRADD:
        return "MATH_MULSHRADD";
    case OP_JCMP_I:
        return "JCMP_I";
    case OP_JCMP_F:
        return "JCMP_F";
    case OP_MOV_VAR:
        return "MOV_VAR";
    case OP_SYSCALL:
        return "SYSCALL";

    case OP_LINE:
        return "LINE";
    case OP_CHECKINT:
        return "CHECKINT";
    case OP_END:
        return "END";
    case OP_HALT:
        return "HALT";

    default:
        return NULL;
    }
}

/* ======================================================================
 * Bytecode reading helpers
 * ====================================================================== */
static uint16_t rd16(const uint8_t * p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
static int16_t rdi16(const uint8_t * p) {
    int16_t v;
    memcpy(&v, p, 2);
    return v;
}
static uint32_t rd32(const uint8_t * p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static int64_t rdi64(const uint8_t * p) {
    int64_t v;
    memcpy(&v, p, 8);
    return v;
}

static const char * jcmp_name(uint8_t rel) {
    switch (rel) {
    case BC_JCMP_EQ:
        return "EQ";
    case BC_JCMP_NE:
        return "NE";
    case BC_JCMP_LT:
        return "LT";
    case BC_JCMP_GT:
        return "GT";
    case BC_JCMP_LE:
        return "LE";
    case BC_JCMP_GE:
        return "GE";
    default:
        return "?";
    }
}
static double rdf64(const uint8_t * p) {
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* Helper: slot name lookup */
static const char * slot_name(BCCompiler * cs, uint16_t idx) {
    if (idx < cs->slot_count && cs->slots[idx].name[0])
        return cs->slots[idx].name;
    return "?";
}

/* Helper: subfun name lookup */
static const char * subfun_name(BCCompiler * cs, uint16_t idx) {
    if (idx < cs->subfun_count && cs->subfuns[idx].name[0])
        return cs->subfuns[idx].name;
    return "?";
}

/* ======================================================================
 * bc_disassemble -- Print human-readable bytecode listing
 * ====================================================================== */
void bc_disassemble(BCCompiler * cs) {
    const uint8_t * code = cs->code;
    uint32_t len = cs->code_len;
    uint32_t pc = 0;

    dbg_print("=== Disassembly (%u bytes) ===\r\n", len);

    while (pc < len) {
        uint32_t start = pc;
        uint8_t op = code[pc++];
        const char * name = opcode_name(op);

        if (!name) {
            dbg_print("  %04X: ??? (0x%02X)\r\n", start, op);
            continue;
        }

        /* Decode operands based on opcode */
        switch (op) {
        case OP_PUSH_INT: {
            int64_t v = rdi64(code + pc);
            pc += 8;
            dbg_print("  %04X: %-16s %lld\r\n", start, name, (long long)v);
            break;
        }
        case OP_PUSH_FLT: {
            double v = rdf64(code + pc);
            pc += 8;
            dbg_print("  %04X: %-16s %.10g\r\n", start, name, v);
            break;
        }
        case OP_PUSH_STR: {
            uint16_t idx = rd16(code + pc);
            pc += 2;
            if (idx < cs->const_count) {
                dbg_print("  %04X: %-16s [%d] \"%.30s\"\r\n", start, name, idx,
                          (const char *)cs->constants[idx].data);
            } else {
                dbg_print("  %04X: %-16s [%d] ???\r\n", start, name, idx);
            }
            break;
        }

        /* slot:16 opcodes */
        case OP_LOAD_I:
        case OP_LOAD_F:
        case OP_LOAD_S:
        case OP_STORE_I:
        case OP_STORE_F:
        case OP_STORE_S: {
            uint16_t s = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s %d (%s)\r\n", start, name, s, slot_name(cs, s));
            break;
        }

        case OP_INC_I:
        case OP_INC_F: {
            uint16_t raw = rd16(code + pc);
            pc += 2;
            uint16_t s = raw & 0x7FFFu;
            dbg_print("  %04X: %-16s %s%d (%s)\r\n", start, name,
                      (raw & 0x8000u) ? "local:" : "global:",
                      s, slot_name(cs, s));
            break;
        }

        /* slot:16, ndim:8 opcodes */
        case OP_LOAD_ARR_I:
        case OP_LOAD_ARR_F:
        case OP_LOAD_ARR_S:
        case OP_STORE_ARR_I:
        case OP_STORE_ARR_F:
        case OP_STORE_ARR_S:
        case OP_DIM_ARR_I:
        case OP_DIM_ARR_F:
        case OP_DIM_ARR_S: {
            uint16_t s = rd16(code + pc);
            pc += 2;
            uint8_t nd = code[pc++];
            dbg_print("  %04X: %-16s %d (%s) ndim=%d\r\n", start, name, s, slot_name(cs, s), nd);
            break;
        }

        /* local offset:16 opcodes */
        case OP_LOAD_LOCAL_I:
        case OP_LOAD_LOCAL_F:
        case OP_LOAD_LOCAL_S:
        case OP_STORE_LOCAL_I:
        case OP_STORE_LOCAL_F:
        case OP_STORE_LOCAL_S: {
            uint16_t off = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s off=%d\r\n", start, name, off);
            break;
        }

        /* local offset:16, ndim:8 opcodes */
        case OP_LOAD_LOCAL_ARR_I:
        case OP_LOAD_LOCAL_ARR_F:
        case OP_LOAD_LOCAL_ARR_S:
        case OP_STORE_LOCAL_ARR_I:
        case OP_STORE_LOCAL_ARR_F:
        case OP_STORE_LOCAL_ARR_S: {
            uint16_t off = rd16(code + pc);
            pc += 2;
            uint8_t nd = code[pc++];
            dbg_print("  %04X: %-16s off=%d ndim=%d\r\n", start, name, off, nd);
            break;
        }

        /* Relative jump: offset:16 */
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ: {
            int16_t off = rdi16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s %+d -> %04X\r\n", start, name, off, (unsigned)(pc + off));
            break;
        }

        case OP_JCMP_I:
        case OP_JCMP_F: {
            uint8_t rel = code[pc++];
            int16_t off = rdi16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s %s %+d -> %04X\r\n",
                      start, name, jcmp_name(rel), off, (unsigned)(pc + off));
            break;
        }

        case OP_MOV_VAR: {
            uint8_t kind = code[pc++];
            uint16_t src_raw = rd16(code + pc);
            pc += 2;
            uint16_t dst_raw = rd16(code + pc);
            pc += 2;
            const char * kind_name = "?";
            switch (kind) {
            case BC_MOV_INT:
                kind_name = "I";
                break;
            case BC_MOV_FLT:
                kind_name = "F";
                break;
            case BC_MOV_STR:
                kind_name = "S";
                break;
            }
            dbg_print("  %04X: %-16s kind=%s src=%s dst=%s\r\n",
                      start, name, kind_name, slot_name(cs, src_raw), slot_name(cs, dst_raw));
            break;
        }

        /* Absolute jump: addr:32 */
        case OP_JMP_ABS:
        case OP_GOSUB: {
            uint32_t addr = rd32(code + pc);
            pc += 4;
            dbg_print("  %04X: %-16s -> %04X\r\n", start, name, addr);
            break;
        }

        /* FOR init/next: var:16, lim:16, step:16, offset:16 */
        case OP_FOR_INIT_I:
        case OP_FOR_NEXT_I:
        case OP_FOR_INIT_F:
        case OP_FOR_NEXT_F: {
            uint16_t var = rd16(code + pc);
            pc += 2;
            uint16_t lim = rd16(code + pc);
            pc += 2;
            uint16_t step = rd16(code + pc);
            pc += 2;
            int16_t off = rdi16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s %s lim=%d stp=%d %+d->%04X\r\n",
                      start, name, slot_name(cs, var), lim, step, off, (unsigned)(pc + off));
            break;
        }

        /* CALL SUB/FUN: idx:16, nargs:8 */
        case OP_CALL_SUB:
        case OP_CALL_FUN: {
            uint16_t idx = rd16(code + pc);
            pc += 2;
            uint8_t nargs = code[pc++];
            dbg_print("  %04X: %-16s %s nargs=%d\r\n", start, name, subfun_name(cs, idx), nargs);
            break;
        }

        /* ENTER_FRAME: nlocals:16 */
        case OP_ENTER_FRAME: {
            uint16_t n = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s nlocals=%d\r\n", start, name, n);
            break;
        }

        /* PRINT: flags:8 */
        case OP_PRINT_INT:
        case OP_PRINT_FLT:
        case OP_PRINT_STR: {
            uint8_t flags = code[pc++];
            dbg_print("  %04X: %-16s flags=0x%02X\r\n", start, name, flags);
            break;
        }

        /* STR_INSTR: nargs:8 */
        case OP_STR_INSTR: {
            uint8_t nargs = code[pc++];
            dbg_print("  %04X: %-16s nargs=%d\r\n", start, name, nargs);
            break;
        }

        /* LINE: lineno:16 */
        case OP_LINE: {
            uint16_t line = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s %d\r\n", start, name, line);
            break;
        }

        case OP_FASTGFX_SWAP:
        case OP_FASTGFX_SYNC:
        case OP_FASTGFX_CREATE:
        case OP_FASTGFX_CLOSE:
        case OP_FASTGFX_FPS:
        case OP_RGB:
        case OP_COLOUR:
        case OP_PAUSE:
        case OP_STR_DATE:
        case OP_STR_TIME:
        case OP_KEYDOWN:
        case OP_PIN_READ:
        case OP_PIN_WRITE:
        case OP_MATH_MULSHR:
        case OP_MATH_SQRSHR:
        case OP_MATH_MULSHRADD:
            dbg_print("  %04X: %-16s\r\n", start, name);
            break;

        case OP_PWM: {
            uint8_t subop = code[pc++];
            dbg_print("  %04X: %-16s subop=%u", start, name, (unsigned)subop);
            if (subop == BC_PWM_CONFIG) {
                uint8_t present = code[pc++];
                dbg_print(" present=0x%02X", (unsigned)present);
            } else if (subop == BC_PWM_SYNC) {
                uint16_t present = rd16(code + pc);
                pc += 2;
                dbg_print(" present=0x%04X", (unsigned)present);
            }
            dbg_print("\r\n");
            break;
        }

        case OP_SERVO: {
            uint8_t present = code[pc++];
            dbg_print("  %04X: %-16s present=0x%02X\r\n", start, name, (unsigned)present);
            break;
        }

        case OP_SYSCALL: {
            uint16_t sysid = rd16(code + pc);
            pc += 2;
            uint8_t argc = code[pc++];
            uint8_t auxlen = code[pc++];
            dbg_print("  %04X: %-16s id=%u argc=%u aux=%u\r\n",
                      start, name, (unsigned)sysid, (unsigned)argc, (unsigned)auxlen);
            pc += auxlen;
            break;
        }

        case OP_SETPIN: {
            uint16_t mode = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s mode=%u\r\n", start, name, (unsigned)mode);
            break;
        }

        case OP_FONT: {
            uint8_t argc = code[pc++];
            dbg_print("  %04X: %-16s argc=%u\r\n", start, name, (unsigned)argc);
            break;
        }

        case OP_BOX: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_BOX_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_CIRCLE: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_BOX_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_TRIANGLE: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_TRIANGLE_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_POLYGON: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_POLYGON_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_DRAW_LINE: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_LINE_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_TEXT: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_TEXT_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_CLS: {
            uint8_t has_arg = code[pc++];
            dbg_print("  %04X: %-16s arg=%s\r\n", start, name, has_arg ? "stack" : "-");
            break;
        }

        case OP_PIXEL: {
            int i;
            uint8_t field_count = code[pc++];
            dbg_print("  %04X: %-16s argc=%u", start, name, (unsigned)field_count);
            for (i = 0; i < BC_PIXEL_ARG_COUNT; i++) {
                uint8_t kind = code[pc++];
                dbg_print("%s", i == 0 ? " " : ", ");
                switch (kind) {
                case BC_BOX_ARG_EMPTY:
                    dbg_print("-");
                    break;
                case BC_BOX_ARG_STACK:
                    dbg_print("stack");
                    break;
                case BC_BOX_ARG_GLOBAL_ARR_I:
                case BC_BOX_ARG_GLOBAL_ARR_F:
                case BC_BOX_ARG_LOCAL_ARR_I:
                case BC_BOX_ARG_LOCAL_ARR_F: {
                    uint16_t slot = rd16(code + pc);
                    pc += 2;
                    dbg_print("%s[%u]",
                              (kind == BC_BOX_ARG_GLOBAL_ARR_I || kind == BC_BOX_ARG_GLOBAL_ARR_F) ? "garr" : "larr",
                              (unsigned)slot);
                    break;
                }
                default:
                    dbg_print("kind=%u", (unsigned)kind);
                    break;
                }
            }
            dbg_print("\r\n");
            break;
        }

        case OP_RESTORE_DATA: {
            uint16_t di = rd16(code + pc);
            pc += 2;
            dbg_print("  %04X: %-16s data_index=%u\r\n", start, name, di);
            break;
        }

        /* All other opcodes have no operands */
        default:
            dbg_print("  %04X: %s\r\n", start, name);
            break;
        }
    }
    dbg_print("\r\n");
}

/* ======================================================================
 * bc_dump_stats -- Print compiler statistics
 * ====================================================================== */
void bc_dump_stats(BCCompiler * cs) {
    dbg_print("=== Compiler Stats ===\r\n");
    dbg_print("  Code:     %5u / %5d bytes (%d%%)\r\n",
              cs->code_len, BC_MAX_CODE,
              (int)(100L * cs->code_len / BC_MAX_CODE));
    dbg_print("  Vars:     %5d / %5d slots\r\n",
              cs->slot_count, BC_MAX_SLOTS);
    dbg_print("  Consts:   %5d / %5d\r\n",
              cs->const_count, BC_MAX_CONSTANTS);
    dbg_print("  SUB/FUN:  %5d / %5d\r\n",
              cs->subfun_count, BC_MAX_SUBFUNS);
    dbg_print("  Fixups:   %5d / %5d\r\n",
              cs->fixup_count, BC_MAX_FIXUPS);
    dbg_print("  LineMap:  %5d / %5d\r\n",
              cs->linemap_count, BC_MAX_LINEMAP);
    dbg_print("  Locals:   %5d / %5d\r\n",
              cs->local_count, BC_MAX_LOCALS);
    dbg_print("  DATA:     %5d / %5d\r\n",
              cs->data_count, BC_MAX_DATA_ITEMS);

    /* Variable slot listing */
    if (cs->slot_count > 0) {
        dbg_print("  Variables:\r\n");
        for (int i = 0; i < cs->slot_count; i++) {
            BCSlot * s = &cs->slots[i];
            if (!s->name[0]) continue;
            const char * type = "???";
            if (s->type == T_INT)
                type = "INT";
            else if (s->type == T_NBR)
                type = "FLT";
            else if (s->type == T_STR)
                type = "STR";
            dbg_print("    [%3d] %-16s %s%s\r\n", i, s->name, type,
                      s->is_array ? " (array)" : "");
        }
    }

    /* SUB/FUNCTION listing */
    if (cs->subfun_count > 0) {
        dbg_print("  SUB/FUNCTION:\r\n");
        for (int i = 0; i < cs->subfun_count; i++) {
            BCSubFun * sf = &cs->subfuns[i];
            dbg_print("    [%2d] %-16s @%04X p=%d l=%d %s\r\n",
                      i, sf->name, sf->entry_addr, sf->nparams, sf->nlocals,
                      sf->return_type ? "FUN" : "SUB");
        }
    }

    dbg_print("\r\n");
}

/* ======================================================================
 * bc_dump_vm_state -- Print VM state at point of error
 * ====================================================================== */
void bc_dump_vm_state(BCVMState * vm) {
    dbg_print("=== VM State ===\r\n");
    dbg_print("  Line: %d\r\n", (int)vm->current_line);
    if (vm->bytecode && vm->pc) {
        uint32_t offset = (uint32_t)(vm->pc - vm->bytecode);
        dbg_print("  PC: 0x%04X (offset %u / %u)\r\n",
                  offset, offset, vm->bytecode_len);

        /* Show the opcode at PC if valid */
        if (offset < vm->bytecode_len) {
            const char * name = opcode_name(vm->bytecode[offset]);
            dbg_print("  Next op: 0x%02X (%s)\r\n",
                      vm->bytecode[offset], name ? name : "???");
        }

        /* Show a few bytes around PC */
        dbg_print("  Bytes around PC:");
        int start = (int)offset - 4;
        if (start < 0) start = 0;
        int end = offset + 8;
        if ((uint32_t)end > vm->bytecode_len) end = vm->bytecode_len;
        for (int i = start; i < end; i++) {
            if (i == (int)offset)
                dbg_print(" [%02X]", vm->bytecode[i]);
            else
                dbg_print(" %02X", vm->bytecode[i]);
        }
        dbg_print("\r\n");
    }

    dbg_print("  Stack pointer: %d\r\n", vm->sp);
    /* Show top of stack */
    if (vm->sp >= 0) {
        int show = vm->sp < 4 ? vm->sp + 1 : 4;
        dbg_print("  Stack (top %d):\r\n", show);
        for (int i = 0; i < show; i++) {
            int idx = vm->sp - i;
            uint8_t t = vm->stack_types[idx];
            if (t == T_INT)
                dbg_print("    [%d] INT = %lld\r\n", idx, (long long)vm->stack[idx].i);
            else if (t == T_NBR)
                dbg_print("    [%d] FLT = %.10g\r\n", idx, (double)vm->stack[idx].f);
            else if (t == T_STR)
                dbg_print("    [%d] STR = \"%s\"\r\n", idx,
                          vm->stack[idx].s ? (const char *)vm->stack[idx].s : "(null)");
            else
                dbg_print("    [%d] type=0x%02X\r\n", idx, t);
        }
    }

    dbg_print("  Call stack depth: %d\r\n", vm->csp);
    dbg_print("  FOR stack depth: %d\r\n", vm->fsp);
    dbg_print("  GOSUB stack depth: %d\r\n", vm->gsp);
    dbg_print("  Frame base: %d, locals top: %d\r\n",
              vm->frame_base, vm->locals_top);
    dbg_print("\r\n");
}

/* ======================================================================
 * Crash breadcrumb system
 *
 * On device: BCCrashInfo lives in __uninitialized_ram, which survives
 * watchdog/soft resets.  bc_crash_checkpoint() writes the current stage
 * and stack pointer there.  If a HardFault fires, sigbus() calls
 * bc_crash_save_fault() to capture ARM fault registers, then the
 * device resets.  On next boot, bc_crash_dump_if_any() checks for
 * a valid breadcrumb and prints the crash report.
 *
 * On host: regular static var (for testing the API).
 * ====================================================================== */

/* Storage attribute comes from port_config.h: the device ports put the
 * struct in .uninitialized_data so it survives a soft / watchdog reset
 * for next-boot crash-report recovery; host's BC_CRASH_INFO_ATTR
 * expands to empty (plain BSS). */
#include "port_config.h"
BC_CRASH_INFO_ATTR BCCrashInfo bc_crash_info;

/*
 * Record a checkpoint — always writes to breadcrumb, always prints.
 * Printing ensures we see progress even if the display is cleared on reset.
 */
void bc_crash_checkpoint(int stage, const char * label) {
    /* If this is the first checkpoint since last clear, init rolling state */
    if (bc_crash_info.magic != BC_CRASH_MAGIC) {
        bc_crash_info.sp_lowest = 0xFFFFFFFFu;
        bc_crash_info.trail_head = 0;
        bc_crash_info.trail_count = 0;
        for (int t = 0; t < BC_CRASH_TRAIL_LEN; t++) bc_crash_info.trail[t] = 0;
    }
    bc_crash_info.magic = BC_CRASH_MAGIC;

    /* Save previous label as context for the crash dump */
    for (int j = 0; j < 31; j++) bc_crash_info.prev_label[j] = bc_crash_info.label[j];
    bc_crash_info.prev_label[31] = '\0';

    bc_crash_info.checkpoint = (uint32_t)stage;

    /* Capture stack pointer via port hook (device reads `sp`; host
     * returns 0). */
    {
        extern uint32_t port_bc_crash_get_sp(void);
        uint32_t sp_val = port_bc_crash_get_sp();
        bc_crash_info.sp = sp_val;
        if (sp_val && sp_val < bc_crash_info.sp_lowest) bc_crash_info.sp_lowest = sp_val;
    }

    /* Snapshot heap usage (works on host too, returns 0 on device) */
    bc_crash_info.heap_used = (uint32_t)bc_alloc_bytes_used_peek();
    bc_crash_info.heap_capacity = (uint32_t)bc_alloc_bytes_capacity();

    /* Zero the fault registers so stale values don't mislead */
    bc_crash_info.cfsr = 0;
    bc_crash_info.hfsr = 0;
    bc_crash_info.bfar = 0;
    bc_crash_info.mmfar = 0;

    /* Push into checkpoint trail (ring buffer) */
    bc_crash_info.trail[bc_crash_info.trail_head] = (uint8_t)stage;
    bc_crash_info.trail_head = (uint8_t)((bc_crash_info.trail_head + 1) % BC_CRASH_TRAIL_LEN);
    if (bc_crash_info.trail_count < BC_CRASH_TRAIL_LEN) bc_crash_info.trail_count++;

    /* Copy label */
    int i;
    for (i = 0; i < 31 && label[i]; i++)
        bc_crash_info.label[i] = label[i];
    bc_crash_info.label[i] = '\0';

    /* Print checkpoint only when debug is enabled — otherwise the output
     * floods the small PicoCalc screen and hides program output. */
    if (bc_debug_enabled) {
        dbg_print("VMRUN[%d] %s  SP=0x%08X  heap=%u/%u\r\n", stage, label,
                  (unsigned)bc_crash_info.sp,
                  (unsigned)bc_crash_info.heap_used,
                  (unsigned)bc_crash_info.heap_capacity);
    }
}

/* Snapshot key compiler state into the crash breadcrumb.  Safe to call with
 * cs==NULL. */
void bc_crash_snapshot_cs(BCCompiler * cs) {
    if (!cs) {
        bc_crash_info.cs_code = 0;
        bc_crash_info.cs_code_len = 0;
        bc_crash_info.cs_code_capacity = 0;
        bc_crash_info.cs_linemap = 0;
        bc_crash_info.cs_linemap_count = 0;
        bc_crash_info.cs_current_line = 0;
        bc_crash_info.cs_has_error = 0;
        return;
    }
    bc_crash_info.cs_code = (uint32_t)(uintptr_t)cs->code;
    bc_crash_info.cs_code_len = (uint32_t)cs->code_len;
    bc_crash_info.cs_code_capacity = 0; /* not tracked in BCCompiler */
    bc_crash_info.cs_linemap = (uint32_t)(uintptr_t)cs->linemap;
    bc_crash_info.cs_linemap_count = (uint32_t)cs->linemap_count;
    bc_crash_info.cs_current_line = (uint32_t)cs->current_line;
    bc_crash_info.cs_has_error = (uint32_t)cs->has_error;
}

/*
 * Called from sigbus() (HardFault handler) to snapshot ARM fault registers
 * into the breadcrumb before the device resets.
 */
void bc_crash_save_fault(void) {
    extern void port_bc_crash_save_fault_regs(BCCrashInfo * info);
    port_bc_crash_save_fault_regs(&bc_crash_info);
}

/*
 * Called at boot after display init.  If the breadcrumb has valid data,
 * print the crash report and clear it.
 */
void bc_crash_dump_if_any(void) {
    if (bc_crash_info.magic != BC_CRASH_MAGIC) return;

    dbg_print("\r\n");
    dbg_print("==================================\r\n");
    dbg_print("   VM CRASH REPORT\r\n");
    dbg_print("==================================\r\n");
    dbg_print("  Last checkpoint: %d\r\n", (int)bc_crash_info.checkpoint);
    dbg_print("  Label:  %s\r\n", bc_crash_info.label);
    dbg_print("  SP:     0x%08X\r\n", (unsigned)bc_crash_info.sp);
    dbg_print("  SPlow:  0x%08X\r\n", (unsigned)bc_crash_info.sp_lowest);
    dbg_print("  Prev:   %s\r\n", bc_crash_info.prev_label);
    dbg_print("  Heap:   %u / %u\r\n",
              (unsigned)bc_crash_info.heap_used,
              (unsigned)bc_crash_info.heap_capacity);
    dbg_print("  CS.code=0x%08X len=%u line=%u err=%u\r\n",
              (unsigned)bc_crash_info.cs_code,
              (unsigned)bc_crash_info.cs_code_len,
              (unsigned)bc_crash_info.cs_current_line,
              (unsigned)bc_crash_info.cs_has_error);
    dbg_print("  CS.linemap=0x%08X cnt=%u\r\n",
              (unsigned)bc_crash_info.cs_linemap,
              (unsigned)bc_crash_info.cs_linemap_count);
    /* Print checkpoint trail (oldest -> newest) */
    dbg_print("  Trail:  ");
    {
        int n = bc_crash_info.trail_count;
        int head = bc_crash_info.trail_head;
        int start = (head - n + BC_CRASH_TRAIL_LEN) % BC_CRASH_TRAIL_LEN;
        for (int k = 0; k < n; k++) {
            int idx = (start + k) % BC_CRASH_TRAIL_LEN;
            dbg_print("%d ", (int)bc_crash_info.trail[idx]);
        }
    }
    dbg_print("\r\n");

    /* ARM CFSR/HFSR/BFAR/MMFAR decode. On host (and any port that
     * doesn't implement port_bc_crash_save_fault_regs) the four
     * registers stay 0 and the decode prints a single line of zeros
     * with no bit names — harmless. */
    {
        uint32_t cfsr = bc_crash_info.cfsr;
        dbg_print("  CFSR:   0x%08X", (unsigned)cfsr);
        if (cfsr & 0x00000001) dbg_print(" IACCVIOL");
        if (cfsr & 0x00000002) dbg_print(" DACCVIOL");
        if (cfsr & 0x00000008) dbg_print(" MUNSTKERR");
        if (cfsr & 0x00000010) dbg_print(" MSTKERR");
        if (cfsr & 0x00000080) dbg_print(" MMARVALID");
        if (cfsr & 0x00000100) dbg_print(" IBUSERR");
        if (cfsr & 0x00000200) dbg_print(" PRECISERR");
        if (cfsr & 0x00000400) dbg_print(" IMPRECISERR");
        if (cfsr & 0x00000800) dbg_print(" UNSTKERR");
        if (cfsr & 0x00001000) dbg_print(" STKERR");
        if (cfsr & 0x00008000) dbg_print(" BFARVALID");
        if (cfsr & 0x00010000) dbg_print(" UNDEFINSTR");
        if (cfsr & 0x00020000) dbg_print(" INVSTATE");
        if (cfsr & 0x00040000) dbg_print(" INVPC");
        if (cfsr & 0x00080000) dbg_print(" NOCP");
        if (cfsr & 0x01000000) dbg_print(" UNALIGNED");
        if (cfsr & 0x02000000) dbg_print(" DIVBYZERO");
        dbg_print("\r\n");

        dbg_print("  HFSR:   0x%08X", (unsigned)bc_crash_info.hfsr);
        if (bc_crash_info.hfsr & 0x40000000) dbg_print(" FORCED");
        if (bc_crash_info.hfsr & 0x00000002) dbg_print(" VECTTBL");
        dbg_print("\r\n");

        if (cfsr & 0x00008000) /* BFARVALID */
            dbg_print("  BFAR:   0x%08X  <-- faulting address\r\n",
                      (unsigned)bc_crash_info.bfar);
        if (cfsr & 0x00000080) /* MMARVALID */
            dbg_print("  MMFAR:  0x%08X  <-- faulting address\r\n",
                      (unsigned)bc_crash_info.mmfar);
    }

    dbg_print("==================================\r\n\r\n");

    /* Clear so we don't repeat on next boot */
    bc_crash_info.magic = 0;
}

/*
 * Explicitly clear the breadcrumb (called on successful VM completion).
 */
void bc_crash_clear(void) {
    bc_crash_info.magic = 0;
}
