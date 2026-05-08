# ESP32-S3 (Adafruit Metro) Stdio Port Plan

**Goal:** prove the HAL is genuinely portable to a non-ARM, non-Pico target by bringing up an MMBasic stdio REPL on the Adafruit Metro ESP32-S3 over USB Serial/JTAG. Success = the directory-composition standard from the real-hal refactor extends across architectures, not just board variants of one chip family.

**Hardware/toolchain:**
- Adafruit Metro ESP32-S3 — 8 MB flash, 2 MB Quad PSRAM, native USB. Verify the exact module on your board before Phase A: Adafruit also ships an N8 (no PSRAM) variant under similar SKUs. The plan assumes the WROOM-1U N8R2 (2 MB PSRAM).
- ESP-IDF ≥ 5.0, Xtensa GCC (`xtensa-esp-elf-gcc`).
- Console: `ESP_CONSOLE_USB_SERIAL_JTAG` (built-in controller, no TinyUSB dependency).
- Filesystem: FATFS over a flash partition mounted at `/sd` (matches existing `ff.c` + `FF_MAX_LFN_LARGE`).

**Scope:** stdio REPL only. No display, no audio, no keyboard matrix, no WiFi. The litmus test is "does an MMBasic prompt work end-to-end on Xtensa with FATFS persistence?" — not a feature-complete PicoMite. Display/audio/WiFi land in follow-on phases once the core is proven.

## Layout

```
ports/esp32_s3_metro/
├── CMakeLists.txt              # ESP-IDF project root
├── port_config.h               # HAL_PORT_* values
├── partitions.csv              # 2 MB app, 64 KB nvs, 5 MB fatfs (no OTA slots)
├── sdkconfig.defaults          # console, PSRAM, FreeRTOS settings
├── README.md                   # build/flash/monitor instructions
└── main/
    ├── CMakeLists.txt          # main component
    ├── app_main.c              # ESP32 entry → MMBasic_RunPromptLoop
    ├── esp32_console.c         # USB Serial/JTAG ↔ MMputchar / MMgetchar
    ├── hal_time_esp32.c        # esp_timer_get_time
    ├── hal_flash_esp32.c       # esp_partition_*
    ├── hal_storage_esp32.c     # NVS-backed Options blob
    ├── hal_filesystem_esp32.c  # FATFS via VFS at /sd
    ├── hal_keyboard_esp32.c    # fgetc(stdin) shim
    ├── hal_pin_esp32_stub.c    # stdio scope: no GPIO
    ├── hal_audio_esp32_stub.c  # stdio scope: no audio
    └── hal_vm_framebuffer_esp32_stub.c
```

The MMBasic core sources stay where they are. The ESP-IDF main component's own `CMakeLists.txt` enumerates them via `idf_component_register(SRCS "../../../MMBasic.c" "../../../bc_source.c" ... INCLUDE_DIRS "../../../" "../../../core/state")`. Verbose but correct — `EXTRA_COMPONENT_DIRS` doesn't apply (it scans for *components*, each needing its own `CMakeLists.txt`; the repo root isn't a component). Source list is generated from `mmbasic_stdio/Makefile`'s `CORE_SRCS + BC_SRCS` so the two ports stay in sync.

## Phase A — toolchain bring-up

Empty ESP-IDF project. LED blink + "hello" line over USB Serial/JTAG. No MMBasic linkage yet.

`sdkconfig.defaults` settings:
- Console: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.
- PSRAM: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MODE_QUAD=y`, `CONFIG_SPIRAM_SPEED_80M=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=8192` (allocations ≤ 8 KB stay in internal SRAM).
- Stack: `CONFIG_ESP_TASK_STACK_SIZE_MAIN=24576` (24 KB; the recursive descent parser + `setjmp`/`longjmp` error path on a deep IF nest has bitten Pico builds at smaller sizes; over-provisioning is cheap on the 320 KB internal pool).
- Watchdog: `CONFIG_ESP_TASK_WDT_INIT=n`. Without this, long-running BASIC `FOR` loops fire the task watchdog (~5 s default) and reboot. Hard requirement, not optional.
- Radios off: `CONFIG_ESP_WIFI_ENABLED=n`, `CONFIG_BT_ENABLED=n`. Saves ~1 MB binary and frees the WiFi RAM region.
- `CONFIG_FREERTOS_UNICORE=n` (use both cores, even if MMBasic stays on one).

**Exit gate:** `idf.py build flash monitor` shows banner + 1 Hz heartbeat. Confirms board, toolchain, USB Serial/JTAG.

## Phase B — link MMBasic core

Create the ESP-IDF main component. Pull in `mmbasic_stdio`'s source list (`MMBasic.c`, `Commands.c`, `Functions.c`, `Operators.c`, `MATHS.c`, `Memory.c`, `MMBasic_Print.c`, `gfx_*_shared.c`, `mm_misc_shared.c`, state files, `Draw.c`, `RGB121.c`, `Tilemap.c`, `FileIO.c`, `Audio.c`, `BmpDecoder.c`, `re.c`, `picojpeg.c`, the stub set, `display_pixel_host.c`, plus the BC_SRCS).

Component CMakeLists adds `target_compile_definitions(${COMPONENT_LIB} PUBLIC MMBASIC_HOST MMBASIC_ESP32 FF_MAX_LFN_LARGE)`. The `MMBASIC_ESP32` macro is set on the build line — never gated against in shared code (HAL purity), readable only inside `ports/esp32_s3_metro/`. `PUBLIC` so the macro reaches every TU compiled into the component, including the upward-relative `../../../*.c` core sources.

Stub every `hal_*` symbol the linker demands. The first link pass surfaces 50+ undefined references — not a handful — and each needs a stub with the *correct* prototype, not just an empty body. `hal_pin_*` alone has 30+ functions; `hal_audio_*` similar. Plan on iterative link-error-driven development.

Phase B exit gate also runs `grep -rn '__builtin_arm' MMBasic.c bc_*.c Draw.c FileIO.c` and confirms zero hits. ARM intrinsics in shared code would block the Xtensa build; the HAL refactor should have eliminated them, but verify.

`port_config.h` minimal first cut:
- `HAL_PORT_HEAP_MEMORY_SIZE` = 1500 * 1024 (1.5 MB in PSRAM)
- `HAL_PORT_FILES_MAX` = 64
- `HAL_PORT_HAS_*` = 0 (no peripherals)
- `MMBASIC_BANNER_NAME` = `"MMBasic ESP32-S3 Metro"`
- `HAL_PORT_DEVICE_NAME` = `"ESP32-S3 Metro"`
- `HAL_PORT_RAM_FUNC(name)` = `name`

**Exit gate:** `idf.py build` succeeds; `.bin` flashes; heartbeat still runs. Proves the HAL surface compiles cleanly on Xtensa with no target ifdef leaks.

## Phase C — stdio + time + heap

Real impls for the smallest functional set:
- `hal_time_esp32.c` — `esp_timer_get_time()` for microseconds, `vTaskDelay` for sleeps.
- `hal_pin_esp32_stub.c`, `hal_audio_esp32_stub.c`, `hal_vm_framebuffer_esp32_stub.c` — empty bodies satisfying the contract surface.
- `esp32_console.c` — non-blocking stdio over USB Serial/JTAG requires explicit setup; newlib's VFS does *not* default to non-blocking on the JTAG console:
  1. `usb_serial_jtag_driver_install(&config)` to install the IDF driver (without this, `read()` on stdin fails immediately).
  2. `esp_vfs_usb_serial_jtag_use_driver()` to switch the VFS layer over to the installed driver.
  3. `esp_vfs_dev_usb_serial_jtag_set_rx_line_endings(ESP_LINE_ENDINGS_CR)` and `..._set_tx_line_endings(ESP_LINE_ENDINGS_CRLF)` to keep ANSI escapes intact for Editor.c.
  4. `fcntl(fileno(stdin), F_SETFL, O_NONBLOCK)` so `MMgetchar`'s poll loop returns -1 instead of blocking.
  5. TX path: pass `0` timeout to `usb_serial_jtag_write_bytes` (or use `putchar` accepting that `fwrite` may return short on full FIFO) so output doesn't hang when the host monitor disconnects.
- BC heap allocator override — `bc_alloc.c` already wraps allocation; on ESP32, route to `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. The `MALLOC_CAP_8BIT` flag is non-optional: pure `MALLOC_CAP_SPIRAM` returns 32-bit-aligned-only memory on some configs, which the byte-addressed BC heap will misuse.
- Heap split (now in scope, not deferred): VM scratch / dispatch state in internal SRAM, slot storage and program text in PSRAM. Quad PSRAM on ESP32-S3 at 80 MHz hits ~30-40 MB/s vs. ~600 MB/s internal — 15-20× slower for cache-cold access, 2-3× cache-warm. Single-heap-in-PSRAM degrades the BC compiler's small-allocation churn enough that `mand` benchmark feels it; split it now while the allocator is being touched anyway.

In `app_main`, hard-code `MMBasic_Init()` then `tokenize_and_run("PRINT 1+1")`. Validate output before wiring the prompt loop.

**Exit gate:** serial monitor shows `2` after flash. Proves the HAL contracts hold on Xtensa, the BC compiler runs, the VM dispatches, and the heap allocator hands out PSRAM.

## Phase D — interactive REPL

Replace the hard-coded program with `MMBasic_RunPromptLoop`. `hal_keyboard_esp32.c` reads from stdin (blocking is fine on a dedicated MMBasic FreeRTOS task spawned via `xTaskCreate` with a 24 KB stack — `xTaskCreate` initializes per-task `_reent` automatically; raw FreeRTOS task creation does not).

`Editor.c` and `MMBasic_Prompt.c` emit ANSI escape sequences for line editing — same code path as `mmbasic_ansi`. Validate that USB Serial/JTAG passes them through (most terminals do; macOS `screen /dev/cu.usbmodem*` works).

**Exit gate:** typing `FOR i=1 TO 10 : PRINT i*i : NEXT i` and pressing Enter prints squares 1, 4, 9, ... 100. Multi-line edit, GOTO/GOSUB, IF/THEN/ELSE all work. Explicitly **excluded** from this gate: `LOAD`, `SAVE`, `RUN "filename"`, `FILES`, `CHDIR` — FATFS isn't mounted yet, and exercising file commands here will crash on an unmounted volume. Phase E owns the file-command verification.

## Phase E — persistence

- `hal_flash_esp32.c` — `esp_partition_find_first` for the `fatfs` partition; expose erase/write/read sized to `SAVEDVARS_FLASH_SIZE` blocks (4 KB). Sector size must match `CONFIG_WL_SECTOR_SIZE` (default 4 KB on ESP32-S3); mismatched size silently corrupts the wear-leveling layer's metadata.
- `hal_storage_esp32.c` — Options struct stored in NVS:
  - `nvs_flash_init()` first; on `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND`, erase the partition and re-init. Standard ESP-IDF idiom.
  - `nvs_set_blob` has a default per-blob cap of ~4 KB. Measure `sizeof(Options)` first; if it exceeds the cap, either split across multiple keys or bump `CONFIG_NVS_MAX_ENTRY_SIZE`.
  - Survives reflash unless the user passes `idf.py erase-flash`.
- `hal_filesystem_esp32.c` — `esp_vfs_fat_spiflash_mount_rw_wl(...)` with `format_if_mount_failed=true` so first boot formats the FATFS partition automatically. Mounted at `/sd`. FATFS configuration via existing `ffconf.h` + `FF_MAX_LFN_LARGE` (passed via the component's compile defs).
- `FileIO.c` is already FatFS-flavored (uses `f_open`, `f_read`, etc.); no modifications expected, just the right mount.

**Exit gate:**
1. `SAVE "test.bas"` → power cycle → `RUN "test.bas"` produces the saved program's output.
2. `OPTION SAVE` → power cycle → option survives.
3. `FILES` lists the saved program.

## Phase F — gate integration + buildall

- Extend `tools/check_hal_purity.sh`:
  - Rename the "host-port WASM-clean" tier to "host-port-clean" (covers ESP32 too).
  - Add `MMBASIC_ESP32` and `__XTENSA__` to the forbidden-macro list **for `ports/host_native/*.c` only**; future ESP32 logic must not bleed back into host_native.
  - The new `ports/esp32_s3_metro/main/*.c` directory is *also* added to the host-port-clean tier, but with `MMBASIC_ESP32` whitelisted (the macro is the port's own identity tag — fine to use inside its own port directory).
- `buildall.sh` doesn't fit (different toolchain, different build system). Add a sibling `buildesp32.sh` that:
  - Sources `~/esp/esp-idf/export.sh` if `idf.py` isn't on PATH.
  - Wraps `idf.py build` with sane defaults and reports OK/FAIL the same way.
  - Is **opt-in**, not part of any release-gate CI. The repo's existing CI assumes a fresh shell with arm-none-eabi tools; `idf.py` requires a heavyweight environment that doesn't belong in the default gate.
- `ports/esp32_s3_metro/README.md` documents the install steps (esp-idf checkout, env activation), the `idf.py build flash monitor` command line, and notes that `buildesp32.sh` is the local convenience wrapper.

**Exit gate (overall):**
- All existing validations stay green: `host/run_tests.sh` 240/240, `tools/check_hal_purity.sh`, `buildall.sh` 14/14.
- `idf.py build` clean.
- Flashed firmware boots into the REPL, FOR/IF/GOSUB/PRINT/SAVE/LOAD/RUN/CHDIR all work.
- Cold-boot persistence verified (SAVE → power cycle → RUN).
- New port directory passes the host-port-clean purity tier.

## Risks worth pre-flagging

1. **PSRAM latency.** Quad PSRAM on ESP32-S3 at 80 MHz hits ~30-40 MB/s vs. internal SRAM's ~600 MB/s — **15-20× slower cache-cold, 2-3× cache-warm**. Phase C addresses this directly with the heap split (not deferred). If `mand` still feels sluggish after the split, the next mitigation is moving FontTable[] / static program text into internal SRAM via `EXT_RAM_BSS_ATTR` overrides.

2. **`FF_MAX_LFN_LARGE` propagation.** Every host-style port passes this `-D`. The ESP-IDF main component's `CMakeLists.txt` must use `target_compile_definitions(${COMPONENT_LIB} PUBLIC FF_MAX_LFN_LARGE MMBASIC_HOST MMBASIC_ESP32)`. Without it, FATFS reverts to 63-byte LFN and FileIO truncates from deep paths — silently, no compile error.

3. **Newlib reentrancy.** ESP-IDF's newlib has per-task `_reent`. fopen/fread/fwrite work via VFS as long as the task was created via `xTaskCreate` (not raw FreeRTOS APIs). `host_fs.c`'s pthread assumptions (used by host_native for argv-relative path resolution) may not apply — worst case, write a thinner ESP32-specific filesystem shim instead of reusing host_fs.c.

4. **`__attribute__((optimize("-Os")))`.** Xtensa GCC honors it (unlike macOS clang, which warns and ignores). Real risk: the per-function `-Os` override changes inlining behavior on Xtensa more aggressively than on ARM, potentially affecting Draw.c hot loops. If perf surprises appear, check the disassembly for missed inlines.

5. **USB Serial/JTAG flow control.** The built-in controller has no hardware flow control. With the recommended `0` write-timeout, output is dropped (not blocked) when the host monitor disconnects. Interactive use is fine; benchmark output may look truncated — that's the dropped bytes, not a code bug.

6. **Brownout detector.** PSRAM init draws inrush; some Metro boards trip the BOD on cold boot at 3.0 V. If flashing fails intermittently, lower `CONFIG_ESP32S3_BROWNOUT_DET_LVL` or disable. Defer until it's seen.

7. **Stack size.** Phase A sets `CONFIG_ESP_TASK_STACK_SIZE_MAIN=24576`. If even 24 KB isn't enough for `bc_compiler_alloc` on a deeply nested program, run MMBasic on a dedicated FreeRTOS task with `xTaskCreate(..., 32768, ...)`. The Pico builds have hit recursion-depth limits at smaller sizes; over-provisioning by 8 KB is cheap insurance.

## Out of scope (deferred)

- **Display.** No SPI LCD wiring in the litmus test. Adding SCREEN/PIXEL/DRAW means new `drivers/spi_lcd/` glue + `hal_display_esp32.c` driving SPI master. Material follow-on phase.
- **WiFi/BLE.** ESP32-S3 has both natively, far easier than the CYW43 dance on RP2350. Trivially gated on `HAL_PORT_HAS_WIFI = 1` once the litmus is proven.
- **Audio.** I2S audio output exists in ESP-IDF; pairs naturally with `drivers/pwm_synth/` once a board-specific I2S codec is chosen.
- **Keyboard.** USB host (TinyUSB), I2C keypad (PicoCalc-style), or PS/2 — all defer to a hardware-bound follow-on.

## Effort estimate

- Phase A: 1 session.
- Phase B: 3 sessions. Pulling 80+ source files into one ESP-IDF component, writing 50+ stub functions with the *correct* signatures (not just empty bodies), and chasing the iterative undefined-reference link errors out of Xtensa. First-time-on-Xtensa build surprises eat real time.
- Phase C: 1 session.
- Phase D: 1 session.
- Phase E: 3 sessions. FATFS first-boot format, sector size verification, NVS init dance with re-init-on-version-bump, and verifying SAVE/LOAD survives a power cycle (not just a soft reset).
- Phase F: 1 session.

Total: ~8-11 sessions to a working stdio REPL on real hardware. The earlier 6-8 estimate underweighted Phase B and Phase E.

## Why this is worth doing

Two payoffs. First, it's the strongest possible test of the HAL refactor — if the same core sources compile and run on a Xtensa chip with FreeRTOS underneath, the "directory composition not preprocessor" claim is empirically true, not just a stylistic preference. Second, it opens the path to absorbing other architectures (the [Armmite STM32 ingest goal](../../memory/project_real_hal_armmite_goal.md) would follow the same pattern), and to feature-complete ESP32 boards once the litmus is in place.
