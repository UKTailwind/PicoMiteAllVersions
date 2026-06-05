# MMBasic Bytecode VM -- Status & Plan

## Current Status (49/49 tests passing)

### What's Done

**Compiler** (bc_compiler*.c, ~170 KB code in flash via XIP):
- Two-pass compilation from tokenized ProgMemory
- Shunting-yard expression compiler with full type tracking
- All control flow: IF/ELSEIF/ELSE/ENDIF, FOR/NEXT, DO/LOOP, WHILE/LOOP, SELECT CASE
- SUB/FUNCTION with locals, recursion, GOSUB/GOTO with labels
- PRINT with semicolons, commas, formatting
- DIM (scalar + array), LET, DATA/READ/RESTORE
- INC, CONST, RANDOMIZE, ERROR, CLEAR
- Bridge fallback for all unhandled commands

**VM** (bc_vm.c, ~104 KB code in flash via XIP):
- Computed-goto dispatch (fast on ARM)
- ~148 opcodes implemented
- Stack-based with separate int/float/string value stacks
- Local variable frames, call stack, FOR stack
- Rotating string temp buffers

**Native Functions** (no bridge overhead):
- String: LEN, LEFT$, RIGHT$, MID$, UCASE$, LCASE$, INSTR (2/3-arg), CHR$, ASC, VAL, STR$, HEX$, OCT$, BIN$, SPACE$, STRING$, INKEY$
- Math: SIN, COS, TAN, ATN, ASIN, ACOS, ATAN2, SQR, LOG, EXP, ABS, SGN, INT, FIX, CINT, RAD, DEG, PI, MAX, MIN, RND
- Everything else: bridged to interpreter's cmd_*/fun_* functions

**Test Suite** (ports/host_native/tests/):
- 49 compare-mode tests -- all PASS (interpreter output == VM output)
- Covers: arithmetic, strings, control flow, recursion, arrays, type coercion, DATA/READ, bridge functions, edge cases, INC/CONST, RANDOMIZE/RND/SPACE$/STRING$/INKEY$

**Host Build** (host/):
- Native macOS build for off-device testing
- `./build.sh && ./run_tests.sh` -- builds and runs all 49 tests
- `./run_bench.sh` -- runs benchmarks with timing

### What's Left

## Phase 1: Memory -- Dynamic Allocation (DONE)

**Problem:** BCCompiler and BCVMState structs were too large for device heap with inline arrays.

**Solution:** Converted both structs to dynamically allocated pointer-based arrays. Platform-conditional limits via `#ifdef MMBASIC_HOST`.

### BCCompiler

API: `bc_compiler_alloc()` / `bc_compiler_free()` / `bc_compiler_init()`

| Array | Host | Device | Device Size |
|-------|------|--------|-------------|
| code[] | 64 KB | 16 KB | 16 KB |
| constants[] | 512 | 64 | ~16 KB |
| slots[] | 512 | 128 | ~8.7 KB |
| subfuns[] | 256 | 32 | ~5.4 KB |
| fixups[] | 2048 | 256 | ~3.6 KB |
| linemap[] | 4096 | 512 | ~3 KB |
| nest_stack[] | 64 | 16 | ~3.6 KB |
| data_pool[] | 1024 | 128 | ~1.1 KB |
| locals[] | 64 | 64 | ~2.2 KB |
| **Total** | **344 KB** | | **~60.8 KB** |

BCCompiler struct itself: 288 bytes (just pointers + counters).

### BCVMState

API: `bc_vm_alloc()` / `bc_vm_free()`

| Array | Host | Device | Device Size |
|-------|------|--------|-------------|
| globals[] | 512 | 128 | ~1.5 KB |
| global_types[] | 512 | 128 | 128 B |
| arrays[] | 512 | 128 | ~6.1 KB |
| locals[] | 1024 | 256 | ~3 KB |
| local_types[] | 1024 | 256 | 256 B |
| local_arrays[] | 1024 | 256 | ~6.1 KB |
| **Total** | **79.6 KB** | | **~18.4 KB** |

BCVMState struct itself: 6.1 KB (stack, call_stack, for_stack, str_temp are inline).

### Device Memory Budget

| | RP2040 | RP2350 |
|---|---|---|
| Heap available | 128 KB | 300 KB |
| Compiler heap | 60.8 KB | 60.8 KB |
| VM heap | 18.4 KB | 18.4 KB |
| Structs on stack | 6.4 KB | 6.4 KB |
| **Total FRUN** | **85.6 KB** | **85.6 KB** |
| **Remaining** | **42.4 KB** | **214.4 KB** |

Allocation: calloc/free on host, GetMemory/FreeMemory on device.

## Phase 2: Device Build & Test

1. Add bc_*.c to CMakeLists.txt (already partly done)
2. Build for RP2350 -- verify fits in flash
3. Test FRUN on actual PicoCalc hardware
4. Run benchmark comparisons on device

## Phase 3: Performance (MEASURED)

Host benchmark results (macOS, Apple Silicon):

| Benchmark | Interpreter | VM | Speedup |
|-----------|-------------|-----|---------|
| Fibonacci(30) | 5.9s | 0.23s | **~25x** |
| Mandelbrot 161x161 | 1.5s | 0.11s | **~13x** |
| Matrix 41x41 multiply | 0.10s | 0.01s | **~8x** |
| Sieve x10 | 0.10s | 0.02s | **~4x** |

Recursive function calls (Fibonacci) benefit most from bytecode dispatch. Float-heavy inner loops (Mandelbrot) also show strong speedup. Array-heavy code (Sieve) benefits less since array access still goes through similar indexing.

## Platform Memory Summary

| | RP2040 | RP2350 |
|---|---|---|
| Total SRAM | 264 KB | 520 KB |
| Flash | 2 MB | 4 MB |
| MMBasic heap (GetMemory pool) | 128 KB | 300 KB |
| g_vartbl | 30 KB | 48 KB |
| Max variables | 512 | 768 |
| Max SUB/FUN | 256 | 512 |
| Code execution | Flash via XIP | Flash via XIP |
| Compiler code in flash | ~170 KB | ~170 KB |
| VM code in flash | ~104 KB | ~104 KB |

Key insight: Compiler and VM **code** lives in flash, executes via XIP, costs zero RAM. Only working **data** needs RAM, temporarily, from the existing interpreter heap.

## Files

```
runtime/vm/bytecode.h              -- Opcode definitions, compiler/VM structs, platform limits
runtime/vm/bc_compiler_core.c      -- Bytecode emission helpers, slot/constant management, alloc/free
runtime/vm/bc_compiler_internal.h  -- Internal compiler API
runtime/vm/bc_source.c             -- Raw-source frontend
runtime/vm/bc_vm.c                 -- VM dispatch loop
runtime/vm/bc_runtime.c            -- FRUN/runtime entry points
runtime/vm/bc_bridge.c             -- Bridge to interpreter's cmd_*/fun_* functions
ports/host_native/                 -- Native macOS build for off-device testing
ports/host_native/tests/t*.bas     -- host-native BASIC test programs
```
