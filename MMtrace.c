/* ============================================================================
 *  MMtrace.c - Per-statement trace cache for MMBasic
 * ----------------------------------------------------------------------------
 *  Compiles LET assignments and IF conditions into a compact bytecode on first
 *  execution, then replays that bytecode on every subsequent hit without
 *  re-parsing the BASIC source.
 *
 *  Supported LET forms (compile-time validated; if any check fails the
 *  statement is marked BAD and never re-attempted, falling back to the normal
 *  interpreter forever):
 *
 *      lhs = const
 *      lhs = rhs_var
 *      lhs = operand BINOP operand
 *      lhs = f(operand)                -- single intrinsic call
 *      lhs = array(i)                  -- 1-D or 2-D element, global or local
 *
 *  lhs may be a scalar variable or an array element (1-D or 2-D).
 *  Variables may be global or LOCAL scalars of type T_NBR, T_INT, or T_STR.
 *  Constants (T_CONST) and struct members are not supported.
 *
 *  Supported string LET forms (lhs must be a plain string scalar):
 *      s$ = "literal"          simple literal assign (OptionEscape must be off)
 *      s$ = t$                 string copy
 *      s$ = s$ + "literal"     in-place append literal; bails if overflow
 *      s$ = s$ + t$            in-place append variable; bails if overflow
 *  The left operand of '+' must be the same variable as the LHS.
 *  String by-ref parameters (T_PTR), string arrays, and any other string
 *  expression form fall through to the interpreter unchanged.
 *
 *  Supported binary operators:
 *    NBR: + - * /  ^
 *    INT: + - *  \ (integer divide)  MOD
 *
 *  Supported comparisons (result T_INT, MMBasic convention -1/0):
 *    = <> < > <= >=  (NBR and INT operands)
 *
 *  Supported logical/bitwise (INT operands only):
 *    AND  OR  XOR  <<  >>  NOT
 *
 *  Supported zero-argument constants (folded to OP_LOAD_CONST_NBR at compile time):
 *    PI
 *
 *  Supported intrinsics — 1-arg (NBR argument, NBR result unless noted):
 *    SIN  COS  TAN  ASIN  ACOS  ATAN  SQR  ABS  INT()
 *    EXP  LOG  SGN  FIX  CINT  DEG  RAD
 *    Trig intrinsics bail to the interpreter when OPTION ANGLE DEGREES is set.
 *    LOG bails when arg <= 0 (preserves interpreter error).
 *    SGN, FIX, CINT return T_INT; all others return T_NBR.
 *
 *  Supported intrinsics — 2-arg (both coerced to NBR, result NBR):
 *    ATAN2(y,x)  MAX(a,b)  MIN(a,b)
 *    ATAN2 bails on OPTION ANGLE DEGREES (output scaling).
 *    MAX/MIN with 3+ arguments are not cached (fall back to interpreter).
 *
 *  Local variable handling:
 *    Local scalar and array references compile to LVAR opcodes that store the
 *    variable name rather than its slot index.  On the first replay inside a
 *    new call frame (detected via g_local_frame_gen) re_resolve_locals() walks
 *    the op list and re-resolves each LVAR name to its current slot.
 *    Subsequent replays within the same frame execute without any lookup.
 *    A single LET may reference at most TRACE_MAX_LVARS (8) distinct local
 *    variable names; lines exceeding this limit are marked BAD.
 *
 *  IF condition caching:
 *    Single-line IF conditions (up to tokenTHEN / tokenGOTO) are compiled and
 *    cached as ENTRY_KIND_IF entries using the same op set.
 *
 *  Cache table:
 *    Open-addressed hash keyed by the source token pointer (cmdline address in
 *    ProgMemory).  This pointer is stable for the life of the loaded program;
 *    any event that restructures ProgMemory or the variable table
 *    (NEW, EDIT, DIM, ERASE, OPTION BASE, OPTION EXPLICIT) calls
 *    TraceCacheInvalidateAll() which bumps g_local_frame_gen and wipes all
 *    ST_COMPILED entries back to ST_EMPTY.
 *
 *  Memory layout (dynamic arena design):
 *    [N × struct cache_entry] + [arena: N × TRACE_ARENA_RATIO × sizeof(entry)]
 *    Default (N=64): ~1.5 KB headers + ~12 KB arena = ~13.5 KB total.
 *    Allocated lazily on first OPTION TRACECACHE ON; freed by
 *    OPTION TRACECACHE OFF or TraceCacheFree().
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* strtod */
#include <math.h>   /* INFINITY */
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "MMtrace.h"

/* ------------------------------------------------------------------------- */
/* Public state                                                               */
/* ------------------------------------------------------------------------- */
uint16_t g_local_frame_gen = 0;
uint8_t g_trace_cache_flags = 0;

/* ------------------------------------------------------------------------- */
/* Configuration                                                              */
/* ------------------------------------------------------------------------- */
/* Memory layout (dynamic arena design):                                     */
/*   Each cache slot is now a fixed-size ~24-byte header; the variable-      */
/*   length ops[] and lnames[][] live in a separate arena region within the  */
/*   same slab.  Only the bytes actually used by a compiled statement are    */
/*   charged to the arena, so simple statements (2 ops, 0 lvars = 32 B)     */
/*   no longer consume the same space as complex ones.                       */
/*                                                                           */
/*   Slab layout:  [header × N][arena × TRACE_ARENA_RATIO × header_bytes]   */
/*   Default (N=64): ~1.5 KB headers + ~12 KB arena ≈ 13.5 KB total.        */
/*   (Old fixed layout: 64 × 404 B ≈ 26 KB, max 16 ops, max 4 lvars.)       */
/* TRACE_CACHE_SIZE_DEFAULT/MIN/MAX are now in MMtrace.h */
#define TRACE_ARENA_RATIO 8 /* arena bytes = RATIO × header bytes   */
#define TRACE_MAX_OPS 64    /* max ops per compiled statement        */
#define TRACE_MAX_STACK 8   /* max expression stack depth in replayer */
#define TRACE_MAX_LVARS 8   /* max distinct LVAR names in one LET;   \
                               lname_lens[] stored inline in header, \
                               name strings stored in the arena.         */

/* Runtime-selectable cache size (always a power of two).  Settable via
 * OPTION TRACECACHE ON [size].  Changing it is only allowed when the slab
 * is not currently allocated; OPTION TRACECACHE OFF / RUN free the slab.  */
static uint16_t g_trace_cache_size = TRACE_CACHE_SIZE_DEFAULT;
static uint16_t g_trace_cache_mask = TRACE_CACHE_SIZE_DEFAULT - 1;

/* ------------------------------------------------------------------------- */
/* Op codes                                                                   */
/* ------------------------------------------------------------------------- */
enum trace_opcode
{
    OP_END = 0, /* terminator (not strictly needed; n_ops loop) */

    /* loads push onto a small operand stack */
    OP_LOAD_CONST_NBR, /* push fconst                                 */
    OP_LOAD_CONST_INT, /* push iconst                                 */
    OP_LOAD_GVAR_NBR,  /* push g_vartbl[varindex].val.f               */
    OP_LOAD_GVAR_INT,  /* push g_vartbl[varindex].val.i               */
    OP_LOAD_LVAR_NBR,  /* push local; varindex re-resolved on gen change */
    OP_LOAD_LVAR_INT,
    /* by-reference local (T_PTR): val.s holds caller's address.            */
    OP_LOAD_LVAR_NBR_PTR,
    OP_LOAD_LVAR_INT_PTR,

    /* 1-D array element load.  TOS holds INT index (already coerced from
     * NBR if necessary).  Replay reads dims[0] from g_vartbl, bounds-checks
     * against g_OptionBase..dims[0], and pushes the element value.          */
    OP_LOAD_GVAR_NBR_AIDX1,
    OP_LOAD_GVAR_INT_AIDX1,
    OP_LOAD_LVAR_NBR_AIDX1,
    OP_LOAD_LVAR_INT_AIDX1,

    /* 2-D array element load.  Stack at entry: [..., int_i, int_j].  Both
     * indices are bounds-checked against dims[0]/dims[1] and the element at
     *   base[ (i-Base) + (j-Base)*(dims[0]+1-Base) ]
     * (matches MMBasic's findvar() linearization order) is pushed.          */
    OP_LOAD_GVAR_NBR_AIDX2,
    OP_LOAD_GVAR_INT_AIDX2,
    OP_LOAD_LVAR_NBR_AIDX2,
    OP_LOAD_LVAR_INT_AIDX2,

    /* binary ops consume top two elements, push one result of the named type */
    OP_ADD_NBR,
    OP_SUB_NBR,
    OP_MUL_NBR,
    OP_DIV_NBR, /* always NBR result    */
    OP_EXP_NBR, /* ^   - both operands NBR, result NBR (pow)              */
    OP_ADD_INT,
    OP_SUB_INT,
    OP_MUL_INT,    /* INT result           */
    OP_MOD_INT,    /* MMBasic Mod - integer-only, /0 errors                  */
    OP_DIVINT_INT, /* MMBasic \\  - integer-only divide, /0 errors        */

    /* unary ops on TOS, type preserved */
    OP_NEG_NBR,
    OP_NEG_INT,

    /* in-place type conversion */
    OP_TOS_INT_TO_NBR,  /* promote TOS INT -> NBR                           */
    OP_TOS1_INT_TO_NBR, /* promote TOS-1 INT -> NBR (lifts buried operand) */
    OP_TOS_NBR_TO_INT,  /* round TOS NBR -> INT via FloatToInt64            */
    OP_TOS1_NBR_TO_INT, /* round TOS-1 NBR -> INT via FloatToInt64          */

    /* intrinsic 1-arg function calls.  All bail to interpreter when           */
    /* useoptionangle is set (so degree-mode programs still work, just slow)   */
    OP_SIN,
    OP_COS,
    OP_TAN,
    OP_ASIN,
    OP_ACOS,
    OP_ATAN,
    OP_SQR,     /* bails on negative arg                                  */
    OP_ABS_NBR, /* TOS NBR -> NBR                                          */
    OP_ABS_INT, /* TOS INT -> INT                                          */
    OP_INT_OF,  /* floor(NBR) -> INT  (Int())                              */
    OP_EXP,     /* exp(NBR) -> NBR                                         */
    OP_LOG,     /* log(NBR) -> NBR; bails if arg <= 0                      */
    OP_SGN,     /* sign(NBR) -> INT: -1, 0, +1                             */
    OP_FIX,     /* trunc-toward-zero(NBR) -> INT  (Fix())                  */
    OP_CINT,    /* round-to-nearest(NBR) -> INT  (CInt())                  */
    OP_DEG,     /* radians -> degrees: NBR * RADCONV -> NBR                */
    OP_RAD,     /* degrees -> radians: NBR / RADCONV -> NBR                */

    /* 2-arg intrinsics: consume NBR[sp-2] and NBR[sp-1], push one NBR    */
    OP_ATAN2,   /* atan2(y, x) -> NBR; bails on useoptionangle             */
    OP_MAX_NBR, /* max(a, b)   -> NBR                                      */
    OP_MIN_NBR, /* min(a, b)   -> NBR                                      */

    /* store TOS to a global scalar slot */
    OP_STORE_GVAR_NBR,
    OP_STORE_GVAR_INT,
    /* store TOS to a local scalar slot (varindex re-resolved on gen change) */
    OP_STORE_LVAR_NBR,
    OP_STORE_LVAR_INT,
    /* store TOS to a by-ref local (T_PTR) - dereferences val.s             */
    OP_STORE_LVAR_NBR_PTR,
    OP_STORE_LVAR_INT_PTR,

    /* 1-D array element store.  Stack at entry: [..., index_int, value].
     * Pops both, bounds-checks, writes element.                             */
    OP_STORE_GVAR_NBR_AIDX1,
    OP_STORE_GVAR_INT_AIDX1,
    OP_STORE_LVAR_NBR_AIDX1,
    OP_STORE_LVAR_INT_AIDX1,

    /* 2-D array element store.  Stack at entry: [..., int_i, int_j, value]. */
    OP_STORE_GVAR_NBR_AIDX2,
    OP_STORE_GVAR_INT_AIDX2,
    OP_STORE_LVAR_NBR_AIDX2,
    OP_STORE_LVAR_INT_AIDX2,

    /* Comparison operators.  Consume two operands, push T_INT result
     * (-1 = true, 0 = false — MMBasic convention).                          */
    OP_EQ_NBR, /* NBR == NBR -> INT */
    OP_EQ_INT, /* INT == INT -> INT */
    OP_NE_NBR,
    OP_NE_INT,
    OP_LT_NBR,
    OP_LT_INT,
    OP_GT_NBR,
    OP_GT_INT,
    OP_LTE_NBR,
    OP_LTE_INT,
    OP_GTE_NBR,
    OP_GTE_INT,

    /* Logical / bitwise operators.  Both operands must be T_INT.            */
    OP_AND_INT, /* bitwise AND  */
    OP_OR_INT,  /* bitwise OR   */
    OP_XOR_INT, /* bitwise XOR  */
    OP_SHL_INT, /* left shift   */
    OP_SHR_INT, /* right shift  */

    /* Unary bitwise NOT.  Operand must be T_INT.                            */
    OP_NOT_INT,

    /* String operations -------------------------------------------------- */
    /* OP_PUSH_STR_LIT: u.iconst holds the tokenized-source address of the
     * opening '"' of a string literal.  Replay parses the literal bytes
     * into a C-stack scratch buffer and pushes its address.  Only valid
     * when OptionEscape is off; OptionEscape programs fall back to the
     * interpreter.                                                           */
    OP_PUSH_STR_LIT,
    /* Load val.s pointer of a global/local string scalar onto the string
     * stack.  varindex is the g_vartbl slot.                                */
    OP_LOAD_GVAR_STR,
    OP_LOAD_LVAR_STR,
    /* Copy TOS string into the destination variable via Mstrcpy; pop TOS.  */
    OP_STORE_GVAR_STR,
    OP_STORE_LVAR_STR,
    /* In-place append: Mstrcat g_vartbl[varindex].val.s with TOS string.
     * Bails if combined length > MAXSTRLEN so the interpreter handles it.
     * Pops TOS; no separate STORE op needed.                                */
    OP_APPEND_INPLACE_STR,
    OP_APPEND_INPLACE_LVAR_STR,

};

/* Slim op record.  For LVAR ops, `lname_idx` indexes into the entry's
 * name pool (lnames[][]) - the pool is sized small (TRACE_MAX_LVARS) and
 * shared across all LVAR ops in the same LET.  Non-LVAR ops ignore namelen. */
struct trace_op
{
    uint8_t opcode;
    uint8_t lname_idx; /* 0..TRACE_MAX_LVARS-1; meaningful only for LVAR ops */
    uint8_t pad[2];
    int32_t varindex; /* resolved slot; for LVAR re-resolved       */
    union
    {
        MMFLOAT fconst;
        int64_t iconst;
    } u;
};

/* ------------------------------------------------------------------------- */
/* Cache table                                                                */
/* ------------------------------------------------------------------------- */
enum entry_state
{
    ST_EMPTY = 0, /* slot never used                         */
    ST_PENDING,   /* seen once; compile attempt next visit   */
    ST_COMPILED,  /* ops valid, ready to replay              */
    ST_BAD        /* compile rejected; never retry           */
};

/* Entry kinds — distinguishes LET from IF-condition entries in the same table */
#define ENTRY_KIND_LET 0 /* default (zero from memset) */
#define ENTRY_KIND_IF 1
#define ENTRY_KIND_JUMP 3 /* GOTO / GOSUB / RESTORE with literal target       */

struct cache_entry
{
    unsigned char *src;   /* hash key & guard (NULL when ST_EMPTY)    */
    uint32_t payload_off; /* byte offset into g_arena; valid only when
                             state == ST_COMPILED.  Arena layout:
                             [n_ops × struct trace_op]
                             [n_lnames × char[MAXVARLEN]]             */
    uint16_t frame_gen;   /* g_local_frame_gen at last resolve        */
    uint8_t state;
    uint8_t n_ops;
    uint8_t has_locals;                  /* 1 if any LVAR op present                 */
    uint8_t n_lnames;                    /* 0..TRACE_MAX_LVARS distinct LVAR names   */
    uint8_t compile_attempts;            /* incremented on each PENDING->compile fail */
    uint8_t entry_kind;                  /* ENTRY_KIND_LET or ENTRY_KIND_IF          */
    uint8_t lname_lens[TRACE_MAX_LVARS]; /* upper-cased name lengths (inline) */
};

static struct cache_entry *g_cache = NULL; /* lazily allocated              */
/* Arena for variable-length compiled payloads.  Lives inside the same slab
 * as g_cache: g_arena = (unsigned char*)g_cache + header section size.     */
static unsigned char *g_arena = NULL;
static uint32_t g_arena_cap = 0;  /* total arena bytes              */
static uint32_t g_arena_used = 0; /* bytes consumed (bump pointer)  */

/* statistics (visible from C; printable via cmd_end perf report later) */
uint32_t g_trace_replays = 0;
uint32_t g_trace_compiles_ok = 0;
uint32_t g_trace_compiles_bad = 0;
uint32_t g_trace_lookup_null = 0; /* cache_lookup_or_create returned NULL */
uint32_t g_trace_alloc_fail = 0;  /* ensure_cache_alloc could not allocate slab */
uint32_t g_trace_optin_skip = 0;  /* opt-in list active and statement not on it */
uint32_t g_trace_jump_hits = 0;   /* GOTO/GOSUB/RESTORE cache hits            */
uint8_t g_trace_debug_bad = 0;
static uint8_t g_trace_last_was_full = 0; /* set by compile_let/if when arena is full */
static uint8_t g_trace_lookup_warned = 0; /* one-shot: suppress repeated lookup_null prints */
static void debug_print_stmt(unsigned char *stmt, const char *tag);
/* Per-sub replay counters.  Indexed by g_current_sub_idx (0..MAXSUBFUN-1)
 * for code inside a SUB/FUNCTION, or MAXSUBFUN for top-level program code
 * (g_current_sub_idx == -1).  Heap-allocated alongside the cache slab;
 * NULL when the cache is not allocated.                                   */
uint32_t *g_tc_sub_let_hits = NULL;
uint32_t *g_tc_sub_if_hits = NULL;

/* Map g_current_sub_idx (-1 = top level) to a safe array index.          */
static inline int tc_sub_idx(void)
{
    extern int g_current_sub_idx;
    return (g_current_sub_idx >= 0 && g_current_sub_idx < MAXSUBFUN)
               ? g_current_sub_idx
               : MAXSUBFUN;
}

/* ------------------------------------------------------------------------- */
/* Lazy allocation                                                            */
/* ------------------------------------------------------------------------- */
static int ensure_cache_alloc(void)
{
    if (g_cache != NULL)
        return 1;
    /* Single contiguous slab: header table followed by payload arena.       */
    size_t hdr_bytes = (size_t)g_trace_cache_size * sizeof(struct cache_entry);
    size_t arena_bytes = hdr_bytes * TRACE_ARENA_RATIO;
    size_t total = hdr_bytes + arena_bytes;
    unsigned char *slab = (unsigned char *)GetMemory(total);
    if (slab == NULL)
    {
        g_trace_alloc_fail++;
        return 0;
    }
    /* GetMemory zeros - all slots start ST_EMPTY (=0) with src=NULL.        */
    g_cache = (struct cache_entry *)slab;
    g_arena = slab + hdr_bytes;
    g_arena_cap = (uint32_t)arena_bytes;
    g_arena_used = 0;
    /* Allocate per-sub hit counters (GetMemory zeros them).                 */
    if (g_tc_sub_let_hits == NULL)
        g_tc_sub_let_hits = (uint32_t *)GetMemory((MAXSUBFUN + 1) * sizeof(uint32_t));
    if (g_tc_sub_if_hits == NULL)
        g_tc_sub_if_hits = (uint32_t *)GetMemory((MAXSUBFUN + 1) * sizeof(uint32_t));
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Hash table lookup                                                          */
/*                                                                            */
/* Open addressing with bounded linear probing (TRACE_PROBE_LIMIT).  Key is   */
/* the cmdline pointer (stable for the life of the loaded program).          */
/*                                                                            */
/* If we don't find the key within the probe budget and no ST_EMPTY slot is   */
/* available, we return NULL: the statement runs through the normal           */
/* interpreter forever.  Crucially we DO NOT evict ST_BAD slots - they are    */
/* memoised "don't bother" markers and recycling them turns hot non-cacheable */
/* statements into a recompile-then-fail loop on every iteration.             */
/* ------------------------------------------------------------------------- */
#define TRACE_PROBE_LIMIT 8

/* When compile_let() fails, the cause may be transient: typical BASIC code
 * creates variables on first assignment (no OPTION EXPLICIT), so the LHS or
 * a forward-referenced RHS scalar may not yet exist when the trace cache
 * first sees a statement.  Rather than mark the entry ST_BAD on the first
 * failure and never retry, we leave it ST_PENDING and try again on the next
 * visit, up to TRACE_COMPILE_RETRY_BUDGET attempts.  After that we give up
 * to avoid burning compile cycles on lines that genuinely cannot compile
 * (unsupported intrinsics, expressions exceeding TRACE_MAX_OPS, etc.).      */
#define TRACE_COMPILE_RETRY_BUDGET 2

static struct cache_entry * __not_in_flash_func(cache_lookup_or_create)(unsigned char *src)
{
    if (!ensure_cache_alloc())
        return NULL;

    /* Fibonacci/golden-ratio multiplicative hash.  One multiply spreads the
     * narrow flash-address range into all 32 bits; the fold brings the
     * high-quality upper bits into the range selected by & mask.            */
    uint32_t h = (uint32_t)(uintptr_t)src * 0x9E3779B9u;
    h ^= h >> 16;

    int idx = (int)(h & g_trace_cache_mask);
    int probes;
    for (probes = 0; probes < TRACE_PROBE_LIMIT; probes++)
    {
        struct cache_entry *e = &g_cache[idx];
        if (e->src == src)
            return e;
        if (e->state == ST_EMPTY)
        {
            /* claim the slot */
            e->src = src;
            e->state = ST_PENDING;
            e->n_ops = 0;
            e->has_locals = 0;
            e->frame_gen = 0;
            e->compile_attempts = 0;
            return e;
        }
        idx = (idx + 1) & g_trace_cache_mask;
    }
    return NULL; /* probe chain saturated - skip caching this statement */
}

/* ------------------------------------------------------------------------- */
/* Compile-time helpers                                                       */
/* ------------------------------------------------------------------------- */

/* Skip past T_NEWLINE / T_LINENBR / 0 markers - things that signify          */
/* "end of statement" in the tokenised stream.                                */
static int is_stmt_end(unsigned char c)
{
    return (c == 0) || (c == ':') || (c == '\'') ||
           (c == T_NEWLINE) || (c == T_LINENBR) || (c == T_CMDEND);
}

/* Read a name from p (must start with isnamestart).  Returns the next char
 * after the name + optional suffix ($ % !) into *pp.  Writes the name into
 * namebuf (NUL-terminated, original case).  *suffix is set to 0/'$'/'%'/'!'.  */
static int read_name(unsigned char *p, unsigned char **pp,
                     unsigned char *namebuf, int bufsz, unsigned char *suffix)
{
    int n = 0;
    if (!isnamestart(*p))
        return 0;
    while (isnamechar(*p))
    {
        if (n + 1 >= bufsz)
            return 0;
        namebuf[n++] = *p++;
    }
    namebuf[n] = 0;
    if (*p == '$' || *p == '%' || *p == '!')
        *suffix = *p++;
    else
        *suffix = 0;
    /* Refuse struct-member access ('.') - that belongs to the slow path.
     * Array subscripts ('(') are allowed: the caller decides scalar vs
     * array by peeking at the next non-space character.                    */
    {
        unsigned char *q = p;
        while (*q == ' ')
            q++;
        if (*q == '.')
            return 0;
    }
    *pp = p;
    return n;
}

/* Scope tags for resolve_scalar(). */
#define SCOPE_NONE 0
#define SCOPE_GLOBAL 1
#define SCOPE_LOCAL 2
#define SCOPE_LOCAL_PTR 3  /* by-ref parameter (T_PTR) */
#define SCOPE_CONST 4      /* T_CONST global - resolved as inline constant */
#define SCOPE_GLOBAL_ARR 5 /* 1-D global array (NBR or INT) */
#define SCOPE_LOCAL_ARR 6  /* 1-D local array (incl. by-ref T_PTR) */

/* Resolve a name to a SCALAR variable slot.  Returns SCOPE_LOCAL or
 * SCOPE_GLOBAL on success and writes varindex/type/upper-cased name; returns
 * SCOPE_NONE on any failure (not found, array, struct, string, const, ...).
 *
 * findvar() is deliberately avoided - it raises hard errors on dimension or
 * type mismatch and has no "look but don't touch" mode.  We walk the
 * appropriate half of g_vartbl[] directly using the same precedence as
 * findvar(): if g_LocalIndex != 0, try locals at that scope first, then
 * fall through to globals.
 *
 * `out_upname`/`out_uplen` receive the upper-cased name (caller-allocated
 * MAXVARLEN buffer).  These are needed only for SCOPE_LOCAL re-resolution
 * but are written for both for caller convenience.                          */
static int resolve_scalar(unsigned char *namebuf, unsigned char suffix,
                          int *out_type, int *out_idx,
                          unsigned char *out_upname, int *out_uplen,
                          MMFLOAT *out_const_f, int64_t *out_const_i)
{
    if (suffix == '$')
        return SCOPE_NONE; /* strings unsupported in 1.x */

    /* Upper-case the name; slot names in g_vartbl are stored upper-cased.   */
    int len = 0;
    while (namebuf[len] && len < MAXVARLEN)
    {
        out_upname[len] = (unsigned char)mytoupper(namebuf[len]);
        len++;
    }
    if (len < MAXVARLEN)
        out_upname[len] = 0;
    *out_uplen = len;

    int loc_size = GetLocalVarHashSize();

    /* ---- LOCAL pass (only when inside a sub/fun frame) ----------------- */
    if (g_LocalIndex)
    {
        for (int i = 0; i < loc_size; i++)
        {
            if (g_vartbl[i].name[0] == 0)
                continue;
            if (g_vartbl[i].level != g_LocalIndex)
                continue;
            unsigned char *tn = g_vartbl[i].name;
            int j = 0;
            while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
                j++;
            if (j != len)
                continue;
            if (j < MAXVARLEN && tn[j] != 0)
                continue;
            if (g_vartbl[i].dims[0] != 0)
                return SCOPE_NONE;
            int t = g_vartbl[i].type;
            if (t & (T_STR | T_CONST))
                return SCOPE_NONE;
#ifdef STRUCTENABLED
            if (t & T_STRUCT)
                return SCOPE_NONE;
#endif
            if (!(t & (T_NBR | T_INT)))
                return SCOPE_NONE;
            if (suffix == '%' && !(t & T_INT))
                return SCOPE_NONE;
            if (suffix == '!' && !(t & T_NBR))
                return SCOPE_NONE;
            *out_type = (t & T_NBR) ? T_NBR : T_INT;
            *out_idx = i;
            return (t & T_PTR) ? SCOPE_LOCAL_PTR : SCOPE_LOCAL;
        }
    }

    /* ---- GLOBAL pass --------------------------------------------------- */
    for (int i = loc_size; i < MAXVARS; i++)
    {
        if (g_vartbl[i].name[0] == 0)
            continue;
        unsigned char *tn = g_vartbl[i].name;
        int j = 0;
        while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
            j++;
        if (j != len)
            continue;
        if (j < MAXVARLEN && tn[j] != 0)
            continue;
        if (g_vartbl[i].dims[0] != 0)
            return SCOPE_NONE;
        int t = g_vartbl[i].type;
        if (t & (T_STR | T_PTR))
            return SCOPE_NONE;
#ifdef STRUCTENABLED
        if (t & T_STRUCT)
            return SCOPE_NONE;
#endif
        if (!(t & (T_NBR | T_INT)))
            return SCOPE_NONE;
        if (suffix == '%' && !(t & T_INT))
            return SCOPE_NONE;
        if (suffix == '!' && !(t & T_NBR))
            return SCOPE_NONE;
        *out_type = (t & T_NBR) ? T_NBR : T_INT;
        *out_idx = i;
        if (t & T_CONST)
        {
            /* Snapshot the constant's value at compile time; cache will be
             * invalidated whenever the program is edited or re-DIM'd.       */
            if (t & T_NBR)
                *out_const_f = g_vartbl[i].val.f;
            else
                *out_const_i = g_vartbl[i].val.i;
            return SCOPE_CONST;
        }
        return SCOPE_GLOBAL;
    }
    return SCOPE_NONE;
}

/* Resolve a name to a 1-D or 2-D array slot.  Returns SCOPE_GLOBAL_ARR or
 * SCOPE_LOCAL_ARR (which covers both ordinary locals and by-ref T_PTR
 * arrays - addressing is identical because val.s is the base pointer in
 * either case).  Returns SCOPE_NONE on any failure (>2-D, string, struct,
 * const, etc.).  Numeric MMBasic arrays are always 8 bytes per element
 * (MMFLOAT or int64_t).  *out_ndim is set to 1 or 2 on success.            */
static int resolve_array_nd(unsigned char *namebuf, unsigned char suffix,
                            int *out_type, int *out_idx, int *out_ndim,
                            unsigned char *out_upname, int *out_uplen)
{
    if (suffix == '$')
        return SCOPE_NONE;

    int len = 0;
    while (namebuf[len] && len < MAXVARLEN)
    {
        out_upname[len] = (unsigned char)mytoupper(namebuf[len]);
        len++;
    }
    if (len < MAXVARLEN)
        out_upname[len] = 0;
    *out_uplen = len;

    int loc_size = GetLocalVarHashSize();

    /* ---- LOCAL pass ---------------------------------------------------- */
    if (g_LocalIndex)
    {
        for (int i = 0; i < loc_size; i++)
        {
            if (g_vartbl[i].name[0] == 0)
                continue;
            if (g_vartbl[i].level != g_LocalIndex)
                continue;
            unsigned char *tn = g_vartbl[i].name;
            int j = 0;
            while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
                j++;
            if (j != len)
                continue;
            if (j < MAXVARLEN && tn[j] != 0)
                continue;
            /* Must be 1-D or 2-D (no higher).                               */
            if (g_vartbl[i].dims[0] == 0)
                return SCOPE_NONE;
            int ndim;
            if (MAXDIM > 1 && g_vartbl[i].dims[1] != 0)
            {
                if (MAXDIM > 2 && g_vartbl[i].dims[2] != 0)
                    return SCOPE_NONE;
                ndim = 2;
            }
            else
            {
                ndim = 1;
            }
            int t = g_vartbl[i].type;
            if (t & (T_STR | T_CONST))
                return SCOPE_NONE;
#ifdef STRUCTENABLED
            if (t & T_STRUCT)
                return SCOPE_NONE;
#endif
            if (!(t & (T_NBR | T_INT)))
                return SCOPE_NONE;
            if (suffix == '%' && !(t & T_INT))
                return SCOPE_NONE;
            if (suffix == '!' && !(t & T_NBR))
                return SCOPE_NONE;
            *out_type = (t & T_NBR) ? T_NBR : T_INT;
            *out_idx = i;
            *out_ndim = ndim;
            return SCOPE_LOCAL_ARR;
        }
    }

    /* ---- GLOBAL pass --------------------------------------------------- */
    for (int i = loc_size; i < MAXVARS; i++)
    {
        if (g_vartbl[i].name[0] == 0)
            continue;
        unsigned char *tn = g_vartbl[i].name;
        int j = 0;
        while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
            j++;
        if (j != len)
            continue;
        if (j < MAXVARLEN && tn[j] != 0)
            continue;
        if (g_vartbl[i].dims[0] == 0)
            return SCOPE_NONE;
        int ndim;
        if (MAXDIM > 1 && g_vartbl[i].dims[1] != 0)
        {
            if (MAXDIM > 2 && g_vartbl[i].dims[2] != 0)
                return SCOPE_NONE;
            ndim = 2;
        }
        else
        {
            ndim = 1;
        }
        int t = g_vartbl[i].type;
        if (t & (T_STR | T_PTR | T_CONST))
            return SCOPE_NONE;
#ifdef STRUCTENABLED
        if (t & T_STRUCT)
            return SCOPE_NONE;
#endif
        if (!(t & (T_NBR | T_INT)))
            return SCOPE_NONE;
        if (suffix == '%' && !(t & T_INT))
            return SCOPE_NONE;
        if (suffix == '!' && !(t & T_NBR))
            return SCOPE_NONE;
        *out_type = (t & T_NBR) ? T_NBR : T_INT;
        *out_idx = i;
        *out_ndim = ndim;
        return SCOPE_GLOBAL_ARR;
    }
    return SCOPE_NONE;
}

/* Resolve a name to a STRING scalar slot.  Returns SCOPE_LOCAL or
 * SCOPE_GLOBAL on success; SCOPE_NONE on failure (not found, array,
 * non-string, constant, by-ref).  Writes the upper-cased name into
 * out_upname (caller-supplied MAXVARLEN buffer) and its length into
 * *out_uplen.                                                               */
static int resolve_str_scalar(unsigned char *namebuf, int *out_idx,
                              unsigned char *out_upname, int *out_uplen)
{
    int len = 0;
    while (namebuf[len] && len < MAXVARLEN)
    {
        out_upname[len] = (unsigned char)mytoupper(namebuf[len]);
        len++;
    }
    if (len < MAXVARLEN)
        out_upname[len] = 0;
    *out_uplen = len;

    int loc_size = GetLocalVarHashSize();

    if (g_LocalIndex)
    {
        for (int i = 0; i < loc_size; i++)
        {
            if (g_vartbl[i].name[0] == 0)
                continue;
            if (g_vartbl[i].level != g_LocalIndex)
                continue;
            unsigned char *tn = g_vartbl[i].name;
            int j = 0;
            while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
                j++;
            if (j != len || (j < MAXVARLEN && tn[j] != 0))
                continue;
            if (g_vartbl[i].dims[0] != 0)
                return SCOPE_NONE;
            int t = g_vartbl[i].type;
            if (!(t & T_STR))
                return SCOPE_NONE;
            if (t & (T_CONST | T_PTR))
                return SCOPE_NONE;
            *out_idx = i;
            return SCOPE_LOCAL;
        }
    }

    for (int i = loc_size; i < MAXVARS; i++)
    {
        if (g_vartbl[i].name[0] == 0)
            continue;
        unsigned char *tn = g_vartbl[i].name;
        int j = 0;
        while (j < len && j < MAXVARLEN && tn[j] == out_upname[j])
            j++;
        if (j != len || (j < MAXVARLEN && tn[j] != 0))
            continue;
        if (g_vartbl[i].dims[0] != 0)
            return SCOPE_NONE;
        int t = g_vartbl[i].type;
        if (!(t & T_STR))
            return SCOPE_NONE;
        if (t & T_CONST)
            return SCOPE_NONE;
        *out_idx = i;
        return SCOPE_GLOBAL;
    }
    return SCOPE_NONE;
}

/* Re-resolve every LVAR op's varindex against the current local frame.
 * Called from replay when e->frame_gen != g_local_frame_gen.  Returns 1 on
 * success (all locals re-resolved with matching type), 0 if any local has
 * disappeared / changed shape (caller marks the entry BAD).                */
static int __not_in_flash_func(re_resolve_locals)(struct cache_entry *e)
{
    int loc_size = GetLocalVarHashSize();
    if (!g_LocalIndex)
        return 0; /* called from top level: no locals exist */
    struct trace_op *ops_base = (struct trace_op *)(g_arena + e->payload_off);
    unsigned char *lnames_base = (unsigned char *)(ops_base + e->n_ops);
    for (int k = 0; k < e->n_ops; k++)
    {
        struct trace_op *op = ops_base + k;
        int is_ptr;
        int is_array = 0;
        int is_str = 0;
        switch (op->opcode)
        {
        case OP_LOAD_LVAR_NBR:
        case OP_LOAD_LVAR_INT:
        case OP_STORE_LVAR_NBR:
        case OP_STORE_LVAR_INT:
            is_ptr = 0;
            break;
        case OP_LOAD_LVAR_NBR_PTR:
        case OP_LOAD_LVAR_INT_PTR:
        case OP_STORE_LVAR_NBR_PTR:
        case OP_STORE_LVAR_INT_PTR:
            is_ptr = 1;
            break;
        case OP_LOAD_LVAR_NBR_AIDX1:
        case OP_LOAD_LVAR_INT_AIDX1:
        case OP_STORE_LVAR_NBR_AIDX1:
        case OP_STORE_LVAR_INT_AIDX1:
            is_array = 1;
            is_ptr = 0;
            break;
        case OP_LOAD_LVAR_NBR_AIDX2:
        case OP_LOAD_LVAR_INT_AIDX2:
        case OP_STORE_LVAR_NBR_AIDX2:
        case OP_STORE_LVAR_INT_AIDX2:
            is_array = 2;
            is_ptr = 0;
            break;
        case OP_LOAD_LVAR_STR:
        case OP_STORE_LVAR_STR:
        case OP_APPEND_INPLACE_LVAR_STR:
            is_str = 1;
            is_ptr = 0;
            break;
        default:
            continue;
        }
        int li = op->lname_idx;
        if (li >= e->n_lnames)
            return 0;
        int len = e->lname_lens[li];
        unsigned char *want = lnames_base + li * MAXVARLEN;
        int found = -1;
        for (int i = 0; i < loc_size; i++)
        {
            if (g_vartbl[i].name[0] == 0)
                continue;
            if (g_vartbl[i].level != g_LocalIndex)
                continue;
            unsigned char *tn = g_vartbl[i].name;
            int j = 0;
            while (j < len && j < MAXVARLEN && tn[j] == want[j])
                j++;
            if (j != len)
                continue;
            if (j < MAXVARLEN && tn[j] != 0)
                continue;
            found = i;
            break;
        }
        if (found < 0)
            return 0;
        int t = g_vartbl[found].type;
        if (is_str)
        {
            /* String LVAR ops: variable must still be a plain string scalar. */
            if (!(t & T_STR))
                return 0;
            if (t & (T_CONST | T_PTR))
                return 0;
            if (g_vartbl[found].dims[0] != 0)
                return 0;
            op->varindex = found;
            continue;
        }
        if (t & (T_STR | T_CONST))
            return 0;
#ifdef STRUCTENABLED
        if (t & T_STRUCT)
            return 0;
#endif
        if (is_array)
        {
            /* Must still be the same shape (1-D vs 2-D) and same element
             * type as we compiled for.                                      */
            if (g_vartbl[found].dims[0] == 0)
                return 0;
            if (is_array == 1)
            {
                if (MAXDIM > 1 && g_vartbl[found].dims[1] != 0)
                    return 0;
            }
            else /* is_array == 2 */
            {
                if (MAXDIM <= 1 || g_vartbl[found].dims[1] == 0)
                    return 0;
                if (MAXDIM > 2 && g_vartbl[found].dims[2] != 0)
                    return 0;
            }
            int want_nbr = (op->opcode == OP_LOAD_LVAR_NBR_AIDX1 ||
                            op->opcode == OP_STORE_LVAR_NBR_AIDX1 ||
                            op->opcode == OP_LOAD_LVAR_NBR_AIDX2 ||
                            op->opcode == OP_STORE_LVAR_NBR_AIDX2);
            if (want_nbr && !(t & T_NBR))
                return 0;
            if (!want_nbr && !(t & T_INT))
                return 0;
            op->varindex = found;
            continue;
        }
        if (g_vartbl[found].dims[0] != 0)
            return 0;
        /* The PTR-ness must match what we compiled for: we cannot turn a
         * non-pointer load into a pointer load (or vice versa) at runtime
         * because the opcodes use different value paths.                  */
        int now_ptr = (t & T_PTR) ? 1 : 0;
        if (now_ptr != is_ptr)
            return 0;
        /* Type must still match the opcode flavour we compiled for.         */
        int want_nbr = (op->opcode == OP_LOAD_LVAR_NBR ||
                        op->opcode == OP_STORE_LVAR_NBR ||
                        op->opcode == OP_LOAD_LVAR_NBR_PTR ||
                        op->opcode == OP_STORE_LVAR_NBR_PTR);
        if (want_nbr && !(t & T_NBR))
            return 0;
        if (!want_nbr && !(t & T_INT))
            return 0;
        op->varindex = found;
    }
    e->frame_gen = g_local_frame_gen;
    return 1;
}

/* Parse a numeric literal at *pp.  On success returns 1 and advances *pp;    */
/* sets *type to T_INT or T_NBR and writes the value into the appropriate    */
/* output.  Mirrors the integer/float decision logic in getvalue().            */
static int parse_numeric(unsigned char **pp, int *type, MMFLOAT *fout, int64_t *iout)
{
    unsigned char *p = *pp;
    skipspace(p);
    unsigned char c = *p;

    if (c == '&')
    {
        /* based integer constant */
        p++;
        unsigned char base = mytoupper(*p++);
        int64_t v = 0;
        if (base == 'H')
        {
            while (isxdigit(*p))
            {
                unsigned char ch = *p++;
                int d = (ch >= 'A' && ch <= 'F')   ? ch - 'A' + 10
                        : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
                                                   : ch - '0';
                v = (v << 4) | d;
            }
        }
        else if (base == 'O')
        {
            while (*p >= '0' && *p <= '7')
                v = (v << 3) | (*p++ - '0');
        }
        else if (base == 'B')
        {
            while (*p == '0' || *p == '1')
                v = (v << 1) | (*p++ - '0');
        }
        else
            return 0;
        *type = T_INT;
        *iout = v;
        *pp = p;
        return 1;
    }

    if (!(c == '.' || (c >= '0' && c <= '9')))
        return 0;

    /* gather chars into a temp buffer for strtod                             */
    char buf[40];
    int n = 0;
    int seen_dot = 0, seen_exp = 0;

    while (n < (int)sizeof(buf) - 1)
    {
        c = *p;
        if (c >= '0' && c <= '9')
        {
            buf[n++] = c;
            p++;
        }
        else if (c == '.' && !seen_dot && !seen_exp)
        {
            seen_dot = 1;
            buf[n++] = c;
            p++;
        }
        else if ((c == 'e' || c == 'E') && !seen_exp)
        {
            seen_exp = 1;
            seen_dot = 1; /* exp form forces float                       */
            buf[n++] = c;
            p++;
            if (*p == '+' || *p == '-')
            {
                buf[n++] = *p++;
            }
        }
        else
            break;
    }
    buf[n] = 0;
    if (n == 0)
        return 0;

    if (!seen_dot)
    {
        /* pure integer literal */
        int64_t v = 0;
        for (int i = 0; i < n; i++)
            v = v * 10 + (buf[i] - '0');
        *type = T_INT;
        *iout = v;
    }
    else
    {
        *type = T_NBR;
        *fout = (MMFLOAT)strtod(buf, NULL);
    }
    *pp = p;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Recursive-descent precedence parser                                        */
/*                                                                            */
/* Grammar:                                                                   */
/*   expr(prec)  = unary { binop expr(...) }                                  */
/*   unary       = '-' unary | primary                                        */
/*   primary     = '(' expr(7) ')' | numeric | intrinsic '(' expr(7) ')'      */
/*               | scalar-var                                                 */
/*                                                                            */
/* MMBasic precedence (lower number = binds tighter):                         */
/*   0  ^                                                                     */
/*   1  *  /                                                                  */
/*   2  +  -                                                                  */
/*   (others: \, MOD, shifts, comparisons, AND/OR/XOR -- not yet supported)   */
/*                                                                            */
/* All operators except ^ are left-associative; ^ is right-associative.       */
/* Compile-time type tracking pushes type-conversion ops as needed so the     */
/* replayer can be a straight-line stack VM.                                  */
/* ------------------------------------------------------------------------- */

/* Compile-time scratch context.  Lives on the stack of compile_let; the
 * results (ops, lnames) are memcpy'd into the cache_entry on success.       */
struct compile_ctx
{
    struct trace_op ops[TRACE_MAX_OPS];
    int nops;
    int has_locals;
    int n_lnames;
    uint8_t lname_lens[TRACE_MAX_LVARS];
    unsigned char lnames[TRACE_MAX_LVARS][MAXVARLEN];
};

/* Find or add an upper-cased local-name in the pool; returns 0..n-1 index,
 * or -1 if the pool is full.                                                 */
static int lname_intern(struct compile_ctx *cx, unsigned char *up, int ulen)
{
    for (int i = 0; i < cx->n_lnames; i++)
    {
        if (cx->lname_lens[i] != ulen)
            continue;
        int j = 0;
        while (j < ulen && cx->lnames[i][j] == up[j])
            j++;
        if (j == ulen)
            return i;
    }
    if (cx->n_lnames >= TRACE_MAX_LVARS)
        return -1;
    int idx = cx->n_lnames++;
    cx->lname_lens[idx] = (uint8_t)ulen;
    memcpy(cx->lnames[idx], up, ulen);
    return idx;
}

/* Forward declaration; expr/unary/primary recurse. */
static int compile_expr(unsigned char **pp, struct compile_ctx *cx,
                        int max_prec);

/* Look up the binop info for token byte c.  Returns 1 and fills out_prec
 * (0..2) and the (NBR, INT) opcodes if c is one of our supported ops, else 0.
 * Operators that yield only NBR (like '/' and '^') write OP_END into op_int.  */
static int classify_binop(unsigned char c,
                          int *out_prec,
                          uint8_t *op_nbr, uint8_t *op_int)
{
    if (c < C_BASETOKEN)
        return 0;
    void (*f)(void) = tokenfunction(c);
    if (f == op_add)
    {
        *out_prec = 2;
        *op_nbr = OP_ADD_NBR;
        *op_int = OP_ADD_INT;
        return 1;
    }
    if (f == op_subtract)
    {
        *out_prec = 2;
        *op_nbr = OP_SUB_NBR;
        *op_int = OP_SUB_INT;
        return 1;
    }
    if (f == op_mul)
    {
        *out_prec = 1;
        *op_nbr = OP_MUL_NBR;
        *op_int = OP_MUL_INT;
        return 1;
    }
    if (f == op_div)
    {
        *out_prec = 1;
        *op_nbr = OP_DIV_NBR;
        *op_int = OP_DIV_NBR; /* always NBR */
        return 1;
    }
    if (f == op_exp)
    {
        *out_prec = 0;
        *op_nbr = OP_EXP_NBR;
        *op_int = OP_EXP_NBR; /* always NBR */
        return 1;
    }
    if (f == op_mod)
    {
        /* INT-only operator; both NBR sides are coerced via FloatToInt64.   */
        *out_prec = 1;
        *op_nbr = OP_MOD_INT;
        *op_int = OP_MOD_INT;
        return 1;
    }
    if (f == op_divint)
    {
        /* INT-only \\ operator; same precedence as * /; both sides coerced. */
        *out_prec = 1;
        *op_nbr = OP_DIVINT_INT;
        *op_int = OP_DIVINT_INT;
        return 1;
    }
    /* Comparison operators — prec 5 except = which is prec 6.
     * All produce T_INT; emit_binop handles the type path.                  */
    if (f == op_lt)
    {
        *out_prec = 5;
        *op_nbr = OP_LT_NBR;
        *op_int = OP_LT_INT;
        return 1;
    }
    if (f == op_gt)
    {
        *out_prec = 5;
        *op_nbr = OP_GT_NBR;
        *op_int = OP_GT_INT;
        return 1;
    }
    if (f == op_lte)
    {
        *out_prec = 5;
        *op_nbr = OP_LTE_NBR;
        *op_int = OP_LTE_INT;
        return 1;
    }
    if (f == op_gte)
    {
        *out_prec = 5;
        *op_nbr = OP_GTE_NBR;
        *op_int = OP_GTE_INT;
        return 1;
    }
    if (f == op_ne)
    {
        *out_prec = 5;
        *op_nbr = OP_NE_NBR;
        *op_int = OP_NE_INT;
        return 1;
    }
    if (f == op_equal)
    {
        *out_prec = 6;
        *op_nbr = OP_EQ_NBR;
        *op_int = OP_EQ_INT;
        return 1;
    }
    /* Logical/bitwise — prec 7; all INT-only so op_nbr == op_int.           */
    if (f == op_and)
    {
        *out_prec = 7;
        *op_nbr = OP_AND_INT;
        *op_int = OP_AND_INT;
        return 1;
    }
    if (f == op_or)
    {
        *out_prec = 7;
        *op_nbr = OP_OR_INT;
        *op_int = OP_OR_INT;
        return 1;
    }
    if (f == op_xor)
    {
        *out_prec = 7;
        *op_nbr = OP_XOR_INT;
        *op_int = OP_XOR_INT;
        return 1;
    }
    /* Bit-shift — prec 4; INT-only.                                          */
    if (f == op_shiftleft)
    {
        *out_prec = 4;
        *op_nbr = OP_SHL_INT;
        *op_int = OP_SHL_INT;
        return 1;
    }
    if (f == op_shiftright)
    {
        *out_prec = 4;
        *op_nbr = OP_SHR_INT;
        *op_int = OP_SHR_INT;
        return 1;
    }
    return 0;
}

/* If c is a token byte for a 1-arg intrinsic we support, return the OP_xxx
 * for it and write the input/output type info.  Else return 0.
 * in_type:  0 = NBR, 1 = INT, 2 = either (preserves)
 * out_type: 0 = NBR, 1 = INT, 2 = same-as-input                              */
static int classify_intrinsic(unsigned char c, uint8_t *opcode,
                              int *in_type, int *out_type)
{
    if (c < C_BASETOKEN)
        return 0;
    void (*f)(void) = tokenfunction(c);
    /* Trig: input NBR (radians), output NBR.  Replayer bails on optionangle. */
    if (f == fun_sin)
    {
        *opcode = OP_SIN;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_cos)
    {
        *opcode = OP_COS;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_tan)
    {
        *opcode = OP_TAN;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_asin)
    {
        *opcode = OP_ASIN;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_acos)
    {
        *opcode = OP_ACOS;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_atn)
    {
        *opcode = OP_ATAN;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_sqr)
    {
        *opcode = OP_SQR;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_abs)
    {
        /* Type-preserving; emit decided at compile time.                    */
        *opcode = OP_ABS_NBR; /* placeholder - real opcode picked by caller  */
        *in_type = 2;
        *out_type = 2;
        return 1;
    }
    if (f == fun_int)
    {
        *opcode = OP_INT_OF;
        *in_type = 0;
        *out_type = 1;
        return 1;
    }
    if (f == fun_exp)
    {
        *opcode = OP_EXP;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_log)
    {
        *opcode = OP_LOG;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_sgn)
    {
        *opcode = OP_SGN;
        *in_type = 0;
        *out_type = 1;
        return 1;
    }
    if (f == fun_fix)
    {
        *opcode = OP_FIX;
        *in_type = 0;
        *out_type = 1;
        return 1;
    }
    if (f == fun_cint)
    {
        *opcode = OP_CINT;
        *in_type = 0;
        *out_type = 1;
        return 1;
    }
    if (f == fun_deg)
    {
        *opcode = OP_DEG;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    if (f == fun_rad)
    {
        *opcode = OP_RAD;
        *in_type = 0;
        *out_type = 0;
        return 1;
    }
    return 0;
}

/* If c is a token byte for a supported 2-arg intrinsic, write its opcode
 * and return 1.  Both arguments are always coerced to T_NBR by the caller. */
static int classify_intrinsic2(unsigned char c, uint8_t *opcode)
{
    if (c < C_BASETOKEN)
        return 0;
    void (*f)(void) = tokenfunction(c);
    if (f == fun_atan2)
    {
        *opcode = OP_ATAN2;
        return 1;
    }
    if (f == fun_max)
    {
        *opcode = OP_MAX_NBR;
        return 1;
    }
    if (f == fun_min)
    {
        *opcode = OP_MIN_NBR;
        return 1;
    }
    return 0;
}

/* Emit a load for the given scalar reference. */
static int emit_load_scalar(unsigned char *namebuf, unsigned char suffix,
                            struct compile_ctx *cx)
{
    int t, idx, ulen;
    unsigned char up[MAXVARLEN];
    MMFLOAT cf = 0.0;
    int64_t ci = 0;
    int scope = resolve_scalar(namebuf, suffix, &t, &idx, up, &ulen, &cf, &ci);
    if (scope == SCOPE_NONE)
        return 0;
    if (cx->nops >= TRACE_MAX_OPS)
        return 0;
    struct trace_op *op = &cx->ops[cx->nops++];
    op->opcode = 0;
    op->lname_idx = 0;
    op->varindex = idx;
    if (scope == SCOPE_CONST)
    {
        /* T_CONST global -> bake value in as an inline constant.            */
        if (t == T_NBR)
        {
            op->opcode = OP_LOAD_CONST_NBR;
            op->u.fconst = cf;
        }
        else
        {
            op->opcode = OP_LOAD_CONST_INT;
            op->u.iconst = ci;
        }
    }
    else if (scope == SCOPE_GLOBAL)
        op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_GVAR_NBR : OP_LOAD_GVAR_INT);
    else
    {
        int li = lname_intern(cx, up, ulen);
        if (li < 0)
            return 0;
        if (scope == SCOPE_LOCAL_PTR)
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_LVAR_NBR_PTR : OP_LOAD_LVAR_INT_PTR);
        else
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_LVAR_NBR : OP_LOAD_LVAR_INT);
        op->lname_idx = (uint8_t)li;
        cx->has_locals = 1;
    }
    return t;
}

/* Emit a 1-D array element load.  *pp points just past the array name; we
 * consume '(', the index expression, and the matching ')'.  Returns the
 * element type (T_NBR or T_INT) on success, 0 on any failure (caller bails).
 * The supplied namebuf/suffix have already been read by compile_primary.    */
static int emit_load_array_1d(unsigned char *namebuf, unsigned char suffix,
                              unsigned char **pp, struct compile_ctx *cx)
{
    int t, idx, ulen, ndim;
    unsigned char up[MAXVARLEN];
    int scope = resolve_array_nd(namebuf, suffix, &t, &idx, &ndim, up, &ulen);
    if (scope == SCOPE_NONE)
        return 0;

    /* '(' index [, index] ')' */
    unsigned char *p = *pp;
    skipspace(p);
    if (*p != '(')
        return 0;
    p++;
    int idx_t = compile_expr(&p, cx, 7);
    if (!idx_t)
        return 0;
    if (idx_t == T_NBR)
    {
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = OP_TOS_NBR_TO_INT;
    }
    skipspace(p);

    int got_dims = 1;
    if (*p == ',')
    {
        p++;
        if (ndim != 2)
            return 0; /* variable is 1-D but source supplied 2 indices */
        int idx2_t = compile_expr(&p, cx, 7);
        if (!idx2_t)
            return 0;
        if (idx2_t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS_NBR_TO_INT;
        }
        skipspace(p);
        got_dims = 2;
    }
    if (*p != ')')
        return 0;
    p++;
    if (got_dims != ndim)
        return 0; /* dim count mismatch -> bail to interpreter */

    /* Emit the load.                                                        */
    if (cx->nops >= TRACE_MAX_OPS)
        return 0;
    struct trace_op *op = &cx->ops[cx->nops++];
    op->opcode = 0;
    op->lname_idx = 0;
    op->varindex = idx;
    if (scope == SCOPE_GLOBAL_ARR)
    {
        if (ndim == 1)
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_GVAR_NBR_AIDX1
                                                : OP_LOAD_GVAR_INT_AIDX1);
        else
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_GVAR_NBR_AIDX2
                                                : OP_LOAD_GVAR_INT_AIDX2);
    }
    else /* SCOPE_LOCAL_ARR */
    {
        int li = lname_intern(cx, up, ulen);
        if (li < 0)
            return 0;
        if (ndim == 1)
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_LVAR_NBR_AIDX1
                                                : OP_LOAD_LVAR_INT_AIDX1);
        else
            op->opcode = (uint8_t)((t == T_NBR) ? OP_LOAD_LVAR_NBR_AIDX2
                                                : OP_LOAD_LVAR_INT_AIDX2);
        op->lname_idx = (uint8_t)li;
        cx->has_locals = 1;
    }
    *pp = p;
    return t;
}

/* Parse a primary: '(' expr ')', constant, intrinsic call, or scalar.
 * Returns the type pushed (T_NBR or T_INT), or 0 on failure.                  */
static int compile_primary(unsigned char **pp, struct compile_ctx *cx)
{
    unsigned char *p = *pp;
    skipspace(p);
    unsigned char c = *p;

    /* parenthesised sub-expression */
    if (c == '(')
    {
        p++;
        int t = compile_expr(&p, cx, 7);
        if (!t)
            return 0;
        skipspace(p);
        if (*p != ')')
            return 0;
        p++;
        *pp = p;
        return t;
    }

    /* numeric literal (or based literal &Hxx) */
    if (c == '.' || c == '&' || (c >= '0' && c <= '9'))
    {
        int t;
        MMFLOAT f = 0.0;
        int64_t iv = 0;
        if (!parse_numeric(&p, &t, &f, &iv))
            return 0;
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        struct trace_op *op = &cx->ops[cx->nops++];
        op->opcode = 0;
        op->lname_idx = 0;
        if (t == T_NBR)
        {
            op->opcode = OP_LOAD_CONST_NBR;
            op->u.fconst = f;
        }
        else
        {
            op->opcode = OP_LOAD_CONST_INT;
            op->u.iconst = iv;
        }
        *pp = p;
        return t;
    }

    /* Token-byte cases: intrinsic call.                                      */
    if (c >= C_BASETOKEN)
    {
        uint8_t op_intrin;
        int in_t, out_t;
        if (classify_intrinsic(c, &op_intrin, &in_t, &out_t))
        {
            p++; /* token includes '(' */
            int arg_t = compile_expr(&p, cx, 7);
            if (!arg_t)
                return 0;
            skipspace(p);
            if (*p != ')')
                return 0;
            p++;

            /* Coerce TOS to required input type, if required. */
            if (in_t == 0 && arg_t == T_INT)
            {
                if (cx->nops >= TRACE_MAX_OPS)
                    return 0;
                cx->ops[cx->nops++].opcode = OP_TOS_INT_TO_NBR;
                arg_t = T_NBR;
            }
            else if (in_t == 1 && arg_t == T_NBR)
            {
                /* Truncating NBR -> INT: not supported; bail.               */
                return 0;
            }

            /* Pick the right opcode for type-preserving Abs */
            uint8_t real_op = op_intrin;
            int result_t;
            if (op_intrin == OP_ABS_NBR)
            {
                real_op = (arg_t == T_NBR) ? OP_ABS_NBR : OP_ABS_INT;
                result_t = arg_t;
            }
            else
            {
                result_t = (out_t == 0) ? T_NBR : (out_t == 1) ? T_INT
                                                               : arg_t;
            }
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = real_op;
            *pp = p;
            return result_t;
        }
        /* 2-arg intrinsics: ATAN2, MAX, MIN */
        uint8_t op2_intrin;
        if (classify_intrinsic2(c, &op2_intrin))
        {
            p++; /* skip token byte, which includes '(' */

            /* compile first argument */
            int arg1_t = compile_expr(&p, cx, 7);
            if (!arg1_t)
                return 0;
            if (arg1_t == T_INT)
            {
                if (cx->nops >= TRACE_MAX_OPS)
                    return 0;
                cx->ops[cx->nops++].opcode = OP_TOS_INT_TO_NBR;
            }

            skipspace(p);
            if (*p != ',')
                return 0;
            p++;

            /* compile second argument */
            int arg2_t = compile_expr(&p, cx, 7);
            if (!arg2_t)
                return 0;
            if (arg2_t == T_INT)
            {
                if (cx->nops >= TRACE_MAX_OPS)
                    return 0;
                cx->ops[cx->nops++].opcode = OP_TOS_INT_TO_NBR;
            }

            skipspace(p);
            if (*p != ')')
                return 0; /* 3+-arg MAX/MIN: bail, let interpreter handle */
            p++;

            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = op2_intrin;
            *pp = p;
            return T_NBR;
        }

        /* Zero-argument numeric constants (T_FNA | T_NBR with no side effects).
         * Folded to OP_LOAD_CONST_NBR at compile time so no new opcode is
         * needed and the replayer handles them for free.                      */
        if ((tokentype(c) & (T_FNA | T_NBR)) == (T_FNA | T_NBR))
        {
            void (*f)(void) = tokenfunction(c);
            MMFLOAT cval;
            if (f == fun_pi)
                cval = M_PI;
            else
                return 0; /* other T_FNA functions may have side effects */
            p++;
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            struct trace_op *op = &cx->ops[cx->nops++];
            op->opcode = OP_LOAD_CONST_NBR;
            op->lname_idx = 0;
            op->u.fconst = cval;
            *pp = p;
            return T_NBR;
        }

        /* Unknown token here (could be ')' or a non-supported function) */
        return 0;
    }

    /* Scalar or 1-D array variable reference */
    if (isnamestart(c))
    {
        unsigned char namebuf[MAXVARLEN + 1], suffix;
        if (!read_name(p, &p, namebuf, sizeof(namebuf), &suffix))
            return 0;
        /* Peek past whitespace for '(' to dispatch array vs scalar.        */
        unsigned char *q = p;
        while (*q == ' ')
            q++;
        int t;
        if (*q == '(')
        {
            t = emit_load_array_1d(namebuf, suffix, &p, cx);
        }
        else
        {
            t = emit_load_scalar(namebuf, suffix, cx);
        }
        if (!t)
            return 0;
        *pp = p;
        return t;
    }

    return 0;
}

/* Parse a unary expression: optional '-' or 'Not' followed by primary.      */
static int compile_unary(unsigned char **pp, struct compile_ctx *cx)
{
    unsigned char *p = *pp;
    skipspace(p);
    unsigned char c = *p;
    if (c >= C_BASETOKEN && tokenfunction(c) == op_subtract)
    {
        p++;
        int t = compile_unary(&p, cx);
        if (!t)
            return 0;
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = (uint8_t)((t == T_NBR) ? OP_NEG_NBR : OP_NEG_INT);
        *pp = p;
        return t;
    }
    if (c >= C_BASETOKEN && tokenfunction(c) == op_not)
    {
        p++;
        int t = compile_unary(&p, cx);
        if (!t)
            return 0;
        /* Not requires INT; coerce NBR operand first */
        if (t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS_NBR_TO_INT;
        }
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = OP_NOT_INT;
        *pp = p;
        return T_INT;
    }
    int t = compile_primary(&p, cx);
    *pp = p;
    return t;
}

/* True when op_n is a comparison opcode (NBR variant) — result is always T_INT. */
#define IS_CMP_OP(o) ((o) == OP_EQ_NBR || (o) == OP_NE_NBR || \
                      (o) == OP_LT_NBR || (o) == OP_GT_NBR || \
                      (o) == OP_LTE_NBR || (o) == OP_GTE_NBR)

/* True when both op_n/op_i are INT-only and share the same code (And/Or/Xor/Shl/Shr). */
#define IS_INT_ONLY_LOGIC(o) ((o) == OP_AND_INT || (o) == OP_OR_INT ||  \
                              (o) == OP_XOR_INT || (o) == OP_SHL_INT || \
                              (o) == OP_SHR_INT)

/* Emit a binop with promotion.  TOS-1 has type lhs_t, TOS has type rhs_t.
 * Returns the result type, or 0 on failure.                                   */
static int emit_binop(struct compile_ctx *cx,
                      int lhs_t, int rhs_t, uint8_t op_n, uint8_t op_i)
{
    /* Comparison operators: result is T_INT regardless of operand types.
     * If either operand is NBR, promote both to NBR then emit the NBR variant;
     * otherwise both are INT and we emit the INT variant directly.           */
    if (IS_CMP_OP(op_n))
    {
        if (lhs_t == T_NBR || rhs_t == T_NBR)
        {
            if (rhs_t == T_INT)
            {
                if (cx->nops >= TRACE_MAX_OPS)
                    return 0;
                cx->ops[cx->nops++].opcode = OP_TOS_INT_TO_NBR;
            }
            if (lhs_t == T_INT)
            {
                if (cx->nops >= TRACE_MAX_OPS)
                    return 0;
                cx->ops[cx->nops++].opcode = OP_TOS1_INT_TO_NBR;
            }
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = op_n; /* OP_xx_NBR */
        }
        else
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = op_i; /* OP_xx_INT */
        }
        return T_INT;
    }

    /* And / Or / Xor / Shl / Shr: INT-only, coerce both operands if needed. */
    if (IS_INT_ONLY_LOGIC(op_n))
    {
        if (rhs_t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS_NBR_TO_INT;
        }
        if (lhs_t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS1_NBR_TO_INT;
        }
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = op_n;
        return T_INT;
    }

    /* Mod is INT-only: coerce both operands to INT, emit, return T_INT.    */
    if (op_n == OP_MOD_INT || op_n == OP_DIVINT_INT)
    {
        if (rhs_t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS_NBR_TO_INT;
        }
        if (lhs_t == T_NBR)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS1_NBR_TO_INT;
        }
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = op_n;
        return T_INT;
    }
    int force_nbr = (op_n == OP_DIV_NBR) || (op_n == OP_EXP_NBR) ||
                    (lhs_t == T_NBR) || (rhs_t == T_NBR);
    if (force_nbr)
    {
        if (rhs_t == T_INT)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS_INT_TO_NBR;
        }
        if (lhs_t == T_INT)
        {
            if (cx->nops >= TRACE_MAX_OPS)
                return 0;
            cx->ops[cx->nops++].opcode = OP_TOS1_INT_TO_NBR;
        }
        if (cx->nops >= TRACE_MAX_OPS)
            return 0;
        cx->ops[cx->nops++].opcode = op_n;
        return T_NBR;
    }
    if (cx->nops >= TRACE_MAX_OPS)
        return 0;
    cx->ops[cx->nops++].opcode = op_i;
    return T_INT;
}

/* Parse an expression, consuming binops while their precedence (lower-is-
 * tighter) is <= max_prec.  Returns the result type, or 0 on failure.         */
static int compile_expr(unsigned char **pp, struct compile_ctx *cx,
                        int max_prec)
{
    unsigned char *p = *pp;
    int lhs_t = compile_unary(&p, cx);
    if (!lhs_t)
        return 0;

    while (1)
    {
        skipspace(p);
        unsigned char c = *p;
        if (is_stmt_end(c))
            break;
        int prec;
        uint8_t op_n, op_i;
        if (!classify_binop(c, &prec, &op_n, &op_i))
            break;
        if (prec > max_prec)
            break;
        p++;

        /* Right-associative for ^ (allow same prec on RHS); else strict.    */
        int next_max = (op_n == OP_EXP_NBR) ? prec : (prec - 1);

        int rhs_t = compile_expr(&p, cx, next_max);
        if (!rhs_t)
            return 0;

        lhs_t = emit_binop(cx, lhs_t, rhs_t, op_n, op_i);
        if (!lhs_t)
            return 0;
    }
    *pp = p;
    return lhs_t;
}

/* Compile cmdline (LHS = RHS) into entry ops.  Returns 1 on success, 0 if
 * the statement does not fit our supported subset (caller marks BAD).        */
/* Compile the string RHS of a string LET.  Called from compile_let when the
 * LHS is a string scalar.  p points just past the LHS name (before whitespace
 * and '=').  Fills cx with ops and returns 1 on success, 0 to bail.
 *
 * Supported forms:
 *   s$ = "literal"
 *   s$ = t$
 *   s$ = s$ + "literal"    (in-place append; LHS must be the left operand)
 *   s$ = s$ + t$            (in-place append; LHS must be the left operand)  */
static int compile_str_rhs(unsigned char *p, struct compile_ctx *cx,
                           int lhs_scope, int lhs_idx,
                           unsigned char *lhs_up, int lhs_ulen)
{
    /* --- '=' ---------------------------------------------------------------- */
    skipspace(p);
    if (*p < C_BASETOKEN || tokenfunction(*p) != op_equal)
        return 0;
    p++;
    skipspace(p);

    /* --- First primary ------------------------------------------------------- */
    int rhs1_is_lit = 0;
    unsigned char *rhs1_lit_ptr = NULL;
    int rhs1_idx = -1, rhs1_scope = SCOPE_NONE;
    unsigned char rhs1_up[MAXVARLEN];
    int rhs1_ulen = 0;

    if (*p == '"')
    {
        if (OptionEscape)
            return 0;
        rhs1_is_lit = 1;
        rhs1_lit_ptr = p;
        p++;
        while (*p && *p != '"')
            p++;
        if (*p != '"')
            return 0;
        p++;
    }
    else if (isnamestart(*p))
    {
        unsigned char rhs1_name[MAXVARLEN + 1];
        unsigned char rhs1_sfx;
        if (!read_name(p, &p, rhs1_name, sizeof(rhs1_name), &rhs1_sfx))
            return 0;
        if (rhs1_sfx != '$' && rhs1_sfx != 0)
            return 0;
        rhs1_scope = resolve_str_scalar(rhs1_name, &rhs1_idx, rhs1_up, &rhs1_ulen);
        if (rhs1_scope == SCOPE_NONE)
            return 0;
    }
    else
        return 0;

    skipspace(p);

    /* --- Optional '+' and second primary ------------------------------------ */
    int has_plus = 0;
    int rhs2_is_lit = 0;
    unsigned char *rhs2_lit_ptr = NULL;
    int rhs2_idx = -1, rhs2_scope = SCOPE_NONE;
    unsigned char rhs2_up[MAXVARLEN];
    int rhs2_ulen = 0;

    if (*p >= C_BASETOKEN && tokenfunction(*p) == op_add)
    {
        /* Only allow in-place append: left operand must be the same var as LHS */
        if (rhs1_is_lit)
            return 0;
        if (rhs1_idx != lhs_idx)
            return 0;
        has_plus = 1;
        p++;
        skipspace(p);
        if (*p == '"')
        {
            if (OptionEscape)
                return 0;
            rhs2_is_lit = 1;
            rhs2_lit_ptr = p;
            p++;
            while (*p && *p != '"')
                p++;
            if (*p != '"')
                return 0;
            p++;
        }
        else if (isnamestart(*p))
        {
            unsigned char rhs2_name[MAXVARLEN + 1];
            unsigned char rhs2_sfx;
            if (!read_name(p, &p, rhs2_name, sizeof(rhs2_name), &rhs2_sfx))
                return 0;
            if (rhs2_sfx != '$' && rhs2_sfx != 0)
                return 0;
            rhs2_scope = resolve_str_scalar(rhs2_name, &rhs2_idx, rhs2_up, &rhs2_ulen);
            if (rhs2_scope == SCOPE_NONE)
                return 0;
        }
        else
            return 0;
    }

    skipspace(p);
    if (!is_stmt_end(*p))
        return 0;

    /* --- Emit ops ----------------------------------------------------------- */
    if (!has_plus)
    {
        /* s$ = "lit"  or  s$ = t$ */
        if (cx->nops + 2 > TRACE_MAX_OPS)
            return 0;
        if (rhs1_is_lit)
        {
            cx->ops[cx->nops].opcode = OP_PUSH_STR_LIT;
            cx->ops[cx->nops].lname_idx = 0;
            cx->ops[cx->nops].varindex = 0;
            cx->ops[cx->nops].u.iconst = (int64_t)(uintptr_t)rhs1_lit_ptr;
            cx->nops++;
        }
        else
        {
            cx->ops[cx->nops].opcode = (rhs1_scope == SCOPE_LOCAL) ? OP_LOAD_LVAR_STR : OP_LOAD_GVAR_STR;
            cx->ops[cx->nops].varindex = rhs1_idx;
            cx->ops[cx->nops].lname_idx = 0;
            cx->ops[cx->nops].u.iconst = 0;
            if (rhs1_scope == SCOPE_LOCAL)
            {
                int li = lname_intern(cx, rhs1_up, rhs1_ulen);
                if (li < 0)
                    return 0;
                cx->ops[cx->nops].lname_idx = (uint8_t)li;
            }
            cx->nops++;
        }
        cx->ops[cx->nops].opcode = (lhs_scope == SCOPE_LOCAL) ? OP_STORE_LVAR_STR : OP_STORE_GVAR_STR;
        cx->ops[cx->nops].varindex = lhs_idx;
        cx->ops[cx->nops].lname_idx = 0;
        cx->ops[cx->nops].u.iconst = 0;
        if (lhs_scope == SCOPE_LOCAL)
        {
            int li = lname_intern(cx, lhs_up, lhs_ulen);
            if (li < 0)
                return 0;
            cx->ops[cx->nops].lname_idx = (uint8_t)li;
        }
        cx->nops++;
    }
    else
    {
        /* s$ = s$ + "lit"  or  s$ = s$ + t$ */
        if (cx->nops + 2 > TRACE_MAX_OPS)
            return 0;
        if (rhs2_is_lit)
        {
            cx->ops[cx->nops].opcode = OP_PUSH_STR_LIT;
            cx->ops[cx->nops].lname_idx = 0;
            cx->ops[cx->nops].varindex = 0;
            cx->ops[cx->nops].u.iconst = (int64_t)(uintptr_t)rhs2_lit_ptr;
            cx->nops++;
        }
        else
        {
            cx->ops[cx->nops].opcode = (rhs2_scope == SCOPE_LOCAL) ? OP_LOAD_LVAR_STR : OP_LOAD_GVAR_STR;
            cx->ops[cx->nops].varindex = rhs2_idx;
            cx->ops[cx->nops].lname_idx = 0;
            cx->ops[cx->nops].u.iconst = 0;
            if (rhs2_scope == SCOPE_LOCAL)
            {
                int li = lname_intern(cx, rhs2_up, rhs2_ulen);
                if (li < 0)
                    return 0;
                cx->ops[cx->nops].lname_idx = (uint8_t)li;
            }
            cx->nops++;
        }
        cx->ops[cx->nops].opcode = (lhs_scope == SCOPE_LOCAL) ? OP_APPEND_INPLACE_LVAR_STR : OP_APPEND_INPLACE_STR;
        cx->ops[cx->nops].varindex = lhs_idx;
        cx->ops[cx->nops].lname_idx = 0;
        cx->ops[cx->nops].u.iconst = 0;
        if (lhs_scope == SCOPE_LOCAL)
        {
            int li = lname_intern(cx, lhs_up, lhs_ulen);
            if (li < 0)
                return 0;
            cx->ops[cx->nops].lname_idx = (uint8_t)li;
        }
        cx->nops++;
    }

    if (lhs_scope == SCOPE_LOCAL || rhs1_scope == SCOPE_LOCAL || rhs2_scope == SCOPE_LOCAL)
        cx->has_locals = 1;
    return 1;
}

static int compile_let(unsigned char *cmdline, struct cache_entry *e)
{
    unsigned char *p = cmdline;
    struct compile_ctx cx;

    /* Zero only the bookkeeping; ops/lnames will be filled by the parser.   */
    cx.nops = 0;
    cx.has_locals = 0;
    cx.n_lnames = 0;

    /* --- LHS ----------------------------------------------------------- */
    skipspace(p);
    if (!isnamestart(*p))
        return 0;
    unsigned char lhsname[MAXVARLEN + 1], lhssuffix;
    if (!read_name(p, &p, lhsname, sizeof(lhsname), &lhssuffix))
        return 0;

    /* Decide scalar vs array LHS by peeking past whitespace for '('.       */
    int lhs_is_array = 0;
    {
        unsigned char *q = p;
        while (*q == ' ')
            q++;
        if (*q == '(')
            lhs_is_array = 1;
    }

    int lhs_type, lhs_idx, lhs_ulen;
    unsigned char lhs_up[MAXVARLEN];
    int lhs_scope;
    int lhs_ndim = 0; /* set when lhs_is_array */

    /* --- String LHS fast-path ------------------------------------------- */
    if ((lhssuffix == '$' || lhssuffix == 0) && !lhs_is_array)
    {
        int str_idx;
        unsigned char str_up[MAXVARLEN];
        int str_ulen;
        int str_scope = resolve_str_scalar(lhsname, &str_idx, str_up, &str_ulen);
        if (str_scope == SCOPE_NONE)
        {
            /* suffix==0 variable not found as string; fall through to numeric path */
            if (lhssuffix != 0)
                return 0;
            goto numeric_path;
        }
        if (!(g_trace_cache_flags & TCF_LET_STR))
            return 0;
        if (!compile_str_rhs(p, &cx, str_scope, str_idx, str_up, str_ulen))
            return 0;
        /* Commit (mirrors the numeric-path commit below). */
        size_t str_ops_bytes = (size_t)cx.nops * sizeof(struct trace_op);
        size_t str_name_bytes = (size_t)cx.n_lnames * MAXVARLEN;
        size_t str_payload = (str_ops_bytes + str_name_bytes +
                              (sizeof(struct trace_op) - 1)) &
                             ~(sizeof(struct trace_op) - 1);
        if (g_arena_used + str_payload > g_arena_cap)
        {
            g_trace_alloc_fail++;
            g_trace_last_was_full = 1;
            return 0;
        }
        e->payload_off = g_arena_used;
        memcpy(g_arena + g_arena_used, cx.ops, str_ops_bytes);
        if (str_name_bytes)
            memcpy(g_arena + g_arena_used + str_ops_bytes, cx.lnames, str_name_bytes);
        g_arena_used += (uint32_t)str_payload;
        e->n_ops = (uint8_t)cx.nops;
        e->has_locals = (uint8_t)cx.has_locals;
        e->frame_gen = g_local_frame_gen;
        e->n_lnames = (uint8_t)cx.n_lnames;
        if (cx.n_lnames > 0)
            memcpy(e->lname_lens, cx.lname_lens, cx.n_lnames);
        return 1;
    }

numeric_path:
    if (!(g_trace_cache_flags & TCF_LET_NUM))
        return 0;
    if (lhs_is_array)
    {
        lhs_scope = resolve_array_nd(lhsname, lhssuffix, &lhs_type, &lhs_idx,
                                     &lhs_ndim, lhs_up, &lhs_ulen);
        if (lhs_scope == SCOPE_NONE)
            return 0;
        if (lhs_scope == SCOPE_LOCAL_ARR)
            cx.has_locals = 1;

        /* Compile the index expression(s) now so they end up on the stack
         * BELOW the RHS value when the STORE executes.  Stack at store:
         *   1-D: [..., int_i, value]       2-D: [..., int_i, int_j, value] */
        skipspace(p);
        if (*p != '(')
            return 0;
        p++;
        int idx_t = compile_expr(&p, &cx, 7);
        if (!idx_t)
            return 0;
        if (idx_t == T_NBR)
        {
            if (cx.nops >= TRACE_MAX_OPS)
                return 0;
            cx.ops[cx.nops++].opcode = OP_TOS_NBR_TO_INT;
        }
        skipspace(p);
        int got_dims = 1;
        if (*p == ',')
        {
            p++;
            if (lhs_ndim != 2)
                return 0;
            int idx2_t = compile_expr(&p, &cx, 7);
            if (!idx2_t)
                return 0;
            if (idx2_t == T_NBR)
            {
                if (cx.nops >= TRACE_MAX_OPS)
                    return 0;
                cx.ops[cx.nops++].opcode = OP_TOS_NBR_TO_INT;
            }
            skipspace(p);
            got_dims = 2;
        }
        if (*p != ')')
            return 0;
        p++;
        if (got_dims != lhs_ndim)
            return 0;
    }
    else
    {
        MMFLOAT lhs_cf = 0.0;
        int64_t lhs_ci = 0;
        lhs_scope = resolve_scalar(lhsname, lhssuffix, &lhs_type, &lhs_idx,
                                   lhs_up, &lhs_ulen, &lhs_cf, &lhs_ci);
        (void)lhs_cf;
        (void)lhs_ci;

        /* If the LHS scalar doesn't exist yet, the typical cause is the
         * usual BASIC "create on first assignment" idiom (no OPTION
         * EXPLICIT).  Auto-create it now so we can compile this LET on the
         * first visit instead of waiting for the interpreter to run it.
         *
         * Restrictions:
         *  - Top-level only (g_LocalIndex == 0).  Inside a sub, an
         *    auto-created scalar would become a global, but a later LOCAL
         *    declaration (or a LOCAL we haven't reached yet) could shadow
         *    it.  We'd have a stale resolution baked into ops[].  Let the
         *    retry budget cover that case.
         *  - OPTION EXPLICIT off.  When EXPLICIT is on, undeclared use is
         *    an error and we must let the interpreter raise it.
         *  - String LHS not supported by the cache, so don't create slots
         *    we can't use.                                                  */
        if (lhs_scope == SCOPE_NONE && g_LocalIndex == 0 && !OptionExplicit && lhssuffix != '$')
        {
            unsigned char nbuf[MAXVARLEN + 2];
            int nl = 0;
            while (nl < MAXVARLEN && lhsname[nl])
            {
                nbuf[nl] = lhsname[nl];
                nl++;
            }
            if (lhssuffix == '%' || lhssuffix == '!')
                nbuf[nl++] = lhssuffix;
            nbuf[nl] = 0;
            (void)findvar(nbuf, V_FIND); /* creates as default-typed scalar */
            lhs_scope = resolve_scalar(lhsname, lhssuffix, &lhs_type,
                                       &lhs_idx, lhs_up, &lhs_ulen,
                                       &lhs_cf, &lhs_ci);
        }

        if (lhs_scope == SCOPE_NONE)
            return 0;
        if (lhs_scope == SCOPE_CONST)
            return 0; /* cannot store to a constant - let interpreter raise it */
        if (lhs_scope == SCOPE_LOCAL || lhs_scope == SCOPE_LOCAL_PTR)
            cx.has_locals = 1;
    }

    /* --- '=' ----------------------------------------------------------- */
    skipspace(p);
    if (*p < C_BASETOKEN || tokenfunction(*p) != op_equal)
        return 0;
    p++;

    /* --- RHS expression (full precedence allowed) --------------------- */
    int rhs_type = compile_expr(&p, &cx, 7);
    if (!rhs_type)
        return 0;

    skipspace(p);
    if (!is_stmt_end(*p))
        return 0; /* trailing junk -> bail (matches old semantics) */

    /* --- Final assignment-time type coercion --------------------------- */
    if (lhs_type == T_NBR && rhs_type == T_INT)
    {
        if (cx.nops >= TRACE_MAX_OPS)
            return 0;
        cx.ops[cx.nops++].opcode = OP_TOS_INT_TO_NBR;
        rhs_type = T_NBR;
    }
    else if (lhs_type == T_INT && rhs_type == T_NBR)
    {
        /* Assignment-time round-half-away-zero, matching FloatToInt64.       */
        if (cx.nops >= TRACE_MAX_OPS)
            return 0;
        cx.ops[cx.nops++].opcode = OP_TOS_NBR_TO_INT;
        rhs_type = T_INT;
    }

    if (cx.nops >= TRACE_MAX_OPS)
        return 0;
    struct trace_op *st = &cx.ops[cx.nops++];
    st->lname_idx = 0;
    if (lhs_is_array)
    {
        if (lhs_scope == SCOPE_GLOBAL_ARR)
        {
            if (lhs_ndim == 1)
                st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_GVAR_NBR_AIDX1
                                                           : OP_STORE_GVAR_INT_AIDX1);
            else
                st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_GVAR_NBR_AIDX2
                                                           : OP_STORE_GVAR_INT_AIDX2);
        }
        else /* SCOPE_LOCAL_ARR */
        {
            int li = lname_intern(&cx, lhs_up, lhs_ulen);
            if (li < 0)
                return 0;
            if (lhs_ndim == 1)
                st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_LVAR_NBR_AIDX1
                                                           : OP_STORE_LVAR_INT_AIDX1);
            else
                st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_LVAR_NBR_AIDX2
                                                           : OP_STORE_LVAR_INT_AIDX2);
            st->lname_idx = (uint8_t)li;
        }
    }
    else if (lhs_scope == SCOPE_GLOBAL)
        st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_GVAR_NBR : OP_STORE_GVAR_INT);
    else
    {
        int li = lname_intern(&cx, lhs_up, lhs_ulen);
        if (li < 0)
            return 0;
        if (lhs_scope == SCOPE_LOCAL_PTR)
            st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_LVAR_NBR_PTR : OP_STORE_LVAR_INT_PTR);
        else
            st->opcode = (uint8_t)((lhs_type == T_NBR) ? OP_STORE_LVAR_NBR : OP_STORE_LVAR_INT);
        st->lname_idx = (uint8_t)li;
    }
    st->varindex = lhs_idx;

    /* --- Commit: bump-allocate from arena -------------------------------- */
    size_t ops_bytes = (size_t)cx.nops * sizeof(struct trace_op);
    size_t name_bytes = (size_t)cx.n_lnames * MAXVARLEN;
    /* Round up to struct trace_op alignment so the next entry's ops start
     * properly aligned in the arena.                                        */
    size_t payload = (ops_bytes + name_bytes + (sizeof(struct trace_op) - 1)) & ~(sizeof(struct trace_op) - 1);
    if (g_arena_used + payload > g_arena_cap)
    {
        g_trace_alloc_fail++;
        g_trace_last_was_full = 1;
        return 0;
    }
    e->payload_off = g_arena_used;
    memcpy(g_arena + g_arena_used, cx.ops, ops_bytes);
    if (cx.n_lnames > 0)
        memcpy(g_arena + g_arena_used + ops_bytes, cx.lnames, name_bytes);
    g_arena_used += (uint32_t)payload;

    e->n_ops = (uint8_t)cx.nops;
    e->has_locals = (uint8_t)cx.has_locals;
    e->frame_gen = g_local_frame_gen;
    e->n_lnames = (uint8_t)cx.n_lnames;
    if (cx.n_lnames > 0)
        memcpy(e->lname_lens, cx.lname_lens, cx.n_lnames);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Replayer                                                                   */
/* ------------------------------------------------------------------------- */
/* Core replay engine.  Runs all compiled ops in entry e.
 * - LET entries: STORE ops consume the stack; sp == 0 after the loop.
 * - IF entries:  expression result sits on TOS; sp == 1 after the loop.
 *   When out_bool != NULL the TOS value is written as 0/1 on success.
 * Returns 1 on clean execution, 0 if any guard fails (caller marks ST_BAD). */
static int __not_in_flash_func(replay_common)(struct cache_entry *e, int *out_bool)
{
    MMFLOAT fstk[TRACE_MAX_STACK];
    int64_t istk[TRACE_MAX_STACK];
    unsigned char *sstk[TRACE_MAX_STACK]; /* string pointer stack           */
    uint8_t tag[TRACE_MAX_STACK];         /* T_NBR, T_INT, or T_STR         */
    unsigned char lit_buf[STRINGSIZE];    /* scratch for OP_PUSH_STR_LIT   */
    int sp = 0;

    int n = e->n_ops;
    struct trace_op *op = (struct trace_op *)(g_arena + e->payload_off);

    for (int i = 0; i < n; i++, op++)
    {
        switch (op->opcode)
        {
        case OP_LOAD_CONST_NBR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            fstk[sp] = op->u.fconst;
            tag[sp] = T_NBR;
            sp++;
            break;

        case OP_LOAD_CONST_INT:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            istk[sp] = op->u.iconst;
            tag[sp] = T_INT;
            sp++;
            break;

        case OP_LOAD_GVAR_NBR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            fstk[sp] = g_vartbl[op->varindex].val.f;
            tag[sp] = T_NBR;
            sp++;
            break;

        case OP_LOAD_GVAR_INT:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            istk[sp] = g_vartbl[op->varindex].val.i;
            tag[sp] = T_INT;
            sp++;
            break;

        case OP_LOAD_LVAR_NBR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            fstk[sp] = g_vartbl[op->varindex].val.f;
            tag[sp] = T_NBR;
            sp++;
            break;

        case OP_LOAD_LVAR_INT:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            istk[sp] = g_vartbl[op->varindex].val.i;
            tag[sp] = T_INT;
            sp++;
            break;

        case OP_LOAD_LVAR_NBR_PTR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            fstk[sp] = *(MMFLOAT *)g_vartbl[op->varindex].val.s;
            tag[sp] = T_NBR;
            sp++;
            break;

        case OP_LOAD_LVAR_INT_PTR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            istk[sp] = *(long long int *)g_vartbl[op->varindex].val.s;
            tag[sp] = T_INT;
            sp++;
            break;

        /* --- 1-D array element loads.  TOS = INT index, replaced by value. */
        case OP_LOAD_GVAR_NBR_AIDX1:
        case OP_LOAD_LVAR_NBR_AIDX1:
        {
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            int64_t iv = istk[sp - 1];
            int vi = op->varindex;
            int64_t hi = g_vartbl[vi].dims[0];
            if (iv < g_OptionBase || iv > hi)
                return 0; /* let interpreter raise "Index out of bounds" */
            MMFLOAT *base = (MMFLOAT *)g_vartbl[vi].val.s;
            fstk[sp - 1] = base[iv - g_OptionBase];
            tag[sp - 1] = T_NBR;
            break;
        }
        case OP_LOAD_GVAR_INT_AIDX1:
        case OP_LOAD_LVAR_INT_AIDX1:
        {
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            int64_t iv = istk[sp - 1];
            int vi = op->varindex;
            int64_t hi = g_vartbl[vi].dims[0];
            if (iv < g_OptionBase || iv > hi)
                return 0;
            long long int *base = (long long int *)g_vartbl[vi].val.s;
            istk[sp - 1] = base[iv - g_OptionBase];
            tag[sp - 1] = T_INT;
            break;
        }

        /* --- 2-D array element loads.  Stack: [..., int_i, int_j].         */
        case OP_LOAD_GVAR_NBR_AIDX2:
        case OP_LOAD_LVAR_NBR_AIDX2:
        {
            if (sp < 2 || tag[sp - 1] != T_INT || tag[sp - 2] != T_INT)
                return 0;
            int64_t jv = istk[sp - 1];
            int64_t iv = istk[sp - 2];
            int vi = op->varindex;
            int64_t hi0 = g_vartbl[vi].dims[0];
            int64_t hi1 = g_vartbl[vi].dims[1];
            if (iv < g_OptionBase || iv > hi0)
                return 0;
            if (jv < g_OptionBase || jv > hi1)
                return 0;
            int64_t stride = hi0 + 1 - g_OptionBase;
            int64_t lin = (iv - g_OptionBase) + (jv - g_OptionBase) * stride;
            MMFLOAT *base = (MMFLOAT *)g_vartbl[vi].val.s;
            sp--;
            fstk[sp - 1] = base[lin];
            tag[sp - 1] = T_NBR;
            break;
        }
        case OP_LOAD_GVAR_INT_AIDX2:
        case OP_LOAD_LVAR_INT_AIDX2:
        {
            if (sp < 2 || tag[sp - 1] != T_INT || tag[sp - 2] != T_INT)
                return 0;
            int64_t jv = istk[sp - 1];
            int64_t iv = istk[sp - 2];
            int vi = op->varindex;
            int64_t hi0 = g_vartbl[vi].dims[0];
            int64_t hi1 = g_vartbl[vi].dims[1];
            if (iv < g_OptionBase || iv > hi0)
                return 0;
            if (jv < g_OptionBase || jv > hi1)
                return 0;
            int64_t stride = hi0 + 1 - g_OptionBase;
            int64_t lin = (iv - g_OptionBase) + (jv - g_OptionBase) * stride;
            long long int *base = (long long int *)g_vartbl[vi].val.s;
            sp--;
            istk[sp - 1] = base[lin];
            tag[sp - 1] = T_INT;
            break;
        }

        case OP_TOS_INT_TO_NBR:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            fstk[sp - 1] = (MMFLOAT)istk[sp - 1];
            tag[sp - 1] = T_NBR;
            break;

        case OP_TOS1_INT_TO_NBR:
            if (sp < 2 || tag[sp - 2] != T_INT)
                return 0;
            fstk[sp - 2] = (MMFLOAT)istk[sp - 2];
            tag[sp - 2] = T_NBR;
            break;

        case OP_TOS_NBR_TO_INT:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            istk[sp - 1] = FloatToInt64(fstk[sp - 1]);
            tag[sp - 1] = T_INT;
            break;

        case OP_TOS1_NBR_TO_INT:
            if (sp < 2 || tag[sp - 2] != T_NBR)
                return 0;
            istk[sp - 2] = FloatToInt64(fstk[sp - 2]);
            tag[sp - 2] = T_INT;
            break;

        case OP_NEG_NBR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            fstk[sp - 1] = -fstk[sp - 1];
            break;

        case OP_NEG_INT:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            istk[sp - 1] = -istk[sp - 1];
            break;

        case OP_ADD_NBR:
            if (sp < 2)
                return 0;
            fstk[sp - 2] = fstk[sp - 2] + fstk[sp - 1];
            if (fstk[sp - 2] == INFINITY)
                return 0; /* fall back to interpreter for error path */
            tag[sp - 2] = T_NBR;
            sp--;
            break;

        case OP_SUB_NBR:
            if (sp < 2)
                return 0;
            fstk[sp - 2] = fstk[sp - 2] - fstk[sp - 1];
            tag[sp - 2] = T_NBR;
            sp--;
            break;

        case OP_MUL_NBR:
            if (sp < 2)
                return 0;
            fstk[sp - 2] = fstk[sp - 2] * fstk[sp - 1];
            if (fstk[sp - 2] == INFINITY)
                return 0;
            tag[sp - 2] = T_NBR;
            sp--;
            break;

        case OP_DIV_NBR:
            if (sp < 2)
                return 0;
            if (fstk[sp - 1] == 0.0)
                return 0; /* divide-by-zero - let interpreter raise the error */
            fstk[sp - 2] = fstk[sp - 2] / fstk[sp - 1];
            if (fstk[sp - 2] == INFINITY)
                return 0;
            tag[sp - 2] = T_NBR;
            sp--;
            break;

        case OP_EXP_NBR:
            if (sp < 2)
                return 0;
            fstk[sp - 2] = pow(fstk[sp - 2], fstk[sp - 1]);
            if (fstk[sp - 2] != fstk[sp - 2] || fstk[sp - 2] == INFINITY)
                return 0; /* NaN or overflow - let interpreter handle */
            tag[sp - 2] = T_NBR;
            sp--;
            break;

        case OP_ADD_INT:
            if (sp < 2)
                return 0;
            istk[sp - 2] = istk[sp - 2] + istk[sp - 1];
            tag[sp - 2] = T_INT;
            sp--;
            break;

        case OP_SUB_INT:
            if (sp < 2)
                return 0;
            istk[sp - 2] = istk[sp - 2] - istk[sp - 1];
            tag[sp - 2] = T_INT;
            sp--;
            break;

        case OP_MUL_INT:
            if (sp < 2)
                return 0;
            istk[sp - 2] = istk[sp - 2] * istk[sp - 1];
            tag[sp - 2] = T_INT;
            sp--;
            break;

        case OP_MOD_INT:
            if (sp < 2 || tag[sp - 1] != T_INT || tag[sp - 2] != T_INT)
                return 0;
            if (istk[sp - 1] == 0)
                return 0; /* let interpreter raise the divide-by-zero error */
            istk[sp - 2] = istk[sp - 2] % istk[sp - 1];
            tag[sp - 2] = T_INT;
            sp--;
            break;

        case OP_DIVINT_INT:
            if (sp < 2 || tag[sp - 1] != T_INT || tag[sp - 2] != T_INT)
                return 0;
            if (istk[sp - 1] == 0)
                return 0; /* let interpreter raise the divide-by-zero error */
            istk[sp - 2] = istk[sp - 2] / istk[sp - 1];
            tag[sp - 2] = T_INT;
            sp--;
            break;

        /* --- 1-arg intrinsics; replay-time bail on degree mode --------- */
        case OP_SIN:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            fstk[sp - 1] = sin(fstk[sp - 1]);
            break;
        case OP_COS:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            fstk[sp - 1] = cos(fstk[sp - 1]);
            break;
        case OP_TAN:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            fstk[sp - 1] = tan(fstk[sp - 1]);
            break;
        case OP_ASIN:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            if (fstk[sp - 1] < -1.0 || fstk[sp - 1] > 1.0)
                return 0; /* domain error - let interpreter raise it */
            fstk[sp - 1] = asin(fstk[sp - 1]);
            break;
        case OP_ACOS:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            if (fstk[sp - 1] < -1.0 || fstk[sp - 1] > 1.0)
                return 0;
            fstk[sp - 1] = acos(fstk[sp - 1]);
            break;
        case OP_ATAN:
            if (sp < 1 || tag[sp - 1] != T_NBR || useoptionangle)
                return 0;
            fstk[sp - 1] = atan(fstk[sp - 1]);
            break;
        case OP_SQR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            if (fstk[sp - 1] < 0.0)
                return 0; /* "Negative argument" - let interpreter raise */
            fstk[sp - 1] = sqrt(fstk[sp - 1]);
            break;
        case OP_ABS_NBR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            if (fstk[sp - 1] < 0.0)
                fstk[sp - 1] = -fstk[sp - 1];
            break;
        case OP_ABS_INT:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            if (istk[sp - 1] < 0)
                istk[sp - 1] = -istk[sp - 1];
            break;
        case OP_INT_OF:
            /* floor() of NBR -> INT (matches fun_int).                     */
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            istk[sp - 1] = (int64_t)floor(fstk[sp - 1]);
            tag[sp - 1] = T_INT;
            break;
        case OP_EXP:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            fstk[sp - 1] = exp(fstk[sp - 1]);
            break;
        case OP_LOG:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            if (fstk[sp - 1] <= 0.0)
                return 0; /* zero/negative arg — let interpreter raise error */
            fstk[sp - 1] = log(fstk[sp - 1]);
            break;
        case OP_SGN:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            {
                MMFLOAT v = fstk[sp - 1];
                istk[sp - 1] = (v > 0.0) ? 1 : (v < 0.0) ? -1
                                                         : 0;
                tag[sp - 1] = T_INT;
            }
            break;
        case OP_FIX:
            /* truncate toward zero — matches fun_fix C-cast behaviour */
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            istk[sp - 1] = (int64_t)fstk[sp - 1];
            tag[sp - 1] = T_INT;
            break;
        case OP_CINT:
            /* round to nearest — matches FloatToInt64 / fun_cint */
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            {
                MMFLOAT v = fstk[sp - 1];
                if (v < -9.2e18 || v > 9.2e18)
                    return 0; /* overflow — let interpreter raise error */
                istk[sp - 1] = (v >= 0.0) ? (int64_t)(v + 0.5) : (int64_t)(v - 0.5);
                tag[sp - 1] = T_INT;
            }
            break;
        case OP_DEG:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            fstk[sp - 1] *= RADCONV;
            break;
        case OP_RAD:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            fstk[sp - 1] /= RADCONV;
            break;
        case OP_ATAN2:
            if (sp < 2 || tag[sp - 2] != T_NBR || tag[sp - 1] != T_NBR || useoptionangle)
                return 0; /* degree-mode output scaling: let interpreter handle */
            fstk[sp - 2] = atan2(fstk[sp - 2], fstk[sp - 1]);
            sp--;
            break;
        case OP_MAX_NBR:
            if (sp < 2 || tag[sp - 2] != T_NBR || tag[sp - 1] != T_NBR)
                return 0;
            if (fstk[sp - 1] > fstk[sp - 2])
                fstk[sp - 2] = fstk[sp - 1];
            sp--;
            break;
        case OP_MIN_NBR:
            if (sp < 2 || tag[sp - 2] != T_NBR || tag[sp - 1] != T_NBR)
                return 0;
            if (fstk[sp - 1] < fstk[sp - 2])
                fstk[sp - 2] = fstk[sp - 1];
            sp--;
            break;

        case OP_STORE_GVAR_NBR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            g_vartbl[op->varindex].val.f = fstk[--sp];
            break;

        case OP_STORE_GVAR_INT:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            g_vartbl[op->varindex].val.i = istk[--sp];
            break;

        case OP_STORE_LVAR_NBR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            g_vartbl[op->varindex].val.f = fstk[--sp];
            break;

        case OP_STORE_LVAR_INT:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            g_vartbl[op->varindex].val.i = istk[--sp];
            break;

        case OP_STORE_LVAR_NBR_PTR:
            if (sp < 1 || tag[sp - 1] != T_NBR)
                return 0;
            *(MMFLOAT *)g_vartbl[op->varindex].val.s = fstk[--sp];
            break;

        case OP_STORE_LVAR_INT_PTR:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            *(long long int *)g_vartbl[op->varindex].val.s = istk[--sp];
            break;

        /* --- 1-D array element stores.  Stack: [..., int_index, value].   */
        case OP_STORE_GVAR_NBR_AIDX1:
        case OP_STORE_LVAR_NBR_AIDX1:
        {
            if (sp < 2 || tag[sp - 1] != T_NBR || tag[sp - 2] != T_INT)
                return 0;
            int64_t iv = istk[sp - 2];
            int vi = op->varindex;
            int64_t hi = g_vartbl[vi].dims[0];
            if (iv < g_OptionBase || iv > hi)
                return 0;
            MMFLOAT *base = (MMFLOAT *)g_vartbl[vi].val.s;
            base[iv - g_OptionBase] = fstk[sp - 1];
            sp -= 2;
            break;
        }
        case OP_STORE_GVAR_INT_AIDX1:
        case OP_STORE_LVAR_INT_AIDX1:
        {
            if (sp < 2 || tag[sp - 1] != T_INT || tag[sp - 2] != T_INT)
                return 0;
            int64_t iv = istk[sp - 2];
            int vi = op->varindex;
            int64_t hi = g_vartbl[vi].dims[0];
            if (iv < g_OptionBase || iv > hi)
                return 0;
            long long int *base = (long long int *)g_vartbl[vi].val.s;
            base[iv - g_OptionBase] = istk[sp - 1];
            sp -= 2;
            break;
        }

        /* --- 2-D array element stores.  Stack: [..., int_i, int_j, value]. */
        case OP_STORE_GVAR_NBR_AIDX2:
        case OP_STORE_LVAR_NBR_AIDX2:
        {
            if (sp < 3 || tag[sp - 1] != T_NBR ||
                tag[sp - 2] != T_INT || tag[sp - 3] != T_INT)
                return 0;
            int64_t jv = istk[sp - 2];
            int64_t iv = istk[sp - 3];
            int vi = op->varindex;
            int64_t hi0 = g_vartbl[vi].dims[0];
            int64_t hi1 = g_vartbl[vi].dims[1];
            if (iv < g_OptionBase || iv > hi0)
                return 0;
            if (jv < g_OptionBase || jv > hi1)
                return 0;
            int64_t stride = hi0 + 1 - g_OptionBase;
            int64_t lin = (iv - g_OptionBase) + (jv - g_OptionBase) * stride;
            MMFLOAT *base = (MMFLOAT *)g_vartbl[vi].val.s;
            base[lin] = fstk[sp - 1];
            sp -= 3;
            break;
        }
        case OP_STORE_GVAR_INT_AIDX2:
        case OP_STORE_LVAR_INT_AIDX2:
        {
            if (sp < 3 || tag[sp - 1] != T_INT ||
                tag[sp - 2] != T_INT || tag[sp - 3] != T_INT)
                return 0;
            int64_t jv = istk[sp - 2];
            int64_t iv = istk[sp - 3];
            int vi = op->varindex;
            int64_t hi0 = g_vartbl[vi].dims[0];
            int64_t hi1 = g_vartbl[vi].dims[1];
            if (iv < g_OptionBase || iv > hi0)
                return 0;
            if (jv < g_OptionBase || jv > hi1)
                return 0;
            int64_t stride = hi0 + 1 - g_OptionBase;
            int64_t lin = (iv - g_OptionBase) + (jv - g_OptionBase) * stride;
            long long int *base = (long long int *)g_vartbl[vi].val.s;
            base[lin] = istk[sp - 1];
            sp -= 3;
            break;
        }

        /* --- Comparison operators.  Both operands must be same type (emit_binop
         * inserted coercions at compile time).  Result is T_INT (1 or 0).    */
#define CMP_CASE_NBR(OP, expr)                                             \
    case OP:                                                               \
        if (sp < 2 || tag[sp - 2] != T_NBR || tag[sp - 1] != T_NBR)        \
            return 0;                                                      \
        istk[sp - 2] = (expr) ? 1 : 0; /* match op_lt/op_lte etc: C 0/1 */ \
        tag[sp - 2] = T_INT;                                               \
        sp--;                                                              \
        break;
#define CMP_CASE_INT(OP, expr)                                               \
    case OP:                                                                 \
        if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)          \
            return 0;                                                        \
        istk[sp - 2] = (expr) ? 1 : 0; /* match op_ne/op_equal etc: C 0/1 */ \
        sp--;                                                                \
        break;

            CMP_CASE_NBR(OP_EQ_NBR, fstk[sp - 2] == fstk[sp - 1])
            CMP_CASE_INT(OP_EQ_INT, istk[sp - 2] == istk[sp - 1])
            CMP_CASE_NBR(OP_NE_NBR, fstk[sp - 2] != fstk[sp - 1])
            CMP_CASE_INT(OP_NE_INT, istk[sp - 2] != istk[sp - 1])
            CMP_CASE_NBR(OP_LT_NBR, fstk[sp - 2] < fstk[sp - 1])
            CMP_CASE_INT(OP_LT_INT, istk[sp - 2] < istk[sp - 1])
            CMP_CASE_NBR(OP_GT_NBR, fstk[sp - 2] > fstk[sp - 1])
            CMP_CASE_INT(OP_GT_INT, istk[sp - 2] > istk[sp - 1])
            CMP_CASE_NBR(OP_LTE_NBR, fstk[sp - 2] <= fstk[sp - 1])
            CMP_CASE_INT(OP_LTE_INT, istk[sp - 2] <= istk[sp - 1])
            CMP_CASE_NBR(OP_GTE_NBR, fstk[sp - 2] >= fstk[sp - 1])
            CMP_CASE_INT(OP_GTE_INT, istk[sp - 2] >= istk[sp - 1])
#undef CMP_CASE_NBR
#undef CMP_CASE_INT

        /* --- Logical/bitwise ops.  Both operands must be T_INT.            */
        case OP_AND_INT:
            if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)
                return 0;
            istk[sp - 2] = istk[sp - 2] & istk[sp - 1];
            sp--;
            break;
        case OP_OR_INT:
            if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)
                return 0;
            istk[sp - 2] = istk[sp - 2] | istk[sp - 1];
            sp--;
            break;
        case OP_XOR_INT:
            if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)
                return 0;
            istk[sp - 2] = istk[sp - 2] ^ istk[sp - 1];
            sp--;
            break;
        case OP_SHL_INT:
        {
            if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)
                return 0;
            int64_t count = istk[sp - 1];
            if (count < 0 || count >= 64)
                return 0; /* let interpreter handle edge case */
            istk[sp - 2] = (int64_t)((uint64_t)istk[sp - 2] << (unsigned)count);
            sp--;
            break;
        }
        case OP_SHR_INT:
        {
            if (sp < 2 || tag[sp - 2] != T_INT || tag[sp - 1] != T_INT)
                return 0;
            int64_t count = istk[sp - 1];
            if (count < 0 || count >= 64)
                return 0;
            istk[sp - 2] = istk[sp - 2] >> (unsigned)count;
            sp--;
            break;
        }
        case OP_NOT_INT:
            if (sp < 1 || tag[sp - 1] != T_INT)
                return 0;
            istk[sp - 1] = ~istk[sp - 1];
            break;

        /* --- String ops -------------------------------------------------- */
        case OP_PUSH_STR_LIT:
        {
            if (sp >= TRACE_MAX_STACK)
                return 0;
            unsigned char *src = (unsigned char *)(uintptr_t)(uint32_t)op->u.iconst;
            if (*src != '"')
                return 0;
            src++;
            unsigned char *q = src;
            while (*q && *q != '"')
                q++;
            if (*q != '"')
                return 0;
            int slen = (int)(q - src);
            if (slen > MAXSTRLEN)
                return 0;
            lit_buf[0] = (unsigned char)slen;
            memcpy(lit_buf + 1, src, (size_t)slen);
            sstk[sp] = lit_buf;
            tag[sp] = T_STR;
            sp++;
            break;
        }

        case OP_LOAD_GVAR_STR:
        case OP_LOAD_LVAR_STR:
            if (sp >= TRACE_MAX_STACK)
                return 0;
            sstk[sp] = g_vartbl[op->varindex].val.s;
            tag[sp] = T_STR;
            sp++;
            break;

        case OP_STORE_GVAR_STR:
        case OP_STORE_LVAR_STR:
            if (sp < 1 || tag[sp - 1] != T_STR)
                return 0;
            Mstrcpy(g_vartbl[op->varindex].val.s, sstk[--sp]);
            break;

        case OP_APPEND_INPLACE_STR:
        case OP_APPEND_INPLACE_LVAR_STR:
        {
            if (sp < 1 || tag[sp - 1] != T_STR)
                return 0;
            unsigned char *dst = g_vartbl[op->varindex].val.s;
            unsigned char *src = sstk[--sp];
            if ((int)*dst + (int)*src > MAXSTRLEN)
                return 0;
            Mstrcat(dst, src);
            break;
        }

        default:
            return 0;
        }
    }

    /* IF-condition path: exactly one result must remain on the stack.       */
    if (out_bool != NULL)
    {
        if (sp != 1)
            return 0;
        *out_bool = (tag[0] == T_INT) ? (istk[0] != 0) : (fstk[0] != 0.0);
    }
    return 1;
}

/* Thin wrappers so call-sites read clearly. */
static int __not_in_flash_func(replay_let)(struct cache_entry *e) { return replay_common(e, NULL); }
static int __not_in_flash_func(replay_if_cond)(struct cache_entry *e, int *out_bool) { return replay_common(e, out_bool); }

/* ------------------------------------------------------------------------- */
/* Compile IF condition                                                       */
/* ------------------------------------------------------------------------- */
/* Compiles the expression starting at condline (a stable ProgMemory pointer,
 * saved from cmdline before getargs is called in cmd_if).  The expression
 * runs up to tokenTHEN, tokenGOTO, or a statement-end marker.               */
static int compile_if_cond(unsigned char *condline, struct cache_entry *e)
{
    unsigned char *p = condline;
    struct compile_ctx cx;
    cx.nops = 0;
    cx.has_locals = 0;
    cx.n_lnames = 0;

    /* Parse the whole condition at full precedence (7 covers And/Or/Xor).   */
    int rtype = compile_expr(&p, &cx, 7);
    if (!rtype)
        return 0;

    /* Expression must end cleanly at THEN, GOTO, or a stmt delimiter.       */
    skipspace(p);
    if (*p != tokenTHEN && *p != tokenGOTO && !is_stmt_end(*p))
        return 0;

    /* Commit to arena — same pattern as compile_let commit section.         */
    size_t ops_bytes = (size_t)cx.nops * sizeof(struct trace_op);
    size_t name_bytes = (size_t)cx.n_lnames * MAXVARLEN;
    size_t payload = (ops_bytes + name_bytes + (sizeof(struct trace_op) - 1)) & ~(sizeof(struct trace_op) - 1);
    if (g_arena_used + payload > g_arena_cap)
    {
        g_trace_alloc_fail++;
        g_trace_last_was_full = 1;
        return 0;
    }
    e->payload_off = g_arena_used;
    memcpy(g_arena + g_arena_used, cx.ops, ops_bytes);
    if (cx.n_lnames > 0)
        memcpy(g_arena + g_arena_used + ops_bytes, cx.lnames, name_bytes);
    g_arena_used += (uint32_t)payload;

    e->n_ops = (uint8_t)cx.nops;
    e->has_locals = (uint8_t)cx.has_locals;
    e->frame_gen = g_local_frame_gen;
    e->n_lnames = (uint8_t)cx.n_lnames;
    if (cx.n_lnames > 0)
        memcpy(e->lname_lens, cx.lname_lens, cx.n_lnames);
    e->entry_kind = ENTRY_KIND_IF;
    return 1;
}

/* Per-sub opt-in: when g_sub_optin_active is non-zero, only LETs whose
 * enclosing sub (subfun[] index recorded in g_current_sub_idx) is on the
 * opt-in list will be cached/replayed.                                       */
static uint8_t g_sub_optin[MAXSUBFUN];
static uint8_t g_sub_optin_active = 0;

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */
void TraceCacheInit(void)
{
    /* GetMemory cannot be safely called this early - leave g_cache NULL and
     * defer allocation to the first ensure_cache_alloc().                    */
    g_cache = NULL;
    g_local_frame_gen = 0;
    g_trace_replays = 0;
    g_trace_compiles_ok = 0;
    g_trace_compiles_bad = 0;
    g_trace_jump_hits = 0;
}

void TraceCacheReset(void)
{
    /* The MMBasic heap is reset wholesale on NEW / cmd_run start.  Our heap
     * pointer is therefore stale.  Drop it; it will be lazily re-allocated. */
    g_cache = NULL;
    g_arena = NULL;
    g_arena_cap = 0;
    g_arena_used = 0;
    /* Per-sub hit arrays live in the same heap — now invalid; drop pointers.
     * New GetMemory() in ensure_cache_alloc() will zero-fill on next RUN.   */
    g_tc_sub_let_hits = NULL;
    g_tc_sub_if_hits = NULL;
    g_local_frame_gen = 0;
    g_trace_replays = 0;
    g_trace_compiles_ok = 0;
    g_trace_compiles_bad = 0;
    g_trace_lookup_null = 0;
    g_trace_alloc_fail = 0;
    g_trace_optin_skip = 0;
    g_trace_jump_hits = 0;
    g_trace_debug_bad = 0;     /* OPTION CACHE DEBUG resets to OFF on each RUN */
    g_trace_lookup_warned = 0; /* allow one lookup_null warning per RUN */
    /* Reset the master enable.  The program's own OPTION TRACECACHE ON (if
     * any) runs after ClearRuntime, so this does not break programs that
     * request caching.  But it ensures a program WITHOUT that line runs
     * un-cached even after a previous run enabled it.                       */
    g_trace_cache_flags = 0;
    /* Clear opt-in list: subfun[] indices are about to be rebuilt by the
     * parser, so any retained per-sub flags would point at the wrong sub
     * (and silently suppress caching at top level - g_current_sub_idx==-1). */
    {
        extern void OptInResetSilent(void);
        OptInResetSilent();
    }
}

void TraceCacheInvalidateAll(void)
{
    /* Bump the frame generation (cheap; covers in-flight local-aware ops in
     * Phase 1.2+).  Also wipe global-scope entries since their resolved
     * varindex slots may no longer be valid (ERASE / NEW / DIM patterns).   */
    g_local_frame_gen++;
    if (g_cache != NULL)
    {
        memset(g_cache, 0, sizeof(struct cache_entry) * g_trace_cache_size);
        g_arena_used = 0; /* reclaim all payload memory; headers already zeroed */
    }
}

void TraceCacheFree(void)
{
    /* Release the slab back to the BASIC heap.  Called by OPTION TRACECACHE
     * OFF so that disabling the cache reclaims memory right away.           */
    if (g_cache != NULL)
        FreeMemorySafe((void **)&g_cache);
    g_arena = NULL;
    g_arena_cap = 0;
    g_arena_used = 0;
    FreeMemorySafe((void **)&g_tc_sub_let_hits);
    FreeMemorySafe((void **)&g_tc_sub_if_hits);
    g_local_frame_gen++;
}

/* Set the cache slot count.  Must be called when the slab is NOT allocated
 * (i.e. cache currently OFF or freshly reset).  Rounds n up to the next
 * power of two and clamps to [TRACE_CACHE_SIZE_MIN, TRACE_CACHE_SIZE_MAX].
 * Returns the value actually used (so the caller can report it).            */
int TraceCacheSetSize(int n)
{
    if (g_cache != NULL)
        TraceCacheFree();
    if (n < TRACE_CACHE_SIZE_MIN)
        n = TRACE_CACHE_SIZE_MIN;
    if (n > TRACE_CACHE_SIZE_MAX)
        n = TRACE_CACHE_SIZE_MAX;
    /* Round up to next power of two. */
    int p = TRACE_CACHE_SIZE_MIN;
    while (p < n)
        p <<= 1;
    g_trace_cache_size = (uint16_t)p;
    g_trace_cache_mask = (uint16_t)(p - 1);
    return p;
}

int TraceCacheGetSize(void)
{
    return (int)g_trace_cache_size;
}

void TraceCacheOptInClear(void)
{
    memset(g_sub_optin, 0, sizeof(g_sub_optin));
    g_sub_optin_active = 0;
    /* Existing cache entries may have been compiled while the opt-in list
     * was either active or not; safest to wipe so the new policy applies. */
    TraceCacheInvalidateAll();
}

void TraceCacheOptInAdd(int subfun_idx)
{
    if (subfun_idx < 0 || subfun_idx >= MAXSUBFUN)
        return;
    g_sub_optin[subfun_idx] = 1;
    g_sub_optin_active = 1;
    TraceCacheInvalidateAll();
}

/* Internal: wipe opt-in state without re-invalidating the cache.  Called
 * from TraceCacheReset() during RUN/NEW, where the cache is already being
 * dropped wholesale.  The opt-in list references subfun[] indices which
 * are themselves rebuilt by the parser, so any stale entries would target
 * the wrong sub - clear unconditionally.                                    */
void OptInResetSilent(void)
{
    memset(g_sub_optin, 0, sizeof(g_sub_optin));
    g_sub_optin_active = 0;
}

#if TRACE_CACHE_ENABLED

int TraceCacheTryExec(unsigned char *stmt_src)
{
    /* Phase 1.1: only LET statements use the cache. */
    (void)stmt_src;
    return 0;
}

/* Print a single-line debug message for a statement that could not be cached.
 * tag is "TC-BAD" (unsupported expression) or "TC-FULL" (cache full).       */
static void debug_print_stmt(unsigned char *stmt, const char *tag)
{
    unsigned char tmp[64];
    int n = 0;
    unsigned char *q = stmt;
    while (n < (int)sizeof(tmp) - 1 && *q && *q != '\r' && *q != '\n' && *q != ':')
    {
        if (*q >= C_BASETOKEN)
        {
            const unsigned char *kw = tokenname(*q);
            if (kw && *kw)
            {
                while (*kw && n < (int)sizeof(tmp) - 1)
                    tmp[n++] = *kw++;
                if (n < (int)sizeof(tmp) - 1)
                    tmp[n++] = ' ';
            }
            else
                tmp[n++] = '?';
            q++;
        }
        else
            tmp[n++] = *q++;
    }
    tmp[n] = 0;
    char dbg[160];
    const char *sname = "(top)";
    if (g_current_sub_idx >= 0 && g_current_sub_idx < MAXSUBFUN &&
        subfun[g_current_sub_idx] != NULL)
    {
        static char nm[MAXVARLEN + 1];
        unsigned char *sp = subfun[g_current_sub_idx] + sizeof(CommandToken);
        skipspace(sp);
        int j = 0;
        while (j < MAXVARLEN && isnamechar(*sp))
            nm[j++] = *sp++;
        nm[j] = 0;
        sname = nm;
    }
    snprintf(dbg, sizeof(dbg), "[%s] %s: %s\r\n", tag, sname, tmp);
    MMPrintString(dbg);
}

int __not_in_flash_func(TraceCacheTryLet)(unsigned char *cmdline)
{
    /* Caller already inlined the g_trace_cache_flags check.                 */

    /* Per-sub opt-in: if a list is active, only cache LETs that are
     * lexically inside one of the listed subs.  Top-level program code and
     * non-listed subs are never cached.  Skipping the lookup also prevents
     * the cache from being filled by drive-by LETs while the user is trying
     * to focus capacity on hot subs.                                         */
    if (g_sub_optin_active)
    {
        if (g_current_sub_idx < 0 ||
            g_current_sub_idx >= MAXSUBFUN ||
            !g_sub_optin[g_current_sub_idx])
        {
            g_trace_optin_skip++;
            return 0;
        }
    }

    struct cache_entry *e = cache_lookup_or_create(cmdline);
    if (e == NULL)
    {
        g_trace_lookup_null++;
        if (g_trace_debug_bad && !g_trace_lookup_warned)
        {
            g_trace_lookup_warned = 1;
            debug_print_stmt(cmdline, "TC-FULL");
        }
        return 0;
    }

    switch (e->state)
    {
    case ST_COMPILED:
        if (e->entry_kind != ENTRY_KIND_LET)
            return 0; /* IF entry in our probe chain — skip */
        /* Local-aware re-resolve: if any operand is a local and the frame
         * generation has advanced since we last resolved, walk the LVAR ops
         * and re-resolve them against the current frame.  If any local has
         * disappeared, mark the entry BAD.                                   */
        if (e->has_locals && e->frame_gen != g_local_frame_gen)
        {
            if (!re_resolve_locals(e))
            {
                e->state = ST_BAD;
                return 0;
            }
        }
        if (replay_let(e))
        {
            g_trace_replays++;
            if (g_tc_sub_let_hits)
                g_tc_sub_let_hits[tc_sub_idx()]++;
            return 1;
        }
        /* replay bailed (e.g. divide-by-zero, overflow) - let the
         * interpreter run it; downgrade to BAD so we don't keep retrying.   */
        e->state = ST_BAD;
        return 0;

    case ST_PENDING:
        g_trace_last_was_full = 0;
        if (compile_let(cmdline, e))
        {
            e->state = ST_COMPILED;
            g_trace_compiles_ok++;
            if (replay_let(e))
            {
                g_trace_replays++;
                if (g_tc_sub_let_hits)
                    g_tc_sub_let_hits[tc_sub_idx()]++;
                return 1;
            }
            e->state = ST_BAD;
            return 0;
        }
        /* Compile failed.  If the failure was caused by a not-yet-created
         * variable (typical first-assignment LET) the next visit will
         * succeed, so leave the entry PENDING for a few retries before
         * giving up permanently.                                            */
        e->compile_attempts++;
        if (e->compile_attempts < TRACE_COMPILE_RETRY_BUDGET)
            return 0;
        e->state = ST_BAD;
        g_trace_compiles_bad++;
        if (g_trace_debug_bad)
            debug_print_stmt(cmdline, g_trace_last_was_full ? "TC-FULL" : "TC-BAD");
        return 0;

    case ST_BAD:
    default:
        return 0;
    }
}

/* Try to evaluate a cached IF condition.
 *   condline : stable ProgMemory pointer saved from cmdline BEFORE getargs.
 *   out_result: set to 0 (false) or 1 (true) when the function returns 1.
 * Returns 1 if the condition was served from cache, 0 if not cached / bail. */
int __not_in_flash_func(TraceCacheTryIf)(unsigned char *condline, int *out_result)
{
    if (g_sub_optin_active)
    {
        if (g_current_sub_idx < 0 ||
            g_current_sub_idx >= MAXSUBFUN ||
            !g_sub_optin[g_current_sub_idx])
        {
            g_trace_optin_skip++;
            return 0;
        }
    }

    struct cache_entry *e = cache_lookup_or_create(condline);
    if (e == NULL)
    {
        g_trace_lookup_null++;
        if (g_trace_debug_bad && !g_trace_lookup_warned)
        {
            g_trace_lookup_warned = 1;
            debug_print_stmt(condline, "TC-FULL");
        }
        return 0;
    }

    switch (e->state)
    {
    case ST_COMPILED:
        if (e->entry_kind != ENTRY_KIND_IF)
            return 0; /* LET entry in probe chain */
        if (e->has_locals && e->frame_gen != g_local_frame_gen)
        {
            if (!re_resolve_locals(e))
            {
                e->state = ST_BAD;
                return 0;
            }
        }
        if (replay_if_cond(e, out_result))
        {
            g_trace_replays++;
            if (g_tc_sub_if_hits)
                g_tc_sub_if_hits[tc_sub_idx()]++;
            return 1;
        }
        e->state = ST_BAD;
        return 0;

    case ST_PENDING:
        g_trace_last_was_full = 0;
        if (compile_if_cond(condline, e))
        {
            e->state = ST_COMPILED;
            g_trace_compiles_ok++;
            if (replay_if_cond(e, out_result))
            {
                g_trace_replays++;
                if (g_tc_sub_if_hits)
                    g_tc_sub_if_hits[tc_sub_idx()]++;
                return 1;
            }
            e->state = ST_BAD;
            return 0;
        }
        e->compile_attempts++;
        if (e->compile_attempts >= TRACE_COMPILE_RETRY_BUDGET)
        {
            e->state = ST_BAD;
            g_trace_compiles_bad++;
            if (g_trace_debug_bad)
                debug_print_stmt(condline, g_trace_last_was_full ? "TC-FULL" : "TC-BAD");
        }
        return 0;

    case ST_BAD:
    default:
        return 0;
    }
}

/* =========================================================================
 * Jump target cache — GOTO / GOSUB / RESTORE with a literal line number or
 * label.  The resolved ProgMemory address is stored in the arena (4 bytes)
 * on the first execution and returned directly on every subsequent call,
 * bypassing findlabel() / findline() entirely.
 *
 * Variable-argument RESTORE must NOT call TraceCacheStoreJump because its
 * target changes with the variable value.
 * =========================================================================
 */

int __not_in_flash_func(TraceCacheTryJump)(unsigned char *key, unsigned char **out_target)
{
    struct cache_entry *e = cache_lookup_or_create(key);
    if (e == NULL)
    {
        g_trace_lookup_null++;
        return 0;
    }
    if (e->state != ST_COMPILED || e->entry_kind != ENTRY_KIND_JUMP)
        return 0;
    unsigned char *tgt;
    memcpy(&tgt, g_arena + e->payload_off, sizeof(tgt));
    *out_target = tgt;
    g_trace_jump_hits++;
    return 1;
}

void TraceCacheStoreJump(unsigned char *key, unsigned char *target)
{
    struct cache_entry *e = cache_lookup_or_create(key);
    if (e == NULL)
        return;
    /* Never overwrite an already-compiled or permanently-bad entry.          */
    if (e->state == ST_COMPILED || e->state == ST_BAD)
        return;
    /* Align arena write pointer to sizeof(unsigned char *).                  */
    uint32_t sz = (uint32_t)sizeof(unsigned char *);
    uint32_t aligned_off = (g_arena_used + sz - 1u) & ~(sz - 1u);
    if (aligned_off + sz > g_arena_cap)
    {
        g_trace_alloc_fail++;
        return;
    }
    e->payload_off = aligned_off;
    memcpy(g_arena + aligned_off, &target, sz);
    g_arena_used = aligned_off + sz;
    e->entry_kind = ENTRY_KIND_JUMP;
    e->n_ops = 0;
    e->state = ST_COMPILED;
    g_trace_compiles_ok++;
}

#endif /* TRACE_CACHE_ENABLED */
