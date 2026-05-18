# VM Architecture Snapshot

This document describes the current VM-oriented prototype architecture as it exists in the tree today.

## Scope

- BASIC program execution on device is VM-owned.
- The host contains both:
  - the legacy interpreter oracle
  - the VM implementation and test harness
- The firmware carries both the legacy prompt/control infrastructure and VM-owned execution.

## High-Level Shape

### Host

- `host/mmbasic_test --interp`
  - runs the legacy interpreter path
  - uses `host/host_stubs_legacy.c` as the host hardware/environment shim boundary
- `host/mmbasic_test --vm`
  - runs the VM-owned source frontend and VM runtime
- default compare mode
  - runs interpreter first, then VM
  - compares stdout and error behavior
- pixel tests
  - compare host-rendered framebuffers where the host oracle supports the primitive

### Device

One firmware target:

- **`build2350/`** — `cmake ..` then `make -C build2350 -j8`
  - legacy interpreter prompt with VM-owned BASIC program execution
  - `RUN` compiles source through the VM-owned frontend and executes on the VM

## Major Components

### Legacy Oracle Path

- Core sources:
  - `core/mmbasic/MMBasic.c`
  - `core/mmbasic/Commands.c`
  - `core/mmbasic/Functions.c`
  - `core/mmbasic/Operators.c`
  - `core/mmbasic/MATHS.c`
  - `core/mmbasic/Memory.c`
- Host boundary:
  - `host/host_stubs_legacy.c`
- Purpose:
  - semantic oracle for language behavior
  - oracle for host-safe syscalls where the host shim is independent

Current caveat:
- oracle independence is not complete yet
- some host legacy shim paths still route through `vm_sys_*`
- until that is removed, those families are weaker oracle coverage than pure interpreter-only paths

### VM Frontend and Runtime

- Source frontend:
  - `runtime/vm/bc_source.c`
- Compiler/core metadata:
  - `runtime/vm/bc_compiler_core.c`
  - `runtime/vm/bytecode.h`
- VM execution:
  - `runtime/vm/bc_vm.c`
  - `runtime/vm/bc_runtime.c`
- Debug/disassembly:
  - `runtime/vm/bc_debug.c`

The VM frontend compiles raw `.bas` source directly. It does not depend on the legacy tokenizer for the VM path.

## VM Execution Model

### Core VM Ops

These remain dedicated VM instructions:

- stack/value movement
- variable and array load/store
- type conversion
- arithmetic, comparisons, and bitwise ops
- control flow
- call/frame management
- DATA/READ/RESTORE
- hot language/runtime builtins

These are language/runtime semantics, not syscalls.

### Syscall ABI

The default syscall path is now a generic ABI:

- `OP_SYSCALL`
- syscall/intrinsic id
- standard decode/dispatch path in `bc_vm.c`

Current implemented syscall families already emitted through the generic ABI by default:

- graphics and graphics-query builtins
- `FRAMEBUFFER` (CREATE, LAYER, WRITE, CLOSE, MERGE, SYNC, WAIT, COPY)
- `FASTGFX`
- `DATE$`, `TIME$`, `KEYDOWN()`, `PAUSE`
- `PLAY`
- `SETPIN`, `PIN()`, `PWM`, `SERVO`
- current file commands and file I/O operations

Dedicated opcodes still exist for core VM semantics and may remain for proven hot paths, but they are no longer the default expansion pattern for syscalls.

### Peephole Optimizer

The source frontend includes a compile-time peephole optimizer controlled by `bc_opt_level` (default: 1, CLI: `-O0`/`-O1`).

The optimizer automatically rewrites common instruction sequences into fused superinstructions during compilation. It operates on a 1-2 instruction peephole window immediately after parsing. All rewrites are transparent — the user writes normal BASIC and gets faster bytecode.

#### Implemented superinstructions

| Opcode | Pattern | Effect |
|--------|---------|--------|
| `OP_MATH_MULSHR` | `(a * b) \ 2^n` | Fused multiply + integer shift-right |
| `OP_MATH_SQRSHR` | `(a * a) \ 2^n` | Same-variable squared variant |
| `OP_MATH_MULSHRADD` | `((a * b) \ 2^n) + c` | MULSHR cascaded with addition |
| `OP_JCMP_I` | integer compare + conditional jump | Fused comparison + branch |
| `OP_JCMP_F` | float compare + conditional jump | Float variant of JCMP |
| `OP_MOV_VAR` | `a = b` (simple assignment) | Direct typed variable copy |

Additional peephole rewrites:
- `INC a%, const` — constant increment folded into assignment
- `INC a%, expr` / `INC a, expr` — expression increment folded for integer and float

These optimizations are particularly effective for fixed-point arithmetic patterns (e.g., Mandelbrot inner loops using `(a * b) \ SCALE`), where the fused MULSHR/MULSHRADD operations avoid intermediate stack traffic.

#### Testing methodology

Peephole optimizations are tested in paired fashion:

- **Peephole tests** (`t0XX_*_peephole.bas`): run with `--vm-disasm` and verify the expected fused opcode appears (and the unfused form does not).
- **Equivalence tests** (`t0XX_*_opt_equiv.bas`): compare `-O0` output vs `-O1` output byte-for-byte to verify behavioral correctness.

A mandelbrot smoke test runs the full program with `-O1` as an integration check.

#### Implementation

Fusion functions in `bc_source.c`: `source_try_fuse_mulshr()`, `source_try_fuse_mulshradd()`, `source_try_fuse_mov_assignment()`, `source_jcmp_relation()`, `source_emit_rel_jump()`. Opcodes are defined in `bytecode.h`.

## Fast Loop Optimization

The compiler includes a register micro-op optimization for DO WHILE/LOOP loops. It converts stack-based bytecode into a register-based micro-op program that executes entirely within a flat register file, eliminating stack push/pop overhead and enabling better data locality.

### How it works

1. The compiler emits normal stack bytecodes for the loop body
2. After the loop is complete, a post-pass (`source_convert_fast_loop`) attempts to convert the stack bytecodes into register micro-ops
3. If conversion succeeds, the original bytecodes are replaced with a single `OP_FAST_LOOP` instruction containing the micro-op program
4. If conversion fails (unsupported opcodes, too many registers, etc.), the original bytecodes are kept unchanged

The register file layout is: `[locals][globals][constants][temps]`

### Two modes of operation

**Automatic** — The compiler silently attempts fast loop conversion on every DO WHILE/LOOP. If conversion fails, it falls back to normal bytecode with no error. The user never knows whether a loop was optimized or not.

**Explicit (`'!FAST` annotation)** — Placing a `'!FAST` comment on the line before a DO WHILE loop requires the optimization to succeed. If conversion fails, it is a compile error. This lets performance-sensitive code assert that the optimization is applied.

```basic
'!FAST
Do While i% < limit%
  ' ... integer-only loop body ...
Loop
```

### What can be optimized

The converter supports:
- Integer and float arithmetic (`+`, `-`, `*`, `\`, `MOD`)
- Bitwise ops (`AND`, `OR`, `XOR`, `<<`, `>>`)
- Comparisons (all 6 relational operators)
- Local and global variable load/store
- 1D array load/store
- Type conversions (`INT`↔`FLOAT`)
- Conditional and unconditional jumps (`IF`/`EXIT DO`)
- Fused operations (`MULSHR`, `SQRSHR`, `MULSHRADD`, `JCMP`)

### What cannot be optimized

- Loops at module scope (global variable register allocation limitation)
- String operations
- Function/sub calls
- PRINT or other I/O
- Multi-dimensional array access
- Any syscall

Loops containing unsupported operations silently fall back to normal bytecode in auto mode, or produce a compile error in `'!FAST` mode.

### Performance

Benchmarked at ~3x speedup on integer-heavy inner loops (Mandelbrot, Sieve of Eratosthenes).

### Cost

- +7 KB flash, +0 bytes static RAM
- ~12 KB temporary heap during compilation (freed after)
- 512 bytes stack during execution (register file)

### Implementation

- Converter: `source_convert_fast_loop()` in `bc_source.c`
- Executor: `op_fast_loop` in `bc_vm.c`
- Micro-op definitions: `ROP_*` constants in `bytecode.h`

## Native Syscall Rule

VM syscall behavior must live in VM-owned code.

Rules:

- **This is a blocking repository rule, not a preference.**
- copy/adapt useful logic from legacy sources
- copy/adapt dependent legacy helpers when the legacy implementation depends on them
- do not call or wrap legacy `cmd_*`, `fun_*`, `Ext*`, or old drawing/file handlers from the VM path
- do not invent new algorithms when legacy behavior already exists
- keep novel code limited to:
  - frontend/dispatch glue
  - required adaptation layers
  - host mocks/shims

Current VM syscall modules:

- `runtime/vm/vm_sys_graphics.c`
- `runtime/vm/vm_sys_file.c`
- `runtime/vm/vm_sys_pin.c`
- `runtime/vm/vm_sys_time.c`
- `runtime/vm/vm_sys_input.c`

## Memory Model

The allocator is no longer fully monolithic, but it is not fully separated yet.

### Current State

- device heap:
  - `runtime/vm/bc_alloc.c`
  - fixed `bc_heap` arena on device
- compile scratch:
  - temporary compile arena carved from the top of the device heap
  - released wholesale before execution
- program image/runtime:
  - retained compiler output is copied into runtime-owned storage before the compile arena is released
- VM instance state:
  - `BCCompiler` and `BCVMState` are explicit device-static state, not heap-allocated on device
- graphics scratch:
  - reusable grow-on-demand scratch buffers live in `runtime/vm/vm_sys_graphics.c`
  - reset between runs with `vm_sys_graphics_reset()`

### What This Fixes

- compile-time scratch no longer competes with runtime allocations during program execution
- hot graphics paths no longer churn heap allocations on every triangle/polygon/arc/thick-circle draw

### What Still Shares the Runtime Heap

- BASIC arrays
- mutable strings
- runtime metadata/tables
- non-graphics syscall scratch that has not yet been isolated

So memory separation is improved, but not complete.

## Graphics Architecture

Graphics on the VM path are native VM syscalls, not interpreter bridge calls.

Implemented primitive families:

- `BOX`
- `RBOX`
- `ARC`
- `TRIANGLE`
- `POLYGON`
- `CIRCLE`
- `LINE`
- `PIXEL`
- `TEXT`
- `FONT`
- `COLOUR` / `COLOR`
- `CLS`
- `FASTGFX`
- `FRAMEBUFFER` (CREATE, LAYER, WRITE, CLOSE, MERGE, SYNC, WAIT, COPY)

The `FRAMEBUFFER` implementation covers the LCD-style dual-buffer model: `CREATE` allocates a frame buffer, `LAYER [colour]` allocates a layer buffer with optional transparent color, `WRITE N/F/L` switches the drawing target, `MERGE` composites the layer onto the frame (NOW/B/R/A modes), `COPY` transfers between buffers with optional background mode, and `SYNC`/`WAIT` handle synchronization.

The host framebuffer backend (`host/host_framebuffer_backend.h`) provides a full 32-bit-per-pixel simulation of the dual-buffer model for deterministic oracle comparison.

Host framebuffer comparison is useful for deterministic regressions, but device validation is still required for:

- LCD clearing
- frame pacing
- FASTGFX/device presentation behavior
- FRAMEBUFFER merge timing and background copy behavior on real hardware
- SD and memory-pressure behavior

## Prompt / Shell Boundary

The prompt boundary now depends on which device target is being discussed.

Current state:

- BASIC program execution is VM-owned
- the firmware uses the legacy prompt infrastructure
- BASIC program execution is VM-owned

## Verification Status

Current baseline at the time of this document update:

- `./host/run_tests.sh`: `188 passed, 0 failed`

## Remaining Architectural Risks

Highest-priority current risks:

1. Oracle independence is incomplete.
2. Non-graphics syscall scratch still shares the runtime heap.
3. The generic syscall ABI exists, but obsolete dedicated syscall paths still remain in the runtime and need cleanup.

## Reference Documents

- [VM Cutover Plan](./vm-cutover-plan.md)
- [VM Command Coverage](./vm-command-coverage.md)
- [Host Harness README](../host/README.md)
