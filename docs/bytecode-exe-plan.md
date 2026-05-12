# MMBasic Bytecode Executables (`.EXE`) - Plan

User-facing goal: make precompiled MMBasic programs feel like executables:

```basic
SAVE EXE "GAME.EXE"
RUN "GAME.EXE"
RUN "GAME.EXE", FORCE
```

These are not DOS executables and they do not contain native x86 code. They are MMBasic bytecode images with a strict compatibility header, loaded by the VM and still using the existing interpreter/HAL bridge for runtime services.

## Motivation

The bytecode VM already gives us most of the shape:

- BASIC source compiles into VM bytecode.
- Hot/simple operations run directly in `bc_vm_execute()`.
- Complex commands/functions thunk back into the existing interpreter/runtime surface through bridge opcodes and VM syscalls.
- Ports such as pc386 already exercise graphics, files, audio, keyboard, and options through those shared paths.

Persisting compiled bytecode gives us an executable artifact without designing a native C ABI, relocation format, process model, or DOS compatibility layer.

The `.EXE` extension is intentionally user-facing fun. Correctness comes from a magic/versioned header, not the extension.

## Non-Goals

- Native machine-code executables.
- DOS `.EXE` compatibility.
- Dynamic linking against C symbols in `mmbasic.elf`.
- Security or sandboxing. A valid bytecode executable runs inside the same VM/interpreter runtime.
- Long-term bytecode stability before the VM ABI is declared stable. Early builds should reject mismatches and ask the user to recompile.

## Current Architecture Notes

Current VM run path is centered on `bc_run_source_string_ex()`:

1. Allocate `BCCompiler` and `BCVMState`.
2. Compile source with `bc_compile_source()`.
3. Compact compiler state with `bc_compiler_compact()`.
4. Prepare bridge/tokenized program state with `bc_bridge_prepare_subfun(source)`.
5. Initialize VM with `bc_vm_init(vm, cs)`.
6. Run with `bc_vm_execute(vm)`.

The compacted runtime state is not just `code[]`. A runnable image needs:

- `BCCompiler.code`
- `BCCompiler.constants`
- `BCCompiler.slots`
- `BCCompiler.subfuns`
- `BCCompiler.subfun_locals_base`
- `BCCompiler.local_meta`
- `BCCompiler.data_pool`
- bridge/tokenized program data needed by `OP_BRIDGE_CMD`, bridged sub/function calls, `findlabel`, `RESTORE`, and other interpreter fallbacks

Compiler-only tables (`fixups`, `linemap`, `labelmap`, `nest_stack`, `locals`) do not need to be serialized unless we later want debug/source mapping.

## File Format

Use an explicit little-endian format. The first version can be simple and strict.

Header:

```c
typedef struct {
    char     magic[4];              /* "MMBX" */
    uint16_t format_version;        /* file container format */
    uint16_t header_size;
    uint32_t flags;

    uint32_t mmbasic_version;       /* packed project version/build id */
    uint32_t bytecode_abi_version;  /* opcode/layout compatibility */
    uint32_t vm_syscall_abi_version;
    uint32_t bridge_abi_version;

    uint16_t endian;                /* 0x1234 */
    uint16_t pointer_bits;          /* 32 for pc386/current device VM */
    uint32_t required_features;     /* graphics/audio/files/etc. */

    uint32_t code_size;
    uint32_t constants_count;
    uint32_t slots_count;
    uint32_t subfuns_count;
    uint32_t locals_base_count;
    uint32_t local_meta_count;
    uint32_t data_count;
    uint32_t bridge_size;

    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t header_crc32;
} MMBXHeader;
```

Payload sections, in fixed order:

1. code bytes
2. constants
3. slots
4. subfuns
5. subfun locals base
6. local metadata
7. DATA pool
8. bridge/tokenized program buffer

Each section should be length-prefixed in the payload too, even though the header carries sizes. That makes future extension and corruption diagnostics easier.

## Compatibility Rules

Default behavior must be conservative:

- Bad magic: not an MMBasic bytecode executable.
- Unsupported format version: error.
- CRC failure: error, never force.
- Endian/pointer-size mismatch: error, never force in v1.
- Bytecode ABI mismatch: error with "recompile" message.
- VM syscall or bridge ABI mismatch: error with "recompile" message.
- Missing required port feature: error.

Example user-facing errors:

```text
Error : Executable bytecode version mismatch; recompile GAME.EXE
Error : Executable requires graphics
Error : Executable is corrupt
```

`FORCE` may bypass version mismatches only:

```basic
RUN "GAME.EXE", FORCE
```

`FORCE` must not bypass corruption, impossible architecture mismatches, or missing required runtime facilities. Its job is experimentation across nearby builds, not running damaged files.

## Command Surface

Initial command set:

```basic
SAVE EXE "name.exe"
RUN "name.exe"
RUN "name.exe", FORCE
```

`SAVE EXE` compiles the current program in memory and writes a bytecode executable.

`RUN "name.exe"` should detect by magic header, not extension. Extension is only user convention.

Later additions:

```basic
COMPILE "source.bas" AS "name.exe"
LIST EXE "name.exe"
```

`LIST EXE` can print header information: format, VM ABI, source name/hash, required features, code size, data size.

## Implementation Phases

### Phase 0 - ABI Numbers and Feature Flags

Add explicit constants in one VM header:

```c
#define MMBX_FORMAT_VERSION 1
#define BC_BYTECODE_ABI_VERSION 1
#define BC_VM_SYSCALL_ABI_VERSION 1
#define BC_BRIDGE_ABI_VERSION 1
```

Add feature flags:

```c
BC_FEATURE_FILES
BC_FEATURE_GRAPHICS
BC_FEATURE_AUDIO
BC_FEATURE_KEYBOARD
BC_FEATURE_FASTGFX
BC_FEATURE_BRIDGE
```

Every opcode/syscall/bridge layout change that can break old images must bump the relevant ABI.

### Phase 1 - Split Compile From Run

Refactor `bc_run_source_string_ex()` into reusable pieces:

```c
int bc_compile_source_to_image(const char *source,
                               const char *source_name,
                               BCCompiledImage *out);

int bc_run_compiled_image(BCCompiledImage *image,
                          int flags);

void bc_compiled_image_free(BCCompiledImage *image);
```

`BCCompiledImage` owns the compacted compiler state plus bridge data. The existing `bc_run_source_string_ex()` becomes a wrapper:

```c
compile source -> image
run image
free image
```

This phase should not add files yet. It should preserve all existing VM tests.

### Phase 2 - In-Memory Serialize/Deserialize

Add:

```c
int bc_image_serialize(const BCCompiledImage *image,
                       uint8_t **out,
                       size_t *out_len);

int bc_image_deserialize(const uint8_t *buf,
                         size_t len,
                         unsigned flags,
                         BCCompiledImage *out);
```

Test without filesystem:

1. Compile source.
2. Serialize to memory.
3. Free original image.
4. Deserialize.
5. Run.

This catches pointer/offset mistakes before file I/O is involved.

### Phase 3 - Bridge Buffer Persistence

Make bridge state explicit. Today bridge prep is source-driven:

```c
bc_bridge_prepare_subfun(source)
```

For `.EXE`, the loader needs to install prebuilt bridge state without source:

```c
int bc_bridge_export(uint8_t **out, size_t *out_len);
int bc_bridge_import(const uint8_t *data, size_t len);
void bc_bridge_release_subfun_buffer(void);
```

If export/import is too invasive initially, v1 may store source text as a fallback bridge payload and rebuild the bridge buffer at load time. That is easier but less pure:

- Pros: faster implementation, lower risk.
- Cons: `.EXE` still contains source, load pays some tokenization cost, not a fully compiled-only artifact.

Preferred v1: export/import tokenized bridge buffer.

Acceptable prototype: source fallback behind a format flag.

### Phase 4 - File Commands

Implement `SAVE EXE` using existing file APIs.

Implement `RUN` magic detection:

1. Open requested file.
2. Read first 4 bytes.
3. If magic is `MMBX`, run bytecode executable.
4. Otherwise follow existing BASIC source load/run behavior.

Add `RUN ..., FORCE` parsing for executable files only.

### Phase 5 - Versioned Rejection and Diagnostics

Add friendly diagnostics for:

- corrupt file
- unsupported format
- bytecode ABI mismatch
- syscall ABI mismatch
- bridge ABI mismatch
- missing feature

Add `LIST EXE` if useful for debugging.

### Phase 6 - Tests

Host and pc386 should both cover:

- arithmetic/loops/strings
- variables and arrays
- DATA/READ/RESTORE
- SUB/FUNCTION
- file commands
- graphics/FASTGFX where supported
- bridged command fallback
- corrupt file rejection
- ABI mismatch rejection
- `FORCE` bypasses version mismatch
- `FORCE` does not bypass CRC failure

pc386 harness should run from copied disk images, as current tests do, to avoid image write locks.

## Data Representation Concerns

The structs in `bytecode.h` are currently in-memory C structs. Serializing them raw is tempting but risky:

- padding can differ by compiler/target
- enum sizes are compiler-dependent
- `MMFLOAT` representation should be explicitly assumed or encoded
- string layout is MMBasic-specific

For v1, because current targets are little-endian 32-bit-ish MMBasic builds, raw struct sections may be acceptable behind strict ABI and pointer-size checks. But the format should still be written/read through helper functions so we can move to canonical packed records later without rewriting command logic.

Recommended rule:

- v1 can be strict and target-bound.
- v2 should use canonical packed records if cross-target portability becomes important.

## Feature Detection

The compiler/image writer should set required features by scanning emitted opcodes/syscalls:

- `OP_FILE` or file syscalls -> files
- graphics opcodes/syscalls -> graphics
- FASTGFX opcodes/syscalls -> fastgfx
- audio syscalls when added -> audio
- bridge opcodes -> bridge

The loader should compare required features against the current port. This avoids loading a graphics executable on a serial-only or headless build and then failing halfway through.

## Risk Areas

1. **Bridge state.** This is the largest unknown. Many VM fallbacks rely on tokenized interpreter state, not only bytecode.
2. **Struct layout drift.** Raw `BCSlot`/`BCSubFun` persistence is fragile unless ABI versioning is strict.
3. **Error recovery.** VM execution uses MMBasic `error()`/`longjmp` flow. Loader cleanup must survive errors without leaking VM allocations.
4. **Memory pressure on small devices.** Saving/loading images must avoid holding source, serialized payload, compiler tables, VM tables, and bridge buffers all at once.
5. **Debuggability.** Once source is absent, runtime errors still need line numbers. `OP_LINE` gives line numbers, but source text listing/debug info is separate.

## Recommended First Milestone

Start with host_native, then pc386:

```basic
SAVE EXE "T.EXE"
RUN "T.EXE"
```

Program coverage:

```basic
Print "hello"
For I%=1 To 3
  Print I%, I%*I%
Next
```

No bridge fallback required beyond PRINT. Then extend to:

- file command
- graphics command
- SUB/FUNCTION
- a program that forces `OP_BRIDGE_CMD`

## Definition of Done

This feature is usable when:

- `.EXE` files are generated by `SAVE EXE`.
- `RUN` auto-detects and executes `.EXE` by magic header.
- ABI mismatches produce a clear recompile error.
- `RUN ..., FORCE` exists and is intentionally limited.
- Host and pc386 test harnesses cover both success and rejection paths.
- Existing BASIC source `RUN` behavior is unchanged.
