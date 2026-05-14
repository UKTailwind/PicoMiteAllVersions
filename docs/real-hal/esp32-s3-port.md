# ESP32-S3 (Adafruit Metro) Port Plan

**Goal:** an MMBasic stdio REPL on the Adafruit Metro ESP32-S3 over USB Serial/JTAG, structured as a real device port that mirrors `ports/pico_sdk_common/` — not a host-shape simulator. The ESP-IDF is the hardware-access layer behind the HAL; nothing in core MMBasic learns about it.

Companion log: [esp32-s3-port-log.md](esp32-s3-port-log.md).
Network core follow-on: [network-core-plan.md](network-core-plan.md).
WiFi/browser video, keyboard, and sound follow-on:
[web-console-driver-plan.md](web-console-driver-plan.md).

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
| C — interactive REPL | ✅ | PRINT, FOR/NEXT, IF/ELSE, GOTO/GOSUB, LIST, EDIT, CPU RESTART, CLS, COLOUR all work |
| C — A: drive (LFS) | ✅ | LFS over `esp_partition_*`, bundled demos seed-only with zero-byte repair, FILES/LOAD/SAVE-to-file/RUN/FRUN all work for files on A: |
| C — VM source compiler regression fixes | ✅ | adjacent string literals (`""` in PRINT), post-compact heap fragmentation, wrapped-multiply optimizer semantics |
| D — decouple from host_native | 🔧 | Runtime/peripheral host_native sources are gone; ESP32 owns the port surface in `esp32_*.c` and `hal_*_esp32.c`; core/shared Pico SDK leakage is clean; strict link policy is active. BASIC-visible GPIO DOUT/DIN/ARAW, WS2812 output, and the WEB network surface are hardware-smoked. Legacy `hardware/*` header shims now live under `ports/pico_sdk_compat/`. Remaining debt: PWM/servo are not wired and MQTT is plain TCP only. |
| E — real flash persistence | ✅ | NVS-backed Options and numbered `FLASH SAVE`/`FLASH LOAD` slots are implemented and hardware-smoked. `VAR SAVE` shares the `mmslots` backing. |
| F — gate + plan hygiene | ✅ | ESP32 port files are in the HAL purity gate; `docs/real-hal-plan.md`, this port README, and opt-in `buildesp32.sh` are current. |
| G — device smoke + HAL cleanup | ✅ | G0 smoke suite is implemented and passing, including opt-in flash/VAR persistence and network conformance. G1 plan/comment drift cleanup, G2 explicit ESP32 port config, G3 neutral hardware shims, and G4 removal of the temporary host compile identity are done. |
| H — PSRAM contract | 🔧 | ESP-IDF Octal PSRAM is enabled caps-only for the N16R8 Metro, with port-local reporting and an opt-in heap_caps march smoke. BASIC `PSRAMsize` remains 0 so generic `Memory.c` does not enter the RP2350 PSRAM allocator. |

**Headline broken-but-silent bug** (fixed in D1 below): `flash_range_erase` / `flash_range_program` in `esp32_flash_storage.c` previously targeted a 256-byte placeholder buffer and silently no-op'd past the end. ESP32 now routes program-region writes into `flash_prog_buf` and routes saved-vars / numbered-slot writes to the `mmslots` partition.

**Tests**: `python3.11 -m py_compile porttools/basic_serial.py porttools/esp32_fs_vm_smoke.py porttools/network_conformance.py porttools/pico_fs_vm_smoke.py porttools/esp32_tcp_smoke.py` passes. `./host/run_tests.sh` is 244/244, with the VM pin-mode helper and HAL purity clean. `./buildall.sh` builds all 14 device variants and passes the RAM baseline gate. `./buildesp32.sh build` is green; current image size is `0x191e50` with 22% of the app partition free. The latest flashed Metro smoke passed the default `esp32_fs_vm_smoke.py` suites, opt-in `psram`, opt-in `flash --var-save`, and network conformance through `esp32_fs_vm_smoke.py network --run-network` (TCP client/server/transmit page/file/css/js/image/code, UDP, TFTP, telnet console after BASIC error, NTP, and MQTT). B:/SD is skipped as unconfigured. Host-side ESP32 smoke tooling lives in `porttools/`; see [porttools/README.md](../../porttools/README.md).

**Latest hardware smoke (2026-05-13)**:
- A: drive bundled demos now include `mand.bas`. Demo population is seed-only; non-empty user-edited files are not overwritten at boot, while zero-byte bundled demos are repaired.
- `SAVE "file.bas"` now errors `No program` before opening/truncating the target if no tokenized program is loaded. This prevents the confusing empty-program clobber path after editing a file without `LOAD`.
- `RUN "mand.bas"` and `FRUN "mand.bas"` both produce checksum `552868`. Current Metro measurement: `FRUN` ~359 ms / ~8554 pixels/sec; `RUN` ~8569 ms / ~358 pixels/sec, about 24x faster through bytecode.
- Ordinary BASIC `(a*b)\2^n` optimizer fusion now preserves wrapped integer multiply semantics via dedicated bytecode ops. Explicit `MULSHR()` and `!ASM mulshr` remain wide fixed-point multiply-shift operations.
- `FLASH SAVE 1`, reset, `FLASH LOAD 1`, `RUN` reloads and runs `hello.bas` from the dedicated `mmslots` partition.
- `WEB CONNECT` joins WiFi; `WEB SCAN array%()` returns long-string scan data.
- `OPTION TCP SERVER PORT`, `WEB TCP INTERRUPT`, `WEB TCP READ`, `WEB TCP SEND`, `WEB TCP CLOSE`, and `WEB TRANSMIT PAGE/FILE/CODE/CSS/JS/IMAGE` serve a multi-file website from A: and are fetchable from macOS.
- `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`, `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, and `WEB CLOSE TCP CLIENT` pass the Mac-side smoke in `porttools/esp32_tcp_smoke.py`.
- `OPTION UDP SERVER PORT`, `WEB UDP SEND`, UDP receive state through `MM.MESSAGE$` / `MM.ADDRESS$`, `WEB NTP`, and plain-TCP `WEB MQTT CONNECT/PUBLISH/SUBSCRIBE/UNSUBSCRIBE/CLOSE` have hardware-smoked.

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

6. **Heap is tight and the port code knows it.** 48 KB MMBasic heap while WiFi is enabled. ESP-IDF now detects the Metro's Octal PSRAM, but it is caps-only and not part of `AllMemory`. Compiler scratch tables allocate from ESP-IDF internal heap on ESP32 so large compile-time temporaries do not consume `AllMemory`, and VM runtime allocations still come from the 48 KB MMBasic heap. New runtime allocations in this port must respect the same fragmentation discipline.

7. **Pico source is the reference.** When in doubt about how a HAL contract is supposed to be exercised, read pico's impl — not host_native's. Host has historical shape (POSIX-rooted, malloc-flavoured) that doesn't translate.

8. **Keep tests passing every commit.** Host suite must stay at the current count or higher — never lower. HAL purity must stay green. ESP-IDF build must stay green. ESP32 hardware smoke (`FRUN "mand.bas"` / sieve.bas / fizzbuzz.bas) must stay green.

## Layout (current, not aspirational)

```
ports/esp32_s3_metro/
├── CMakeLists.txt              # ESP-IDF project root
├── port_config.h               # Explicit ESP32 HAL_PORT_* values; no host_native inheritance
├── partitions.csv              # 1 MB app + 12 MB lfsdata + 1 MB mmslots
├── sdkconfig.defaults          # USB JTAG console, watchdog off, WiFi enabled, caps-only Octal PSRAM
├── probe.py                    # pyserial test driver (avoids picocom DTR pulse)
├── README.md                   # build/flash/monitor, current status, known gaps
└── main/
    ├── CMakeLists.txt          # main component, source list, strict-link policy
    ├── esp32_platform.h        # MMBASIC_ESP32 + Pico SDK section-attr stubs
    ├── app_main.c              # IDF entry, MMBasic boot, REPL launch
    ├── esp32_console.c         # USB Serial/JTAG byte I/O via esp32_console_*
    ├── esp32_lfs.c             # LFS over esp_partition_* for the A: drive
    ├── esp32_flash_storage.c   # flash_target_*/esp32_options_snapshot/SaveProgramToFlash
    ├── esp32_wifi.c            # ESP-IDF WiFi + BASIC WEB/TCP/UDP/NTP/plain MQTT surface
    ├── esp32_compat.c          # flash_prog_buf, timegm, readusclock/uSec compatibility
    ├── esp32_system.c          # cmd_cpu (esp_restart)
    ├── esp32_terminal.c        # ANSI terminal hooks for CLS and COLOUR
    ├── hal_filesystem_esp32.c  # path/file/dir ops via lfs_*
    ├── hal_time_esp32.c        # esp_timer_get_time, vTaskDelay
    ├── hal_pin_esp32.c         # GPIO/ADC HAL over ESP-IDF
    ├── hal_pin_esp32_stub.c    # retained on disk, not linked
    ├── hal_audio_esp32_stub.c  # stdio scope: no audio
    ├── hal_vm_framebuffer_esp32_stub.c  # stdio scope: no display
    ├── hal_keyboard_esp32_stub.c
    ├── hal_storage_esp32_stub.c
    ├── hal_flash_esp32_stub.c
    └── demos/{hello,fizzbuzz,sieve,mand,web_hello,site*}  # EMBED_TXTFILES; seed-only auto-populated to A:
```

Host-side serial/network smoke tooling lives in `porttools/`. The important
entry points are `porttools/basic_serial.py` for prompt-driven command checks
and `porttools/esp32_tcp_smoke.py` for Mac-side TCP client request/stream
checks.

The MMBasic core sources stay in the repo root; the IDF main component enumerates them via `idf_component_register(SRCS "../../../MMBasic.c" ...)`.

### Remaining host-shape debt

The ESP32 link line no longer includes `host_runtime.c`, `host_peripheral_stubs.c`, `host_fs.c`, `host_keys.c`, `host_sim_slowdown.c`, or `host_sim_emit_stub.c`. The old `HOST_NATIVE_REUSED` shortcut is gone.

Remaining coupling is narrower:

- The simulator VM syscall bodies live under `ports/vm_sys_sim/` for host-style builds. ESP32 no longer links either simulator body: file syscalls use shared device `vm_sys_file.c`, and pin syscalls use ESP32-owned `vm_sys_pin_esp32.c` plus the Metro pin table.
- ESP32 no longer needs a `pico/stdlib.h` compatibility shim for core/shared code. A strict scan of the core/shared scope is clean for Pico SDK includes/APIs. Remaining `hardware/*` Pico SDK header shims come from the neutral `ports/pico_sdk_compat/` include path until they disappear behind HAL.
- `ports/esp32_s3_metro/port_config.h` now defines ESP32's `HAL_PORT_*` surface explicitly instead of inheriting host defaults.
- `esp32_platform.h` defines `MMBASIC_ESP32` only. The temporary host compile identity is gone; the HAL purity gate now checks the ESP32 platform/CMake files for regression.

### Strict symbol policy

No ESP32 symbol should depend on link-order tricks:

- `--wrap` is forbidden. ESP32 owns `port_drive_check` and any other ESP32-specific hook directly.
- `--allow-multiple-definition` is forbidden. Duplicate strong functions must fail the link.
- `-fcommon` is allowed as a temporary compatibility flag for legacy tentative globals only. It must not be used to mask duplicate functions.
- Unsupported hardware should be represented by explicit ESP32 HAL or command stubs, not inherited host behavior.

## Current behaviour reference

What works end-to-end on the Metro today:

- `idf.py -p /dev/cu.usbmodem* flash` → boot → `>` prompt
- `PRINT`, `FOR`/`NEXT`, `IF`/`ELSE`/`END IF`, `GOTO`/`GOSUB`, `LIST`, `EDIT`, `CLS`, `COLOUR`, `CPU RESTART`
- `A:` (drive switch), `FILES` (lists embedded demos), `LOAD "hello.bas"`, `RUN "hello.bas"`, `RUN "fizzbuzz.bas"`, `RUN "sieve.bas"`
- `FRUN "hello.bas"` (works post-`""`-fix), `FRUN "fizzbuzz.bas"`, `FRUN "sieve.bas"` (works post-heap-reorder; ~5× faster than RUN)
- `RUN "mand.bas"` and `FRUN "mand.bas"` produce the same checksum (`552868`); `FRUN` is about 24x faster on the current benchmark.
- `SAVE "file.bas"` to A: works when a program is loaded and refuses empty-program saves before truncating the target.
- `FLASH SAVE 1`, reset, `FLASH LOAD 1`, `RUN` works for numbered flash slots backed by `mmslots`.
- `WS2812 B, GP46, 1, &Hrrggbb` drives the Metro onboard RGB LED through ESP-IDF RMT.
- `WEB CONNECT` joins configured WiFi; `WEB SCAN` and `WEB SCAN array%()` report visible networks.
- `OPTION TCP SERVER PORT`, `WEB TCP INTERRUPT`, `WEB TCP READ`, `WEB TCP SEND`, `WEB TCP CLOSE`, and `WEB TRANSMIT PAGE/FILE/CODE/CSS/JS/IMAGE` serve BASIC-generated responses and files from A:.
- `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`, `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, and `WEB CLOSE TCP CLIENT` work against Mac-side TCP smoke endpoints.
- `OPTION UDP SERVER PORT` and `WEB UDP SEND` work; UDP receive updates `MM.MESSAGE$` and `MM.ADDRESS$`.
- `WEB NTP` updates BASIC `DATE$` and `TIME$`.
- `WEB MQTT CONNECT/PUBLISH/SUBSCRIBE/UNSUBSCRIBE/CLOSE` works for plain TCP MQTT; received messages update `MM.TOPIC$` and `MM.MESSAGE$`.
- `B:` rejects with "B: drive not configured on this board" (correct)

What's broken or stubbed:
- **PWM/servo as BASIC-visible hardware** — GPIO DOUT/DIN/ARAW is wired and hardware-smoked, but LEDC-backed PWM/servo is not implemented yet and errors explicitly.
- **MQTT TLS/cert handling** — MQTT is currently plain TCP only.
- **BLE/Bluetooth** — no ESP32 BLE/Bluetooth BASIC surface is implemented.
- ESP32-local `cmd_*`/`fun_*` peripheral stubs — unsupported hardware errors or no-ops are expected until each domain gets a real ESP32 HAL/driver.

## Stage D — Decouple from host_native

Goal: ESP32 behaves like a real device port. Device-facing behavior lives in `hal_*_esp32.c`, `esp32_*.c`, or ESP32 driver directories. Host-native can be a reference, but not a linked behavior source.

### D1. Fix silent SAVE-to-slot bug ✅

**Problem**: `esp32_flash_storage.c::flash_range_erase`/`flash_range_program` bound-checked against a 256-byte `host_flash_target_buf` and silently `return`'d if the call exceeded bounds. Real SAVE writes multi-KB programs; bytes went nowhere, no error.

**Fix (landed)**: rewrote `flash_range_erase` / `flash_range_program` to mirror host_native's offset-routing in `host_fs_shims.c`. Two regions:
- Program-flash region (off ∈ [0, sizeof flash_prog_buf)): hits `flash_prog_buf`. `load_basic_source(0, MAX_PROG_SIZE)` works.
- Slot region (off ≥ FLASH_TARGET_OFFSET + ...): writes past the 256-byte placeholder error() out loudly.

Stage E1 replaced the slot region's RAM placeholder with `esp_partition_*` against the real `mmslots` flash partition.

### D2. Link `hal_pin_esp32.c` instead of `hal_pin_esp32_stub.c` ✅

`hal_pin_esp32.c` (~200 lines, real ESP-IDF GPIO + ADC oneshot impl) is now linked instead of `hal_pin_esp32_stub.c`. Required adding `esp_adc` to the IDF component REQUIRES list. Build green.

Superseded caveat: the low-level HAL originally only proved that GPIO/ADC linked. D9 added the ESP32 pin table and BASIC command/function path, then hardware-smoked `SETPIN GP13,DOUT`, `PIN(GP13)=1/0`, `SETPIN GP13,DIN,PULLUP`, and `SETPIN GP1,ARAW`.

`hal_pin_esp32_stub.c` is left on disk but no longer in the link.

### Stage D-decouple — ESP32 satisfies the HAL contract; nothing more

**Reframing**: ESP32's port surface is defined by the HAL contract (`hal/hal_*.h`) and the small set of `port_*` / console-glue / cmd-stub symbols core code requires. ESP32 owns its impl of that surface in `ports/esp32_s3_metro/main/esp32_*.c` files and `hal_*_esp32.c` files — most of them no-ops and stubs. host_native is **irrelevant** to that impl: it's a different port, with POSIX/test-harness shape behind the same contract. ESP32 doesn't borrow from host_native any more than pico does. The removed `HOST_NATIVE_REUSED` list was an early bring-up shortcut, not architecture.

**Step A — inventory the contract surface ✅**. Full per-symbol assignment table at [esp32-s3-decouple-inventory.md](esp32-s3-decouple-inventory.md).

Headline: **277 symbols** where host_native is currently the sole provider, split across 11 owner files (5 new, 6 existing-extended):

| Owner | Count | New or existing |
|---|---|---|
| `esp32_peripheral_stubs.c` | 150 | new |
| `esp32_compat.c` | 26 | existing |
| `esp32_console.c` | 25 | existing |
| `esp32_default_hooks.c` | 17 | new |
| `esp32_globals.c` | 14 | new |
| `hal_vm_framebuffer_esp32_stub.c` | 14 | existing |
| `hal_audio_esp32_stub.c` | 9 | existing |
| `esp32_runtime.c` | 9 | new |
| `esp32_flash_storage.c` | 8 | existing |
| `esp32_cmd_files_hooks.c` | 3 | new |
| `esp32_lfs.c` | 2 | existing |

Method: `xtensa-esp-elf-nm` undefined-set (non-host_native objs) ∩ defined-set (host_native objs). 1238 total undef refs across non-hn → 435 genuine gaps after subtracting locally-defined → 277 of those 435 are host_native-provided (the rest come from libc/esp-idf and are fine). Stale objs (`esp32_glue.c.obj`, `esp32_disk.c.obj`, the two superseded `_stub` siblings) excluded.

**Step B — write minimal ESP32 files ✅**. ESP32 now has port-local TUs in `ports/esp32_s3_metro/main/`:
- `esp32_console.c` (existing) — extend with `MMputchar`/`MMPrintString`/etc routers if not already present.
- `esp32_globals.c` — tentative-def globals + the trivial port hooks that just return `0` or no-op.
- `esp32_default_hooks.c` — the ~35 `port_*` no-ops as one-liners. ESP32-specific overrides (`port_drive_check`, `port_vm_time_get_tm`) live elsewhere and this file's no-op gets replaced per-symbol.
- `esp32_runtime.c` — `CheckAbort`, `check_interrupt`, `routinechecks`, `CallCFunction`, `host_runtime_begin/finish/configure` (all no-ops on ESP32; app_main is the entry).
- `esp32_peripheral_stubs.c` — `cmd_i2c` / `cmd_pwm` / `cmd_spi` / etc as `void cmd_x(void) { error("X not supported on this port yet"); }`. `cmd_setpin`/`fun_pin` route through `vm_sys_pin.c` HAL just like host does.
- `esp32_cmd_files_hooks.c` (or fold into `esp32_runtime.c`) — `port_drive_check` (A:-only error if drive!='A'), `port_mount_sd_drive` (no-op), `port_apply_load_overrides` (no-op), `cmd_files_save_program_context` etc as no-ops.

**Step C — drop host_native runtime/peripheral files ✅**. `HOST_NATIVE_REUSED` is gone. ESP32 no longer links `host_runtime.c`, `host_peripheral_stubs.c`, `host_fs.c`, `host_keys.c`, `host_sim_slowdown.c`, or `host_sim_emit_stub.c`. `--wrap=port_drive_check` is gone; ESP32 owns the symbol directly.

**Step D — `port_bc_runtime_free_source` cleanup ✅**. `bc_runtime.c` declares the hook only. Each port supplies exactly one strong definition:
- `host_bc_runtime_noop.c` — no-op because host test source may be malloc-owned.
- `esp32_runtime.c` — BC_FREE body.
- `ports/pico_sdk_common/bc_runtime_pico.c` — BC_FREE body for Pico SDK ports.

**Step E — kill `--allow-multiple-definition` ✅**. The ESP32 component does not pass `-Wl,--allow-multiple-definition`. Use `-fcommon` only for legacy tentative globals (`gui_bcolour`, `FSerror`, etc.). If future duplicate strong functions appear, fix the source list or ownership; do not re-add the linker flag.

Longer term, replace `-fcommon` with single-owner globals plus `extern` declarations. That is codebase-wide mechanical cleanup and lives outside this port-specific bring-up unless it blocks ESP32.

**Step F — remove the host-mode compile identity ✅**. ESP32 builds with `MMBASIC_ESP32` and explicit HAL/port feature macros only. `MMBASIC_HOST` is no longer defined by `esp32_platform.h` or the ESP32 CMake component, and the HAL purity gate checks for regression.

**Step G — neutralize remaining shim paths ✅**. The VM syscall simulator bodies moved to `ports/vm_sys_sim/`. ESP32 now links shared device `vm_sys_file.c` and ESP32-owned `vm_sys_pin_esp32.c`, not simulator syscall bodies. The legacy Pico SDK `hardware/*` header shims moved to neutral `ports/pico_sdk_compat/`; ESP32 no longer includes `ports/host_native`.

**Step H — verify**. ESP32 IDF build green with strict duplicate-function link rules; host tests green; HAL purity clean; hardware smoke: CLS clears, COLOUR changes colour, SETPIN+PIN drives a GPIO, ARAW returns ADC data, FRUN mand.bas/sieve.bas runs.

**What this is NOT**:
- Not a copy-paste of `host_runtime.c` into `esp32_runtime.c`.
- Not a refactor of host_native's runtime behavior. Relocating generic compatibility shims out of `ports/host_native/` is allowed because those shims are not host behavior.
- Not a port to the ESP32 of host's lifecycle / test-harness concerns. ESP32's lifecycle is `app_main`. Host's `host_runtime_begin/finish/configure` exist on ESP32 only as no-op stubs because some core path calls them; nothing more.

### D5. Resolve `cmd_cls`/`cmd_colour` collision ✅

**First attempt (failed in hardware)**: weak default in `Draw.c` + strong override in `esp32_terminal.c`. Looked correct on paper. On hardware, both `port_terminal_handle_cls` and `port_terminal_emit_colour` resolved to `Draw.c.obj` (the weak default). Diagnosis: `--allow-multiple-definition` (still in the ESP32 link line for grandfathered tentative-def merging) defeats the weak attribute — first-defined wins, weak/strong is ignored. Lesson: weak attributes are unsafe in a build using `--allow-multiple-definition`. **See rule 3 above.**

**Second attempt (landed)**: strong-only pattern. `Draw.c` declares both hooks `extern` only, no body. Each port supplies exactly one strong definition in a port-only TU:
- `ports/esp32_s3_metro/main/esp32_terminal.c` — emits ANSI clear / 24-bit colour escapes.
- `ports/host_native/host_terminal_hooks_noop.c` — no-op (framebuffer ports run Draw.c's framebuffer path). Linked by host_native, mmbasic_stdio, mmbasic_ansi, host_wasm.
- `ports/pico_sdk_common/terminal_hooks_noop.c` — no-op (pico has a real LCD). Linked by every pico variant via PICOMITE_SOURCES.

`cmd_cls` / `cmd_colour` deleted from `esp32_terminal.c`; `cmd_locate` / `cmd_inverse` turned out to be dead code (no BASIC `LOCATE` / `INVERSE` keyword is registered in `AllCommands.h`) and were removed. Map verified post-build: `port_terminal_handle_cls` and `port_terminal_emit_colour` resolve to `esp32_terminal.c.obj` on ESP32 and to the noop file on each host build. Host tests and ESP32 build are green.

### D8. Override port_config values that are wrong on ESP32 ✅

`ports/esp32_s3_metro/port_config.h` now overrides:
- `HAL_PORT_NBR_PINS` → 49 (was inherited 44; ESP32-S3 has 49 GPIOs 0–48). Bigger arrays: `ExtCurrentConfig[NBRPINS+1]`, `PinDef[NBRPINS+1]`, `p100interrupts[NBRPINS+1]`.
- `HAL_PORT_HEAP_TOP` / `HAL_PORT_HEAP_TOP_USB` → 0 (sentinel; was RP2040 mmap address).
- Heap comment reconciled with the current 48 KB WiFi-enabled value and the deferred PSRAM plan.

Documented but not yet overridden:
- `HAL_PORT_FLASH_TARGET_OFFSET` (1 MB on host; revisit when E1 lands and esp_partition_t replaces the offset arithmetic).
- `HAL_PORT_PWM_SLICE_COUNT` / `HAL_PORT_PIO_COUNT` (RP2040 numbers; inert until a real PWM/PIO impl exists).

### D9. Remove app_main's hard-coded Option-init dance 🔧

E2 removed the unconditional overwrite behavior: `app_main.c` now loads the NVS-backed option mirror, validates it, and only applies ESP32 serial defaults when saved options are missing or invalid. Remaining cleanup is to move the ESP32-specific default selection out of `app_main.c` into a neutral first-boot/default-options helper.

### D10. Split WS2812 into shared command + per-port HAL ✅

`cmd_WS2812` no longer lives as a Pico-only body in `External.c` or as an ESP32 unsupported stub. The BASIC parser, pin validation, and RGB/GRB byte packing now live in `cmd_ws2812_shared.c`; the wire-level timing lives behind `hal_ws2812_write()`.

Port backends:
- `ports/esp32_s3_metro/main/hal_ws2812_esp32.c` uses ESP-IDF RMT TX.
- `ports/pico_sdk_common/hal_ws2812_pico.c` preserves the legacy Pico SysTick/GPIO timing path.

Hardware smoke on ESP32 passed with the Metro onboard NeoPixel on `GP46`: red, green, blue, off, and white all accepted. Pico's WS2812 shared command and Pico HAL backend build as part of a full `COMPILE=PICO -DPICOCALC=true` firmware image.

## Stage E — Real flash persistence

Real device-shape persistence. Replaces the current RAM mirrors with `esp_partition_*`-backed storage.

### E1. SAVE-to-slot via esp_partition ✅

Implemented and hardware-smoked. `partitions.csv` allocates a dedicated 1 MB `mmslots` data partition after `lfsdata`. `esp32_flash_storage.c` maps the front of that partition with `esp_partition_mmap()` and points:

- `SavedVarsFlash` at the saved-vars area.
- `flash_target_contents` at the numbered-slot area.

The legacy MMBasic offsets are translated at the port boundary:

- saved-vars / slot offsets → partition-relative offsets for `esp_partition_erase_range()` and `esp_partition_write()`
- program-region offsets, addressed either as raw offset zero or legacy `PROGSTART`, → `flash_prog_buf`
- `flash_target_contents` → const-pointer view via `esp_partition_mmap(...)`

Match pico's slot model: N slots × MAX_PROG_SIZE each; `cmd_save N` writes to slot N.

**Exit gate passed**: `FLASH SAVE 1`, reset, `FLASH LOAD 1`, `RUN` reloaded and ran `hello.bas`. The first smoke found an ESP32 adapter bug where the slot persisted and listed correctly, but `FLASH LOAD` did not repopulate runnable program memory because `PROGSTART` writes were ignored. Fixed by normalizing both offset-zero and `PROGSTART` program-region writes to `flash_prog_buf`.

### E2. Options blob in NVS ✅

Implemented behind `hal_flash_esp32.c`:

- `nvs_flash_init()` is lazy-initialized; on `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND`, the NVS partition is erased and re-initialized.
- `app_main.c` loads the NVS blob into `esp32_flash_option_buf` before `LoadOptions()`, validates the loaded struct, and only applies first-boot serial defaults when the saved blob is missing or invalid. It also emits the saved default terminal colours on boot so serial users see the restored setting immediately.
- `SaveOptions()` writes the full `struct option_s` blob via `nvs_set_blob()` + `nvs_commit()` and refreshes the RAM mirror used by later `LoadOptions()` calls.
- `hal_flash_read_jedec_id()` now reports the ESP-IDF flash size, so `ResetOptions()` records 16 MB on the N16R8 board instead of the old stub's zero response.

**Exit gate passed**: `OPTION DEFAULT COLOURS GREEN` persisted across reset/reflash and emitted the saved green ANSI prompt sequence on boot. `OPTION DEFAULT COLOURS WHITE` restored white-on-black and was verified across reset.

### E3. PSRAM policy

8 MB Octal PSRAM is on-chip and enabled in `sdkconfig.defaults` through ESP-IDF, not through the Pico/RP2350 PSRAM path.

Current contract:

- ESP-IDF owns PSRAM init/cache/flash coordination. ESP32 port code may allocate explicit external RAM with `heap_caps_malloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
- `CONFIG_SPIRAM_USE_CAPS_ALLOC=y`; `malloc()` is not configured to spill into PSRAM. This keeps existing library and VM allocation behavior stable.
- BASIC `PSRAMsize` remains 0 on ESP32. Consequently `MM.INFO(PSRAM SIZE)` reports 0 and generic `Memory.c` does not route large arrays/strings or `AllMemory` through the RP2350 `GetPSMemory()` / `psmap` fallback.
- ESP32 `MM.INFO(HEAP)` reports internal 8-bit heap, not SPIRAM. External RAM capacity is reported only through the ESP32-specific PSRAM keys below.
- Port-local reporting lives behind ESP32-specific `MM.INFO(ESP32 PSRAM SIZE)`, `MM.INFO(ESP32 PSRAM FREE)`, and `MM.INFO(ESP32 PSRAM LARGEST)`, which read ESP-IDF/heap_caps state.
- The opt-in PSRAM smoke uses `MM.INFO(ESP32 PSRAM MARCH n)` via `porttools/esp32_fs_vm_smoke.py psram`. It allocates SPIRAM through heap_caps, runs a destructive march over that allocation, frees it, and then verifies the BASIC prompt still responds.

Do not route ESP32 PSRAM through `PSRAMbase`, `psmap`, QMI/XIP cache helpers, the RP2350 `RAM` slot model, or generic `GetPSMemory()` until a deliberate ESP32 allocator contract is designed and tested.

Capture this decision so the next session doesn't re-litigate it.

## Stage F — Plan + gate hygiene

### F1. Wire ESP32 into HAL purity gate ✅

`tools/check_hal_purity.sh` now has ESP32 port awareness. `ports/esp32_s3_metro/main/*.c` are checked with the same strict target/port-config/runtime-fold rules as the promoted core files. `MMBASIC_ESP32` is allowed as the port's own identity tag; other target macros are forbidden.

The gate also flags accidental `host_native` source reuse or include-path regression in `ports/esp32_s3_metro/main/CMakeLists.txt`.

### F2. Update `docs/real-hal-plan.md` scoreboard ✅

The phase table now points at this plan with the current D/E/F state. `esp32-s3-port.md` and the session log are listed in the topic reference.

### F3. Rewrite `ports/esp32_s3_metro/README.md` ✅

The README now documents the N16R8 hardware, ESP-IDF 5.3 setup, build/flash/monitor flow, BOOT+RESET recovery for macOS USB-CDC binding hangs, `probe.py` as the preferred smoke driver, current working behavior, and known incomplete areas.

### F4. Optional: `buildesp32.sh` ✅

Sibling of `buildall.sh`. Runs the HAL purity gate, sources `~/esp/esp-idf/export.sh` if `idf.py` is not already available, and runs `idf.py build` for `ports/esp32_s3_metro/`. It is opt-in and not wired into CI because `idf.py` requires a heavyweight environment that does not belong in the default gate.

## Stage G — Device smoke suite + HAL cleanup

**Goal:** before adding PSRAM or broader hardware features, make ESP32 as easy to regression-test on real hardware as Pico now is. The suite should be prompt-driven, repeatable, and suitable for both manual bringup and CI-on-a-bench later.

### G0. Comprehensive ESP32 on-device smoke suite ✅

Implemented as `porttools/esp32_fs_vm_smoke.py`, modeled on `pico_fs_vm_smoke.py` but ESP32-aware.

Current coverage:

- **Device/prompt:** sync, banner, `MM.INFO$(ID)`, `MM.INFO(CPUSPEED)`, `MM.INFO(HEAP)`, `MM.INFO(STACK)`, `MM.INFO(FREE SPACE)`.
- **A: filesystem battery:** create/delete files and dirs, `CHDIR`, `MKDIR`, `RMDIR`, `KILL`, `RENAME`, `COPY`, wildcard `DIR$`, filenames with spaces, overwrite/error behavior, file size/existence checks.
- **B: / SD path where present:** detect configured/unconfigured state cleanly; do not require a card for the default suite.
- **BASIC file I/O:** `OPEN`, `PRINT #`, `INPUT$`, `LINE INPUT`, `LOC`, `LOF`, `EOF`, `SEEK`, append and overwrite behavior.
- **Program lifecycle:** `LOAD`, `SAVE`, `RUN`, `FRUN`, autorun load, empty-program save refusal, error recovery back to prompt.
- **Bytecode VM:** arithmetic, strings, arrays, `SUB`/`FUNCTION`, `SELECT CASE`, `DATA`/`READ`/`RESTORE`, VM-side file I/O, and a sieve/math benchmark with known result.
- **Flash persistence:** `FLASH SAVE`, reset/reconnect, `FLASH LOAD`, `RUN`, `VAR SAVE` if supported through the same `mmslots` backing.
- **GPIO:** safe DOUT/DIN/ARAW checks on documented pins. PWM/servo should explicitly verify the current "not supported" error until LEDC lands.
- **WS2812:** exercise the onboard NeoPixel on `GP46` with a short red/green/blue/off sequence, gated behind an opt-in flag if visual confirmation is required.
- **Network smoke hooks:** `network --run-network` chains to `porttools/network_conformance.py`; the default suite remains runnable without WiFi credentials.
- **Reset workflow:** document and automate the preferred reset/reconnect path, avoiding the macOS DTR/HUPCL trap already called out in `probe.py`.

Default `all` avoids destructive or environment-dependent checks unless explicitly requested. Named suites include `fs`, `program`, `vm`, `flash`, `gpio`, `ws2812`, and `network`.

Exit gate:

- `./buildesp32.sh build` passes.
- The smoke runner passes on a Metro ESP32-S3 from a fresh prompt.
- The suite catches intentionally unsupported PWM/servo paths cleanly, so unsupported features remain visible rather than silently no-oping.
- Opt-in `flash --var-save` and `network --run-network` passed in the latest G0 PM gate.

### G1. Plan/comment drift cleanup ✅

Fix stale text before deeper refactors:

- Replace old larger-heap references with the current 48 KB WiFi-enabled MMBasic heap, and explain that ESP32 compiler scratch tables use ESP-IDF internal heap while VM runtime allocations remain on the 48 KB MMBasic heap.
- Update comments in `hal_flash_esp32.c` / `main/CMakeLists.txt` that still describe slot persistence as "Stage E1" even though `mmslots` is implemented.
- Keep README, this plan, and `port_config.h` aligned on PSRAM being present in hardware but disabled by policy.

### G2. Explicit ESP32 port config ✅

Stopped inheriting `ports/host_native/port_config.h`.

Required shape:

- `ports/esp32_s3_metro/port_config.h` defines every `HAL_PORT_*` value it relies on directly, or includes a neutral shared defaults header that is not host-owned.
- Set inert/nonexistent hardware counts deliberately. Do not inherit RP2040 PWM/PIO numbers.
- Keep the 48 KB heap until PSRAM work deliberately changes the memory contract.

### G3. Neutralize remaining host-native header shims ✅

The build no longer includes `ports/host_native` for legacy `hardware/*` shim headers.

Done by moving the generic `hardware/*` compatibility shim tree to `ports/pico_sdk_compat/` and wiring ESP32 to include that neutral path. Host-native also includes the neutral path for the same compile-time shims.

Exit gate: `ports/esp32_s3_metro/main/CMakeLists.txt` has no `ports/host_native` include path and the HAL purity gate still passes.

### G4. Remove ESP32's `MMBASIC_HOST` compile identity ✅

ESP32 now defines `MMBASIC_ESP32` only. The old `MMBASIC_HOST` bringup tag was removed from the force-included platform header and from the ESP-IDF component compile definitions.

Exit gate:

- ESP32 builds with `MMBASIC_ESP32` and explicit HAL/port feature macros only.
- No core/shared file needed an ESP32-specific target gate.
- No host-only behavior is selected accidentally by the ESP32 build.

### G5. Optional hardware cleanup before PSRAM ⏳

Only after G0-G4 are green:

- LEDC-backed PWM/servo, if needed for near-term BASIC compatibility.
- MQTT TLS/cert handling.
- UART `COM:` support.

These are lower priority than making the port identity clean and testable.

## Stage H — ESP32 PSRAM contract

**Superseded by [`esp32-psram-realign-plan.md`](esp32-psram-realign-plan.md).**

The original Stage H (H1–H3) walled ESP32 PSRAM off behind port-specific
`MM.INFO(ESP32 PSRAM …)` keys with `PSRAMsize = 0` permanently. That
policy is gone. ESP32-S3 PSRAM is now owned by MMBasic through the same
shared path as RP2350: a fixed slab is reserved at boot via
`heap_caps_aligned_alloc(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, its base
and size are published to `PSRAMbase` / `PSRAMsize`, and BASIC sees the
identical surface — `MM.INFO(PSRAM SIZE)`, `RAM TEST` / `RAM SAVE` /
`RAM LIST` / `RAM LOAD` / `RAM RUN`, `Memory.c` routing for large
arrays, the lot. The cache + nocache-alias divergence lives behind
`hal/hal_psram.h`; `RAM TEST NOCACHE` errors on ESP32 because
`hal_psram_nocache_alias()` returns `NULL`.

See the realign plan for the per-phase trail, the slab-size knob
(`HAL_PORT_PSRAM_SLAB_BYTES` in `port_config.h`), and the cross-target
smoke harness `porttools/psram_smoke.py`.

## Out of scope (deferred — don't expand without an explicit goal)

- **Display.** SPI LCD / VGA via LCD_CAM. Multi-session. See "Display follow-on (sketch)" at the bottom of this doc.
- **Audio.** I2S codec, MP3 decode, PWM synth port.
- **Keyboard.** USB host (TinyUSB), I2C keypad (PicoCalc-style), PS/2.
- **BLE/Bluetooth.** WiFi and the BASIC WEB/TCP/UDP/NTP/plain-MQTT surface have landed; BLE remains out of scope.
- **PSRAM-backed BASIC heap/display.** ESP-IDF PSRAM is enabled for explicit port allocations only. Moving `AllMemory`, arrays/strings, bytecode arenas, or framebuffers to PSRAM is deferred until an ESP32 allocator/display contract exists.
- **OTA.** Two app slots, signed updates. Out of scope for a stdio litmus.

## Risks worth pre-flagging

1. **macOS USB-CDC binding flakiness**. After many fast reset cycles, macOS gets confused about the USB CDC binding. Symptom: `idf.py flash` hangs on "Connecting...". Recovery: hold BOOT, press RESET, release BOOT (enters ROM USB Direct mode, stable CDC ACM). `probe.py` already documents this.

2. ~~**`HAL_PORT_NBR_PINS` overflow risk**~~. D8 landed: NBRPINS = 49 covers all ESP32-S3 GPIOs.

3. **Heap fragmentation on FRUN with big arrays**. `bc_compiler_alloc` reorder (committed) helps; t170_frun_post_compact_array.bas is the regression net. If a real program OOMs at array alloc, document the program and failure first. The mitigation order is now Stage G smoke coverage, then explicit ESP32 allocation policy in Stage H, not ad hoc heap growth.

4. **`flash_target_contents` host-shape leak**. Currently a `const uint8_t *` global pointing at a RAM mirror. Real device shape is XIP-mapped flash (pico) or `esp_partition_mmap` (esp32). E1 fixes ESP32; the type signature on host is wrong-but-harmless until anyone tries to use it as a real flash view there.

5. **Newlib reentrancy + FreeRTOS**. ESP-IDF newlib has per-task `_reent`. Tasks created via `xTaskCreate` get reentrancy automatically; raw FreeRTOS APIs don't. If a future change spawns its own MMBasic worker task, use `xTaskCreate`.

6. **Brownout detector**. PSRAM init draws inrush current; some Metro boards trip BOD on cold boot. Defer until PSRAM lands; lower `CONFIG_ESP32S3_BROWNOUT_DET_LVL` if needed.

## Files to know

- `ports/esp32_s3_metro/main/CMakeLists.txt` — source-of-truth for what's linked. It should have no `HOST_NATIVE_REUSED`, no `--wrap`, and no `--allow-multiple-definition`.
- `ports/esp32_s3_metro/main/esp32_flash_storage.c` — flash backing storage, now mirrors host_native's offset-routing post-D1.
- `ports/esp32_s3_metro/main/esp32_system.c` — `CPU RESTART` / sleep commands.
- `ports/esp32_s3_metro/main/esp32_cmd_files_hooks.c` — owns `port_drive_check` directly.
- `ports/esp32_s3_metro/main/demos/mand.bas` — current short bytecode benchmark; `RUN` and `FRUN` must keep matching checksum `552868`.
- `ports/host_native/host_bc_runtime_noop.c` — host's BC_FREE no-op implementation.
- `ports/esp32_s3_metro/port_config.h` — D8 edits land here.
- `ports/esp32_s3_metro/probe.py` — debug driver; use it instead of picocom.
- `ports/vm_sys_sim/` — simulator VM syscall bodies used by host-style builds only.
- `ports/host_native/pico/` — host-owned Pico compatibility headers.
- `ports/pico_sdk_compat/hardware/` — neutral legacy Pico SDK `hardware/*` compatibility shims.
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

**The fastest path from here**: keep the on-device ESP32 smoke suite green while enabling PSRAM through an ESP32-specific contract. Do not add more build-rule overrides to paper over missing ESP32 HAL/stub ownership.
