# MMBasic Host Build

Native macOS/Linux test harness for the legacy MMBasic interpreter and the bytecode VM.

For the current architecture snapshot, see:

- [docs/vm-architecture.md](../docs/vm-architecture.md)
- [docs/vm-command-coverage.md](../docs/vm-command-coverage.md)

The host binary is `mmbasic_test`. It has two execution engines:

- `--interp`: legacy interpreter oracle.
- `--vm`: bytecode VM implementation.
- `--vm-source`: VM-owned raw-source frontend to bytecode to VM.
- `--source-compare`: legacy interpreter oracle compared with raw-source VM frontend.
- default: run both and compare output.

The device uses VM-owned BASIC program execution. The host interpreter remains as the semantic reference for language behavior and host-safe syscalls.

## Quick Start

```bash
cd host
./build.sh
./run_tests.sh
./run_pixel_tests.sh
./run_host_shim_tests.sh
./run_frontend_tests.sh
./run_optimizer_tests.sh
./run_unsupported_tests.sh
./run_missing_syscall_tests.sh
```

Equivalent from the repo root:

```bash
make -C host
./host/run_tests.sh
./host/run_pixel_tests.sh
./host/run_host_shim_tests.sh
./host/run_frontend_tests.sh
./host/run_optimizer_tests.sh
bash host/run_unsupported_tests.sh
./host/run_missing_syscall_tests.sh
```

## Running Programs

```bash
./mmbasic_test program.bas
./mmbasic_test program.bas --interp
./mmbasic_test program.bas --vm
./mmbasic_test program.bas --vm-source
./mmbasic_test program.bas --source-compare
```

The default compare mode runs the legacy interpreter first, then the VM, and fails if stdout or error behavior differs.
`--source-compare` is the migration harness for removing device dependence on the legacy tokenizer: the interpreter still uses the legacy tokenized path as oracle, while the VM compiles directly from raw `.bas` source.

## Test Suites

| Command | Purpose |
|---------|---------|
| `./run_tests.sh` | Runs all `tests/t*.bas` oracle tests in compare mode. Current count: 201. |
| `./run_tests.sh --interp` | Runs oracle tests through the legacy interpreter only. |
| `./run_tests.sh --vm` | Runs oracle tests through the VM. |
| `./run_tests.sh tests/t01_print.bas --vm` | Runs one test through one engine. |
| `./run_pixel_tests.sh` | Runs framebuffer assertions through both interpreter and VM. |
| `./run_host_shim_tests.sh` | Runs deterministic host shim tests, including fixed date/time and delayed keyboard injection. Current count: 4. |
| `./run_frontend_tests.sh` | Runs raw-source VM frontend oracle comparisons. Current count: 48. |
| `./run_optimizer_tests.sh` | Runs peephole/superinstruction assertion and equivalence tests. Current count: 22. |
| `./run_unsupported_tests.sh` | Negative tests for unsupported VM syscalls. Current count: 0. |
| `./run_missing_syscall_tests.sh` | Inventory runner for missing VM syscall implementations. Current count: 0. |
| `./run_bench.sh` | Runs host benchmarks. |

`BINARY=...` can override the binary used by the scripts, but the supported default is `./mmbasic_test`.

Keyboard-driven host tests can inject characters with `--keys TEXT` or `--keys-after-ms MS TEXT`. The key string supports escapes such as `\n`, `\r`, `\t`, `\\`, and `\xNN`.

## Test Policy

Oracle comparison tests must be accepted by the legacy interpreter. Do not add VM-specific syntax or VM extensions to `tests/t*.bas`; if the interpreter rejects it, it is not a valid semantic oracle test.

Unsupported syscall tests belong in `tests/unsupported/*.bas`. Each file must fail under `--vm` and must start with:

```basic
' EXPECT_ERROR: expected substring
```

Missing syscall implementation tests belong in `tests/missing_syscalls/*.bas`. These are normal programs that should run under the interpreter oracle but expose native VM syscall gaps. The suite is currently empty and exits cleanly when there are no pending missing-syscall cases.

Host shim tests belong in `tests/host_shims/*.bas`. These are for host harness behavior rather than MMBasic semantic coverage.

Source frontend tests belong in `tests/frontend/*.bas`. These compare `source -> legacy interpreter` against `source -> VM source frontend -> bytecode -> VM`; they should grow before adding more native syscall coverage.

Optimizer tests belong in `tests/frontend/` and are run by `run_optimizer_tests.sh`. Each optimization has two test files:
- **Peephole tests** (`t0XX_*_peephole.bas`): verify the fused opcode appears in `--vm-disasm` output and the unfused form is absent.
- **Equivalence tests** (`t0XX_*_opt_equiv.bas`): compare `-O0` vs `-O1` output to verify correctness across boundary values, sign combinations, and type edge cases.

Graphics tests should prefer deterministic framebuffer assertions in `run_pixel_tests.sh`. Host graphics are still an approximation of hardware, so final validation for FASTGFX, LCD behavior, timing, SD, and resource limits must happen on device.

## Architecture

```text
host/
├── Makefile
├── build.sh
├── run_tests.sh
├── run_pixel_tests.sh
├── run_frontend_tests.sh
├── run_unsupported_tests.sh
├── run_bench.sh
├── host_platform.h
├── host_main.c
├── host_runtime.c
├── host_fastgfx.c
├── host_fs_shims.c
├── host_peripheral_stubs.c
├── host_fb.{c,h}
├── host_fs.{c,h}
├── host_fs_hal.h
├── host_keys.{c,h}
├── host_sim_audio.{c,h}
├── host_sim_server.{c,h}
├── host_terminal.{c,h}
├── host_time.{c,h}
├── tests/
│   ├── t*.bas
│   ├── frontend/
│   ├── host_shims/
│   ├── unsupported/
│   └── missing_syscalls/
└── mmbasic_test
```

The host build is a HAL target using the shared interpreter source. `core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, and `shared/mmbasic/mm_misc_shared.c` compile on both host and device; device-only branches are gated behind `#ifdef MMBASIC_HOST`/`#else`. The HAL surface is the `host_*_hal.h` / `host_*.h` headers listed below; host implementations live in the `host_*.c` files.

Important compiled sources:

| Source | Purpose |
|--------|---------|
| `core/mmbasic/MMBasic.c`, `core/mmbasic/Commands.c`, `core/mmbasic/Functions.c`, `core/mmbasic/Operators.c`, `core/mmbasic/MATHS.c`, `core/mmbasic/Memory.c`, `core/mmbasic/Editor.c`, `core/mmbasic/MMBasic_REPL.c`, `core/mmbasic/MMBasic_Prompt.c`, `core/mmbasic/MMBasic_Print.c` | Shared language runtime — compiled by host and device alike. |
| `core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, `shared/mmbasic/mm_misc_shared.c` | Shared interpreter command handlers. `core/mmbasic/Draw.c` / `core/mmbasic/FileIO.c` use `#ifdef MMBASIC_HOST` to gate device-only peripheral touchpoints. `shared/audio/Audio.c` file-splits at MMBASIC_HOST: device gets the full decoder/PWM body, host gets a thin `cmd_play` that re-uses `host_sim_audio_*`. `shared/mmbasic/mm_misc_shared.c` holds the portable `core/mmbasic/MM_Misc.c` subset (SORT, LongString, FORMAT$, DATE/TIME/TIMER/EPOCH, PAUSE). |
| `gfx_*_shared.c` | Shared host/device graphics primitives used by native VM graphics ops and by Draw.c. |
| `bc_source.c`, `bc_vm.c`, `bc_runtime.c`, `bc_debug.c`, `bc_compiler_core.c` | Bytecode source frontend, VM dispatch, runtime entry points, disassembler. |
| `vm_sys_*.c` | VM syscall implementations (graphics, file, time, pin, input). |
| `runtime/*.c` | Common runtime spine: boot/run helpers, source tokenisation, host-derived console API, abort/service polling, and interrupt helpers. |
| `host_runtime.c` | Host runtime lifecycle (`host_runtime_configure`, `mmbasic_runtime_port_begin`, `host_runtime_finish`), timeout/slowdown poll, host console adapter hooks, hardware-world zero-initialised globals (Option, PinDef, dma_hw, etc.), and `mmbasic_timegm`/`mmbasic_gmtime`. |
| `host_fastgfx.c` | `FASTGFX CREATE/SWAP/CLOSE/SYNC/FPS` host-side double-buffering + `cmd_framebuffer` (FRAMEBUFFER CREATE/LAYER/WRITE/SYNC/WAIT/COPY/MERGE/CLOSE). |
| `host_fs_shims.c` | FatFS directory-walker wrappers (`host_f_findfirst`/`findnext`/`closedir`/`unlink`/`rename`/`mkdir`/`chdir`/`getcwd`), POSIX per-fnbr file table (`host_fs_posix_*`), `ExistsFile`/`ExistsDir`, simulated `flash_range_*` + LFS stubs, `SaveProgramToFlash`, `host_options_snapshot`. |
| `host_peripheral_stubs.c` | No-op `cmd_XXX` / `fun_XXX` stubs for hardware the host doesn't carry (I2C, SPI, PIO, PWM pins, PWM/Servo command parsing routed through the VM pin HAL, IR, keypad, GPS globals, AES, xregex, display_details, BDEC/BMP decoder stubs). |
| `host_fb.{c,h}` | Framebuffer plane + colour conversion + BMP screenshot; backs `DrawPixel`/`DrawRectangle`/`DrawBitmap`/`ScrollLCD`/`ReadBuffer` function pointers. |
| `host_fs.{c,h}`, `host_fs_hal.h` | Opaque POSIX directory walker + whole-path helpers (`host_fs_unlink`/`rename`/`mkdir`/`chdir`/`getcwd`). HAL header declares the `host_fs_posix_*` surface consumed by `core/mmbasic/FileIO.c` preambles. |
| `host_keys.{c,h}` | Scripted-key injection (`--keys`, `--keys-after-ms`) and `host_keydown` polling. |
| `host_sim_audio.{c,h}` | WebAudio JSON emitter — shared by `shared/audio/Audio.c` host body for `cmd_play` tone/stop/volume/pause/resume. |
| `host_sim_server.{c,h}` | `--sim` Mongoose HTTP/WebSocket server: tick thread, cmd stream, key queue, WS-frame emitters. |
| `host_terminal.{c,h}` | stdin raw-mode management. |
| `host_time.{c,h}` | Monotonic microsecond timer, sleep. |
| `host_main.c` | CLI arg parse, `--interp`/`--vm` mode dispatch, `.bas` tokenise, engine runner, output-capture hook, `--sd-root` setup. |

The old bridge fallback is removed. The VM compiler must emit native bytecode for supported statements/functions; unsupported commands bridge back to the interpreter via `OP_BRIDGE_CMD` (PLAY, FILES, COPY, KILL, MKDIR, RMDIR, RENAME, CHDIR, DIR$, CWD$, etc.).

## Program Loading

Default compare mode still reads a `.bas` file as text, prepends generated line numbers when needed, and uses `mmbasic_tokenise_source_to_progmem()` to populate `ProgMemory` with the standard double-zero program terminator.

The interpreter side tokenises through the shared runtime loader. The VM side uses `bc_compile_source()` directly on raw `.bas` text for `--vm` and `--source-compare`.

## Output And Framebuffer Capture

The host shim redirects console output through `host_output_hook` so interpreter and VM output can be compared exactly.

Graphics tests use the host framebuffer and `--assert-pixel x,y,RRGGBB` arguments to verify deterministic drawing behavior. The same `.bas` test is run under both engines.

## Current Verification Snapshot

Current snapshot:

- `make -C host`: passes.
- `make -C build2350 -j8`: passes.
- `./host/run_tests.sh`: `201 passed, 0 failed`.
- `./host/run_pixel_tests.sh`: passes.
- `./host/run_host_shim_tests.sh`: `4 passed, 0 failed`.
- `./host/run_frontend_tests.sh`: `48 passed, 0 failed`.
- `./host/run_optimizer_tests.sh`: `22 passed, 0 failed`.
- `bash host/run_unsupported_tests.sh`: `0 passed, 0 failed`.
- `./host/run_missing_syscall_tests.sh`: `0 passed, 0 failed`.

## Known Build Note

The host Makefile is intentionally small and does not track all header dependencies. If command table or shared header changes produce a stale-object link error, run:

```bash
make -B -C host
```

## Sim Build Invariant

`mmbasic_sim` needs every `host/*.c` source compiled with `-DMMBASIC_SIM` so the `#ifdef MMBASIC_SIM` branches (MMInkey's sim-key pop, host_fb's WebSocket emit calls, host_sim_server linkage) stay in the binary. The Makefile enforces this via a pattern rule that matches every member of `HOST_SRCS` into `sim_obj/`:

```make
HOST_SIM_OBJS = $(HOST_SRCS:%.c=sim_obj/%.o)
$(HOST_SIM_OBJS): sim_obj/%.o: %.c
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
```

When you add a new source file, append it to `HOST_SRCS` and the sim build picks up the `-DMMBASIC_SIM` compile automatically. If you add a host file *outside* `HOST_SRCS` (e.g. listed separately in `SIM_EXTRA_SRCS`), you must give it an explicit `sim_obj/xxx.o: xxx.c` rule that uses `$(SIM_CFLAGS)` — otherwise VPATH routes it through the generic `%.o: ../%.c` rule (plain `$(CFLAGS)`) and the `MMBASIC_SIM` branch drops out silently.

**Symptom of a miss:** `mmbasic_sim` links and boots, the WebSocket connects (`connected` status visible), but the browser canvas never shows the banner and typed keys don't reach MMBasic. Confirm by checking whether `sim_obj/host_runtime.o` shows the string `[sim]` (or whatever `#ifdef MMBASIC_SIM` code you added) via `strings`.
