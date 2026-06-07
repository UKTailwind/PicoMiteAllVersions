# ESP32-S3 PSRAM Realignment Plan

**Status:** Draft, 2026-05-14. Branch: `web-console-driver` (current).

## Goal

ESP32-S3's PSRAM looks **identical to Pico's PSRAM from MMBasic's point
of view**. Same `PSRAMsize`, same `MM.INFO(PSRAM SIZE)`, same `RAM`
command vocabulary, same `Memory.c` routing (`GetMemory` /
`TryGetMemory` / `FreeMemory` / `MemSize` / `UsedHeap` / `FreeHeap`
ranges), same display-buffer behaviour. The under-the-hood acquisition
differs (ESP-IDF `heap_caps_aligned_alloc` vs RP2350 QSPI mapping), but
that lives behind a thin HAL — everything BASIC sees comes from the
shared code paths.

## What we're undoing

Stage H of the ESP32-S3 plan (`esp32-s3-port.md` §H1–H3) deliberately
walled ESP32 PSRAM off:

- `PSRAMsize = 0` permanently → `Memory.c` never routes large
  allocations there; `MM.INFO(PSRAM SIZE)` reports 0.
- Custom MM.INFO keys: `ESP32 PSRAM SIZE`, `ESP32 PSRAM FREE`,
  `ESP32 PSRAM LARGEST`, `ESP32 PSRAM MARCH`.
- Custom `port_memory_report_extra()` printout.
- No `RAM` command on ESP32 — slot model absent.
- Smoke march goes through `heap_caps_malloc` directly, not the shared
  test runner.

Decisions confirmed (2026-05-14):

1. **PSRAM ownership:** reserve a fixed slab via
   `heap_caps_aligned_alloc(N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` at
   boot. Leaves ~1 MB to ESP-IDF for WiFi RX / SmartConfig.
2. **`RAM TEST NOCACHE`:** Pico-only; ESP32 errors on the modifier.
3. **Code location:** lift portable command body to
   `shared/cmd_psram.c`; cache + nocache-alias helpers go behind a tiny
   `hal/hal_psram.h`.

## Final shape

```
PSRAMbase     = slab pointer returned by heap_caps_aligned_alloc
PSRAMsize     = slab size (≈ 7 MB on N16R8 Metro)
PSRAMblock    = PSRAMbase + PSRAMsize + 0x60000     -- shared #define
                (slot region lives in tail of slab; see §3 below)
PSRAMblocksize = slot region bytes                  -- HAL_PORT_PSRAM_BLOCK_SIZE
```

`psmap[]` bitmap is sized to cover the entire slab plus the slot
region. Driver becomes generic (loses the `_pico` suffix); same TU
links on RP2350 and ESP32.

## Phases

### Phase 1 — Generalize the PSRAM heap driver

- Rename `drivers/psram_heap/psram_heap_pico.c` →
  `drivers/psram_heap/psram_heap_real.c`. No symbol changes.
- Update the comment header (no more "rp2350"-specific narrative).
  Bitmap stays at 6 MB — the slot region lives above `PSRAMsize` in
  physical PSRAM and is addressed linearly without the bitmap. ESP32
  uses the same 6 MB heap region + slot region layout inside its slab.
- Verify rp2350 builds still pass (no functional change for them).

**Exit:** `./buildall.sh` green; `ports/host_native/run_tests.sh` 239/239; the
`psmap` extern in `Memory.c` / `Commands.c` resolves against the
renamed TU on RP2350.

### Phase 2 — Lift `cmd_psram` to shared code

- New file: `shared/cmd_psram.c` containing the body of
  `ports/pico_sdk_common/cmd_psram.c`.
- New header: `hal/hal_psram.h` declaring:
  - `void hal_psram_cache_sync(void);`  — clean + invalidate
  - `uint8_t *hal_psram_nocache_alias(uint8_t *base);` — returns
    `NULL` if NOCACHE is unsupported on this port.
  - `void hal_psram_save_settings(void);` / `_restore_settings(void);`
    (rename of the existing `mmbasic_save_psram_settings` etc.).
- RP2350 HAL impl moves into `ports/pico_sdk_common/hal_psram_pico.c`
  (collapse `psram_cache.c` into it). Calls existing
  `xip_cache_*` / QMI helpers.
- Stub HAL impl in `drivers/psram_heap/psram_heap_stub.c` (or new
  `hal_psram_stub.c`) for builds that link the shared cmd but have no
  PSRAM (none expected, but the stub keeps the link clean if a port
  declares `HAL_PORT_PSRAM_BASE != 0` without providing a HAL).
- `ports/pico_sdk_common/cmd_psram.c` becomes a 3-line shim that
  `#include`s `shared/cmd_psram.c` (or is deleted and the shared file
  is added to the pico_sdk_common CMakeLists).
- `cmd_psram`'s `RAM TEST NOCACHE` path: error("NOCACHE not supported
  on this port") when `hal_psram_nocache_alias()` returns `NULL`.
- HAL purity gate stays clean (no new target-macro `#if`s in shared
  TU).

**Exit:** `./buildall.sh` green; `RAM TEST` / `RAM LIST` etc. still
work identically on a Pico variant. Manual hardware smoke on one Pico
target is fine, but the unit tests already exercise the RAM command
path indirectly.

### Phase 3 — ESP32 PSRAM HAL impl + boot wiring

- New file: `ports/esp32_s3_metro/main/hal_psram_esp32.c`
  - `hal_psram_init(void)`: call `esp_psram_init()`, allocate slab via
    `heap_caps_aligned_alloc(PAGESIZE, slab_bytes, MALLOC_CAP_SPIRAM |
    MALLOC_CAP_8BIT)`. Stash the returned pointer/size in static
    globals so `app_main` can publish them to `PSRAMbase`/`PSRAMsize`.
  - `hal_psram_cache_sync(void)`: `esp_cache_msync(slab, slab_bytes,
    ESP_CACHE_MSYNC_FLAG_DIR_C2M | _FLAG_INVALIDATE)`.
  - `hal_psram_nocache_alias()`: return `NULL`.
  - `hal_psram_save_settings()` / `_restore_settings()`: no-ops
    (ESP-IDF manages cache around flash automatically).
- `ports/esp32_s3_metro/main/esp32_compat.c`:
  - Delete the `PSRAMsize = 0` definition (now set by HAL).
  - Delete `mmbasic_save_psram_settings` / `_restore_settings` stubs
    (now in `hal_psram_esp32.c`).
- `ports/esp32_s3_metro/port_config.h`:
  - Define `HAL_PORT_PSRAM_BASE` to a sentinel that resolves at
    runtime; since `configuration.h` uses `PSRAMbase` as an integer
    expression, the cleanest path is to leave `HAL_PORT_PSRAM_BASE`
    *undefined* in port_config (so the default `PSRAMbase = 0` applies
    at preprocess-time) and use a **non-zero `PSRAMsize` runtime
    flag** plus a separate `uint8_t *PSRAMrun_base` global. Then patch
    `Memory.c` / `cmd_psram.c` to use `PSRAMrun_base` instead of
    `(uint8_t*)PSRAMbase` when `PSRAMsize > 0`.

  **Alternative (cleaner) — uniformly switch `PSRAMbase` to a
  runtime extern.** Make `PSRAMbase` a `uintptr_t PSRAMbase`
  variable instead of a port_config integer constant. On Pico it
  gets initialised to `0x11000000` in `pico_boot.c`; on ESP32 it
  gets the slab pointer from `hal_psram_init()`. This is the
  approach the plan adopts — fewer code points to touch later.
- `ports/esp32_s3_metro/main/app_main.c`: call `hal_psram_init()`
  before `mmbasic_runtime_init_common`. Remove the call to
  `esp32_psram_print_boot_report()` (the slab status will print via
  the shared boot-banner path that already reports PSRAMsize).

**Exit:** boot prints PSRAM size identically to a Pico variant;
`MM.INFO(PSRAM SIZE)` returns the slab size in bytes;
`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` drops by exactly the
slab size after init.

### Phase 4 — Wire the `RAM` command into ESP32

- `ports/esp32_s3_metro/main/CMakeLists.txt`: add `shared/cmd_psram.c`
  to the source list. Add `ports/esp32_s3_metro/main/hal_psram_esp32.c`.
- `ports/esp32_s3_metro/port_tokens.h`: define
  `HAL_PORT_RAM_CMD_TOKEN` so the shared dispatch wires `RAM` →
  `cmd_psram`.
- ESP32 must link the **generic** `psram_heap_real.c` (not the stub)
  now that `PSRAMsize > 0`. Add it to the CMakeLists.
- `flash_target_contents` and the flash-slot path on ESP32 already go
  through `esp32_flash_storage*`. Verify `hal_flash_erase` /
  `hal_flash_program` invocations from `RAM LOAD` actually program the
  `mmslots` partition. If they currently route to `flash_prog_buf` for
  the program region, the slot region needs a separate path —
  document if so.

**Exit:** `RAM LIST` on a fresh ESP32 boot shows all slots empty;
`RAM SAVE 1` followed by `RAM LIST 1` works; `RAM TEST 1` runs the
march on the first MB of the slab; `RAM TEST NOCACHE` errors with
"NOCACHE not supported on this port".

### Phase 5 — Decommission the ESP32-specific PSRAM surface

- `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c`: delete the
  `ESP32 PSRAM SIZE` / `FREE` / `LARGEST` / `MARCH` MM.INFO branches.
- Delete `esp32_psram.c` / `esp32_psram.h`. Anything still useful (the
  pattern-march body) is already in `shared/cmd_psram.c`.
- Delete `port_memory_report_extra` from this port — the shared
  `MM.INFO(PSRAM)` / `MEMORY` paths now report it uniformly.
- `porttools/esp32_fs_vm_smoke.py psram`: update the smoke to drive
  `RAM TEST <MB>` instead of `MM.INFO(ESP32 PSRAM MARCH …)`.

**Exit:** `grep -ri "ESP32 PSRAM" ports/esp32_s3_metro/` returns
nothing beyond comments narrating history.

### Phase 6 — Comprehensive PSRAM smoke harness

New harness `porttools/psram_smoke.py` that exercises the full BASIC
PSRAM surface against a connected device. Single script, port-agnostic
— same invocation works on Pico and ESP32. Designed to run from CI on
a bench too, so output is structured (PASS/FAIL per check, machine-
parseable summary at the end).

Surface to cover:

1. **Boot-time invariants**
   - `MM.INFO(PSRAM SIZE)` ≥ expected (configurable per board).
   - `OPTION LIST` includes `OPTION PSRAM` (Pico) or equivalent.
   - `MM.INFO(HEAP)` reports the SRAM heap, distinct from PSRAM.

2. **March test (heap region)**
   - `RAM TEST 1` → expect `RAM TEST OK`.
   - `RAM TEST` (no size) → expect `RAM TEST OK` over the full
     configured heap region.
   - On RP2350 only: `RAM TEST NOCACHE 1` → expect OK.
   - On ESP32: `RAM TEST NOCACHE 1` → expect error message
     "NOCACHE not supported on this port".
   - `RAM TEST ALL` → expect OK over heap + slot region.

3. **Slot lifecycle**
   - `RAM ERASE ALL` then `RAM LIST` → all `MAXRAMSLOTS` slots
     report "available".
   - Upload a tiny BASIC program via `XMODEM` or `AUTOSAVE` content
     fed over the REPL.
   - `RAM SAVE 1` → no error.
   - `RAM LIST` → slot 1 reports "in use" with the program's first
     line quoted.
   - `RAM LIST 1` → full listing of the saved program (compare against
     expected source).
   - `RAM SAVE 1` (second time, same slot) → expect "Already
     programmed" error.
   - `RAM OVERWRITE 1` → no error.
   - `RAM ERASE 1` then `RAM LIST` → slot 1 back to "available".

4. **Slot → flash → run cycle**
   - `RAM SAVE 1`, `NEW`, `RAM LOAD 1`, `LIST` → original program
     present.
   - `RAM SAVE 1`, `NEW`, `RAM RUN 1` → program executes; capture
     stdout, compare to expected.
   - `RAM SAVE 1` from program A, run program B with `RAM CHAIN 1` →
     program A runs to completion (chain semantics).
   - `RAM RUN 0` → falls back to flash program slot (sentinel).

5. **`RAM FILE LOAD`**
   - RP2350: drop a small `.bas` onto the device filesystem (A:/SD).
   - RP2350: `RAM FILE LOAD 2, "test.bas"` → no error.
   - RP2350: `RAM LIST 2` matches the source file contents.
   - ESP32: current expected behavior is an explicit
     "RAM FILE LOAD not supported on this port" error. `RAM SAVE`,
     `RAM LOAD`, `RAM RUN`, `RAM CHAIN`, and `RAM RUN 0` are still
     exercised through the shared RAM slot surface.

6. **`Memory.c` routing observable from BASIC**
   - RP2350: allocate an array larger than `heap_memory_size/2` (DIM
     `big%(N)`). With PSRAM enabled, this must succeed even when the
     SRAM heap couldn't hold it.
   - RP2350 measurable: before allocation, `MM.INFO(HEAP)` is X; after,
     `MM.INFO(HEAP)` ≈ X (the array landed in PSRAM, not SRAM).
   - RP2350: repeat with a string array to exercise the string-heap path.
   - ESP32: current expected behavior is that normal `DIM` allocations
     use the internal MMBasic heap. ESP32 PSRAM is owned by the `RAM`
     command surface and by explicit port allocations, not by generic
     BASIC arrays/strings.

7. **Negative cases**
   - `RAM SAVE 99` → expect "Invalid slot" / range error.
   - `RAM TEST 999` → expect range error.
   - When `PSRAM_CS_PIN = 0` on Pico (or ESP32 slab fails to allocate):
     all `RAM` subcommands error with "PSRAM not enabled".

The harness reuses `porttools/basic_serial.py`'s line-edited
prompt-driven interface (no XMODEM dependency for the simple-program
test; just paste a few lines through the REPL). Per-step timeouts
respect the longest expected operation (`RAM TEST ALL` can take 30+ s
on 8 MB modules).

**Exit:** `psram_smoke.py --port <pico>` and `psram_smoke.py --port
<esp32>` both report PASS for all checks. Recorded in
`docs/real-hal/esp32-s3-port-log.md` with the device IDs used.

### Phase 7 — Display buffers + arrays validation

- Exercise a BASIC program that allocates >24 KB array → confirm via
  `MM.INFO(MEMORY)` and `MM.INFO(PSRAM)` that it lands in the slab.
- Wire a display buffer (when web_console driver lands a real
  framebuffer) and confirm it allocates against PSRAM exactly like
  the dvi_wifi_rp2350 variant does.
- Long-running stress: leave a 1-hour `mand.bas` loop running with
  PSRAM allocations to catch any cache-coherency surprise.

**Exit:** Hardware validation green; no behaviour difference between
ESP32 and Pico from a BASIC program's perspective.

### Phase 8 — Documentation cleanup

- Update `docs/real-hal/esp32-s3-port.md` Stage H sections to match
  reality (PSRAM now owned by MMBasic via the shared path; remove
  H1/H2/H3 ✅ markers from the old policy and rewrite as "superseded
  by esp32-psram-realign-plan.md").
- Add a brief note to `docs/adding-a-new-port.md` describing the
  PSRAM HAL contract so the next port (Armmite, anything else) knows
  exactly which 4 functions to implement.
- Memory notes in `MEMORY.md` get a project entry pointing at this
  doc.

## Assumptions

- **PSRAM ≥ 8 MB on all supported boards.** Pico Plus 2, PGA2350, WeAct
  Studio RP2350B, and the N16R8 Metro all ship 8 MB. The current
  `pico_boot.c` slot arithmetic (`PSRAMblock = PSRAMbase + PSRAMsize +
  0x60000`) puts the slot region past physical PSRAM on a 2 MB module,
  but no shipping board hits that case. Revisit if a smaller-PSRAM
  variant lands.

## Risks / open questions

- **Slab size:** 7 MB is a guess. ESP-IDF WiFi RX uses ~256 KB of
  PSRAM by default when `CONFIG_ESP_WIFI_RX_BA_WIN` is set high.
  Start at 6 MB and bump up after measuring; the slab size is one
  `#define` (`HAL_PORT_PSRAM_SLAB_BYTES` in `port_config.h`).
- **Alignment:** `heap_caps_aligned_alloc` returns a pointer aligned to
  the value we pass. `PAGESIZE` (256 B on Pico) is sufficient for the
  bitmap allocator. PSRAM cache line on ESP32-S3 is 32 B.
- **`hal_flash_erase` for the slot region:** ESP32 doesn't have XIP
  flash mapping like Pico, so `RAM LOAD` (which copies a slot back
  into the program region) actually writes to the `mmslots`
  partition. Verify the path early — could be a Phase 4 blocker.
- **Watchdog during long marches:** RAM TEST iterates over all
  PSRAMsize bytes. On ESP32 with FreeRTOS, this can starve the
  watchdog. Add `esp_task_wdt_reset()` calls inside the inner loop
  (gated through a HAL hook so Pico doesn't link FreeRTOS symbols).
- **Cache coherency for DMA-capable framebuffers:** if a future ESP32
  display panel uses DMA from PSRAM, MMBasic-managed PSRAM allocations
  must come back with `MALLOC_CAP_DMA` too. Defer until display work
  needs it; the slab itself is `_CAP_8BIT` only today.

## Test plan

After each phase:
1. `./ports/host_native/run_tests.sh` (host tests still 239/239).
2. `./buildall.sh` (all 12+ device variants build).
3. `./buildesp32.sh build` (ESP32 image links).
4. On hardware: `RAM LIST`, `RAM TEST 1`, `RAM SAVE 1`, run a
   PRINT-and-allocate program, check `MM.INFO(PSRAM SIZE)`.

CI gate: `tools/check_hal_purity.sh` must stay clean throughout —
no new `#ifdef ESP32_*` or `#ifdef rp2350` in shared TUs.
