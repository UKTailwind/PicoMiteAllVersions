/* ============================================================================
 *  MMtrace.h - Per-statement trace cache for MMBasic
 * ----------------------------------------------------------------------------
 *  Phase 1: skeleton + frame-generation tracking.
 *
 *  The trace cache is an optional accelerator that records the resolved
 *  structure of frequently-executed statements (e.g. plain LET arithmetic) and
 *  replays them with all variable-name lookups, operator-table indirections,
 *  and recursive descent eliminated.
 *
 *  Phase 1 (this file) provides:
 *    - Public types and API stubs for cache lookup / record / replay.
 *    - The g_local_frame_gen counter and helpers, so traces that close over
 *      local variables can guard against stale slot resolution.
 *    - Compile-time gate (TRACE_CACHE_ENABLED) and a runtime feature-bitmask
 *      (g_trace_cache_flags).  Both default to 0 / "off" so behaviour is
 *      unchanged until the LET compiler (Phase 1.1) lands.
 * ============================================================================
 */
#ifndef MMTRACE_H
#define MMTRACE_H

#include <stdint.h>

/* Compile-time gate.  Set to 0 to remove the cache entirely (no hooks, no
 * data, no code).  When 1 the cache is *built* but its runtime behaviour is
 * controlled by g_trace_cache_flags below.                                */
#ifndef TRACE_CACHE_ENABLED
#define TRACE_CACHE_ENABLED 1
#endif

/* Cache size bounds (slot counts; must be powers of two).                 */
#define TRACE_CACHE_SIZE_MIN     16   /* smallest legal slot count         */
#define TRACE_CACHE_SIZE_MAX   4096   /* largest legal slot count          */
#define TRACE_CACHE_SIZE_DEFAULT 64   /* used when no size is specified    */

#ifdef __cplusplus
extern "C"
{
#endif

    /* ---------------------------------------------------------------------- */
    /* Frame-generation counter                                                */
    /*                                                                        */
    /* Bumped on every entry / exit / wholesale clear of a local-variable     */
    /* frame.  Trace ops that resolve to a *local* variable record both the   */
    /* current generation and the resolved slot index; on replay they re-     */
    /* resolve when the generation has changed (a new sub call, unwind, etc.) */
    /* ---------------------------------------------------------------------- */
    extern uint16_t g_local_frame_gen;

    static inline void TraceBumpFrameGen(void)
    {
#if TRACE_CACHE_ENABLED
        /* 16-bit wrap is fine: any cached entry whose generation no longer    */
        /* matches the current value is treated as stale and re-resolved.     */
        g_local_frame_gen++;
#endif
    }

    /* ---------------------------------------------------------------------- */
    /* Runtime feature-enable bitmask                                         */
    /*                                                                        */
    /* Defaults to 0 (all off).  Set via OPTION TRACECACHE ON [size [flags]].*/
    /* Each bit enables one optimisation category independently.              */
    /* ---------------------------------------------------------------------- */

    /* Feature bits for g_trace_cache_flags                                  */
#define TCF_LET_NUM  0x01u  /* numeric scalar / array LET                   */
#define TCF_LET_STR  0x02u  /* string scalar LET  (s$="x", s$=s$+"x")       */
#define TCF_IF       0x04u  /* IF condition caching                          */
#define TCF_LOOP     0x08u  /* DO WHILE/UNTIL condition caching              */
#define TCF_JUMP     0x10u  /* GOTO / GOSUB target caching                   */
#define TCF_RESTORE  0x20u  /* RESTORE target caching                        */
#define TCF_ALL      0x3Fu  /* all features                                  */

    /* Op codes for the DO-loop fast-condition path.  Stored in the dostack
     * at DO setup time; interpreted inline in cmd_loop without going through
     * the trace cache replay machinery.                                     */
#define DOFAST_LT    1u  /* var <  limit                                     */
#define DOFAST_GT    2u  /* var >  limit                                     */
#define DOFAST_LTE   3u  /* var <= limit                                     */
#define DOFAST_GTE   4u  /* var >= limit                                     */
#define DOFAST_EQ    5u  /* var == limit                                     */
#define DOFAST_NE    6u  /* var != limit                                     */

    extern uint8_t g_trace_cache_flags;

    /* ---------------------------------------------------------------------- */
    /* Statistics (visible from cmd_end PERF report)                          */
    /* ---------------------------------------------------------------------- */
    extern uint32_t g_trace_replays;      /* successful cache hits           */
    extern uint32_t g_trace_compiles_ok;  /* statements compiled OK          */
    extern uint32_t g_trace_compiles_bad; /* statements rejected by compiler */
    extern uint32_t g_trace_lookup_null;  /* cache_lookup_or_create returned NULL */
    extern uint32_t g_trace_alloc_fail;   /* slab allocation failed          */
    extern uint32_t g_trace_optin_skip;   /* opt-in list active and stmt not on it */

    /* When non-zero, every compile failure prints a short diagnostic line
     * (sub name + first ~50 source chars).  Toggled by OPTION CACHE DEBUG.   */
    extern uint8_t g_trace_debug_bad;

    /* Per-sub replay counters.  Index 0..MAXSUBFUN-1 = inside that sub;
     * index MAXSUBFUN = top-level code (g_current_sub_idx == -1).
     * Exposed here so the cmd_end perf report can read them.              */
    extern uint32_t *g_tc_sub_let_hits; /* LET replays per sub; NULL if unallocated */
    extern uint32_t *g_tc_sub_if_hits;  /* IF replays per sub; NULL if unallocated  */
    extern uint32_t g_trace_jump_hits;  /* GOTO/GOSUB/RESTORE cache hits            */

    /* ---------------------------------------------------------------------- */
    /* Public API (Phase 1 stubs)                                              */
    /* ---------------------------------------------------------------------- */

    /* Initialise / tear down the cache table.  Called from the program-load
     * paths (PrepareProgram) and program-clear paths (cmd_new / ClearVars(0)).
     * Safe to call multiple times.                                            */
    void TraceCacheInit(void);
    void TraceCacheReset(void);

    /* Free the ~50 KB cache slab back to the BASIC heap.  Called from
     * OPTION TRACECACHE OFF so that disabling the cache mid-program (or
     * interactively) releases its memory immediately rather than waiting
     * for the next RUN.  Safe to call when nothing is allocated.            */
    void TraceCacheFree(void);

    /* Set the cache slot count.  n is rounded up to a power of two and
     * clamped to [TRACE_CACHE_SIZE_MIN, TRACE_CACHE_SIZE_MAX].  Returns the
     * value actually used.  Frees any existing slab; reallocation happens
     * lazily on the next cached LET.                                        */
    int TraceCacheSetSize(int n);
    int TraceCacheGetSize(void);

    /* Invalidate the entire cache.  Called whenever the variable table or
     * sub/fun table is restructured in a way that could invalidate any cached
     * slot resolution: DIM, ERASE, LOCAL, OPTION BASE, OPTION EXPLICIT, NEW,
     * editor edits to ProgMemory.  Cheap (just bumps a global generation).   */
    void TraceCacheInvalidateAll(void);

    /* Per-sub opt-in.  When the opt-in list is empty (default) the cache is
     * used everywhere.  When non-empty, only LETs whose enclosing sub is on
     * the list will be cached/replayed.                                       */
    void TraceCacheOptInClear(void);
    /* Add `subfun_idx` (return value of FindSubFun) to the opt-in list.      */
    void TraceCacheOptInAdd(int subfun_idx);

#if TRACE_CACHE_ENABLED
    /* Try to execute the statement at `stmt_src` from the cache.
     *   stmt_src: pointer into ProgMemory at the statement start (used as key
     *             AND as the safety guard - if program memory shifts the cache
     *             entry simply does not match).
     * Returns:
     *   1  - statement was executed from cache; caller should skip the normal
     *        dispatch and continue with the next statement.
     *   0  - cache miss / disabled / not yet compiled / replay bailed out.
     *        Caller must run the statement through the normal interpreter.   */
    int TraceCacheTryExec(unsigned char *stmt_src);

    /* Try to execute a numeric INC statement from the cache.
     *   cmdline: raw argument bytes BEFORE any getcsargs / makeargs call.
     *            Handles: Inc var  and  Inc var, const  (numeric literals only).
     *            Rejects string Inc, array Inc, and variable-expression steps.
     * Returns:
     *   1  - Inc was executed from cache; cmd_inc must return immediately.
     *   0  - cache miss / too complex; caller runs normal cmd_inc.            */
    int TraceCacheTryInc(unsigned char *cmdline);

    /* Try to execute a LET statement (or implicit assignment) from the cache.
     *   cmdline: as passed to cmd_let - points at the start of the LHS variable
     *            name in the tokenised source.
     * Returns:
     *   1  - LET was executed from the cache; cmd_let must return immediately.
     *   0  - caller must run the LET via the normal interpreter path.         */
    int TraceCacheTryLet(unsigned char *cmdline);

    /* Try to evaluate a cached IF condition.
     *   condline  : stable ProgMemory pointer — save cmdline BEFORE calling
     *               getargs in cmd_if, then pass that saved value here.
     *   out_result: set to 0 (false) or 1 (true) when the call returns 1.
     * Returns:
     *   1  - condition evaluated from cache; caller uses *out_result for r.
     *   0  - not cached / bail; caller must call getnumber(argv[0]).          */
    int TraceCacheTryIf(unsigned char *condline, int *out_result);

    /* Try to use a cached jump target for GOTO / GOSUB / RESTORE.
     *   key       : cmdline pointer (stable ProgMemory address of the argument).
     *   out_target: set to the cached resolved address when the call returns 1.
     * Returns:
     *   1  - cache hit; caller must use *out_target as the destination.
     *   0  - cache miss; caller must call findlabel / findline normally, then
     *        call TraceCacheStoreJump with the resolved address.              */
    int TraceCacheTryJump(unsigned char *key, unsigned char **out_target);

    /* Record a resolved jump target so that TraceCacheTryJump can return it
     * on the next call.  Must NOT be called for variable-argument RESTORE
     * (where the target depends on run-time variable values).                */
    void TraceCacheStoreJump(unsigned char *key, unsigned char *target);

    /* Try to execute a SELECT CASE statement from the pre-compiled arm table.
     *   key        : cmdline pointer (stable ProgMemory address of the selector
     *                expression — used as cache key).
     *   scan_start : nextstmt at cmd_select entry (first stmt inside SELECT body).
     *   sel_type   : T_INT or T_NBR (T_STR always returns 0 immediately).
     *   sel_i/sel_f: the already-evaluated selector value.
     *   out_nextstmt: set to the matching case body's first statement.
     * Returns:
     *   1 - match found; caller must set nextstmt = *out_nextstmt and return.
     *   0 - cache miss / disabled / string selector / ST_BAD; caller runs
     *       the normal linear scan.                                           */
    int TraceCacheTrySelect(unsigned char *key,
                             unsigned char *scan_start,
                             int sel_type, int64_t sel_i, MMFLOAT sel_f,
                             unsigned char **out_nextstmt);

    /* Attempt to compile a simple VAR OP CONST or CONST OP VAR comparison
     * from the tokenised condition at condptr into pre-resolved fast-condition
     * fields.  VAR must be a plain numeric scalar (not an array or struct);
     * CONST must be a numeric literal or a T_CONST variable.
     *
     * On success returns 1 and writes:
     *   *out_var        direct pointer to g_vartbl[vidx].val (or .val.s for T_PTR)
     *   *out_varindex   g_vartbl slot
     *   *out_is_local   1 = local variable (re-resolve when frame_gen changes)
     *   *out_frame_gen  g_local_frame_gen at resolve time
     *   *out_type       T_INT or T_NBR
     *   *out_op         DOFAST_LT / DOFAST_GT / DOFAST_LTE / DOFAST_GTE /
     *                   DOFAST_EQ / DOFAST_NE  (already inverted for CONST-first)
     *   *out_limit_i    constant limit value (integer; valid when *out_type==T_INT)
     *   *out_limit_f    constant limit value (float;   valid when *out_type==T_NBR)
     *   out_name        upper-cased variable name written into caller's
     *                   MAXVARLEN+1-byte buffer (used to re-resolve locals)
     * Returns 0 when the expression is too complex; no out_* are modified.  */
    int TraceCacheCompileDoFast(unsigned char *condptr,
                                 void     **out_var,
                                 int       *out_varindex,
                                 uint8_t   *out_is_local,
                                 uint16_t  *out_frame_gen,
                                 uint8_t   *out_type,
                                 uint8_t   *out_op,
                                 int64_t   *out_limit_i,
                                 MMFLOAT   *out_limit_f,
                                 unsigned char *out_name);
#else
static inline int TraceCacheTryExec(unsigned char *stmt_src)
{
    (void)stmt_src;
    return 0;
}
static inline int TraceCacheTryInc(unsigned char *cmdline)
{
    (void)cmdline;
    return 0;
}
static inline int TraceCacheTryLet(unsigned char *cmdline)
{
    (void)cmdline;
    return 0;
}
static inline int TraceCacheTryIf(unsigned char *condline, int *out_result)
{
    (void)condline; (void)out_result;
    return 0;
}
static inline int TraceCacheTryJump(unsigned char *key, unsigned char **out_target)
{
    (void)key; (void)out_target;
    return 0;
}
static inline void TraceCacheStoreJump(unsigned char *key, unsigned char *target)
{
    (void)key; (void)target;
}
static inline int TraceCacheTrySelect(unsigned char *key, unsigned char *scan_start,
                                       int sel_type, int64_t sel_i, MMFLOAT sel_f,
                                       unsigned char **out_nextstmt)
{
    (void)key; (void)scan_start; (void)sel_type;
    (void)sel_i; (void)sel_f; (void)out_nextstmt;
    return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MMTRACE_H */
