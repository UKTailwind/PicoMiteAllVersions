# Background Tasks Plan

User-facing goal: let an MMBasic program run a second BASIC program in the background while the foreground program (or REPL) keeps the keyboard, on every target the interpreter already runs on.

```basic
BACKGROUND "WEBSERVER.BAS"
' foreground continues here — REPL or running program
PRINT "main", TIME$
PAUSE 1000 : GOTO 10
```

This document memorializes the design conversation behind that feature, including the audit of the existing interpreter that informed the scope, and the staged path that unifies `BACKGROUND` with the existing interrupt subsystem.

## Motivation

MMBasic today is single-threaded by construction. The interpreter is a dispatch loop walking a tokenized program; concurrency is limited to **polled handlers** — `SETTICK`, `ON KEY`, `SETPIN INTERRUPT`, etc. — that fire at statement boundaries via `check_interrupt()`. There is no way for two BASIC programs to coexist.

We want one. "REPL + running program + a web server, all in BASIC, on the same interpreter" is the target. The lulz framing is real: BASIC users get to write `BACKGROUND "WEBSERVER.BAS"` and have it Just Work, on every port from RP2040 to WASM to Pocket 386 to the host.

The design constraints, in priority order:
1. **Portable across all current targets** — RP2040, RP2350, ESP32-S3, Pocket 386, host (macOS/Linux), WASM. No per-target asm.
2. **Minimal rewrite of the existing interpreter** — additions in new files; surgical edits to existing code only where strictly required.
3. **Tasks share global variables** — the "lulz" feature. Tasks talk via globals; no message-passing required for shared state.
4. **Almost useless on RP2040 is acceptable** — RP2040 caps at 1 background task; design the model for capable targets.

## Non-Goals

- **Pre-emptive scheduling.** Cooperative only. Tasks yield at statement boundaries.
- **Per-task C stacks (stackful coroutines).** Ruled out by RP2040 memory budget (8 KB main stack), WASM lack of native stack switching without Asyncify cost, and portability constraint.
- **Multi-core SMP.** Single core, even on RP2350/x86. The existing interpreter is single-threaded throughout; refactoring it into something the C compiler can multi-thread is several orders of magnitude beyond this plan.
- **Process isolation.** Tasks share globals, file handles, hardware state. Races on shared state are the BASIC programmer's responsibility. BASIC has no atomics; we don't invent them.
- **Background stdin.** Only the foreground task reads the keyboard. `INPUT` from background errors; `INKEY$` returns `""`; `KEYDOWN` returns 0.

## BASIC Surface

```basic
BACKGROUND filename$              ' spawn a background task, returns immediately
BACKGROUND filename$, name$       ' spawn with a name (default: filename)
KILL BACKGROUND id                ' terminate one task by id (1-based)
KILL BACKGROUND ALL               ' terminate every background task
BACKGROUNDS                       ' list running background tasks
PRINT MM.BACKGROUND.COUNT         ' how many backgrounds are running
```

Rules a background task obeys:
- Inherits the global variable table from the foreground (the "lulz" feature).
- Has its own local-variable scope (locals named `I` in two tasks don't collide).
- Has its own GOSUB, FOR, DO, and SUB-frame stacks.
- Has its own DATA/READ position, error state, OPTION ERROR SKIP state.
- Cannot `INPUT`/`LINE INPUT`. `INKEY$` returns `""`. `KEYDOWN` returns 0.
- Cannot register interrupts (`SETTICK`, `ON KEY`, `ON PS2`, `SETPIN ... INTERRUPT`, etc.) in Phase A. Phases C/D unify these into the task scheduler so background tasks gain them automatically.
- Can `PRINT` to the same console as foreground; interleaving is the programmer's problem.
- Can `OPEN` files and sockets independently.
- Dies when explicitly `KILL`ed, or when `RUN`/`NEW` resets the foreground program.

## Implementation Model

### Stackless cooperative, with save/restore

No per-task C stack. All tasks share the single native C stack. Each task has an **`InterpState` mirror struct** that holds the interpreter's per-execution globals. `task_yield()` copies the current task's mirror in, the next task's mirror out:

```c
void task_yield(void) {
    interp_save(&current_task->state);
    current_task = scheduler_pick_next();
    interp_restore(&current_task->state);
}
```

The interpreter's source is unchanged. It still references `CurrentLinePtr`, `g_LocalIndex`, `gosubstack`, etc. as file-scope globals. Save/restore at yield points keeps each task's view of those globals consistent with its own program state.

**Cost per context switch**: a memcpy of the mirror struct (typical ~500-800 bytes, see Audit below). On RP2040 at 150 MHz that is roughly 4-6 µs. Yields happen at most once per BASIC statement; statements take microseconds to milliseconds. Overhead is 1-5% in pathological tight loops, invisible in practice.

### Yield seam

`CheckAbort()` in the dispatch loop at `core/mmbasic/MMBasic.c:635`, called at every statement boundary after `ClearTempMemory()` (line 632). This is the right seam: temp memory has been cleared, C stack is at the dispatch loop's iteration boundary, and the WASM port already pumps `emscripten_sleep(0)` here. `task_yield()` hooks in alongside the existing `CheckAbort` and `check_interrupt` calls.

### Scheduler

Round-robin across runnable tasks, with wake-condition gating:

```c
typedef enum {
    TASK_RUNNING,     // ready to run
    TASK_SLEEPING,    // wake at wake_at (PAUSE n)
    TASK_WAITING_KEY, // wake on next key (INPUT, INKEY)  -- Phase B
    TASK_WAITING_PIN, // wake on pin edge                  -- Phase D
    TASK_TICK_HANDLER,// fires every period_ms             -- Phase C
    // future: WAITING_SOCKET, WAITING_WAV, etc.
} task_state_t;
```

The scheduler walks the task list, picks the next `TASK_RUNNING` task (with deadlines honored for sleeping tasks), and switches. Foreground always exists at slot 0; backgrounds occupy slots 1..N.

### `PAUSE` rewrite

The one existing primitive that needs to learn about tasks. Today `cmd_pause` busy-loops on `mmtime_now_ms()`. With tasks:

```c
void cmd_pause(void) {
    current_task->wake_at = mmtime_now_ms() + arg_ms;
    current_task->state = TASK_SLEEPING;
    task_yield();
    // resume here once wake_at has elapsed and the scheduler picks us
}
```

`PAUSE n` in any task gives its slice to others rather than spinning. ~30 LOC.

## Audit: What Goes in `InterpState`

The interpreter has ~30-50 file-scope globals. Most are stale-but-unused at yield points (expression scratch, command-dispatch scratch, marshaling buffers). The audit below distinguishes what *must* be per-task from what stays shared.

### Must be saved/restored

| Field | Source | Why |
|---|---|---|
| `nextstmt` | `MMBasic.c:180` | Read after yield at line 639 |
| `CurrentLinePtr` | `MMBasic.c:181` | Error reporting |
| `ContinuePoint` | `MMBasic.c:182` | CONT after STOP |
| `g_LocalIndex` | `MMBasic.c:107` | Scope depth |
| `gosubstack[]`, `gosubindex`, `errorstack[]` | `Commands.c:109-111` | SUB/FUN call chain |
| `g_forstack[]`, `g_forindex` | `Commands.c:98-99` | FOR loop state |
| `g_dostack[]`, `g_doindex` | `Commands.c:104-105` | DO loop state |
| `NextData`, `NextDataLine` | `MMBasic.c:131-132` | DATA/READ position |
| `OptionErrorSkip`, `SaveErrorMessage`, `Saveerrno` | `MM_Misc.c:210-212` | ON ERROR SKIP state |
| `ErrNext` (jmp_buf) | `MMBasic.c:118` | ON ERROR target |
| `mark` (jmp_buf) | `MMBasic.c:117` | Abort-to-REPL target |
| `CurrentSubFunName`, `CurrentInterruptName` | `MMBasic.c:114-115` | Error messages, IRETURN |
| `g_StrTmp[]`, `g_StrTmpLocalIndex[]`, `g_StrTmpIndex`, `g_TempMemoryIsChanged` | `Memory.c` | Temp memory bookkeeping — see below |
| `TraceBuff[]`, `TraceBuffIndex` | `MMBasic.c:185-186` | TRACE LIST |

Plus new per-task fields: `id`, `state`, `wake_at`, `is_foreground`, `name[]`, `entry_filename[]`.

Mirror struct size: ~500-800 bytes per task if we memcpy only the used portions of `gosubstack`/`g_forstack`/`g_dostack`/`g_StrTmp` (i.e. up to `gosubindex` entries, not the whole `MAXGOSUB` array). Full-array copies push it to ~2-3 KB. Optimize the hot path with bounded copies.

### Subtle: temp memory bookkeeping (`g_StrTmp[]`)

`ClearTempMemory()` at `Memory.c:805` walks from the end of `g_StrTmp[]` and pops while `g_StrTmpLocalIndex[idx-1] >= g_LocalIndex`. It preserves outer-scope temp allocations.

If Task A yields from inside an expression that has pending temp strings, and Task B then runs `ClearTempMemory()` with B's smaller `g_LocalIndex`, **B will free A's temp strings**. Use-after-free, hard to diagnose.

Fix: `g_StrTmp[]`, `g_StrTmpLocalIndex[]`, `g_StrTmpIndex`, `g_TempMemoryIsChanged` all live in the per-task mirror. The heap allocations themselves are unchanged; only the bookkeeping arrays are per-task.

This is the easy-to-miss bug. Smoke test #1 is two tasks each running tight loops that call user FUNCTIONs returning strings — exercises the temp-memory state crossover within seconds if missed.

### Recursive ExecuteProgram and C-stack budget

`ExecuteProgram()` is recursive in C (`MMBasic.c:580` comment; line 1199 in `DefinedSubFun`). A user FUNCTION called from an expression re-enters `ExecuteProgram` for the function body. So when Task A yields from inside a user function body, its C stack contains:

```
ExecuteProgram (outer, frozen at cmd dispatch)
  -> cmd_let -> evaluate -> DefinedSubFun -> ExecuteProgram (inner, current)
       -> cmd_X -> CheckAbort -> task_yield()
```

Stackless yielding is safe — Task A's frozen C frames stay on the shared C stack while other tasks run; they're below the active task's working set and don't get clobbered. **But each task consumes C stack proportional to its nesting depth.** With MAXGOSUB ~50 and per-frame cost ~150 bytes (mostly the local `jmp_buf SaveErrNext`), a deeply-nested task can consume 5-10 KB of C stack.

On RP2040 (main C stack ~8 KB), this caps practical concurrent depth at single-digit tasks each nested a few levels. Per-target task caps:

| Target | `MAX_BACKGROUND` |
|---|---|
| RP2040 | 1 |
| RP2350 | 8 |
| ESP32-S3 | 8 |
| Pocket 386 | 16 |
| Host / WASM | 16+ |

RP2040 being almost useless was an accepted constraint up front.

### Does NOT need save/restore

Stale-but-unused at yield points, so cross-task overwrites are harmless:

- `targ`, `fret`/`iret`/`sret`, `farg1/2`, `iarg1/2`, `sarg1/2`, `ep` — expression scratch
- `cmdtoken`, `cmdline` — command-dispatch scratch
- `argval`, `argtype`, `argv1/2`, `argbuf1/2`, `argbyref` — marshaling globals, freed before inner ExecuteProgram runs (`MMBasic.c:1159`)
- `tknbuf`, `inpbuf` — REPL line input
- `g_StructArg`, `g_StructMemberType`, etc. — findvar scratch
- `DefinedSubFunMem`, `DefinedSubFunLocalIndex` — only live during marshaling, before yield is reachable

These remain shared globals.

## The Local-Variable Fix

MMBasic's locals are **not pointers on a stack**. They are entries in a single shared hash table `g_vartbl[MAXVARS]`, split into a local half (lower) and global half (upper). Each entry has a `level` field — 0 = global, N = locally scoped at SUB/FUNCTION nesting depth N. `g_LocalIndex` is the current scope depth.

`findvar` (`MMBasic.c:2528-2557`) probes the local half for `name == X && level == g_LocalIndex`, falling back to globals on miss. `ClearVars` (`MMBasic.c:3486-3506`) walks `g_hashlist[]` (creation-order list) and deletes locals at or above the exiting level.

**This is broken for tasks** out of the box: two tasks at `g_LocalIndex == 1` (each inside its own SUB) both create entries in the same local half of `g_vartbl` with the same `level == 1`. Hash collisions on identically-named locals (every loop uses `I`) cause one task's `I = I + 1` to read the other task's `I`.

### Fix

Add a 1-byte `task` field to `s_vartbl` and `s_hash` entries. Local-half lookup in `findvar` filters on `task == current_task->id` AND `level == g_LocalIndex`. `ClearVars` filters its hashlist sweep on task ID. Insertion sites stamp the creating task's ID.

Touch points, all mechanical, all guided by existing `level` comparisons:

- `s_vartbl`, `s_hash` structs: +1 byte each (or bit-pack into existing `level`)
- `findvar` local-half search loop (`MMBasic.c:2528-2557`): task filter
- `ClearVars` (`MMBasic.c:3486-3506`): task filter
- ~5 local-insertion sites: set the task field
- `Commands.c:2037, 2057, 2233` and similar (FOR/DO scope cleanup): pair task with level

Globals (level 0) remain genuinely shared across tasks — that's the design. The local-half pool is still shared capacity (`MAXVARS/2` slots across all tasks); two SUB-heavy tasks contend for the same pool. Capacity caveat, not correctness.

## Subsuming Interrupts (Phase C+)

MMBasic's existing "interrupts" are **not actual interrupts**. They are polled handlers dispatched by `check_interrupt()` at the same statement-boundary seam where `task_yield()` would hook in:

```
check_interrupt()  -> finds a triggered source       task_yield()  -> picks next task
  saves error state                                    saves current InterpState
  sets InterruptReturn = nextstmt                      (saved in InterpState)
  pushes synthetic GOSUB on stack                      (per-task gosubstack)
  sets nextstmt = handler_addr                         sets nextstmt to next task's pc
  dispatch loop reads new nextstmt                     dispatch loop reads new nextstmt
```

Both manipulate `nextstmt` and the GOSUB stack to splice execution between statements. The difference is scope: interrupt handlers share their owner's variables; tasks have their own.

### The unified model

Every existing interrupt source is a **task with a wake condition** plus **a scope-sharing rule**:

```c
typedef enum {
    WAKE_NOW,           // foreground, default BACKGROUND task
    WAKE_TIME,          // PAUSE, SETTICK
    WAKE_PIN_EDGE,      // SETPIN INTERRUPT
    WAKE_KEY,           // ON KEY, INKEY, INPUT
    WAKE_PS2,           // ON PS2
    WAKE_KEYPAD,        // KEYPAD INTERRUPT
    WAKE_SERIAL_RX,     // OPEN COMx INTERRUPT
    WAKE_IR,            // IR receiver
    WAKE_WAV_DONE,      // WAV playback complete
    WAKE_CSUB_DONE,     // CSUB completion
    WAKE_I2C_SLAVE,     // I2C slave event
    WAKE_SOCKET,        // future
} wake_kind_t;

typedef enum {
    TASK_PROGRAM,       // BACKGROUND-style: own program, own locals, can yield
    TASK_HANDLER,       // Interrupt-style: parent's scope, runs to IRETURN
} task_kind_t;
```

Then `BACKGROUND "x.bas"` spawns `TASK_PROGRAM` with `WAKE_NOW`; `SETTICK 100, Foo` spawns `TASK_HANDLER` with `WAKE_TIME period=100`; `SETPIN 3, INTERRUPT, Bar, HI` spawns `TASK_HANDLER` with `WAKE_PIN_EDGE`. The scheduler walks the task list each pass and dispatches:

- `TASK_PROGRAM` whose wake fires: full context switch.
- `TASK_HANDLER` whose wake fires: splice into parent task's execution, exactly what `checkdetailinterrupts()` does today.

`PAUSE` and `INPUT` become trivial — they mark self with `WAKE_TIME`/`WAKE_KEY` and yield. No more special-cased rewrites.

`check_interrupt()` and `checkdetailinterrupts()` (~600 lines in `MM_Misc.c`) collapse into the scheduler's wake dispatch. The 30-odd interrupt globals (`TickPeriod[]`, `TickInt[]`, `inttbl[]`, `OnKeyGOSUB`, `OnPS2GOSUB`, `com1_interrupt`, `IrInterrupt`, `WAVInterrupt`, `CSubInterrupt`, etc.) move into per-task `Task.cond.*` fields.

This is the right end state: **one concurrency model in MMBasic**, not two.

### Why phase the unification

The interrupt subsystem has 30+ globals spread across `MM_Misc.c`, `External.c`, `Commands.c`, `Draw.c`. Each registration site needs to be rewritten to spawn a handler task. The 239/239 test suite covers some of this surface but not exhaustively — every SETTICK-using demo is implicitly a regression test for the rewrite.

Doing it in one commit on top of an unproven scheduler piles two big risks together: a missed `InterpState` field plus a botched interrupt migration are hard to bisect. Phase the work so each step is independently shippable and testable.

## Phasing

### Phase A — `BACKGROUND` lands (Foundation)

The genuinely new mechanism: per-task `InterpState`, save/restore, variable-table task-ID, scheduler with `WAKE_NOW` + `WAKE_TIME`, `BACKGROUND`/`KILL` builtins, `PAUSE` rewritten to use the wake mechanism. Interrupts stay foreground-only with a `current_task->is_foreground` gate at registration sites (~30 LOC across the four interrupt-state files).

After Phase A: REPL + game + web server (all in BASIC) ships on the host and WASM. Existing programs run unchanged. RP2040 limited to 1 background task.

**Scope: ~2 weeks, ~800-1000 LOC new, ~80 LOC modified.**

### Phase B — `INPUT`/`INKEY$` wake-aware

Reuses Phase A's wake infrastructure. Foreground `INPUT` becomes "park as `WAKE_KEY`, yield, resume when a complete line is buffered." `INKEY$` similarly. Background tasks still error on `INPUT` (handler-task semantics for keyboard come in Phase C/D anyway).

**Scope: ~3 days, ~150 LOC.**

### Phase C — `SETTICK` migrates to handler tasks

Replace `TickPeriod[]`/`TickTimer[]`/`TickInt[]`/`TickActive[]` globals (in `MM_Misc.c`) with per-task `TASK_HANDLER` instances of `WAKE_TIME`. `cmd_settick` becomes a task-spawning factory. `checkdetailinterrupts`'s tick loop becomes the scheduler's wake-dispatch for `WAKE_TIME` handler tasks.

Background tasks gain `SETTICK` as a side effect: handler tasks spawned by a background parent fire only while the parent is scheduled.

Test against every demo in the corpus that uses `SETTICK` — there are several in the bench/ and host_wasm/demos/ trees.

**Scope: ~3 days, ~200 LOC.**

### Phase D — Pin / key / serial / IR / keypad / WAV / CSUB / I2C interrupts migrate

One source at a time, each its own commit with its own validation. Each migration is mechanical (replace global with handler task) but in a tested hot path, so one-at-a-time minimizes bisect surface.

Order recommendation: `SETPIN INTERRUPT` (simplest, well-tested) → `ON KEY` → `ON PS2` → `KEYPAD INTERRUPT` → serial COMx → IR → WAV → CSUB → I2C slave. Each ~50-100 LOC.

**Scope: ~1 week, ~600 LOC across 8-10 commits.**

### Phase E — `check_interrupt` / `checkdetailinterrupts` deleted

The scheduler now does everything. ~600 lines of `MM_Misc.c` go away. `MMBasic.c:636`'s `check_interrupt()` call is removed (the scheduler runs at 635 and handles all dispatch).

**Scope: ~1 day, net negative LOC.**

### Total

**~4 weeks across Phases A-E**, broken into 12-15 individually shippable commits. The "for the lulz" milestone (REPL + game + web server in BASIC) ships at end of Phase A.

## Validation Strategy

### Primary target: WASM

The WASM port already has the cooperative-yield infrastructure (`emscripten_sleep(0)` in `CheckAbort`/`MMInkey`). `task_yield()` hooks the same seam — no Asyncify changes, no fiber instrumentation, no binary-size hit.

Fast iteration loop (build → reload browser tab → observe), visible interleaving in the canvas and console, no hardware required. Right place to develop and shake out the design.

### Smoke tests, in order

1. **Save/restore correctness — the heisenbug test.** Two tasks running tight loops that each call user FUNCTIONs returning strings. Within seconds, exercises every field that needs to be in the `InterpState` mirror, including the subtle temp-memory bookkeeping. If anything's missed, expect crashes or string corruption.
2. **`PAUSE` wake-aware.** Foreground: `FOR I=1 TO 10: PRINT "F",I: PAUSE 100: NEXT`. Background: `FOR I=1 TO 100: PRINT "B",I: PAUSE 10: NEXT`. Expect ~10 Bs per F (ratio of PAUSE values).
3. **Shared globals.** Foreground increments `HITS` on each click; background prints `HITS` every second. Confirms variable-table sharing for level-0 entries.
4. **Local isolation.** Both tasks call SUBs that declare local `I`. Confirm no clobber across tasks.
5. **Stdin gating.** Background runs `INPUT A$` — expect error. `INKEY$` in background returns `""` while foreground sees keys normally.
6. **`KILL BACKGROUND`.** Foreground spawns background, kills it after 5 seconds. Confirms cleanup (no leaked locals, no orphaned hash entries).

All run as a few-line BASIC programs in the browser. Each phase adds smoke tests for its new wake conditions.

### Device validation

After WASM validation passes, run smoke tests on:
- **Host (macOS/Linux)** — same `InterpState` discipline, larger task budget.
- **Pocket 386 (pc386)** — non-trivial port, validates that nothing in the design assumes Cortex-M or Pico-SDK.
- **RP2040** — confirms the 1-task cap is honored cleanly (graceful error from second `BACKGROUND`).
- **RP2350** — comfortable case, ~8 tasks.

The full 239/239 test suite must continue to pass at every commit. The variable-table task-ID change is the highest-risk single edit; rerun the full suite after that lands.

## Known Limitations (By Design)

- **`PAUSE` is the only blocking primitive that yields cooperatively in Phase A.** Long `LINE`/`BLIT`/`INPUT` block all tasks for their duration — same contract `SETTICK` callbacks already follow today. Phase B fixes `INPUT`. Specific slow primitives can opt into mid-execution preemption by dropping a `task_yield()` poll into their inner loop (per-primitive, future work).
- **Tasks share globals — races possible.** BASIC has no atomics; the "for the lulz" trade.
- **Shared local-variable pool.** `MAXVARS/2` slots shared across all tasks' locals. Two SUB-heavy tasks contend for the same pool. Capacity, not correctness.
- **C stack consumption scales with concurrent nesting depth.** Limits practical RP2040 to 1 background task.
- **Tasks share the framebuffer, file handles, hardware state.** First task to `OPEN` a file owns it; concurrent draws to the same surface interleave at the pixel level. Discipline is the BASIC programmer's job.
- **Interrupts foreground-only until Phase C+.** Background tasks before Phase C implement periodic work as `DO : work : PAUSE n : LOOP`, which is portable and free under the wake-aware scheduler.

## Cross-References

- `core/mmbasic/MMBasic.c:577-644` — `ExecuteProgram()`, the dispatch loop.
- `core/mmbasic/MMBasic.c:2528-2557` — `findvar` local-half lookup; site of task-ID filter.
- `core/mmbasic/MMBasic.c:3480-3550` — `ClearVars`; site of task-ID filter.
- `core/mmbasic/Memory.c:786-815` — `GetTempMemory` / `ClearTempMemory`; the `g_StrTmp[]` bookkeeping that must be per-task.
- `core/mmbasic/MM_Misc.c:2820-2906` — `checkdetailinterrupts`; the existing polled-handler dispatch that Phases C-E subsume.
- `core/mmbasic/Commands.c:98-111` — `g_forstack`, `g_dostack`, `gosubstack` definitions.
- `core/mmbasic/MM_Misc.c:159-211` — interrupt-state globals (`inttbl`, `TickPeriod`, `TickInt`, `OnKeyGOSUB`, `OnPS2GOSUB`, etc.).
- WASM yield hook: `project_wasm_yield_hook.md` in memory; `emscripten_sleep(0)` in CheckAbort/MMInkey funnel.
