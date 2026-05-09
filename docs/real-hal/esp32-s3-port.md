# ESP32-S3 (Adafruit Metro) Port Plan

**Goal:** an MMBasic stdio REPL on the Adafruit Metro ESP32-S3 over USB Serial/JTAG, structured as a real device port that mirrors `ports/pico_sdk_common/` — not a host-shape simulator. The ESP-IDF is the hardware-access layer behind the HAL; nothing in core MMBasic learns about it.

Companion log: [esp32-s3-port-log.md](esp32-s3-port-log.md).

## Hardware

- **Adafruit Metro ESP32-S3 (#5500)** — N16R8 module: **16 MB flash, 8 MB Embedded Octal PSRAM (AP_3v3)**. Confirmed via `esptool chip_id`. (The original plan assumed N8R2 Quad — wrong.)
- Native USB Serial/JTAG (chip's built-in controller, no TinyUSB).
- 49 GPIOs (0–48). 3.3 V logic, no 5 V tolerance.
- ESP-IDF release/v5.3, Xtensa GCC.

## Status (snapshot — keep current)

| Stage | State | What's verified |
|---|---|---|
| A — toolchain | ✅ | blink + heartbeat over USB Serial/JTAG |
| B — link MMBasic core | ✅ | 80+ core/VM TUs compile + link clean on Xtensa |
| C — interactive REPL | ✅ | PRINT, FOR/NEXT, IF/ELSE, GOTO/GOSUB, LIST, EDIT, CPU RESTART, CLS, LOCATE, COLOUR all work |
| C — A: drive (LFS) | ✅ | LFS over `esp_partition_*`, embedded demos auto-populated, FILES/LOAD/SAVE-to-file/RUN/FRUN all work for files on A: |
| C — VM source compiler regression fixes | ✅ | adjacent string literals (`""` in PRINT), post-compact heap fragmentation |
| D — decouple from host_native | 🔧 | D1+D2+D5+D8 ✅; user-facing breakage fixed. **Direction shift**: D3/D4/D6/D7/D9 consolidated into one explicit decouple — ESP32 owns its full port surface in `esp32_*.c` files, drops `HOST_NATIVE_REUSED` entirely, drops `--wrap`, drops `--allow-multiple-definition` (tentative-def globals get fixed too). See "Decouple plan" below. |
| E — real flash persistence | ❌ | SAVE/LOAD-to-slot, OPTION SAVE survival across reboot |
| F — gate + plan hygiene | ❌ | ESP32 port not in purity gate scope; `docs/real-hal-plan.md` has no ESP32 row |

**Headline broken-but-silent bug** (fixed in D1 below): `flash_range_erase` / `flash_range_program` in `esp32_flash_storage.c` previously targeted a 256-byte placeholder buffer and silently no-op'd past the end. Now mirrored on host_native's offset-routing: program-region writes hit `flash_prog_buf`; slot-region writes past 256 bytes `error()` loudly. Real backing lands in Stage E1.

**Tests**: host `./run_tests.sh` 242/242 (includes `t170_frun_post_compact_array.bas` regression test for the heap reorder). HAL purity gate clean over its current scope, but its current scope **does not include `ports/esp32_s3_metro/main/*.c`**.

## Rules and invariants (read first)

These are non-negotiable on this port. Violations are reasons to revert, not feedback to apply later.

1. **Mirror pico, not host_native.** `ports/pico_sdk_common/` has ~25 small per-domain files (`hal_filesystem_pico.c`, `hal_flash_pico.c`, `vm_sys_pin_pico.c`, `cmd_files_hooks.c`, `clear_runtime_port.c`, etc). Each ESP32 file should have a recognisable pico counterpart. If a piece of behaviour exists only in `host_runtime.c` (the host monolith), the right move is to **split it** — not to inherit it on the device.

2. **No new `#ifdef MMBASIC_ESP32` / `#ifdef __XTENSA__` outside `ports/esp32_s3_metro/`.** Core, drivers, HAL contracts stay target-agnostic. The HAL purity gate enforces this for the existing scope and (Stage F1) will enforce it for the ESP32 port too.

3. **One definition per function. Period.** Every symbol the linker resolves has exactly one strong definition in the binary's source list — no `--wrap`, no `--allow-multiple-definition`, no weak-attribute fallbacks, no link-order luck. If two ports need different behaviour for the same hook, each port has its own TU with its own strong definition, and that port's build links exactly its TU. Tentative-def globals (`gui_bcolour`, `FSerror`, etc.) get fixed too — single-TU strong def + `extern` declarations elsewhere — not shrugged off as "grandfathered". Violations of this rule are the root cause of every link-time workaround on this port; the cleanup target is a build that compiles + links with default-strict GCC/clang flags.

4. **No `--wrap` for symbols that should live in a per-port file.** If `port_drive_check` needs ESP32 behaviour, ESP32 owns the symbol — the host port doesn't define it for everyone. The `--wrap`-then-override pattern is a code smell that hides a missing per-port file. Subsumed by rule 3 but worth calling out separately because we keep reaching for it.

5. **ESP-IDF lives behind the HAL.** Direct `esp_partition_*` / `usb_serial_jtag_*` / `heap_caps_*` calls go in:
   - The port's own `esp32_*.c` files (port-local hardware glue).
   - Driver implementations in `drivers/<thing>_esp32/`.
   - `hal_*_esp32.c` HAL impls.

   They never appear in core, in `bc_*.c`, in `vm_sys_*.c`, or in `gfx_*_shared.c`. If a core file would need IDF, it needs a HAL hook instead.

6. **Heap is tight and the port code knows it.** 104 KB internal SRAM heap (PSRAM disabled — see "Why no PSRAM yet" in Stage E). Allocation order matters; `bc_compiler_alloc` is ordered to keep post-compact heap contiguous (committed). New allocations in this port must respect the same fragmentation discipline.

7. **Pico source is the reference.** When in doubt about how a HAL contract is supposed to be exercised, read pico's impl — not host_native's. Host has historical shape (POSIX-rooted, malloc-flavoured) that doesn't translate.

8. **Keep tests passing every commit.** Host suite must stay at 242/242 (or whatever the current count is — never lower). HAL purity must stay green. ESP-IDF build must stay green. ESP32 hardware smoke (FRUN sieve.bas / fizzbuzz.bas) must stay green.

## Layout (current, not aspirational)

```
ports/esp32_s3_metro/
├── CMakeLists.txt              # ESP-IDF project root
├── port_config.h               # HAL_PORT_* values; inherits from host_native
├── partitions.csv              # 1 MB app + 12 MB lfsdata (no FATFS partition; LFS only)
├── sdkconfig.defaults          # USB JTAG console, watchdog off, radios off, PSRAM disabled
├── probe.py                    # pyserial test driver (avoids picocom DTR pulse)
├── README.md                   # build/flash/monitor (NEEDS REWRITE — Stage F3)
└── main/
    ├── CMakeLists.txt          # main component, source list, --wrap, --allow-multiple-definition
    ├── esp32_platform.h        # MMBASIC_HOST + MMBASIC_ESP32 + Pico SDK section-attr stubs
    ├── app_main.c              # IDF entry, MMBasic boot, REPL launch
    ├── esp32_console.c         # USB Serial/JTAG ↔ host_output_hook + host_read_byte_*
    ├── esp32_lfs.c             # LFS over esp_partition_* for the A: drive
    ├── esp32_flash_storage.c   # flash_target_*/host_options_snapshot/SaveProgramToFlash
    ├── esp32_compat.c          # flash_prog_buf, host_time_us_64, timegm, framebuffer no-ops
    ├── esp32_system.c          # cmd_cpu (esp_restart), __wrap_port_drive_check
    ├── esp32_terminal.c        # cmd_cls / cmd_locate / cmd_colour / cmd_inverse (ANSI)
    ├── hal_filesystem_esp32.c  # path/file/dir ops via lfs_*
    ├── hal_time_esp32.c        # esp_timer_get_time, vTaskDelay
    ├── hal_pin_esp32.c         # WRITTEN but NOT linked (Stage D2)
    ├── hal_pin_esp32_stub.c    # currently linked instead
    ├── hal_audio_esp32_stub.c  # stdio scope: no audio
    ├── hal_vm_framebuffer_esp32_stub.c  # stdio scope: no display
    ├── hal_keyboard_esp32_stub.c
    ├── hal_storage_esp32_stub.c
    ├── hal_flash_esp32_stub.c
    └── demos/{hello,fizzbuzz,sieve}.bas  # EMBED_TXTFILES; auto-populated to A: on first format
```

The MMBasic core sources stay in the repo root; the IDF main component enumerates them via `idf_component_register(SRCS "../../../MMBasic.c" ...)`.

### What's in the link line that shouldn't be (debt)

`ports/esp32_s3_metro/main/CMakeLists.txt` still pulls these from `ports/host_native/`:
- `host_runtime.c` — 1040 lines, ~50 symbols ESP32 currently relies on (most globals + most port_* hooks). **Goal: split this in Stage D3 into pico-shaped per-domain files; ESP32 then writes its own siblings.**
- `host_peripheral_stubs.c` — 645 lines, 80 cmd_/fun_ no-op stubs. Mostly genuinely needed; long-term replace with pico's per-driver pattern (out of scope for now).
- `host_fs.c` — POSIX dir-walker. **Not actually called from any ESP32 TU. Drop in Stage D6.**
- `host_keys.c` — test-harness scripted-key injection. Reached only via `host_runtime.c::MMInkey`. Drops out when D3 lands.
- `host_sim_slowdown.c` — 18-line cooperative-yield shim. Reached only via `host_runtime.c::CheckAbort`. Drops out when D3 lands.
- `host_sim_emit_stub.c` — 38 lines of weak no-ops for the WebSocket emit path. Link-only; leave.

### `--wrap` and `--allow-multiple-definition` (current debt)

```cmake
target_link_options(${COMPONENT_LIB} INTERFACE
    -Wl,--allow-multiple-definition
    -Wl,--wrap=port_drive_check
)
```

`--wrap=port_bc_runtime_free_source` was removed in D4 (BC_FREE is now the weak default in `bc_runtime.c`). The remaining `--wrap=port_drive_check` is a workaround for host_native's monolith and goes away in D3.

`--allow-multiple-definition` currently hides:
- `cmd_cpu` — host_peripheral_stubs (no-op) vs esp32_system (real). **Stage D3** moves the host one to a host-shape file ESP32 doesn't link.
- ~~`cmd_cls` / `cmd_locate` / `cmd_colour` / `cmd_inverse`~~ — D5 landed: routed through `port_terminal_handle_cls()` / `port_terminal_emit_colour()` weak hooks in Draw.c. cmd_locate / cmd_inverse turned out to be dead code (no BASIC keyword registered) and were dropped.
- `lfs_t lfs` — host_runtime (line 103) vs esp32_lfs (line 85). **Stage D7** removes the host_runtime definition (host has no real LFS storage anyway).
- `host_sd_root` — host_runtime `extern` vs esp32_flash_storage define. Becomes single-source after D3.
- Tentative-definition merging (`gui_bcolour`, `FSerror`, etc.) — legitimate, grandfathered.

After D3+D7, `--allow-multiple-definition` should still be needed only for tentative-definition merging.

## Current behaviour reference

What works end-to-end on the Metro today (commit before D-stage):

- `idf.py -p /dev/cu.usbmodem* flash` → boot → `>` prompt
- `PRINT`, `FOR`/`NEXT`, `IF`/`ELSE`/`END IF`, `GOTO`/`GOSUB`, `LIST`, `EDIT`, `CLS`, `LOCATE`, `COLOUR`, `INVERSE`, `CPU RESTART`
- `A:` (drive switch), `FILES` (lists embedded demos), `LOAD "hello.bas"`, `RUN "hello.bas"`, `RUN "fizzbuzz.bas"`, `RUN "sieve.bas"`
- `FRUN "hello.bas"` (works post-`""`-fix), `FRUN "fizzbuzz.bas"`, `FRUN "sieve.bas"` (works post-heap-reorder; ~5× faster than RUN)
- `B:` rejects with "B: drive not configured on this board" (correct)

What's broken or stubbed:
- **SAVE to slot N (e.g. `SAVE`, `FLASH SAVE n`)** — D1 landed: errors loudly past the 256-byte placeholder. Real backing in **Stage E1**.
- **OPTION SAVE** — reaches NVS-less host_options_snapshot, RAM-only, doesn't survive reboot. **Stage E2.**
- **PIN()/SETPIN/ADC** — D2 landed: real `hal_pin_esp32.c` linked. PWM (LEDC) still TODO when a caller needs it.
- All 80 `cmd_*`/`fun_*` peripheral stubs from `host_peripheral_stubs.c` — error gracefully, expected.

## Stage D — Decouple from host_native

The biggest debt. Goal: ESP32 port stops leaning on `host_runtime.c`'s monolith for device-shape behaviour. Each item is independent and small enough to fit in a single session.

### D1. Fix silent SAVE-to-slot bug ✅

**Problem**: `esp32_flash_storage.c::flash_range_erase`/`flash_range_program` bound-checked against a 256-byte `host_flash_target_buf` and silently `return`'d if the call exceeded bounds. Real SAVE writes multi-KB programs; bytes went nowhere, no error.

**Fix (landed)**: rewrote `flash_range_erase` / `flash_range_program` to mirror host_native's offset-routing in `host_fs_shims.c`. Two regions:
- Program-flash region (off ∈ [0, sizeof flash_prog_buf)): hits `flash_prog_buf`. `load_basic_source(0, MAX_PROG_SIZE)` works.
- Slot region (off ≥ FLASH_TARGET_OFFSET + ...): writes past the 256-byte placeholder error() out loudly.

Stage E1 replaces the slot region's RAM placeholder with `esp_partition_*` against a real flash partition.

### D2. Link `hal_pin_esp32.c` instead of `hal_pin_esp32_stub.c` ✅

`hal_pin_esp32.c` (~200 lines, real ESP-IDF GPIO + ADC oneshot impl) is now linked instead of `hal_pin_esp32_stub.c`. Required adding `esp_adc` to the IDF component REQUIRES list. Build green.

Hardware test (deferred to user): SETPIN GP13,DOUT : PIN(GP13)=1 → onboard LED lights. PIN(GP1) reads ADC. (Pick a suitable GPIO; verify against Adafruit's pinout.)

`hal_pin_esp32_stub.c` is left on disk but no longer in the link.

### Stage D-decouple — ESP32 satisfies the HAL contract; nothing more

**Reframing**: ESP32's port surface is defined by the HAL contract (`hal/hal_*.h`) and the small set of `port_*` / console-glue / cmd-stub symbols core code requires. ESP32 owns its impl of that surface in `ports/esp32_s3_metro/main/esp32_*.c` files and `hal_*_esp32.c` files — most of them no-ops and stubs. host_native is **irrelevant** to that impl: it's a different port, with POSIX/test-harness shape behind the same contract. ESP32 doesn't borrow from host_native any more than pico does. The current `HOST_NATIVE_REUSED` list is historical accident — early bring-up shortcut — not architecture.

**Step A — inventory the contract surface**. Enumerate every symbol the MMBasic core + drivers + HAL contracts require from a port:
- `nm -u` against `${PORT_LOCAL_SRCS}` + core/state/* + ${BC_SRCS} + ${CORE_SRCS} + ${DEVICE_FACING_SRCS} when host_native is excluded.
- Cross-reference `extern` declarations in core headers (`MMBasic.h`, `bytecode.h`, `hal/*.h`).
- Group by category. Most fall into:
  1. **HAL contract functions** (`hal_pin_*`, `hal_filesystem_*`, `hal_time_*`, etc.) — ESP32 already implements these in `hal_*_esp32.c`.
  2. **Console glue** (`MMputchar`, `MMPrintString`, `SSPrintString`, `MMInkey`, `MMgetchar`, `MMfopen/close/getline`, `putConsole`, `SerialConsolePutC`, `myprintf`, `getConsole`, `kbhitConsole`) — thin routers over the port's IO. ESP32 routes through USB Serial/JTAG via `esp32_console.c`'s existing `host_output_hook`/`host_read_byte_*` mechanism.
  3. **Default port hooks** (~35 `port_*` symbols: `port_apply_default_console_colors`, `port_audio_i2s_pio_slice`, `port_bc_*`, `port_clear_*`, `port_display_*`, `port_factory_reset_*`, `port_heartbeat_*`, `port_keyboard_*`, `port_lcd_*`, `port_mminfo_*`, `port_picocalc_*`, `port_pin_is_reserved_*`, `port_pio_*`, `port_poke_*`, `port_prepare_*`, `port_print_*`, `port_select_*`, `port_system_*`, `port_try_*`, `port_usb_*`, `port_vm_*`, `port_web_*`) — almost all no-op on ESP32. Maybe two need real bodies (`port_drive_check` for A:-only, `port_vm_time_get_tm` for date/time).
  4. **Tentative-def globals** (`gui_fcolour`, `gui_bcolour`, `FSerror`, `Option`, `PinDef[]`, `inttbl[]`, `dma_hw`, `watchdog_hw`, etc.) — declared in core headers, defined in some port TU. ESP32 owns its definitions.
  5. **Interpreter abort / VM trampoline** (`CheckAbort`, `check_interrupt`, `routinechecks`, `CallCFunction`, `CallExecuteProgram`) — ESP32 versions are basically empty.
  6. **Peripheral cmd stubs** (`cmd_i2c`, `cmd_pwm`, `cmd_spi`, `cmd_pio`, `cmd_setpin` etc., plus `fun_*` siblings) — ESP32 doesn't expose most of these yet; stub to `error()` or no-op. Real impl per peripheral over time.

Output: a markdown table inventory with counts per category, posted into the log, and a one-line owner per symbol (existing file or new).

**Step B — write minimal ESP32 files**. New TUs in `ports/esp32_s3_metro/main/` whose total line count should be roughly **<500 lines combined** (versus host_runtime.c's 1032):
- `esp32_console.c` (existing) — extend with `MMputchar`/`MMPrintString`/etc routers if not already present.
- `esp32_globals.c` — tentative-def globals + the trivial port hooks that just return `0` or no-op.
- `esp32_default_hooks.c` — the ~35 `port_*` no-ops as one-liners. ESP32-specific overrides (`port_drive_check`, `port_vm_time_get_tm`) live elsewhere and this file's no-op gets replaced per-symbol.
- `esp32_runtime.c` — `CheckAbort`, `check_interrupt`, `routinechecks`, `CallCFunction`, `host_runtime_begin/finish/configure` (all no-ops on ESP32; app_main is the entry).
- `esp32_peripheral_stubs.c` — `cmd_i2c` / `cmd_pwm` / `cmd_spi` / etc as `void cmd_x(void) { error("X not supported on this port yet"); }`. `cmd_setpin`/`fun_pin` route through `vm_sys_pin.c` HAL just like host does.
- `esp32_cmd_files_hooks.c` (or fold into `esp32_runtime.c`) — `port_drive_check` (A:-only error if drive!='A'), `port_mount_sd_drive` (no-op), `port_apply_load_overrides` (no-op), `cmd_files_save_program_context` etc as no-ops.

**Step C — drop ALL host_native files from the ESP32 link in one move**. After step B compiles + links, delete `HOST_NATIVE_REUSED` entirely from `main/CMakeLists.txt`. Drop `-Wl,--wrap=port_drive_check` (esp32 owns the symbol). Don't try to drop `--allow-multiple-definition` yet (step E).

**Step D — `port_bc_runtime_free_source` cleanup**. Currently has a weak BC_FREE default in `bc_runtime.c`. Per rule 3, weak attributes are unsafe under `--allow-multiple-definition`. Delete the weak body; make it extern-only. Add explicit strong defs:
- `host_bc_runtime_noop.c` — already exists; no-op (host).
- `esp32_runtime.c` — BC_FREE body (new).
- `ports/pico_sdk_common/bc_runtime_pico.c` — recreate with BC_FREE body (was deleted in old D4 misstep).

**Step E — kill `--allow-multiple-definition`**. After step C the only remaining users are the tentative-def globals (`gui_bcolour`, `FSerror`, etc. declared without initialiser in multiple TUs). Two options:
  - **(a)** `-fcommon` on the IDF component. xtensa-gcc honours it; matches pico/host default behaviour for free.
  - **(b)** Single-owner TU per global, `extern` declarations elsewhere. ~30 globals, mechanical edit, codebase-wide effect.
Land (a) first; (b) is the codebase-wide cleanup and lives outside this port plan.

**Step F — verify**. ESP32 IDF build green with default-strict link rules; host 242/242; HAL purity clean; hardware smoke: CLS clears, COLOUR changes colour, SETPIN+PIN drives a GPIO, FRUN sieve.bas runs.

**What this is NOT**:
- Not a copy-paste of `host_runtime.c` into `esp32_runtime.c`.
- Not a refactor of host_native (host's monolith stays intact; we just stop linking it from ESP32).
- Not a port to the ESP32 of host's lifecycle / test-harness concerns. ESP32's lifecycle is `app_main`. Host's `host_runtime_begin/finish/configure` exist on ESP32 only as no-op stubs because some core path calls them; nothing more.

### D5. Resolve `cmd_cls`/`cmd_colour` collision ✅

**First attempt (failed in hardware)**: weak default in `Draw.c` + strong override in `esp32_terminal.c`. Looked correct on paper. On hardware, both `port_terminal_handle_cls` and `port_terminal_emit_colour` resolved to `Draw.c.obj` (the weak default). Diagnosis: `--allow-multiple-definition` (still in the ESP32 link line for grandfathered tentative-def merging) defeats the weak attribute — first-defined wins, weak/strong is ignored. Lesson: weak attributes are unsafe in a build using `--allow-multiple-definition`. **See rule 3 above.**

**Second attempt (landed)**: strong-only pattern. `Draw.c` declares both hooks `extern` only, no body. Each port supplies exactly one strong definition in a port-only TU:
- `ports/esp32_s3_metro/main/esp32_terminal.c` — emits ANSI clear / 24-bit colour escapes.
- `ports/host_native/host_terminal_hooks_noop.c` — no-op (framebuffer ports run Draw.c's framebuffer path). Linked by host_native, mmbasic_stdio, mmbasic_ansi, host_wasm.
- `ports/pico_sdk_common/terminal_hooks_noop.c` — no-op (pico has a real LCD). Linked by every pico variant via PICOMITE_SOURCES.

`cmd_cls` / `cmd_colour` deleted from `esp32_terminal.c`; `cmd_locate` / `cmd_inverse` turned out to be dead code (no BASIC `LOCATE` / `INVERSE` keyword is registered in `AllCommands.h`) and were removed. Map verified post-build: `port_terminal_handle_cls` and `port_terminal_emit_colour` resolve to `esp32_terminal.c.obj` on ESP32 and to the noop file on each host build. Tests 242/242, ESP32 + host builds green.

### D8. Override port_config values that are wrong on ESP32 ✅

`ports/esp32_s3_metro/port_config.h` now overrides:
- `HAL_PORT_NBR_PINS` → 49 (was inherited 44; ESP32-S3 has 49 GPIOs 0–48). Bigger arrays: `ExtCurrentConfig[NBRPINS+1]`, `PinDef[NBRPINS+1]`, `p100interrupts[NBRPINS+1]`.
- `HAL_PORT_HEAP_TOP` / `HAL_PORT_HEAP_TOP_USB` → 0 (sentinel; was RP2040 mmap address).
- Heap comment reconciled with the 104 KB value (was claiming 192 KB; the value was correct, the comment wasn't).

Documented but not yet overridden:
- `HAL_PORT_FLASH_TARGET_OFFSET` (1 MB on host; revisit when E1 lands and esp_partition_t replaces the offset arithmetic).
- `HAL_PORT_PWM_SLICE_COUNT` / `HAL_PORT_PIO_COUNT` (RP2040 numbers; inert until a real PWM/PIO impl exists).

### D9. Remove app_main's hard-coded Option-init dance ⏳

`app_main.c` currently sets ~12 Option fields after `LoadOptions()` because the flash mirror is uninitialised. That logic belongs in a `LoadOptions` "no saved Options found, use defaults" path, gated by an OPTION_ERASE_DEFAULTS macro or similar. Holds until E2 (NVS-backed Options) lands; tracked separately from the decouple plan because it's a flash-persistence question, not a host_native-decouple question.

## Stage E — Real flash persistence

Real device-shape persistence. Replaces the current RAM mirrors with `esp_partition_*`-backed storage.

### E1. SAVE-to-slot via esp_partition

Allocate a partition (or carve from `lfsdata`'s start) for SAVE-to-slot storage. Replace `host_flash_target_buf[256]` placeholder + bounds-checked stubs with:

- `flash_range_erase(off, count)` → `esp_partition_erase_range(slot_partition, off, count)`
- `flash_range_program(off, data, len)` → `esp_partition_write(slot_partition, off, data, len)`
- `flash_target_contents` → const-pointer view via `esp_partition_mmap(...)`

Match pico's slot model: N slots × MAX_PROG_SIZE each; `cmd_save N` writes to slot N.

**Exit gate**: `SAVE 1` then power-cycle then `LOAD 1` then `RUN` → original program runs.

### E2. Options blob in NVS

Replace `host_flash_option_buf[sizeof(struct option_s)]` RAM mirror + `host_options_snapshot()` memcpy with NVS-backed storage:

- `nvs_flash_init()` at boot; on `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND`, erase + re-init (standard ESP-IDF idiom).
- `LoadOptions()` reads NVS blob; on first boot (no key) populates defaults — replacing the current `app_main.c` hard-coded init dance.
- `SaveOptions()` writes NVS blob.
- `nvs_set_blob` has a default ~4 KB cap; verify `sizeof(struct option_s)` fits or bump `CONFIG_NVS_MAX_ENTRY_SIZE`.

**Exit gate**: `OPTION COLOUR GREEN` → power cycle → prompt comes back green. `OPTION COLOUR RESET` undoes it.

### E3. Why no PSRAM yet

8 MB Octal PSRAM is on-chip, currently disabled in `sdkconfig.defaults`. Two reasons not to enable yet:

1. **Internal SRAM works** for the stdio-REPL litmus test. 104 KB heap fits FRUN sieve(6000). Don't turn on a 15-20× slower memory tier without a forcing function.
2. **PSRAM enabling is a non-trivial change** — `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` heap split, BC heap routing decisions, framebuffer-in-PSRAM-with-DMA-cap requirement once a display lands, brownout detector recalibration. Right time is when display or audio land, not stdio-only.

Capture this decision so the next session doesn't re-litigate it.

## Stage F — Plan + gate hygiene

### F1. Wire ESP32 into HAL purity gate

`tools/check_hal_purity.sh` currently has no ESP32 port awareness. Add `ports/esp32_s3_metro/main/*.c` to a strict scope (mirror what host_native gets). Forbidden-macro list for the ESP32 strict scope: any target macro **except** `MMBASIC_ESP32` itself (which is the port's own identity tag — fine inside its own port directory).

ESP32 port code is currently clean (one `#ifndef HAL_FS_ESP32_MAX_OPEN` tunable in `hal_filesystem_esp32.c`, which is fine — it's a port-config tunable, not a target gate). The gate keeps it that way.

### F2. Update `docs/real-hal-plan.md` scoreboard

Add a row to the phase table:

```
| esp32-port | 🔧 | A+B+C done; D (decouple from host_native) in flight; E (flash persistence), F (gate) not started — see real-hal/esp32-s3-port.md |
```

Add `esp32-s3-port.md` to the topic list near the bottom of `real-hal-plan.md`.

### F3. Rewrite `ports/esp32_s3_metro/README.md`

Currently aspirational. Should document:
- Hardware: Adafruit Metro ESP32-S3 (#5500, N16R8).
- Prereqs: ESP-IDF 5.3 (`. ~/esp/esp-idf/export.sh`), `dfu-util`.
- Build: `idf.py build`.
- Flash: `idf.py -p /dev/cu.usbmodem* flash`. Note the BOOT+RESET tap recovery if macOS USB-CDC binding hangs.
- Monitor: `idf.py monitor` or `picocom --noreset --baud 115200 /dev/cu.usbmodem*`. Document picocom's known issue (DTR pulse on open) and `probe.py` as the workaround.
- What works (current behaviour reference, keep in sync with this plan).

### F4. Optional: `buildesp32.sh`

Sibling of `buildall.sh`. Sources `~/esp/esp-idf/export.sh` if needed, runs `idf.py build`, reports OK/FAIL. **Opt-in, not in CI** — `idf.py` requires a heavyweight environment that doesn't belong in the default gate. Land if desired; not blocking.

## Out of scope (deferred — don't expand without an explicit goal)

- **Display.** SPI LCD / VGA via LCD_CAM. Multi-session. See "Display follow-on (sketch)" at the bottom of this doc.
- **Audio.** I2S codec, MP3 decode, PWM synth port.
- **Keyboard.** USB host (TinyUSB), I2C keypad (PicoCalc-style), PS/2.
- **WiFi/BLE.** Native on ESP32-S3, easier than RP2350 CYW43. Land when there's a use case — not before.
- **Octal PSRAM.** See Stage E3.
- **OTA.** Two app slots, signed updates. Out of scope for a stdio litmus.

## Risks worth pre-flagging

1. **macOS USB-CDC binding flakiness**. After many fast reset cycles, macOS gets confused about the USB CDC binding. Symptom: `idf.py flash` hangs on "Connecting...". Recovery: hold BOOT, press RESET, release BOOT (enters ROM USB Direct mode, stable CDC ACM). `probe.py` already documents this.

2. ~~**`HAL_PORT_NBR_PINS` overflow risk**~~. D8 landed: NBRPINS = 49 covers all ESP32-S3 GPIOs.

3. **Heap fragmentation on FRUN with big arrays**. `bc_compiler_alloc` reorder (committed) helps; t170_frun_post_compact_array.bas is the regression net. If a real program OOMs at array alloc despite that, the next mitigations are (a) bump heap (capped at ~110 KB by dram0_0_seg), (b) `heap_caps_malloc` big arrays from IDF heap (130 KB free), (c) enable PSRAM. Document what you observe before mitigating.

4. **`flash_target_contents` host-shape leak**. Currently a `const uint8_t *` global pointing at a RAM mirror. Real device shape is XIP-mapped flash (pico) or `esp_partition_mmap` (esp32). E1 fixes ESP32; the type signature on host is wrong-but-harmless until anyone tries to use it as a real flash view there.

5. **Newlib reentrancy + FreeRTOS**. ESP-IDF newlib has per-task `_reent`. Tasks created via `xTaskCreate` get reentrancy automatically; raw FreeRTOS APIs don't. If a future change spawns its own MMBasic worker task, use `xTaskCreate`.

6. **Brownout detector**. PSRAM init draws inrush current; some Metro boards trip BOD on cold boot. Defer until PSRAM lands; lower `CONFIG_ESP32S3_BROWNOUT_DET_LVL` if needed.

## Files to know

- `ports/esp32_s3_metro/main/CMakeLists.txt` — source-of-truth for what's linked. The `HOST_NATIVE_REUSED` list and `target_link_options` block are where the audit-driven cleanup edits land.
- `ports/esp32_s3_metro/main/esp32_flash_storage.c` — flash backing storage, now mirrors host_native's offset-routing post-D1.
- `ports/esp32_s3_metro/main/esp32_system.c` — has `__wrap_port_drive_check` (D3 target).
- `ports/host_native/host_bc_runtime_noop.c` — host's strong override of the weak BC_FREE default in bc_runtime.c (post-D4).
- `ports/esp32_s3_metro/port_config.h` — D8 edits land here.
- `ports/esp32_s3_metro/probe.py` — debug driver; use it instead of picocom.
- `ports/host_native/host_runtime.c` — the monolith to split (D3).
- `ports/pico_sdk_common/` — the reference for what device-port shape looks like.
- `tools/check_hal_purity.sh` — F1 edits the strict-scope file list here.
- `docs/real-hal-plan.md` — F2 adds the scoreboard row.

## Display follow-on (sketch — deferred)

Not part of the stdio litmus. Path is well-trodden:

**SPI LCD (recommended first cut)**: ESP-IDF has first-class `esp_lcd_panel_*` drivers for ILI9341 / ST7789 / ST7796 / GC9A01 / RM67162 — exactly the panels `drivers/spi_lcd/` already supports on Pico. BASIC surface (`OPTION LCDPANEL ILI9341 ...`, `BACKLIGHT`, `BLIT`, etc.) needs no changes.

Driver path:
- `drivers/spi_lcd_esp32/` wraps `esp_lcd_panel_io_spi` + `esp_lcd_panel_*`, implements the `hal_display_pixel.h` + `hal_spi_lcd_mem332.h` contracts pico's driver exposes. Glue, not rewrite — pico's driver is `pico/multicore.h`-coupled and can't be reused, but the contract surface is identical.
- `hal_display_esp32.c` in the port directory: panel selection, backlight pin, framebuffer dimensions.
- Framebuffer in PSRAM via `heap_caps_malloc(W*H*bpp, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM)`. DMA cap is non-optional for `esp_lcd_panel_io_spi`.
- `port_config.h` sets `HAL_PORT_HAS_SPI_LCD=1`, CS/DC/RESET GPIOs, panel type, dimensions.

Metro ESP32-S3 (#5500) wiring:
- 3.3 V only — no 5 V tolerance. Verify any LCD module is 3.3 V-safe (Adafruit's 1770 / 2478 / 4313 / 4383 are).
- Default SPI: SCK=GPIO39, MOSI=GPIO42, MISO=GPIO21 (rev B SD-slot wiring; remappable via IO MUX).
- Backlight: 80–100 mA on a 2.8" panel — exceeds 40 mA per-GPIO limit. Wire to 3V3 directly or via N-FET; never straight off a GPIO.

Effort: 3–4 sessions for SPI path. VGA via LCD_CAM is multi-session and only worth it on Octal-PSRAM boards where 640×480 is achievable.

---

**The fastest path to a clean Stage D**: items D1 → D2 → D8 are tiny and high-value (no silent SAVE, real PIN, correct pin count). D3 is the big one but unblocks D4/D5/D6/D7 in cascade. Do D1+D2+D8 first to remove user-facing issues, then attack D3.
