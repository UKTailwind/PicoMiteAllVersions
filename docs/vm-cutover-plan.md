# VM Cutover Plan

For the current architecture snapshot, see [vm-architecture.md](./vm-architecture.md).

## Goal

Move to a prototype architecture with:

1. A host-only legacy interpreter preserved as the semantic oracle.
2. VM-owned BASIC program execution on device.
3. The legacy prompt handles OS/shell tasks; BASIC program execution goes through the VM.

## Principles

- The legacy host interpreter remains untouched for semantic validation.
- The device firmware does not carry both the full interpreter and the VM.
- `RUN` executes through the VM.
- `FRUN` is removed from the user model.
- Missing VM functionality faults immediately.
- No bridge or interpreter fallback exists on device.
- On-device `.bas` execution compiles source through a VM-owned frontend, not through the legacy interpreter tokenizer.
- Native VM syscalls must be implemented in VM-owned runtime files by copying/adapting the useful legacy implementation logic. They must not wrap, call, or dispatch through legacy interpreter syscall handlers or helper entrypoints such as `cmd_*`, `fun_*`, `ExtCfg`, `ExtSet`, `ExtInp`, `DrawCircle`, or file command handlers.
- Shared immutable tables, device option state, and hardware SDK primitives may be used by VM syscall modules where needed, but the syscall behavior itself must live in `vm_sys_*` code.
- Legacy modules such as `FileIO.c`, `Commands.c`, `Functions.c`, and old drawing command handlers remain oracle-owned unless they are only changed behind a host/device shim boundary.
- **!!! THIS IS A BLOCKING REPOSITORY RULE, NOT A STYLE PREFERENCE OR BEST-EFFORT GUIDELINE !!!**
- **!!! VM SYSCALL CONVERSIONS MUST COPY LEGACY IMPLEMENTATION CODE INTO VM-OWNED MODULES AND ADAPT IT THERE !!!**
- **!!! IF THE LEGACY IMPLEMENTATION DEPENDS ON HELPERS, COPY/ADAPT THOSE HELPERS TOO !!!**
- **!!! DO NOT INVENT NEW ALGORITHMS WHEN LEGACY CODE ALREADY EXISTS !!!**
- **!!! DO NOT LINK, WRAP, OR DISPATCH BACK INTO LEGACY HANDLERS OR LEGACY DRAWING/FILE HELPER ENTRYPOINTS !!!**
- **!!! NOVEL CODE SHOULD BE LIMITED TO VM FRONTEND/DISPATCH GLUE, REQUIRED ADAPTATION LAYERS, AND HOST MOCKS/SHIMS !!!**

## Current Highest Priorities

These are the current highest-priority architecture items. New syscall expansion is subordinate to these until they are tightened.

1. Oracle independence
   - The host legacy interpreter must remain a real semantic oracle.
   - `host/host_stubs_legacy.c` must not call `vm_sys_*` code.
   - Host oracle support must come from legacy interpreter code plus host hardware/environment shims only.
   - If host support is missing for a legacy-safe feature, copy/adapt legacy logic into the host shim boundary rather than routing through VM-owned implementations.

2. VM memory separation by lifetime
   - Runtime heap, program image storage, compile scratch, and syscall scratch must not remain one monolithic allocator domain.
   - The target model is explicit lifetime separation:
     - compile arena
     - compact program image
     - runtime heap for mutable BASIC state
     - fixed VM state and bounded syscall scratch
   - Avoid transient syscall allocations from the same allocator pool used by long-lived BASIC runtime objects.

3. Standardize the syscall ABI
   - Move toward a standard generic VM syscall ABI instead of continuing to add bespoke per-command encodings.
   - Default design:
     - one generic syscall/intrinsic call format
     - stable argument passing and return conventions
     - one decoder/dispatch path in the VM
   - Optional dedicated opcodes remain allowed only for proven hot paths where profiling or code-size measurements justify them.
   - The generic ABI is the default. Dedicated opcodes are the exception.

4. Isolate the device build from the legacy allocator API
   - `GetMemory`, `GetSystemMemory`, `GetTempMemory`, `FreeMemory`, and `ReAllocMemory` must not exist on the PicoCalc RP2350 VM/device path.
   - This requires a build-target split, not more local allocator patching.
   - Current blocker:
     - the RP2350 firmware target still links major legacy modules such as `MMBasic.c`, `Commands.c`, `Functions.c`, `FileIO.c`, and `Draw.c`
     - the VM/runtime layer still imports a narrowed but still critical legacy support surface such as `error`, `mark`, `MMPrintString`, `Option`, display globals, numeric/string conversion helpers, and prompt/display state
     - `vm_device_support.h` is now the live seam for these imports; it must shrink until the legacy core source list can be removed from the RP2350 target
   - Required sequence:
     1. replace the remaining runtime support surface the VM still imports from legacy modules
     2. split the RP2350 device target source list from the host oracle source list
     3. make the legacy allocator API unavailable in the device target so remaining violations fail at build time
     4. replace the temporary VM support imports with VM-owned or shell-owned implementations as needed

## Plan

### 1. Freeze a host-only legacy interpreter

Status: in progress.

- Create and preserve a dedicated host target that keeps the original interpreter semantics.
- Keep the existing shared host backends for framebuffer, files, timers, input, and screenshots.
- Do not let VM/syscall refactors modify this oracle path.
- Use it as the correctness reference for language features and host-safe syscalls.
- Current implementation uses `ports/host_native/build/mmbasic_test` with `--interp` as the oracle path and `--vm` as the implementation path.
- The active host shim is `host/host_stubs_legacy.c`.
- Remaining gap: some host legacy shims still route through VM-owned syscall helpers. This breaks oracle independence and must be removed before the oracle can be treated as trustworthy for those families.

### 2. Remove interpreter execution from device firmware

Status: in progress.

- Remove general interpreter program execution from the device runtime.
- Do not ship both the interpreter runtime and the VM runtime in firmware.
- Replace legacy BASIC tokenising for `RUN` with a VM-owned source frontend.
- Keep only the prompt/file/editing pieces needed for shell commands while this transition is underway.
- `RUN` no longer executes `ProgMemory` through `ExecuteProgram()`.
- PicoCalc RP2350 no longer reserves the legacy `AllMemory` heap; the legacy allocator API is a compatibility wrapper over `bc_alloc.c`.
- VM/compiler/runtime framebuffer buffers and remaining linked shell/display/file allocation calls now use the same device allocator.
- The remaining interpreter table reservations are `g_vartbl` and `funtbl`; these persist only because interpreter-adjacent modules are still linked.
- The device build still carries interpreter/tokenising code for prompt, editing, command table, and runtime dependencies. The legacy prompt infrastructure remains the active shell.
- Native syscall extraction proceeds only through VM-owned source frontend opcodes and VM-owned `vm_sys_*` runtime modules.
- Progress made toward the split:
  - live VM core files (`bc_runtime.c`, `bc_debug.c`, `bc_vm.c`, `vm_sys_*`, and `gfx_*_shared.c`) no longer include `MMBasic.h` / `MMBasic_Includes.h` directly
  - these imports now flow through `vm_device_support.h`, which exposes the remaining legacy support surface explicitly
  - `CMakeLists.txt` now separates `PICOMITE_BASE_SOURCES`, `PICOMITE_LEGACY_CORE_SOURCES`, and `PICOMITE_VM_SOURCES`

### 2B. Split VM memory by lifetime

Status: started.

- Current device allocator state:
  - `bc_alloc.c` reserves a fixed `bc_heap` arena in `.bss`; default size is 256 KiB.
  - `BC_ALLOC()` uses this arena on device and `calloc/free` on host.
  - `BCCompiler`, `BCVMState`, compiler tables, bytecode, constants, metadata, globals, local tables, BASIC arrays, and mutable string buffers all currently share this one arena.
  - `BCVMState` contains the VM operand stack, call stack, GOSUB stack, FOR stack, and temp string buffers inline, so those stacks are arena-backed only because `BCVMState` itself is arena-allocated.
- This was useful as a prototype because it removed dependence on the legacy interpreter allocator and gave deterministic reset/ownership.
- It is not the desired final memory model because compile-only scratch, immutable program image data, and runtime mutable state have different lifetimes but currently consume one always-reserved `.bss` block.
- Target memory model:
  - Fixed VM instance state is explicit static/global storage or an explicitly reserved small VM state block.
  - Compiler scratch uses a temporary compile arena and is released before execution.
  - Bytecode, constants, DATA, symbol metadata, and sub/function metadata become a compact program image.
  - Immutable program image data should live in flash or a packed program-image buffer; mutable globals/arrays/strings stay in runtime storage.
  - Runtime heap should hold BASIC arrays, mutable strings, and other dynamic runtime objects only.
  - Shrink or eliminate the fixed 256 KiB `bc_heap` after the lifetimes are separated and device/high-water measurements justify the new size.
- Completed first implementation slice:
  - `BCCompiler` and `BCVMState` themselves are no longer allocated from `bc_heap` on device; they are explicit device-static state.
  - The VM runner no longer resets `bc_heap` on normal entry/exit because the caller's source buffer may live in that arena.
  - Arena-owned source text is released immediately after successful compile, before compiler compaction and VM execution.
  - Source file loading now allocates based on actual file size instead of reserving the maximum edit buffer for every `.bas` file.
  - `bc_vm_alloc()` now runs after successful compile and compiler compaction, removing VM runtime tables from compile-time peak memory.
  - RP2350 `bc_heap` is reduced from 256 KiB to 232 KiB.
  - `MEMORY` on PicoCalc RP2350 now reports VM arena capacity, current use, and high-water usage for device-driven sizing.
  - Compiler tables now allocate from a temporary compiler arena carved from the top of the same device heap, not from the general runtime allocator.
  - Compiler compaction copies retained program-image structures from the temporary compiler arena into the runtime allocator, then releases the entire compiler arena before execution.
  - This is not a rigid fixed pool split for runtime lifetimes; it is a temporary compile-time carve-out whose space returns to the runtime allocator before program execution.
  - Hot graphics scratch no longer churns the general runtime allocator on each draw; `vm_sys_graphics.c` now owns reusable grow-on-demand scratch buffers that are reset between runs.
  - Non-graphics syscall scratch still shares the same allocator domain as long-lived runtime objects; that remains the main correctness/supportability risk under memory pressure.
  - Rebuild and measure `.bss`/headroom after each slice.

### 2C. Standardize the VM syscall ABI

Status: started.

- Replace continued growth of bespoke per-command operand layouts with a standard generic syscall/intrinsic ABI.
- Default ABI target:
  - generic syscall opcode
  - syscall/intrinsic id
  - standard argument count and ordering
  - standard result convention
  - one common VM-side decode/dispatch path
- Keep dedicated opcodes only for:
  - core arithmetic/control-flow primitives
  - demonstrably hot builtins or syscalls where measurements justify specialization
- Do not add new bespoke operand encodings unless they are explicitly treated as dedicated hot opcodes.
- Compiler/frontend work should converge on emitting the generic ABI by default.
- VM runtime work should converge on decoding through a shared dispatcher by default.
- This is now the primary architecture direction for future syscall expansion.
- Completed first implementation slice:
  - `OP_SYSCALL` now exists as the standard VM syscall/intrinsic envelope.
  - `bc_source.c` now emits the current implemented VM syscall surface through `OP_SYSCALL` by default instead of command-specific opcodes.
  - The shared syscall path currently covers:
    - graphics commands and graphics/query builtins
    - `FRAMEBUFFER` (CREATE, LAYER, WRITE, CLOSE, MERGE, SYNC, WAIT, COPY)
    - `FASTGFX`
    - `DATE$`, `TIME$`, `KEYDOWN()`, `PAUSE`
    - `PLAY`
    - `SETPIN`, `PIN()`, `PWM`, `SERVO`
    - current file commands and file I/O operations
  - Dedicated legacy-style opcodes still exist in the runtime as compatibility/hot-path candidates, but they are no longer the default compiler target for the implemented syscall surface.
- Remaining ABI work:
  - remove or quarantine obsolete dedicated syscall opcodes once the generic path is stable enough
  - move any remaining current syscall implementations that still depend on bespoke sub-encodings onto cleaner shared metadata formats where practical
  - decide which dedicated opcodes are actually justified by profiling and code-size measurements

### 2D. Peephole optimizer

Status: implemented — 7 optimization families active.

- The source frontend (`bc_source.c`) now includes a compile-time peephole optimizer.
- Controlled by `bc_opt_level`: 0 = disabled, 1 = enabled (default).
- CLI: `-O0` / `-O1`.
- Operates on a 1-2 instruction window immediately after parsing each statement/expression.

Implemented superinstructions:

| Opcode | Pattern | Bytecode |
|--------|---------|----------|
| `OP_MATH_MULSHR` | `(a * b) \ 2^n` | `0xEE` |
| `OP_MATH_SQRSHR` | `(a * a) \ 2^n` | `0xF9` |
| `OP_MATH_MULSHRADD` | `((a * b) \ 2^n) + c` | `0xFA` |
| `OP_JCMP_I` | integer compare + branch | `0xFB` |
| `OP_JCMP_F` | float compare + branch | `0xFC` |
| `OP_MOV_VAR` | `a = b` typed copy | `0xFD` |

Additional rewrites: `INC a%, const` and `INC a%, expr` / `INC a, expr` fold constant/expression increments into direct assignment, eliminating the `INC_I`/`INC_F` opcode.

Testing methodology:

- **Peephole assertion tests** (`tests/frontend/t0XX_*_peephole.bas`): run `--vm-disasm` and verify the fused opcode appears / unfused form is absent.
- **Equivalence tests** (`tests/frontend/t0XX_*_opt_equiv.bas`): compare `-O0` vs `-O1` output byte-for-byte to verify behavioral correctness across sign combinations, boundary values, and type edge cases.
- **Smoke test**: mandelbrot with `-O1` as a full-program integration check.
- Runner: `./ports/host_native/run_optimizer_tests.sh` (22 tests).

Remaining optimizer work:
- profile on device to measure actual cycle savings for each superinstruction
- evaluate whether additional hot patterns (e.g., array index, FOR loop step) justify new fused opcodes
- consider a post-compile pass for optimizations that span more than the current 1-2 instruction window

### 2A. Build a VM-owned source frontend

Status: started.

- Add a raw-source compiler entrypoint that emits bytecode directly.
- Do not depend on `tokenise()`, `commandtbl`, `tokentbl`, `cmd_*`, `fun_*`, or handler function pointers for device compilation.
- Keep the old tokenized compiler only as a temporary compatibility path while the new frontend reaches feature parity.
- Host differential testing should compare:
  - `source -> legacy interpreter`
  - `source -> VM source frontend -> bytecode -> VM`
- First implemented slice:
  - `bc_source.c` / `bc_source.h`
  - host `--vm-source` mode
  - host `--source-compare` mode
  - `PRINT` with literal integer, float, string, parenthesized string concatenation, `?`, `;`, and colon-separated statements
- Next frontend slices should migrate language constructs before any more syscall expansion:
  - scalar variables and assignment
  - arithmetic/comparison expression precedence
  - `IF`/`THEN`/`ELSE`
  - `FOR`/`NEXT`
  - `GOTO`/labels/line references
  - `DIM` and arrays
  - `SUB`/`FUNCTION`

### 3. Replace the device prompt with a minimal shell

Status: partially complete.

- Support operational commands such as:
  - `RUN`
  - `LOAD`
  - `SAVE`
  - `FILES`
  - `NEW`
  - `LIST`
  - `EDIT`
  - selected `OPTION` and system commands
- Reject arbitrary immediate BASIC at the prompt with a clear error.
- Treat the prompt as OS control, not a BASIC REPL.
- Current prompt rejects arbitrary immediate BASIC with `Immediate BASIC disabled`.
- The firmware uses the legacy prompt for shell commands.
- BASIC program execution goes through the VM via `RUN`.

### 4. Make `RUN` VM-owned

Status: complete for the current prototype path.

- `RUN` should always:
  - load source
  - compile source through the VM-owned frontend
  - compile to bytecode
  - execute on the VM
- Remove `FRUN` from the user model.
- `FRUN` has been removed from the command table rather than aliased.
- Host VM mode calls `bc_run_current_program()` directly.

### 5. Eliminate bridge and fallback behavior

Status: complete for the VM compiler/runtime path.

- Any unimplemented VM syscall or statement must fault immediately.
- No bridge to the interpreter.
- No interpreter fallback on device.
- New VM syscall support should not call through legacy interpreter command/function handlers.
- If old implementation code is needed, copy/adapt the minimum required logic into a `vm_sys_*` native runtime module and keep legacy behavior available for host oracle comparison.
- This is a hard rule: VM syscalls must not wrap existing legacy syscall functions. A conversion is not complete if the VM opcode reaches back into `cmd_*`, `fun_*`, `Ext*`, old drawing handlers, or old file command handlers.
- `OP_BUILTIN_*` bridge opcodes and VM handlers have been removed.
- The old `bc_bridge.c` implementation has been removed and the remaining VM runtime entrypoints live in `bc_runtime.c`.
- Unsupported commands/functions now fail at compile time with `Unsupported VM command: ...` or `Unsupported VM function: ...`.
- Negative tests live under `ports/host_native/tests/unsupported/` and are run by `ports/host_native/run_unsupported_tests.sh`.
- Current graphics VM syscalls dispatch through the VM-owned `vm_sys_graphics.c` module: `BOX`, `RBOX`, `ARC`, `TRIANGLE`, `POLYGON`, `CIRCLE`, `CLS`, `LINE`, `PIXEL`, `TEXT`, `FONT`, and `COLOUR`/`COLOR`.
- The RP2350 `CIRCLE` path no longer calls the legacy `DrawCircle()` implementation that allocates scratch storage through `GetTempMemory()`.
- `DATE$` and `TIME$` now dispatch through `vm_sys_time.c`; `KEYDOWN()` dispatches through `vm_sys_input.c`.
- `MM.INFO(HRES)` and `MM.INFO(VRES)` compile to VM-native display-size queries.
- `PLAY TONE`/`PLAY STOP` dispatch through `vm_sys_audio.c`.
- `SETPIN`/`PIN()` digital forms dispatch through `vm_sys_pin.c`, which owns copied/adapted GPIO mapping/config/read/write logic instead of calling legacy `ExtCfg`/`ExtSet`/`ExtInp`.

### 6. Use host differential testing as the main validation loop

Status: active.

- `legacy host interpreter` is the semantic oracle.
- `host VM` is the implementation target.
- Compare outputs, errors, and host-rendered framebuffers where meaningful.
- Oracle tests must stay within the syntax and behavior accepted by the legacy host interpreter.
- If the legacy interpreter rejects a construct, that test is not a valid oracle-comparison test until it is rewritten into a legacy-valid form.
- Use device tests mainly for hardware-facing behavior, timing, and memory/resource validation.
- Remaining gap: the host oracle is currently not fully independent for some file/pin/PWM paths because parts of the host legacy shim call VM-owned helpers. That must be corrected.
- Current baseline:
  - `./ports/host_native/run_tests.sh`: `168 passed, 0 failed`.
  - `./ports/host_native/run_pixel_tests.sh`: passing framebuffer assertions.
  - `./ports/host_native/run_host_shim_tests.sh`: `4 passed, 0 failed`.
  - `./ports/host_native/run_frontend_tests.sh`: `48 passed, 0 failed`.
  - `./ports/host_native/run_optimizer_tests.sh`: `22 passed, 0 failed`.
  - `bash ports/host_native/run_unsupported_tests.sh`: `0 passed, 0 failed`.
  - `./ports/host_native/run_missing_syscall_tests.sh`: `0 passed, 0 failed`.

### 7. Separate language correctness from hardware validation

Status: active.

- For language/runtime behavior and host-safe syscalls, trust the host oracle.
- For display, FASTGFX, timing, DMA, SD, and resource limits, validate on device.
- Do not let shared host approximations weaken the interpreter's role as the core semantic reference.
- Host framebuffer tests are useful for deterministic drawing regressions, but LCD clearing, frame pacing, SD behavior, and memory pressure remain device-validation concerns.

### 8. Future work, not part of this cutover

Status: deferred.

- A VM-based shell.
- A full VM REPL.
- Migrating prompt command execution into the VM.

## Current Build And Test Commands

```bash
make -C ports/host_native
./ports/host_native/run_tests.sh
./ports/host_native/run_pixel_tests.sh
./ports/host_native/run_host_shim_tests.sh
./ports/host_native/run_frontend_tests.sh
./ports/host_native/run_optimizer_tests.sh
bash ports/host_native/run_unsupported_tests.sh
./ports/host_native/run_missing_syscall_tests.sh
make -C build2350 -j8
arm-none-eabi-size build2350/PicoMite.elf
```

Current firmware size snapshot:

```text
text=976656  data=0  bss=296140  dec=1272796
bc_heap=232 KiB
```

## Success Criteria

- Device firmware contains one execution engine for BASIC programs: the VM.
- Device prompt no longer executes arbitrary BASIC.
- Host retains an untouched interpreter for semantic regression testing.
- Missing VM functionality fails loudly instead of silently bridging.

## Remaining Work

- Highest current architecture priorities:
  - restore full host oracle independence by removing `vm_sys_*` calls from `host/host_stubs_legacy.c`
  - finish memory separation by lifetime and remove transient syscall scratch from the general runtime heap
  - standardize the syscall ABI around a generic call/dispatch path, with dedicated opcodes only for justified hot paths
- Continue removing interpreter-only runtime from the device image where it is no longer needed by shell/tokenising/editor workflows.
- Expand the VM-owned source frontend until the device `RUN` path no longer needs legacy tokenising.
- After the frontend cut is in place, continue native VM syscall coverage only where the legacy interpreter supports the same BASIC semantics, implemented outside the legacy interpreter modules.
- Graphics is the highest-priority command-family backlog.
- **!!! GRAPHICS CONVERSIONS MUST COPY/ADAPT LEGACY DRAWING CODE INTO VM-OWNED HELPERS AND SYSCALLS !!!**
- **!!! GRAPHICS CONVERSIONS MUST NOT LINK, WRAP, OR DISPATCH THROUGH OLD DRAWING HANDLERS OR COMMAND ENTRYPOINTS !!!**
- Use `ports/host_native/tests/missing_syscalls/` for fail-first syscall work; move or delete entries when native VM support lands.
- Deferred syscall extraction target is `vm_sys_file.c` for `OPEN`/`CLOSE`/file print-input/`FILES`.
- Expand `vm_sys_pin.c` beyond digital `OFF`/`DIN`/`DOUT` only by copying/adapting each supported legacy mode into the VM-owned module with fail-first oracle/device coverage.
- Replace the temporary PicoCalc/RP2350 allocator compatibility layer by migrating surviving prompt/device services onto explicit VM/system allocation APIs or removing the legacy modules that still require it.
- Keep unsupported VM functionality as loud compile/runtime faults with regression coverage.
- Add device-specific tests for LCD clearing, FASTGFX swaps, frame pacing, SD interactions, and long-running memory pressure.

## Legacy Command Coverage Inventory

The authoritative command-coverage matrix now lives in [vm-command-coverage.md](./vm-command-coverage.md).

It is derived from `AllCommands.h` and uses only:

- `implemented`
- `partial`
- `unimplemented`

That document is about the VM BASIC program path, not the prompt shell.
