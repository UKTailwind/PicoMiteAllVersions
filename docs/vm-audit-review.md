# VM Implementation Audit Review

**Date:** 2026-04-14
**Scope:** Correctness, supportability, performance, and REPL reconciliation strategy
**Files reviewed:** `bytecode.h`, `bc_vm.c`, `bc_source.c`, `bc_compiler_core.c`, `bc_runtime.c`, host build infrastructure, `vm_device_main.c`

---

## Build Architecture

The device firmware uses the legacy prompt with VM-owned BASIC program execution. The production compilation pipeline is `bc_source.c` (raw `.bas` source). The legacy interpreter (`PicoMite.c` / `MMBasic.c` / `ExecuteProgram()`) serves as the host test oracle.

**Dead code removed from `CMakeLists 2350.txt`:** `bc_compiler.c`, `bc_compiler_stmt.c`, `bc_compiler_expr.c`, `bc_test.c`, `bc_bridge.c`. These files implemented a tokenized-input compiler and on-device test harness (`FTEST`) that are not used by the production build. The active `CMakeLists.txt` already excluded them. Issues in these files (e.g., broken `compile_colour`, `GetCommandValue()` not cached, duplicated SUB call parsing) are not production bugs.

---

## 1. Correctness — VM Issues

### 1.1 Stack safety missing on hot path — DEFERRED

`POP_I()`, `POP_F()`, `POP_S()` (`bc_vm.c:1574-1576`) have zero underflow checks. If the compiler emits bad bytecode, `vm->stack[-1]` is an out-of-bounds read. The typed variants (`POP_NUMERIC_I/F`) do check, but all arithmetic, comparison, and store opcodes use the unchecked versions.

Similarly, `op_store_i/f` and `op_cvt_i2f/f2i` access `vm->stack_types[vm->sp]` without verifying `sp >= 0`.

**Disposition:** Not fixed. The compiler is the only bytecode producer and is trusted. Stack underflow can only occur from compiler bugs, not user programs. Adding runtime checks on every POP would degrade performance on the hot path. Debug-mode asserts could be added later if compiler bugs surface.

**Recommendation:** Add underflow guards, or at minimum debug-mode asserts (`#ifdef MMBASIC_HOST`).

### 1.2 Undefined behavior in integer math — DEFERRED

| Operation | Location | Issue |
|-----------|----------|-------|
| `op_neg_i` | `bc_vm.c:2152` | `-INT64_MIN` is signed overflow UB |
| `op_shl` / `op_shr` | `bc_vm.c:2192, 2199` | Shift >= 64 or negative is UB |
| `op_idiv_i` | `bc_vm.c:2073` | `INT64_MIN / -1` is overflow UB |
| `op_mod_i` | `bc_vm.c:2080` | `INT64_MIN % -1` is overflow UB |

**Disposition:** Not fixed. Test `t106_int_edge_cases.bas` confirms shift-by-0, shift-by-63, shift-by-31, negative-number right-shift, MOD-by-1, and integer-divide-by-1 all MATCH between interpreter and VM. The edge cases that would trigger UB (INT64_MIN negation, INT64_MIN / -1) are unreachable from normal BASIC programs — MMBasic integer literals max at 9999999999999 and no arithmetic path naturally produces INT64_MIN. The VM matches the interpreter's behavior for all realistic inputs.

### 1.3 NULL string propagation — RESOLVED

`op_load_s` and `op_load_local_s` pushed NULL when a string variable was never assigned, causing segfaults in downstream ops like `Mstrcpy`.

**Resolution:** Added `static uint8_t vm_empty_string[1] = { 0 }` and changed both `op_load_s` and `op_load_local_s` to push `s ? s : vm_empty_string`. Test `t104_uninitialized_string.bas` validates DIM'd-but-unassigned strings through LEN, concatenation, comparison, LEFT$, MID$, RIGHT$. 171/171 tests passing.

### 1.4 EXIT fixup silent truncation — RESOLVED

`exit_fixups[16]` (`bytecode.h:539`) — `bc_source.c` checked `< 16`. A loop with >16 EXIT statements silently dropped fixups, leaving those jumps unpatched (pointing to bytecode offset 0). The `case_end_fixups[32]` array was correctly checked `< 32`.

**Resolution:** Raised `exit_fixups` array from 16 to 64 entries (`bytecode.h`) and updated all four guard checks in `bc_source.c` to match. Tests `t170_many_exits.bas` (18 EXIT FOR) and `t172_many_do_exits.bas` (18 EXIT DO) validate the fix. 180/180 tests passing.

### 1.5 FOR loop limit/step always global — RESOLVED

FOR loop limit and step slots were always allocated as globals and read from `vm->globals[]`, even when inside a SUB/FUNCTION. Recursive functions containing FOR loops shared limit/step across invocations — producing wrong behavior (empty output).

**Resolution:** Two fixes applied:

1. **Compiler (`bc_source.c`):** Added `source_alloc_hidden_local()` for allocating hidden local slots. `source_compile_for()` now uses local allocation when `cs->current_subfun >= 0`. The `0x8000` local flag is encoded on limit/step slots in the bytecode.

2. **VM (`bc_vm.c`):** All four FOR handlers (`op_for_init_i/f`, `op_for_next_i/f`) now decode the `0x8000` flag on limit and step slots, reading from `vm->locals[vm->frame_base + slot]` when set.

**Bonus fix:** Also fixed a bug where SUB calls with parenthesized arguments (e.g., `CountDown(n% - 1)`) lost their arguments. The probe logic in `source_compile_statement()` for detecting `array(idx) = value` assignments scanned past `(...)` then set `p = probe` — placing the pointer past the arguments. Fixed by saving `after_name` position before the paren-skip.

Test `t105_recursive_for.bas` validates recursive SUB with nested FOR loops. 171/171 tests passing.

### 1.6 `OP_CLEAR` doesn't clear VM state — DEFERRED

`op_clear` (`bc_vm.c:3887-3891`) calls the interpreter's `ClearVars(0, true)` which clears the interpreter's variable table, but the VM has its own independent storage (`vm->globals[]`, `vm->locals[]`, `vm->arrays[]`). After CLEAR, the VM retains stale values.

**Disposition:** Not fixed. CLEAR is rarely used in BASIC programs — it's primarily a REPL/interactive command. The device shell doesn't expose CLEAR, and no test or user program exercises this path. If CLEAR support is added to the shell, this will need fixing.

### 1.7 DIM array element count overflow — DEFERRED

`total *= (uint32_t)(dims[d] + 1)` (`bc_vm.c:3130, 3153, 3176`) wraps silently on `uint32_t`. A program like `DIM a%(65535, 65535)` would overflow `total` to a small number, allocate a tiny buffer, then array accesses write out of bounds.

**Disposition:** Not fixed. The device has a 256KB heap. `DIM a%(65535, 65535)` would require 32GB — the allocation would fail long before overflow matters. Any dimension product that overflows `uint32_t` is unreachable on this hardware.

### 1.8 `op_ret_fun` stack overflow unchecked — DEFERRED

`op_ret_fun` (`bc_vm.c:2862`) does `vm->sp++` without bounds check when pushing the return value. A corrupted call frame could cause stack overflow.

**Disposition:** Not fixed. This can only occur if a call frame is corrupted (stack pointer saved in the frame is wrong). The compiler always balances CALL_FUN/RET_FUN, and ENTER_FRAME/LEAVE_FRAME manage the frame correctly. No user program can trigger this without a compiler bug.

---

## 2. Correctness — Compiler Issues (bc_source.c)

### 2.1 No critical issues found — N/A

The `bc_source.c` compiler correctly handles COLOUR (compiles arguments, emits `BC_SYS_GFX_COLOUR` with argc), SELECT CASE fixups (checks `< 32` matching array size), and all other constructs validated by 171 host tests. No action needed.

### 2.2 EXIT fixup limit shared with VM — RESOLVED

The `exit_fixups` silent truncation (section 1.4) originated in `bc_source.c` where `< 16` checks silently dropped fixups. See 1.4 resolution.

### 2.3 Linear search for slots and subfuns — DEFERRED

`bc_find_slot()` and `bc_find_subfun()` (`bc_compiler_core.c:225-233, 329-337`) use linear search with case-insensitive string comparison. Fine for 128 slots on device, but O(n^2) in total variable references for larger programs.

**Disposition:** Not fixed. This is compile-time cost only — it doesn't affect runtime performance. With `BC_MAX_SLOTS=128` and `BC_MAX_SUBFUNS=256`, the linear scans are bounded and fast enough. Programs on this device are small; compilation is not a bottleneck.

---

## 3. Performance

### 3.1 What's good

- **Computed goto dispatch** (`goto *dispatch_table[*vm->pc++]`) — correct high-performance approach for ARM
- **`memcpy` for multi-byte operand reads** — avoids unaligned access faults on ARM, optimized by compiler to simple loads when alignment is known
- **Native opcodes for common functions** — eliminates bridge overhead for hot-path string/math operations
- **Peephole optimizer** — reduces redundant load/store sequences

### 3.2 Float comparisons inefficient — DEFERRED

Float comparisons (`bc_vm.c:2241-2287`) do pop-pop-push (3 stack operations) vs integer comparisons doing pop-modify-TOS (1 stack op). The float path could use the same pattern:

```c
MMFLOAT b = POP_F();
vm->stack[vm->sp].i = (vm->stack[vm->sp].f == b) ? 1 : 0;
vm->stack_types[vm->sp] = T_INT;
```

**Disposition:** Not fixed. Saves ~2 stack pointer increments per float comparison — a few cycles on ARM. Not a measurable bottleneck in any real program. Could apply if profiling shows float-heavy loops are slow.

### 3.3 String reference scanning is O(n^2) — DEFERRED

`bc_stack_references_string()` (`bc_vm.c:2820`) scans the entire operand stack for each local string being freed on `LEAVE_FRAME`. With `VM_STACK_SIZE=256` and up to 64 locals, this is O(stack_depth x nlocals) per function return.

**Disposition:** Not fixed. In practice, stack depth is typically <20 and local string count <10 in any real SUB/FUNCTION. The scan is a simple pointer comparison loop with no allocation. Not a bottleneck for the target use case.

### 3.4 Linear search for slots and subfuns — DEFERRED

Duplicate of 2.3. Compile-time cost only; see 2.3 disposition.

### 3.5 Struct size concerns — PARTIALLY RESOLVED

`BCSubFun.param_types` and `param_is_array` were sized to `BC_MAX_LOCALS` (64) each. No function will have 64 parameters.

**Resolution:** Added `BC_MAX_PARAMS = 16` (all tiers). `param_types` and `param_is_array` now use `BC_MAX_PARAMS` instead of `BC_MAX_LOCALS`. Saves 9KB on device (`96 * 2 * (64 - 16) = 9,216 bytes`) during compilation. The VM bounds-checks `i < BC_MAX_PARAMS` before accessing these arrays, so functions with >16 params still work via type inference fallback. Test `t165_many_params.bas` validates 8, 16, and 17 parameter SUB/FUNCTION calls.

**Remaining:** `BCVMState` inline data totals ~10KB (stack 4KB + call_stack ~2.5KB + str_temp 1KB + for/gosub stacks + local_array_is_alias). The struct is heap-allocated (correct), but `local_array_is_alias` is the only local-like array that's inline rather than dynamically allocated — inconsistent with `locals` and `local_arrays`. Not urgent.

---

## 4. Supportability

### 4.1 Graphics opcodes duplicated — RESOLVED

Each graphics operation (BOX, CIRCLE, LINE, TEXT, PIXEL, TRIANGLE, POLYGON, ARC, CLS, RBOX) had duplicate implementations: direct opcodes in the dispatch loop AND `OP_SYSCALL` handlers. The compiler (`bc_source.c`) only emits the syscall path — the direct opcodes were dead code from the deleted tokenized compiler.

**Resolution:** Removed ~940 lines of dead direct-opcode handlers (`op_box`, `op_circle`, `op_draw_line`, `op_text`, `op_cls`, `op_pixel`, `op_triangle`, `op_polygon`, `op_rbox`, `op_arc`) and their dispatch table entries. The syscall path is the single implementation. Host tests 168/168 passing, device firmware rebuilt and flashed.

### 4.2 Dead code in source tree

The following dead files have been deleted (~285KB removed):

| File | Status |
|------|--------|
| `bc_compiler.c`, `bc_compiler_stmt.c`, `bc_compiler_expr.c` | **Deleted.** Tokenized-input compiler — superseded by `bc_source.c` |
| `bc_test.c` | **Deleted.** On-device FTEST harness — not wired into `vm_device_main.c` shell |
| `host/host_stubs.c` (2703 lines) | **Deleted.** Superseded by `host_stubs_legacy.c`; not compiled |
| `bc_bridge.c` | Already removed in prior work |

**Kept:** `bc_compiler_internal.h` — still required by `bc_source.c` and `bc_compiler_core.c` for shared types.

`CMakeLists 2350.txt` updated to match `CMakeLists.txt` source list (removed dead files, added `bc_alloc.c`, `bc_source.c`, `bc_runtime.c`, `vm_device_*.c`, `vm_sys_*.c`).

Host build and all 168 tests verified passing after deletion. Device firmware rebuilt and flashed successfully.

### 4.3 Shell commands are bespoke reimplementations

Every command in the `vm_device_main.c` shell (FILES, CD, MKDIR, etc.) is hand-written C, not shared with the interpreter or VM. For example, `vm_device_print_files()` is a 50-line rewrite of the 130-line `cmd_files()` — simpler but less capable (no sorting, no wildcards). Each new shell command requires another bespoke implementation.

**Recommendation:** Route unknown shell input through compile-and-execute (see section 6.4 Option A). Shell commands that already work as VM syscalls would "just work" at the prompt.

### 4.4 String temp buffer depth

`vm_get_str_temp()` (`bc_vm.c:33-37`) rotates through 4 buffers. Deeply nested string expressions (e.g., `LEFT$(RIGHT$(MID$(UCASE$(a$), 1, 5), 3), 2) + CHR$(65)`) needing >4 intermediates would silently recycle a live buffer.

### 4.5 No bounds checks on slot indices

`op_load_i/f/s`, `op_store_i/f/s`, and all array ops index into `vm->globals[]` with a `uint16_t` slot from the bytecode stream, with no bounds check against `BC_MAX_SLOTS`. Same for jump targets — `op_jmp_abs` doesn't validate `addr < bytecode_len`.

If the compiler is trusted this is fine, but bytecode corruption (flash bit-flip) could cause arbitrary memory access.

---

## 5. Test Coverage

### 5.1 Strengths

- 180 host tests with oracle comparison (interpreter output == VM output)
- Covers: arithmetic, strings, control flow, arrays, functions, recursion, graphics, file I/O, benchmarks
- Pixel assertion infrastructure for graphics validation
- Framebuffer snapshot comparison
- Boundary tests for integers, shifts, empty strings, array dimensions
- Stress tests for EXIT fixup limits (>16 exits) and SELECT CASE (30 cases)
- String temp buffer depth testing (nested string function calls)

### 5.2 Gaps

| Gap | Severity | Notes |
|-----|----------|-------|
| ~~No boundary testing (INT64_MIN, empty strings, max array dims)~~ | ~~Medium~~ | Resolved: t166–t169 |
| ~~No stress testing of fixup limits (>16 exits)~~ | ~~Medium~~ | Resolved: t170, t172, t173 |
| `missing_syscalls/` and `unsupported/` test directories empty | Medium | |
| Timer-dependent tests can flake | Low | |
| No test isolation between runs (shared global state) | Medium | |

---

## 6. REPL Reconciliation Strategy

### 6.1 Current state — the device is already fully VM

The device build uses the legacy prompt with VM-owned program execution. `RUN` goes through `bc_run_source_string()` -> compile -> VM execute.

The legacy interpreter (`PicoMite.c` / `MMBasic.c` / `ExecuteProgram()`) exists only in the host build as the test oracle for comparison testing.

### 6.2 What the current shell can do

The `vm_device_main.c` shell is a simple command dispatcher with hardcoded entries:

| Command | Handler |
|---------|---------|
| `RUN [file]` | `vm_device_run_program()` — compile and VM-execute |
| `LOAD file` | Load source into memory |
| `FILES`, `CD`, `MKDIR`, `RMDIR`, `KILL`, `COPY`, `RENAME` | File management (bespoke C reimplementations) |
| `CLS`, `FREE`, `NEW`, `PWD`, `LIST` | System utilities |
| `DRIVE` | Switch A:/B: filesystem |
| bare filename | Treated as `RUN filename` |

### 6.3 What the shell cannot do — the REPL gap

The current shell **cannot execute immediate-mode BASIC**. Examples that don't work:

- `? 2+2` — evaluate and print expression
- `A = 5` — assign a variable interactively
- `FOR I = 1 TO 10 : ? I : NEXT` — inline multi-statement
- `DIM A(10)` — create a variable outside a program
- `? LEFT$("HELLO", 3)` — call functions interactively
- `PRINT MM.HRES` — query system state

In the legacy MMBasic interpreter, the REPL loop tokenizes one line and calls `ExecuteProgram(tknbuf)` — the exact same dispatch loop used for stored programs. This gives full immediate-mode capability for free.

Additionally, shell commands like `FILES` are bespoke reimplementations that don't share code with the VM's `OP_SYSCALL` handlers. This means every new shell capability requires writing it twice.

### 6.4 Options for adding immediate-mode support

#### Option A — Compile-and-execute single statements (recommended)

Treat immediate-mode input the same as a one-line program: compile it to bytecode, execute via VM, discard.

- The `bc_source.c` compiler already handles single statements
- Variables would need to persist across immediate-mode invocations (the VM currently allocates/frees all state per run)
- Control flow spanning multiple lines (multi-line IF, FOR typed one line at a time) would require accumulating input
- Shell commands like FILES would work automatically if they're already supported as VM syscalls
- Eliminates the need for bespoke shell command reimplementations
- **Effort: Medium.** The main challenge is persistent variable state between immediate-mode invocations.

#### Option B — Thin interpreter for immediate mode only

Build a minimal immediate-mode evaluator that handles:
- Expression evaluation and PRINT (the most common REPL use)
- Variable assignment
- Single-line FOR/IF

This avoids the full compilation pipeline for one-off commands. It would be a small, purpose-built evaluator — not a port of the legacy interpreter's 300+ command dispatch.

- **Effort: Medium.** Smaller surface area than Option A but new code to maintain.

#### Option C — Keep the shell as-is (pragmatic)

The current shell handles program execution and file management. If the primary use case is running `.bas` programs (not interactive exploration), the REPL gap may not matter in practice.

- **Effort: None.**
- **Trade-off:** Users accustomed to MMBasic's interactive mode will notice the limitation. Each new shell command requires another bespoke reimplementation.

### 6.5 Recommended path forward

#### Phase 1 — Fix critical bugs (immediate)

Address the correctness issues identified in section 1 of this review. The VM is already the production execution path — these bugs affect real programs.

#### Phase 2 — Incremental syscall coverage

For each command not yet covered by a native opcode or syscall:

1. Add a `BC_SYS_*` entry
2. Write a thin wrapper that receives pre-evaluated arguments from the VM stack and calls the underlying C implementation directly
3. Each new syscall is one small function, testable in isolation on host

Priority order: `INPUT`, `SORT`, `ON GOTO`/`GOSUB`, `SWAP`, `ERASE`, `ON ERROR`

#### Phase 3 — Immediate mode (Option A)

If immediate-mode BASIC is desired, the compile-and-execute approach (Option A) is the most consistent with the existing architecture. Key work items:

1. Persistent `BCVMState` (or at least persistent global variable slots) across immediate-mode invocations
2. Single-statement compilation mode in `bc_source.c`
3. Shell integration: detect non-command input -> compile -> execute -> preserve state
4. Multi-line accumulation for block constructs (IF/FOR/WHILE entered one line at a time)

This is incremental — start with expression evaluation (`? 2+2`) and variable assignment, then add control flow later. As syscall coverage grows (Phase 2), more commands become available at the prompt for free.

---

## 7. Immediate Action Items

| Priority | Item | Effort | Status |
|:--------:|------|--------|--------|
| 1 | Add underflow guards to `POP_I/POP_F/POP_S` | Small | Open |
| 2 | Fix `exit_fixups` limit (raise array or error on overflow) | Trivial | **Done** — raised to 64, tests t170/t172 |
| 3 | Guard integer UB (INT64_MIN negation/division, shifts >= 64) | Small | Open |
| 4 | Allocate FOR limit/step from local slots inside SUB/FUNCTION | Medium | **Done** — see 1.5 |
| 5 | Fix `OP_CLEAR` to zero VM variable storage | Small | Open |
| 6 | Delete dead code (tokenized compiler, FTEST, stale stubs) | Trivial | **Done** |
| 7 | Update `CMakeLists 2350.txt` to match active source list | Trivial | **Done** |
| 8 | Add boundary tests (INT64_MIN, >16 exits) | Medium | **Done** — t166–t173 (8 new tests) |
| 9 | Fix Makefile header dependencies (auto-generate with `-MMD -MP`) | Trivial | **Done** |
