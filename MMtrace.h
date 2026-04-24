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
#else
static inline int TraceCacheTryExec(unsigned char *stmt_src)
{
    (void)stmt_src;
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
#endif

#ifdef __cplusplus
}
#endif

#endif /* MMTRACE_H */
