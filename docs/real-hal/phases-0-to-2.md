# Real HAL — Phases 0 / 0.5 / 1 / 2 (completed)

These four phases are ✅ and their core-file state is clean (no new ifdefs introduced; pre-existing ifdefs in the targeted files were never in scope). Summaries only; detail lives in the commits.

## Phase 0 — Architecture lock + tooling ✅ partial

Core scaffolding + purity gate + scoreboard landed. `check_ram_baseline.sh`, `perf_microbench/`, and the Tier-B display-inline prototype/measurement still pending — all need physical device access.

- Land this plan doc set.
- Create `hal/`, `drivers/`, `ports/` directories.
- `hal/CONTRACT.md` (per-function guarantees).
- `tools/check_hal_purity.sh` — fails if any target macro appears in `core/` or `hal/*.h` (preprocessor-expanded check).
- `tools/check_ram_baseline.sh` — parses linker maps for each device target, compares against `tools/ram_baseline_<target>.txt`. Initial baselines captured from current main.
- `tools/perf_microbench/` — per-opcode VM benchmark, pixel-fill benchmark, IRQ jitter probe.
- Tier-B inline mechanism prototyped on a throwaway display HAL stub; measurement committed; mechanism locked.
- Scoreboard rebaselined 476 → 606 after fixing regex that missed `PICOMITEPLUS`, `PICOCALC`, wider `PICOMITEWEB`.

**Commits:** `f89e9a9` (scaffolding).

**Deferred (needs physical device):** `check_ram_baseline.sh` execution, `perf_microbench/` on-device runs, Tier-B display-inline prototype.

## Phase 0.5 — Hoist cross-cutting state ✅

Global definitions (not declarations) moved into `core/state/`:
- `HRes`, `VRes`, `FontTable[]`, `layer_in_use[]`, `spritebuff[]`, `struct3d[]`, `frameBufferMutex` → `core/state/display_state.c`
- `Option` struct → `core/state/option_state.c`
- Audio voice slots and sample buffers → `core/state/audio_state.c`

Original files keep `extern` declarations and continue to compile; only the storage moved. Plan correction: `PinDef[]` is board-level const in `PicoMite.c`/`host_runtime.c`, not mutable core state — stays where it is.

**Exit gate (met):** all 12 device targets + host + WASM build clean. `./run_tests.sh` 192/192. No behaviour change. Linker maps show globals in their new TUs.

**Commits:** `9a53573`, `f7a06f4`, `896eaa9`, `f1207a6`.

## Phase 1 — `hal_flash.h` ✅

Chosen as the true lowest-risk dry run because `hal_time` crosses the cooperative-yield hook that web-host Phase 2.5/4 depends on; `hal_flash` has no IRQ/jitter semantics — it's the persistent option block + unique device ID, called rarely.

- `hal/hal_flash.h`: `hal_flash_read_options`, `hal_flash_write_options`, `hal_flash_unique_id`, `hal_flash_erase_program_area`; `hal_flash_read_jedec_id` added along the way.
- `ports/pico_sdk_common/hal_flash_pico.c` (calls `hardware_flash`).
- `host/hal_flash_host.c`.
- All 52 `flash_range_*` + `flash_do_cmd` call sites routed through the HAL.
- `host/hardware/regs/addressmap.h` stub.

Note: `Include.h` still pulls `hardware/flash.h` unconditionally — the header is available everywhere even though no core file calls the SDK directly. Clean that up in Phase 3b / the fixup plan's F2.

**Exit gate (met):** core no longer includes `hardware/flash.h`. Tools green. `./run_tests.sh` 192/192. All 12 device targets build.

**Commits:** `33163ad`, `ed610a2`.

## Phase 2 — `hal_time.h` ✅

- `hal/hal_time.h`: `hal_time_us_64`, `hal_time_sleep_us`, `hal_time_ms_tick`, RTC accessors.
- `ports/pico_sdk_common/hal_time_pico.c`.
- 42 core call sites migrated (`MM_Misc.c`, `Audio.c`, `Commands.c`, `Draw.c`, `External.c`, `FileIO.c`, `MATHS.c`, `bc_vm.c`, `mm_misc_shared.c`). All scored files call `hal_time_us_64()` — zero direct `time_us_64()`.
- Host port: thin `host_time.c` shim already existed; added a 1-line forward. No host file renamed.
- Peripheral files (`PicoMite.c`, `I2C.c`, `USBKeyboard.c`, `MMMqtt`, `MMntp`, `MMtcpserver.c`, `XModem.c`) still call SDK time directly — migrate with their HALs.

**Perf gate (met):** `pico_blocks_tilemap` SWAP rate held within 1% of the Phase 1 baseline on web host AND at least one physical device.

**Exit gate (met):** core no longer calls `time_us_64()` directly. FASTGFX 50 Hz invariant verified.

**Commits:** `029170b`, `f2d840f`.
