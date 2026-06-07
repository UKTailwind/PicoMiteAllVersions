# Common Runtime Spine Plan

## Problem

The codebase has a shared MMBasic interpreter core, but it does not yet have
a shared runtime lifecycle.

The Pico SDK firmware uses `PicoMite.c` as its runtime shell. Other ports
replace that shell with their own implementations:

- Pico SDK firmware: `PicoMite.c`
- Native host and WASM: `ports/host_native/host_main.c`,
  `ports/host_native/host_runtime.c`, and WASM-specific entry code
- stdio and ANSI: `ports/mmbasic_stdio/main.c`, `stdio_runtime.c`,
  `ports/mmbasic_ansi/ansi_main.c`
- ESP32-S3: `ports/esp32_s3_metro/main/app_main.c`,
  `esp32_runtime.c`, `esp32_compat.c`, and `esp32_flash_storage.c`
- PC386: `ports/pc386/kmain.c`, `pc386_runtime.c`, `pc386_state.c`, and
  `pc386_peripheral_stubs.c`

Those files do not just provide platform startup. They duplicate runtime
contracts that the interpreter expects globally:

- `MMPrintString`, `MMputchar`, `MMInkey`, `MMgetline`, `MMgetchar`
- `CheckAbort`, `routinechecks`, `check_interrupt`, `cmd_ireturn`
- `SaveProgramToFlash`, source-to-program-memory loading, and LOAD cleanup
- option loading, validation, defaults, and snapshots
- program memory backing and erased-flash sentinels
- reset/error/longjmp setup
- timer and timebase maintenance
- display-console setup
- assorted global backing storage

This makes modular cleanup harder than moving files. The runtime contract is
implicit and scattered. Ports have learned to satisfy it by copying or
stubbing pieces of `PicoMite.c` and `host_runtime.c`.

The host runtime is especially important: it is already partly a common
runtime in disguise. Native host, WASM, ANSI, and stdio builds reuse large
pieces of `ports/host_native/host_runtime.c`. The migration should promote
reusable host-runtime pieces into an explicitly common layer, not invent a
parallel abstraction from scratch.

## Goal

Create a common runtime spine used by every port that can share it:

```c
int mmbasic_runtime_boot(const struct mm_runtime_adapter *port);
int mmbasic_runtime_run_source(const struct mm_runtime_adapter *port,
                               const char *source,
                               unsigned flags);
void mmbasic_runtime_repl(const struct mm_runtime_adapter *port,
                          unsigned flags);
```

The common runtime should own interpreter lifecycle and interpreter-facing
contracts. Port code should own entry policy, hardware startup, OS startup,
and hardware/OS effects.

## Non-Goals

- Do not rewrite the interpreter core.
- Do not replace every driver HAL in this phase.
- Do not create a second hardware HAL. The runtime adapter sits above `hal/`
  and should use existing HAL contracts where they already exist.
- Do not make PC386 or ESP32 feature-complete before extracting common code.
- Do not force every port into interactive REPL mode. Batch, REPL, browser,
  and firmware modes remain distinct entry policies using shared services.

## Architecture

Add a runtime layer under `runtime/` or `src/runtime/`:

- `runtime/runtime.h`
- `runtime/runtime_boot.c`
- `runtime/runtime_console.c`
- `runtime/runtime_abort.c`
- `runtime/runtime_interrupt.c`
- `runtime/runtime_program.c`
- `runtime/runtime_options.c`
- `runtime/runtime_globals.c`

Introduce a runtime adapter struct. This is intentionally key-oriented rather
than byte-only, because some ports already return decoded MMBasic key codes
directly. PC386 PS/2 input is the clearest example.

```c
typedef enum mm_runtime_key_kind {
    MM_RUNTIME_KEY_NONE,
    MM_RUNTIME_KEY_RAW_BYTE,
    MM_RUNTIME_KEY_MMBASIC_CODE
} mm_runtime_key_kind;

typedef struct mm_runtime_key {
    mm_runtime_key_kind kind;
    int value;
} mm_runtime_key;

typedef struct mm_runtime_adapter {
    const char *name;
    unsigned flags;

    void (*early_console_init)(void);
    void (*platform_pre_options)(void);
    void (*platform_validate_or_reset_options)(void);
    void (*platform_apply_option_defaults)(void);
    void (*platform_after_options)(void);

    void (*memory_backing_init)(void);
    void (*timebase_init)(void);
    uint64_t (*time_us)(void);
    void (*sleep_us)(uint32_t us);

    mm_runtime_key (*console_read_key_nonblock)(void);
    mm_runtime_key (*console_read_key_blocking)(uint32_t timeout_ms);
    void (*console_write)(char c, int flush);
    void (*console_drain)(void);

    void (*display_console_init)(void);
    void (*keyboard_init)(void);
    void (*audio_init)(void);
    void (*filesystem_init)(void);
    void (*network_service)(int mode);
    void (*background_service)(void);

    int  (*interrupt_pending)(unsigned char **target);
    void (*before_prompt_loop)(void);
    void (*after_load_program)(void);
    void (*soft_reset)(void);
    void (*fatal_fault)(const char *message);
} mm_runtime_adapter;
```

The exact shape should evolve during extraction, but the boundary should stay
clear:

- Common runtime owns interpreter sequencing.
- Runtime adapters own lifecycle policy and hardware/OS effects.
- Existing HAL owns hardware contracts.
- Port globals move behind state modules only when it reduces real coupling.

## Runtime Responsibilities to Centralize

### Boot and Session Lifecycle

The runtime flow is not identical across ports today. Treat these as common
steps to compose, not a mandatory fixed order:

1. Initialize early console if available.
2. Initialize option storage and program memory backing.
3. Load options when the port uses persisted options.
4. Apply port defaults.
5. Validate options or reset them.
6. Initialize MMBasic core with `InitBasic()` and, where required,
   `InitHeap(true)`.
7. Initialize runtime subsystems: file state, VM state, display console,
   keyboard, network, timers.
8. Clear runtime state with `ClearRuntime(true)` when appropriate.
9. Print banner if the entry policy wants one.
10. Enter `MMBasic_RunPromptLoop()` or run a loaded program.

Examples that must be preserved:

- Pico firmware loads and validates options before later clock/display init,
  and reloads options again after clock setup.
- Native host and WASM do not follow the same `LoadOptions()` /
  `InitHeap(true)` sequence as ESP32, PC386, and Pico.
- WASM snapshots options after display-console overrides, not just during the
  first runtime begin.
- ESP32 validates and persists terminal defaults before entering the REPL.

The common runtime should extract small sequencing helpers first, then compose
them per entry policy.

### Console Contract

Create one common implementation of:

- `MMPrintString`
- `MMputchar`
- `putConsole`
- `SSPrintString`
- `MMgetchar`
- `MMgetline`

The common implementation should call runtime adapter key/output operations
and use shared VT100/function-key decoding where applicable.

Ports may choose one of several console policies:

- serial-only
- display-only
- display plus serial mirror
- host capture hook
- browser callback
- no interactive input

### Service, Abort, and Interrupts

Create shared service helpers used by `CheckAbort()` and `routinechecks()`:

- call network service
- call background service
- drain console if needed
- check timeout/watchdog conditions that are not hardware-specific
- optionally check `MMAbort`

Do not assume every port currently treats `MMAbort` the same way. Pico and
ESP32 longjmp on abort. Host currently uses this path mostly for timeout and
network polling. PC386 currently leaves it as a no-op. The first target is a
shared service boundary; common abort semantics should be enabled per adapter
as ports are ready.

Interrupt return handling is related but distinct. `check_interrupt()` and
`cmd_ireturn()` preserve `OptionErrorSkip`, `MMErrMsg`, `MMerrno`, local
scope state, `CurrentInterruptName`, and SUB interrupt return tokens. That
state handling should be extracted deliberately rather than hidden inside the
abort phase.

### Program Loading and Saving

Split program memory operations into three layers:

- Common source tokenization into `ProgMemory`.
- Common load/save control flow for paths that call tokenization while a
  BASIC command is executing.
- Port-specific persistence of `ProgMemory` to flash/storage.

Today there are multiple stripped-down copies of `load_basic_source()`, while
Pico firmware has a larger `SaveProgramToFlash()` that also extracts
CSUB/CFunction/DefineFont payloads. The common layer should first provide:

- `mmbasic_tokenise_source_to_progmem(const char *source, unsigned flags)`
- `mmbasic_save_loaded_source(const char *source, unsigned flags)`
- continuation-line handling
- erased CFunction sentinel setup via a port-supplied erase/backing contract
- `PSize` maintenance

The second API is necessary because LOAD and editor/save paths can execute
while `inpbuf` and `tknbuf` contain the currently-running statement. Host and
PC386 currently longjmp after LOAD to avoid continuing with clobbered buffers.
The common implementation must preserve that behavior explicitly.

Then device persistence can remain specialized:

- Pico SDK full flash writer with CSUB/CFunction extraction
- host in-memory writer
- ESP32 storage writer
- PC386 RAM/disk-backed writer

### Options

Commonize the option lifecycle without forcing common defaults:

- common helper to load options when a port uses persisted options
- common validity checks for fields that are universal
- port hook for defaults and fixups
- port hook for snapshot/persist after defaults

Universal validation candidates:

- `Option.Magic`
- `Option.Tab`
- `Option.PROG_FLASH_SIZE`
- `Option.Autorun` range

Port-specific validation stays in each adapter:

- CPU speed
- display type
- pin layout
- storage geometry
- console dimensions

### Runtime Globals

The scattered global backing stores are a major blocker. Do not try to fix
all of them at once. Use a staged approach:

1. Inventory globals required by headers but not truly port-specific.
2. Move shared definitions into existing `core/state/*` where possible.
3. Use `runtime/runtime_globals.c` only for symbols that are truly
   runtime-owned and not already covered by `core/state/*`.
4. Leave hardware-only globals in port adapters.
5. Replace direct global requirements with accessors only when doing so
   removes real coupling.

Good candidates for common/global-state consolidation:

- console ring buffer state
- `MMAbort`, `MMCharPos`, `BreakKey`, `WatchdogSet`
- timer counters used by interpreter statements
- flash program memory pointers once storage abstractions exist

Bad first candidates:

- Pico clock/voltage state
- VGA/HDMI scanout scratch globals
- PSRAM hardware detection
- DMA/PWM/PIO registers and compatibility shims

## Migration Plan

## Execution Status

Branch: `common-runtime-spine`

Final correction: early phase notes record that `emcc` was not on `PATH`; the
WASM build script locates `$HOME/emsdk/emsdk_env.sh`, which resolves to
`/Users/joshv/emsdk/emsdk_env.sh` in this workspace. Final WASM validation
passed through `./ports/host_wasm/build.sh`.

| Phase | Status | Agent | Validation | Notes |
|---|---|---|---|---|
| 0. Runtime contract docs | Complete | Einstein | Passed | Created `docs/runtime-contract.md`; no runtime code moved for the phase. Gate feedback found two baseline harness/build issues and they were addressed before Phase 1: host `FILES` now strips MMBasic drive prefixes before the host FatFS RAM-disk walker, and `ports/host_native/run_tests.sh` resolves repo tools correctly when invoked as `./ports/host_native/run_tests.sh`. `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh`, clean `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. |
| 1. Promote names/wrappers | Complete | Mill | Passed with caveats | Added `runtime/runtime.h`; introduced `mmbasic_runtime_port_begin()` as the current port-local lifecycle entry while retaining `host_runtime_begin()` wrappers for compatibility. Native host, WASM, stdio, ANSI, and PC386 owned call sites now use the common name. Validation: `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed), `make -C ports/pc386`, `make -C ports/mmbasic_stdio`, and stdio `PRINT 1+1` smoke passed. `make -C ports/mmbasic_ansi` was attempted but is blocked by the pre-existing missing `ports/host_native/hal_watchdog_host.c` Makefile entry; `emcc` was not on PATH, so WASM smoke was unavailable locally. |
| 2. Service/interrupt/LOAD boundaries | Complete | Bernoulli | Passed with caveats | Added header-only boundary helpers for service polling, interrupt save/restore/IRETURN state, SUB interrupt return tokens, abort checks, and post-LOAD prompt bounces. Kept source loaders, console, and boot sequencing in place. Validation: `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed), `make -C ports/pc386`, `PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. WASM smoke was unavailable because `emcc` was not on PATH. |
| 3. Source loading | Complete | Sagan | Passed with caveats | Added `runtime/runtime_program.c` with `mmbasic_tokenise_source_to_progmem()` and `mmbasic_save_loaded_source()` plus host/batch source flags. Non-Pico loaders now call the common implementation while retaining `load_basic_source`, `load_source`, `pc386_load_source`, and non-Pico `SaveProgramToFlash` compatibility wrappers. Fixed ANSI build-list gaps uncovered by the smoke gate (`hal_watchdog_noop`, `host_web_stubs`, `host_tcp_interrupt_pending`). Validation: `git diff --check`, `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed), targeted continuation/FRUN tests via host suite, `make -C ports/mmbasic_stdio`, stdio `PRINT 1+1` smoke, `make -C ports/mmbasic_ansi`, ANSI script smoke under a PTY, `make -C ports/pc386`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. WASM smoke unavailable because `emcc` is not on PATH. |
| 4. Console | Complete | Anscombe | Passed with caveats | Added `runtime/runtime_console.c` and `mm_runtime_console_adapter` for host-derived console services. Common code now owns `MMInkey`, `MMgetchar`, `putConsole`, `MMputchar`, `MMPrintString`, `SSPrintString`, `MMgetline`, and VT escape decoding for native host/WASM/stdio/ANSI builds. Host-local code keeps raw terminal reads, scripted keys, sim key queue, capture/stdout serial routing, telnet, display byte output, and the stdio-only LF policy flag. Pico, ESP32, and PC386 console bodies remain port-local. Validation: `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed, including `t113_keydown_native`), `make -C ports/mmbasic_stdio`, `ports/mmbasic_stdio/tests/run_tests.sh` (8 passed, 0 failed), stdio `INPUT`/`PRINT` smoke (`? Hello Ada`), `make -C ports/mmbasic_ansi`, `make -C ports/pc386`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. WASM smoke was unavailable because `emcc` is not on PATH. |
| 5. Abort/background/interrupt helpers | Complete | Ohm | Passed with caveats | Added `runtime/runtime_abort.c` and `runtime/runtime_interrupt.c`; `runtime/runtime.h` now exposes linked service, abort-adapter, and interrupt-state APIs instead of header-only bodies. Host, stdio, ANSI, ESP32, PC386, and Pico build lists include the new runtime modules. Host-derived ports poll timeout/network/background through a common abort adapter and preserve timeout exit status (`124` for `--interp --timeout-ms`); ESP32 keeps FreeRTOS yield and opt-in `MMAbort` longjmp; PC386 remains no-op through a null adapter; Pico calls the common abort helper from `CheckAbort()` while keeping device background work in the Pico hook. Validation: `git diff --check`, `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed), host timeout smoke, `make -C ports/mmbasic_stdio` plus stdio `PRINT 1+1` smoke, `make -C ports/mmbasic_ansi` plus PTY smoke, `make -C ports/pc386`, `PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. WASM smoke unavailable because `emcc` is not on PATH. |
| 6. Boot/run sequencing | Complete | Hubble | Passed | Added `runtime/runtime_boot.c` with common init, run-source, and REPL-entry helpers. Host, WASM, stdio, ANSI, ESP32, and PC386 use the helpers only where their existing sequencing matched; firmware entry ordering remains intact. Validation: `git diff --check`, `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed; VM pin-mode and HAL purity clean), `./ports/host_wasm/build.sh` using `/Users/joshv/emsdk/emsdk_env.sh`, `make -C ports/mmbasic_stdio` plus `PRINT 2+2` smoke, `make -C ports/mmbasic_ansi` plus PTY smoke, `make -C ports/pc386`, `PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. |
| 7. Split `PicoMite.c` | Complete | Linnaeus | Passed | Split Pico-only boot, timer, console, fault/abort, program-flash, CFunction bridge, and RP2040 flash-clock helper into `ports/pico_sdk_common/` with `PicoMite.c` reduced to Pico global storage. Validation: `git diff --check`, `./build_firmware.sh rp2040`, and `./build_firmware.sh rp2350` passed. Host gates were not run because this phase only touched Pico firmware sources, the Pico target source list, and Pico-local headers. Caveat: `pico_clock.c` currently owns the RP2040 `modclock()` helper; the broader boot clock sequence remains in `pico_boot.c` to avoid refactoring boot ordering. |
| 8. Retire shims | Complete | Archimedes | Passed | Removed the retired `host_runtime_begin()` compatibility symbol, deleted unused `load_basic_source` / `load_source` / `pc386_load_source` wrappers now that supported call sites use `mmbasic_runtime_port_begin()` and the common source loader, and updated runtime/port docs including WASM toolchain lookup through `$HOME/emsdk/emsdk_env.sh` (`/Users/joshv/emsdk/emsdk_env.sh` here). Kept Pico, ESP32, and PC386 port-local console/abort bodies where they remain intentional. Root-level global movement was skipped as too risky for this retirement pass. Added the missing `WEBRP2350` PicoCalc heap override so the final RAM baseline remains under threshold. Validation: `git diff --check`, `./ports/host_native/build.sh`, `./ports/host_native/run_tests.sh` (243 passed, 0 failed; VM pin-mode and HAL purity clean), `./ports/host_wasm/build.sh`, `make -C ports/mmbasic_stdio` plus `PRINT 1+1` smoke, `make -C ports/mmbasic_ansi` plus PTY smoke, `make -C ports/pc386`, `PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith`, `./build_firmware.sh rp2040`, `./build_firmware.sh rp2350`, and `./buildall.sh` passed. RAM baseline deltas after the heap fix: `WEBRP2350` bss `+4`, `DVIWIFIRP2350` bss `+4`, all other retained targets `+0`. |

## Test Gate Policy

Every phase must have explicit pass/fail criteria before implementation
starts. A phase is not complete because code compiles once; it is complete
when the relevant supported behavior is unchanged.

Use four gate levels:

- **Build gate**: every affected target must compile. Required for every
  phase.
- **Fast gate**: host build plus host BASIC test suite. Required for every
  phase.
- **Smoke gate**: host/WASM smoke tests that exercise basic runtime behavior.
  Required when touching shared runtime, source loading, console, options,
  or port adapters.
- **Final device gate**: RP2040/RP2350 Pico firmware builds and, where still
  supported, the full device matrix. Device validation is allowed to wait
  until the end of the migration, but the code should be kept structured so
  this final gate is expected to pass.

The guiding rule: everything that can reasonably build locally should keep
building, and the BASIC host test suite should keep passing after every phase.
Device validation can wait until final integration.

Default fast gate:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
```

Default firmware gate:

```sh
./build_firmware.sh rp2040
./build_firmware.sh rp2350
```

Full device matrix gate:

```sh
./buildall.sh
```

Use the full device matrix before merging any phase that changes CMake source
lists, shared headers, runtime globals, HAL contracts, or `PicoMite.c`.
If device validation is intentionally deferred, record that explicitly and do
not treat the phase as fully device-validated until the final device gate runs.

Suggested smoke gate commands:

```sh
cd ports/host_wasm && ./build.sh
make -C ports/mmbasic_stdio
make -C ports/mmbasic_ansi
make -C ports/pc386
```

Run the subset that is available on the current machine and relevant to the
files touched. At minimum, runtime phases should include one non-native-host
smoke path when the toolchain is available.

## Phase Gates

| Phase | Required Gates | Pass Criteria |
|---|---|---|
| 0. Runtime contract docs | Fast gate | No code behavior changes; docs accurately list runtime-owned symbols and boot order for each port. |
| 1. Promote names/wrappers | Fast gate, affected build gates, at least one smoke gate | Old public symbols still link; new names are wrappers or aliases only; no behavior changes. |
| 2. Service/interrupt/LOAD boundaries | Fast gate, affected build gates, host/WASM smoke where available | `LOAD`, `RUN`, interrupt return, host timeouts, and Ctrl-C/abort behavior remain unchanged on touched ports. |
| 3. Source loading | Fast gate, affected build gates, WASM/stdout/ANSI/PC386 smoke where available | Interpreter tests pass; `LOAD`, `SAVE`, `RUN`, `FRUN`, editor save, continuation lines, and smaller-program-over-larger-program loads retain behavior. |
| 4. Console | Fast gate, affected build gates, host/WASM/ANSI smoke where available | Prompt echo, PRINT output, INPUT, INKEY$, function keys, arrow keys, backspace/delete, host capture, and noninteractive stdin behavior remain unchanged. |
| 5. Abort/background/interrupt helpers | Fast gate, affected build gates, host/WASM smoke where available | Long-running loops still abort where supported; host timeout still exits tests; network polling still progresses; PC386 remains no-op where intentionally no-op. |
| 6. Boot/run sequencing | Fast gate, affected build gates, host/WASM/stdio/ANSI/PC386 smoke where available | REPL starts on interactive ports; batch run-file modes still exit with correct status; banners/options/default display setup remain unchanged. |
| 7. Split `PicoMite.c` | Fast gate, affected build gates, final device gate if this is the integration phase | Host behavior remains unchanged; Pico runtime split is ready for final firmware validation; final gate confirms boot banner, options, display, keyboard, flash save/load, and audio init still build. |
| 8. Retire shims | Fast gate, all retained build/smoke gates, final device gate | Deleted shims have no unresolved references; all supported ports use common runtime services; unsupported-port removals are documented. |

If a toolchain is unavailable locally, record that explicitly in the commit or
PR notes and run the closest available gate. Do not silently skip a required
gate.

## Behavioral Smoke Tests

For runtime work, add or preserve focused smoke tests in addition to build
success. New smoke tests are expected when a phase creates a realistic
regression risk that existing tests do not cover.

- **Source loading**: load a program, run it, load a shorter program, run it,
  and verify no tail tokens from the first program execute.
- **Continuation lines**: load a source file using `Option.continuation` and
  verify logical-line tokenization matches existing host behavior.
- **LOAD during execution**: execute a BASIC program that calls `LOAD` and
  verify the post-load cleanup path does not continue with clobbered
  `inpbuf`/`tknbuf`.
- **Console editing**: verify backspace, delete, arrows, function-key
  expansion, and ENTER in REPL-capable ports.
- **Abort/service**: run a tight BASIC loop and verify supported ports can
  break/timeout without starving network/background service.
- **Options snapshot**: trigger an error after display/console defaults are
  applied and verify reset/error recovery does not revert those defaults.

## New Smoke Tests to Add

Add small tests close to the port they protect. Prefer host tests first
because they run quickly and catch most interpreter-runtime regressions.

Suggested additions:

- `ports/host_native/tests/runtime_load_shorter.bas`: create/load/run one program, then
  load/run a shorter program and verify the first program's tail never
  executes.
- `ports/host_native/tests/runtime_load_during_run.bas`: a BASIC program that performs
  `LOAD` from inside execution and verifies the post-load cleanup path does
  not continue using clobbered `inpbuf`/`tknbuf`.
- `ports/host_native/tests/runtime_continuation_load.bas`: source loading with
  `OPTION CONTINUATION ON`, verifying logical-line tokenization matches the
  current host behavior.
- `ports/host_native/tests/runtime_options_after_error.bas`: configure console/display
  defaults, intentionally trigger an error, then verify the reset/error path
  keeps the expected defaults.
- `ports/host_native/tests/runtime_timeout_abort.bas`: a tight loop run with the host
  timeout setting, verifying timeout still exits cleanly.
- `ports/mmbasic_stdio/tests/runtime_stdin_input.bas`: stdio-only INPUT /
  PRINT smoke to catch console API regressions.
- `ports/mmbasic_ansi/demos/runtime_repl_keys.bas` or an equivalent scripted
  harness smoke: arrow/backspace/delete/function-key behavior if the ANSI
  harness can drive keys deterministically.
- WASM smoke: a small browser/worker-level check that boots, runs
  `PRINT 1+1`, receives output, and can recover from a BASIC error without
  losing console/display configuration.

When a phase changes runtime behavior, either map it to an existing smoke
test by name or add a new one in the same phase. A phase should not rely only
on broad test-suite success if the likely regression is a narrow lifecycle,
console, or load/save edge case.

### Phase 0: Freeze the Runtime Contract

Create `docs/runtime-contract.md` documenting every interpreter-facing symbol
currently provided by:

- `PicoMite.c`
- `ports/host_native/host_runtime.c`
- `ports/host_native/host_main.c`
- `ports/host_wasm/host_wasm_main.c`
- `ports/mmbasic_stdio/main.c`
- `ports/mmbasic_stdio/stdio_runtime.c`
- `ports/mmbasic_ansi/ansi_main.c`
- `ports/esp32_s3_metro/main/app_main.c`
- `ports/esp32_s3_metro/main/esp32_runtime.c`
- `ports/esp32_s3_metro/main/esp32_compat.c`
- `ports/esp32_s3_metro/main/esp32_flash_storage.c`
- `ports/pc386/kmain.c`
- `ports/pc386/pc386_runtime.c`
- `ports/pc386/pc386_state.c`
- `ports/pc386/pc386_peripheral_stubs.c`

For each symbol, classify it as:

- common runtime
- runtime adapter operation
- port state
- driver state
- obsolete shim

Also document boot variants separately. Record the exact current sequence for
Pico, native host, WASM, stdio, ANSI, ESP32, and PC386 before extracting any
shared boot helper.

Do not move code in this phase.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
./build_firmware.sh rp2040
./build_firmware.sh rp2350
```

### Phase 1: Promote Existing Runtime Pieces Without Moving Behavior

Introduce common names while preserving existing implementations:

- Rename/alias `host_runtime_begin()` to `mmbasic_runtime_port_begin()` only
  where it is actually generic.
- Add `runtime/runtime.h` with declarations for the common runtime API.
- Add thin wrappers so existing port code still links.

This phase should reduce semantic confusion before extraction. For example,
PC386 currently calls a function named `host_runtime_begin()` even though it
is not the host runtime. That naming should go away early.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
make -C ports/pc386
```

### Phase 2: Define Service, Interrupt, and LOAD Boundaries

Before moving source loaders or console code, define the shared contracts for:

- `CheckAbort()`
- `routinechecks()`
- `check_interrupt()`
- `cmd_ireturn()`
- LOAD/editor save cleanup after tokenizing source

This phase can start by extracting helper functions while keeping public
symbols in their current files. The point is to make the coupling explicit
before moving code.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
make -C ports/pc386
```

### Phase 3: Extract Source Loading

Move duplicated source-to-`ProgMemory` logic into a common module:

- host `load_basic_source`
- stdio `load_source`
- ANSI `load_basic_source`
- ESP32 `load_basic_source`
- PC386 `pc386_load_source`

Create:

```c
int mmbasic_tokenise_source_to_progmem(const char *source,
                                       unsigned flags);
int mmbasic_save_loaded_source(const char *source,
                               unsigned flags);
```

Initial flags:

- preserve continuation-line behavior
- allow simplified batch mode
- clear flash/program memory first
- maintain erased CFunction sentinel
- request post-load longjmp cleanup for LOAD/editor paths

Keep Pico `SaveProgramToFlash()` in place for now, but have non-device ports
stop carrying private tokenization copies.

### Follow-Up: Source-Load Policy Boundary

ESP32-S3 exposed a remaining source-loader policy leak: its port-local
`SaveProgramToFlash()` wrapper selected the batch-load flags, so `RUN
"file.bas"` ignored `OPTION CONTINUATION LINES ON`. The immediate ESP32 fix is
to use the common host-load flags, but the port should not choose BASIC source
semantics at all. Future cleanup should replace port-local source-load policy
selection with a common `RUN`/`LOAD` source-load function: parse the command in
core, open/read through the filesystem HAL, tokenize and apply continuation
rules in core, then call a narrow program-storage/persistence primitive only
for the hardware-backed part.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
cd ports/host_wasm && ./build.sh
make -C ports/mmbasic_stdio
make -C ports/mmbasic_ansi
make -C ports/pc386
```

### Phase 4: Extract Common Console

Create a common console module that implements the MMBasic console API using
port key/output operations:

- `MMPrintString`
- `MMputchar`
- `putConsole`
- `SSPrintString`
- `MMgetchar`
- `MMgetline`
- escape sequence decoding
- function-key expansion

Port adapters provide:

- nonblocking key read
- blocking key read
- write byte
- optional display byte
- optional capture hook
- optional input pushback

Start with host, stdio, ANSI, and ESP32. PC386 can join once the key-oriented
boundary is proven. Do Pico firmware after the common console is proven,
because Pico's console path includes USB CDC, UART, telnet, display, and WiFi
polling.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
make -C ports/mmbasic_stdio
make -C ports/mmbasic_ansi
make -C ports/pc386
```

### Phase 5: Extract Abort, Background Service, and Interrupt Helpers

Create common `CheckAbort()` and `routinechecks()` helpers that delegate to
runtime adapter operations.

Common behavior candidates:

- call background service
- process network service
- check `MMAbort` when the adapter enables that behavior
- handle timeout/watchdog conditions that are not hardware-specific
- perform `do_end(false)` and `longjmp(mark, 1)` on abort only for adapters
  that opt in

Port-specific behavior remains behind hooks:

- FreeRTOS yield on ESP32
- host timeout/screenshot capture
- Pico audio/GPS/SD/touch/RTC/Wii polling
- PC386 idle/no-op behavior

Do this in three steps:

1. Common service helpers for host/ESP32/stdio/ANSI/PC386.
2. Shared interrupt helper for the host/ESP32 `check_interrupt()` /
   `cmd_ireturn()` pattern.
3. Pico adapter calls into the common implementation, then keeps device
   background work in a Pico-specific hook.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
./build_firmware.sh rp2040
./build_firmware.sh rp2350
```

### Phase 6: Extract Common Boot/Run Sequencing

Create shared entry helpers:

```c
int mmbasic_runtime_init_common(const mm_runtime_adapter *port,
                                unsigned flags);
int mmbasic_runtime_run_source(const mm_runtime_adapter *port,
                               const char *source,
                               unsigned flags);
void mmbasic_runtime_enter_repl(const mm_runtime_adapter *port,
                                unsigned flags);
```

Entry policies:

- firmware REPL with autorun
- host compare/run-file harness
- stdio run-file only
- browser boot and async run
- ESP32 serial REPL
- PC386 kernel REPL

Keep entry policies in port code. Move only shared sequencing into runtime.
This phase should remove repeated chains where the sequence is genuinely the
same, while preserving port-specific order where it is not.

Validation:

```sh
./ports/host_native/build.sh
./ports/host_native/run_tests.sh
cd ports/host_wasm && ./build.sh
make -C ports/mmbasic_stdio
make -C ports/mmbasic_ansi
make -C ports/pc386
./build_firmware.sh rp2040
./build_firmware.sh rp2350
```

Status 2026-05-13: complete for the non-Pico entry points without starting
the Phase 7 PicoMite split. Added `runtime/runtime_boot.c` with
`mmbasic_runtime_init_common()`, `mmbasic_runtime_run_source()`, and
`mmbasic_runtime_enter_repl()`, plus compatibility wrappers for the earlier
`boot`/`repl` names. Host, WASM, stdio, ANSI, ESP32, and PC386 entry code now
use the common helpers only where their existing sequencing matched; firmware
entry ordering remains intact.

Validation completed:

- `git diff --check`
- `./ports/host_native/build.sh`
- `./ports/host_native/run_tests.sh` — 243 passed, 0 failed; VM pin-mode helper and HAL
  purity gate clean
- `make -C ports/mmbasic_stdio`; `PRINT 2+2` smoke returned `4`
- `make -C ports/mmbasic_ansi`; PTY `PRINT 2+2` smoke returned `4`
- `make -C ports/pc386`
- `PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith`
- `./build_firmware.sh rp2040`
- `./build_firmware.sh rp2350`
- `./ports/host_wasm/build.sh` — sourced `/Users/joshv/emsdk/emsdk_env.sh`
  and built `ports/host_wasm/web/picomite.{mjs,wasm,data}` successfully

### Phase 7: Split `PicoMite.c`

Only after the common modules are used by non-Pico ports should
`PicoMite.c` be split.

Suggested split:

- `ports/pico_sdk_common/pico_boot.c`
- `ports/pico_sdk_common/pico_clock.c`
- `ports/pico_sdk_common/pico_timer.c`
- `ports/pico_sdk_common/pico_console.c`
- `ports/pico_sdk_common/pico_fault.c`
- `ports/pico_sdk_common/pico_program_flash.c`
- `ports/pico_sdk_common/pico_cfunction_bridge.c`

Common pieces should move to `runtime/`; Pico-only pieces should move under
`ports/pico_sdk_common/`.

Keep a tiny `PicoMite.c` wrapper temporarily if that reduces build churn.
Eventually delete it or rename it to a clear Pico SDK adapter name.

Validation:

```sh
./build_firmware.sh rp2040
./build_firmware.sh rp2350
./buildall.sh
```

### Phase 8: Retire Compatibility Shims

Once supported ports use the common runtime spine where their sequencing and
hardware contracts match:

- remove old `host_runtime_begin` naming
- delete duplicated source loaders
- delete duplicated console APIs for host-derived ports; leave Pico, ESP32,
  and PC386 port-local console bodies until their hardware/event models have
  explicit adapter coverage
- delete duplicated abort/routinecheck bodies where they can share common
  adapter hooks; leave port-local longjmp/no-op bodies where required
- reduce root-level global definitions where low-risk; defer broad global
  movement to a dedicated state-extraction pass
- update docs and port guides

This is also the right time to decide which ports remain supported and remove
dead adapters.

## Suggested File Ownership

Common runtime:

- `runtime/runtime.h`
- `runtime/runtime_boot.c`
- `runtime/runtime_console.c`
- `runtime/runtime_abort.c`
- `runtime/runtime_interrupt.c`
- `runtime/runtime_program.c`
- future: `runtime/runtime_options.c`
- future: `runtime/runtime_globals.c`

Pico SDK adapter:

- `ports/pico_sdk_common/pico_runtime_port.c`
- `ports/pico_sdk_common/pico_boot.c`
- `ports/pico_sdk_common/pico_timer.c`
- `ports/pico_sdk_common/pico_console.c`
- `ports/pico_sdk_common/pico_program_flash.c`

Host adapter:

- `ports/host_native/host_runtime_port.c`
- keep harness-specific code in `host_main.c`

WASM adapter:

- `ports/host_wasm/host_wasm_runtime_port.c`
- browser lifecycle remains in `host_wasm_main.c`

ESP32 adapter:

- `ports/esp32_s3_metro/main/esp32_runtime_port.c`
- FreeRTOS/app startup remains in `app_main.c`
- source loading/persistence work currently in `esp32_compat.c` and
  `esp32_flash_storage.c` moves only after the common loader API lands

PC386 adapter:

- `ports/pc386/pc386_runtime_port.c`
- kernel hardware bring-up remains in `kmain.c`
- runtime state and program-save work currently in `pc386_state.c` and
  `pc386_peripheral_stubs.c` moves only after the common state/loader
  contracts land

## Ordering Rationale

Do not start by splitting `PicoMite.c`. It is the largest and most hardware
entangled runtime, so extracting it first would mix architecture work with
Pico-specific regressions.

Start with service boundaries, source loading, console, and abort service
because they are:

- clearly duplicated today
- interpreter-facing rather than hardware-facing
- easy to test on host
- useful before moving files into folders

Once those contracts are common and tested, splitting `PicoMite.c` becomes a
mechanical adapter extraction instead of a risky rewrite.

## Success Criteria

The common-spine migration is successful when:

- all supported ports use the same source-loading implementation
- all supported ports call the same common runtime init/run helpers
- `CheckAbort` and `routinechecks` share common helpers with adapter hooks
- `check_interrupt` and `cmd_ireturn` share common state-handling helpers
- console APIs are common with port key/output backends for host-derived ports,
  with Pico, ESP32, and PC386 exceptions documented until their hardware models
  can be adapted safely
- `PicoMite.c` no longer contains shared interpreter runtime logic
- port-specific files only contain platform setup, hardware services, and
  entry policy
- host tests and Pico firmware builds pass after every phase

Remaining follow-on success criteria after this pass:

- extract common option/global backing storage into runtime-owned modules
- converge Pico, ESP32, and PC386 console/abort bodies behind explicit adapter
  contracts where that does not disturb hardware behavior
