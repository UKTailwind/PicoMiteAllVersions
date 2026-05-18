# HAL decascade plan: kill `PICOMITEWEB` and `HDMI` as build flags

## Goal

Eliminate `PICOMITEWEB` and `HDMI` as compile-time defines that gate large
exclusive trees of code. Replace with palette-style `HAL_PORT_HAS_*` flags
in per-port `port_config.h` files, so a custom build can mix any subset of
hardware features (HDMI + WiFi + I²S + PSRAM + ...) without inventing a
new top-level macro and a new `COMPILE=` enum value.

After this plan lands:
- A new port is a directory under `ports/<port_name>/` with its own
  `port_config.h` (palette of feature flags), `port_sources.cmake` (the
  source files for those features), `port_defaults.c`, `pin_tables.c`.
- The phrase "PICOMITEWEB build" stops meaning anything — WiFi is a
  feature toggle, not a product line.
- Same for "HDMI build".

(Terminology: "port" everywhere — matches the existing `ports/` directory
name and `docs/adding-a-new-port.md`. "Board" is a synonym in informal
prose but is not used as a structural term.)

## Current state

Survey of the source tree at start of this plan, **counting only
preprocessor directives** (not comment mentions), via:
```bash
grep -rE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*\bPICOMITEWEB\b' \
    --include='*.c' --include='*.h'
grep -rE '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*\bHDMI\b' \
    --include='*.c' --include='*.h'
```

- `PICOMITEWEB`: **46** preprocessor sites across ~16 files. (An earlier
  rough count of 65 included comment-only mentions and is wrong.)
- `HDMI`: **68** preprocessor sites across ~13 files. (Earlier 87
  similarly conflated comments.)
- `HAL_PORT_HAS_HDMI` already exists in every `port_config.h` (used as
  a runtime/compile-time *value* — `if (HAL_PORT_HAS_HDMI) ...`). Raw
  `#ifdef HDMI` is still scattered alongside it; the conversion is
  half-done.
- No `HAL_PORT_HAS_WIFI` yet.
- Both macros also drive **CMake source-list inclusion** (not just
  `#ifdef`s). `shared/net/MMtcpserver.c`, `drivers/hdmi/*`, etc. are exclusive to
  certain `COMPILE=` targets in `CMakeLists.txt`.
- `configuration.h` has a mutually-exclusive `#ifdef PICOMITEVGA` /
  `#ifdef PICOMITE` / `#ifdef PICOMITEWEB` ladder defining
  `HEAP_MEMORY_SIZE`, `FLASH_TARGET_OFFSET`, `MAX_CPU`, `MagicKey`,
  `HEAPTOP`, `MAXVARS`, `MAXSUBFUN`, `MAXMODES`, `CONSOLE_RX_BUF_SIZE`,
  `PIOMAX`, `NBRPINS`, `PSRAMbase`, `PSRAMblock`, `PSRAMblocksize`.
  These all need to move per-port.
- `CMakeLists.txt` has **49** separate `if (COMPILE STREQUAL …)` branch
  tests, not just the source-list ones. They drive
  `target_compile_definitions`, `target_compile_options`, library
  linkage (`pico_multicore`, `tinyusb_*`, `cyw43_*`),
  `pico_set_float_implementation`, `pico_set_boot_stage2`, and HAL port
  metadata (`HAL_PORT_DEVICE_NAME`). Stage E has to handle all of them,
  not just source-list selection.
- `.github/workflows/firmware.yml` selects build targets via `sed`
  substitution against `set(COMPILE PICO)`. The matrix currently
  builds 2 targets out of 12; the rest are unverified in CI.

### Critical prerequisite (Stage A0)

A pre-existing problem the rest of the plan depends on: only ~25% of the
files that contain `#ifdef PICOMITEWEB` / `#ifdef HDMI` actually
`#include "port_config.h"`, directly or transitively. The umbrella
headers (`Hardware_Includes.h`, `MMBasic_Includes.h`) do not currently
include it.

This means a naive Stage B that converts `#ifdef PICOMITEWEB` →
`#if HAL_PORT_HAS_WIFI` will **silently** evaluate the new `#if` to 0
on most files (an undefined macro is 0 in `#if` context). Code compiles,
WEB-required blocks vanish, no error. This must be fixed *before* Stage
B begins — see Stage A0 below.

## Stages

Each stage is independently committable and leaves main green. The
sequence matters; partial completion (e.g. stop after Stage B) is safe.

### Stage A0 — Prerequisite: make `port_config.h` reachable everywhere

**Without this, Stage B silently breaks every WiFi-gated and
HDMI-gated path in the codebase.** Stage B must not begin until A0
passes.

A0.1. Add `#include "port_config.h"` to one of the umbrella headers —
      preferably `Hardware_Includes.h`, since core code already includes
      it widely. Verify no circular include dependency results.

A0.2. Audit all files that contain `#ifdef PICOMITEWEB` or `#ifdef
      HDMI` directives (16 + 13 = 29 unique files). For each, verify
      that `port_config.h` is reachable via the include graph after
      A0.1. The list to verify includes (non-exhaustive — the survey
      grep produces the authoritative list):
      - `Custom.c`, `Custom.h`, `Editor.c`, `PicoMite.c`,
        `MMBasic_REPL.c`, `MM_Misc.c`, `drivers/gui_touch/Touch.c`, `XModem.c`,
        `shared/net/MMtcpserver.c`, `Hardware_Includes.h`, `Include.h`,
        `PicoCFunctions.h`, `FileIO.h`, `AllCommands.h`,
        `configuration.h`, `drivers/sd_spi/mmc_stm32.c`,
        `drivers/hdmi/hdmi_modes.c`, `drivers/hdmi/hdmi_scanout.c`,
        `drivers/vga_pio/vga_ops.c`, `vga_mode_ops.c`, `vga_memory.c`,
        `vga_qvga_modes.c`, `ports/pico_sdk_common/cmd_psram.c`,
        `ports/pico_sdk_common/misc_option_setters.c`,
        `ports/pico_sdk_common/print_display_options.c`,
        `ports/pico/port_defaults.c`.

A0.3. Add a **compile-error guard** at the top of `port_config.h`:
      ```c
      #ifndef HAL_PORT_CONFIG_INCLUDED
      #define HAL_PORT_CONFIG_INCLUDED
      ```
      …and then in any TU that uses `HAL_PORT_HAS_*`, add a guard
      check (e.g. via a single shared header `hal_port_assert.h` that
      `#error`s if `HAL_PORT_CONFIG_INCLUDED` isn't defined). This
      makes "macro is silently zero because the header is missing" a
      **build error**, not a runtime crash.

A0.4. Pre-conversion typo audit: `grep -rn 'HMDI\|PICOMITWEB\|PICOMTIE'`
      etc. Fix any pre-existing typos that mechanical conversion would
      otherwise propagate. Known site: `PicoMite.c:2202` has
      `#if defined(PICOMITEVGA) && !defined(HMDI)` (should be `HDMI`).

A0.5. Build matrix: all existing `COMPILE=` targets compile unchanged
      after A0. (See Stage E item E0 about expanding CI to actually
      verify this.)

### Stage A — Establish/normalize palette flags

Vocabulary first, no behavioural change.

A1. Add `HAL_PORT_HAS_WIFI` to every `ports/*/port_config.h`:
    - `web`, `web_rp2350` → `1`.
    - All other ports → `0`.
    - Doc comment: drives CYW43 init, lwIP/mongoose stack, WEB
      commands. Stub paths (existing `shared/net/MMweb_stubs.c` pattern) cover
      `HAS_WIFI=0`.

A2. Add the additional palette flags the conversion will need. The
    plan **commits** to introducing these (not "tentatively floats"):
    - `HAL_PORT_HAS_VGA_PIO` — required so the VGA-family drivers
      (`drivers/vga_pio/*`) can be source-list-gated cleanly in Stage
      C. HDMI is a sibling of VGA-PIO inside the VGA family; without
      this flag, Stage C cannot tell "VGA-PIO files in, HDMI files
      out" from "both in" or "both out."
    - `HAL_PORT_HAS_GUICONTROLS` — replaces raw `#ifdef GUICONTROLS`.
      Currently linked into `PICORP2350`, `PICOUSBRP2350`, and
      `WEBRP2350` builds via cmake. Note: this is a real ~25-site
      conversion (External.c, MMBasic.c, MM_Misc.c, bc_vm.c, Editor.c,
      FileIO.h, AllCommands.h, drivers/gui_touch/Touch.c, Memory.c, Draw.c, PicoMite.c,
      drivers/gui_touch/gui_touch.c), not a trivial rename. Stage B
      handles GUICONTROLS sites alongside HDMI.
    - `HAL_PORT_HAS_TCP_SERVER` is **deferred**. The earlier draft
      proposed splitting it from `HAL_PORT_HAS_WIFI`, but
      `shared/net/MMtcpserver.c` is the only TCP server consumer and it travels
      with WiFi. Splitting requires a corresponding stub-file split
      (the 14 stubs in `shared/net/MMweb_stubs.c` need to be partitioned into
      WiFi-only vs TCP-only sets), which doesn't simplify anything.
      One flag suffices.

A3. No source-code conversions in this stage. Pure palette extension.
    Build matrix: every existing `COMPILE=` target compiles unchanged.

### Stage B — Mechanical conversion of call sites

Bulk of the diff. Every `#ifdef PICOMITEWEB` becomes `#if HAL_PORT_HAS_WIFI`;
every `#ifdef HDMI` becomes `#if HAL_PORT_HAS_HDMI`; every `#ifdef
GUICONTROLS` becomes `#if HAL_PORT_HAS_GUICONTROLS`. Per-file. Build
smoke after each file. Acceptance gate: A0.3's compile-error guard
must be active throughout.

**Compound conditionals require manual translation, not 1:1 swap.**
Examples in the source tree:
- `#if !defined(PICOMITEVGA) || defined(HDMI)` (Custom.c:314) →
  `#if !HAL_PORT_IS_VGA || HAL_PORT_HAS_HDMI`
- `#if (defined(PICOMITEVGA) || defined(PICOMITEWEB)) && !defined(rp2350)`
  (PicoMite.c:637) → `#if (HAL_PORT_IS_VGA || HAL_PORT_HAS_WIFI) && !defined(rp2350)`
- `#elif defined(HDMI)` chains need destructuring into
  `#elif HAL_PORT_HAS_HDMI` (still works, but verify the `#elif`
  semantics survive).

Run a fresh grep for compound forms before each file's conversion;
mechanical 1:1 replacement is insufficient.

Order (do WiFi first — smaller surface, easier rollback). Authoritative
file lists are produced by the survey grep at the top of this doc.
The lists below are illustrative, not canonical:

B1. **WiFi conversion**:
    - Core C files: `PicoMite.c`, `MMBasic_REPL.c`, `MM_Misc.c`,
      `Editor.c`, `Custom.c`, `drivers/gui_touch/Touch.c`, `XModem.c`.
    - Driver/port files: `cmd_psram.c`, `misc_option_setters.c`,
      `ports/pico/port_defaults.c`, `mmc_stm32.c`.
    - Headers: `Custom.h`, `PicoCFunctions.h`, `Hardware_Includes.h`,
      `FileIO.h`, `AllCommands.h`.
    - `shared/net/MMtcpserver.c` — file-level CMake gate, no in-source
      `#ifdef PICOMITEWEB`. Do **not** wrap the body in `#if
      HAL_PORT_HAS_WIFI`; let CMake source-list inclusion handle it
      (see Stage C). The earlier plan draft was wrong on this point.
    - `gfx_3d.c` — comment-only mentions of `PICOMITEWEB`. No
      preprocessor directives. Skip.
    - `configuration.h` WEB blocks — convert any `#ifdef PICOMITEWEB`
      shells. The internal heap/flash cascade is Stage D.

B2. **HDMI + VGA-PIO conversion**:
    - Core: `PicoMite.c`, `Editor.c`, `Custom.c`, `Hardware_Includes.h`,
      `Include.h`.
    - VGA-family drivers: `drivers/vga_pio/vga_ops.c`,
      `vga_mode_ops.c`, `vga_memory.c`, `vga_qvga_modes.c`. These have
      `#ifdef HDMI` blocks **interleaved** with shared `PICOMITEVGA`
      code (e.g. `vga_mode_ops.c:68-82, 110-149`). Convert HDMI
      gates to `#if HAL_PORT_HAS_HDMI`; **leave `PICOMITEVGA` gates
      alone for now** — they become `HAL_PORT_HAS_VGA_PIO` only after
      both are converted, since the files are jointly compiled into
      VGA and HDMI builds.
    - `drivers/hdmi/*` — file-level CMake gate, body unchanged.
    - Port files: `misc_option_setters.c`, `print_display_options.c`.
    - `configuration.h` HDMI block — same caveat as B1.

B3. **GUICONTROLS conversion**: ~25 sites across 12 files (see A2).
    Sequenced after WiFi/HDMI because the WEB build is the only one
    that combines GUICONTROLS with WiFi today; doing GUI last avoids
    having to debug both axes simultaneously.

B4. Sanity gate at end of Stage B: every existing `COMPILE=` target
    builds, all host tests pass (current count: 239), on-device smoke
    works for at least one rp2040 and one rp2350 target. The macros
    `PICOMITEWEB`, `HDMI`, `GUICONTROLS` are still defined by cmake,
    but **no source code consults them** — they're dead at this point.
    Verify with the strengthened acceptance grep (see Acceptance
    section).

### Stage C — CMake source-list decoupling

Today, files like `shared/net/MMtcpserver.c` and `drivers/hdmi/*` are pulled in
by `if (COMPILE STREQUAL "WEBRP2350") ... target_sources(...)` blocks.
Make source-list inclusion driven by per-port composition.

C1. New convention: `ports/<port>/port_sources.cmake`. Each port's
    snippet declares its feature backends:
    ```cmake
    target_sources(PicoMite PRIVATE
        drivers/hdmi/hdmi_scanout.c
        drivers/hdmi/hdmi_modes.c
        drivers/cyw43_glue/...
        shared/net/MMtcpserver.c
    )
    ```
    Top-level `CMakeLists.txt` `include()`s the snippet for the
    selected port — no central enum scrutiny.

C2. Source files that wholesale gate themselves with `#if
    HAL_PORT_HAS_*` (after Stage B) are safe to leave out of
    non-matching ports' source lists; they're also safe to include
    (just become empty translation units). Prefer leaving them out
    to keep build output smaller. Note the VGA-PIO drivers are NOT
    yet in this category at start of Stage C — they're still
    `PICOMITEVGA`-gated at the file level. Stage C must introduce
    `HAL_PORT_HAS_VGA_PIO`-driven inclusion for them.

C3. **`core1stack` arbitration** (was previously misclassified as
    "stub-pair" — it isn't). `core1stack[]` is currently uniquely
    defined in:
    - `drivers/hdmi/hdmi_scanout.c` (size 128)
    - `drivers/vga_pio/vga_qvga_modes.c` (size 128)
    - `drivers/display_merge/display_merge_pico.c` (size 512)
    - `shared/net/MMtcpserver.c` (size 1, canary backstop)
    - `ports/host_native/host_runtime.c` (size 256)

    A combined HDMI+WiFi port pulls in *two* definitions → multiple
    definition link error. Resolve before Stage F by introducing a
    single owner: a new file `ports/pico_sdk_common/core1_runtime.c`
    that defines `core1stack[]` at a size set by a port-config flag
    (`HAL_PORT_CORE1_STACK_WORDS`, defaulting to whatever the largest
    consumer requires for that port). Existing definers convert to
    `extern` declarations. The canary backstop role of MMtcpserver's
    1-word array is taken over by the shared definition (canary at
    `core1stack[0]` regardless of size).

C4. Stub-pair pattern: existing `shared/net/MMweb_stubs.c` proves the model for
    *callable functions*. For every WiFi-gated function, build pulls
    either the real impl or the stub — driven by the port's
    `port_sources.cmake`. Apply the same pattern to HDMI symbols. Do
    NOT apply this pattern to shared state (`core1stack`,
    `Option.MaxCtrls`, etc.); those need real arbitration per C3.

C5. Verification: every existing `COMPILE=` target still produces
    bit-identical or near-identical firmware to before the refactor.
    Bit-identical is unrealistic (source ordering changes); the
    acceptance criterion is **functionally identical** — same boot
    banner, same pin map, same heap size, all on-device behaviours
    preserved. See acceptance criterion 7 below.

### Stage D — `configuration.h` decascading

The `#ifdef PICOMITEVGA` / `#ifdef PICOMITE` / `#ifdef PICOMITEWEB`
ladder in `configuration.h` is mutually exclusive *as written* — it
defines a much larger set of values than the earlier plan draft listed.
Authoritative list (verified against the current `configuration.h`):

D1. Move every value from the cascade into per-port `port_config.h`,
    prefixed `HAL_PORT_*`:
    - **Heap and flash:** `HAL_PORT_HEAP_MEMORY_SIZE`,
      `HAL_PORT_FLASH_TARGET_OFFSET`, `HAL_PORT_HEAP_TOP`,
      `HAL_PORT_MAGIC_KEY`.
    - **CPU clocks:** `HAL_PORT_MAX_CPU`, `HAL_PORT_MIN_CPU`.
    - **MMBasic table caps:** `HAL_PORT_MAX_VARS`,
      `HAL_PORT_MAX_SUBFUN`, `HAL_PORT_MAX_MODES`.
    - **Console + I/O:** `HAL_PORT_CONSOLE_RX_BUF_SIZE` (currently
      `CONSOLE_RX_BUF_SIZE`, gated to PICOMITEWEB at line ~281).
    - **Pin/PIO counts:** `HAL_PORT_PIOMAX`, `HAL_PORT_NBR_PINS`
      (currently `PIOMAX` / `NBRPINS`, in the same WEB block).
    - **PSRAM layout:** `HAL_PORT_PSRAM_BASE`, `HAL_PORT_PSRAM_BLOCK`,
      `HAL_PORT_PSRAM_BLOCK_SIZE` (currently `PSRAMbase`, `PSRAMblock`,
      `PSRAMblocksize`, lines ~306-324).

D2. `configuration.h` shrinks dramatically — keep only the alias
    layer that maps existing core-code names to the new port-config
    names:
    ```c
    #define HEAP_MEMORY_SIZE    HAL_PORT_HEAP_MEMORY_SIZE
    #define FLASH_TARGET_OFFSET HAL_PORT_FLASH_TARGET_OFFSET
    #define CONSOLE_RX_BUF_SIZE HAL_PORT_CONSOLE_RX_BUF_SIZE
    /* ... */
    ```
    Existing source code keeps compiling; values now come from one
    file (the port's), not from a cascade.

D3. Mode-size tables (`MODE1SIZE_S`, `MODE_H_S_ACTIVE_PIXELS`,
    `MODE2SIZE_4`, etc.) follow the same move — per port, or a
    separate `ports/<port>/port_modes.h` if the table is large. These
    tables cross-reference each other (mode size derives from
    `MODE_H_*_ACTIVE_PIXELS` and `MODE_V_*_ACTIVE_LINES`), so the
    move is per-table-block, not per-line. Verify after move that
    every port that uses any mode-table value has the full set
    defined.

D4. Verification post-Stage-D: function-pointer tables
    (`SCREENMODE`-indexed dispatch) and pin-map sizes (`PinDef[]`,
    `PINMAP[]`) consume D1's values. After the move, audit those
    tables compile and have the right element counts on every port.

### Stage E — Drop the build flags + CI matrix migration

E0. **Pre-Stage-E: expand CI matrix.** `.github/workflows/firmware.yml`
    currently builds 2 of the 12 port targets. Stage E's "all 12 build"
    acceptance criterion is unfalsifiable in CI today. Expand the
    matrix to all 12 ports before E1, OR explicitly accept that
    Stage A through E acceptance is local-only and document the
    manual build matrix the developer must run. Recommend the former.

E1. Remove `-DPICOMITEWEB` from `CMakeLists.txt`. Remove `-DHDMI`.
    Remove `-DGUICONTROLS`. They're already unused in source code by
    this point.

E2. Replace the `COMPILE` enum with a simpler `set(PORT pico_rp2350)`
    style selector that just names the port directory and includes
    `ports/<PORT>/port_sources.cmake`. **Scope warning:** the current
    `CMakeLists.txt` has 49 `if (COMPILE STREQUAL …)` branches across
    multiple concerns:
    - Source-list inclusion (largely handled in Stage C).
    - `target_compile_definitions` setting per-port macros
      (`PICOMITEVGA`, `USBKEYBOARD`, `HAL_PORT_DEVICE_NAME`, etc.) →
      these become per-port `target_compile_definitions` calls in the
      `port_sources.cmake`.
    - `target_compile_options` (e.g. `-O0` vs `-O2` per port) → also
      per-port.
    - Library linkage (`pico_multicore`, `tinyusb_*`, `cyw43_*`,
      `pico_lwip_*`) → per-port `target_link_libraries` calls.
    - Pico SDK feature toggles (`pico_set_float_implementation`,
      `pico_set_boot_stage2`) → per-port.

    Decompose E2 into sub-steps in implementation:
    E2a. Move `target_compile_definitions` per port.
    E2b. Move `target_compile_options` per port.
    E2c. Move library linkage per port.
    E2d. Move SDK feature toggles per port.
    E2e. Delete the `COMPILE STREQUAL` ladder; replace with `PORT`
         selector.

E3. Update `.github/workflows/firmware.yml`:
    - Replace the `sed` substitution against `set(COMPILE PICO)` with
      one against `set(PORT <name>)`.
    - Update the matrix entries to use port directory names.
    - Update the release/UF2 naming if it currently embeds the
      `COMPILE` value (check `firmware.yml` lines 47-56).

E4. Build matrix verification: all 12 existing port targets compile
    cleanly. Host tests pass. On-device smoke for representative
    targets (rp2040 + rp2350, with and without LCD).

E5. Update `docs/adding-a-new-port.md` to describe the new
    "compose a `port_config.h` + `port_sources.cmake`" workflow.
    Remove the "add a new `COMPILE` enum value" instructions.

### Stage F — Prove the model with multiple novel feature combinations

A single new port doesn't validate "any subset of features." Validate
shapes the existing 12 ports don't cover, in priority order:

F1. **Primary target — `ports/pico_rp2350b_dvi_wifi/`**: HDMI + WiFi +
    PSRAM + I²S + USB host. The actual hardware target.
    - `port_config.h` — `HAS_HDMI=1`, `HAS_WIFI=1`, `HAS_PSRAM=1`,
      `HAS_HEARTBEAT=0` (CYW43 claims LED), heap size, flash offset,
      `CORE1_STACK_WORDS=128`.
    - `port_sources.cmake` — HDMI driver, CYW43/lwIP/mongoose,
      I²S audio, USB host keyboard.
    - `port_defaults.c` — boot-time defaults (HDMI pin mapping,
      I²S pin mapping, WiFi pin map matching the RM2 module).
    - `pin_tables.c` — RP2350B 80-pin GPIO map.

F2. **Validation port — `ports/vga_wifi_rp2350/`**: VGA-PIO + WiFi.
    Exercises `HAL_PORT_HAS_VGA_PIO=1, HAS_HDMI=0, HAS_WIFI=1`.
    Catches whether the new VGA-PIO flag works without HDMI, and
    whether `core1stack` arbitration handles the
    "VGA-PIO uses core1 for scanout AND CYW43 wants core0" combo.
    May or may not be flashed to real hardware — compile + boot smoke
    suffices.

F3. **Validation port — `ports/spi_lcd_wifi_rp2350/`**: SPI LCD +
    WiFi. Exercises `HAS_HDMI=0, HAS_VGA_PIO=0, HAS_WIFI=1` with a
    real display backend that the existing `web` ports lack. Catches
    whether `drivers/gui_touch/Touch.c`, `drivers/ssd1963/SSD1963.c`, and the SPI-LCD scanout family
    link cleanly when the port mixes WiFi with a non-WEB display
    family.

F4. Validate each F-port. F1 must boot on real hardware; F2 and F3
    must compile + smoke-boot in QEMU or on a representative dev
    board.

F5. SD-less is not a special case — it's an existing supported
    config. Drive A: (flash LittleFS) is always available; drive B:
    (SD/FatFS) is only present when the port configures SD pins.
    A board without an SD card just leaves SD pins unset in
    `port_defaults.c`; all file commands route to drive A:
    automatically.

## Risks / things this plan handwaves

- **Dense `#ifdef` ladders in `Custom.c`, `Hardware_Includes.h`,
  `PicoMite.c`**: mechanical translation will produce ugly nested
  `#if` chains. Some files deserve a real refactor before conversion
  (extract per-feature helper headers). Plan for "pause to clean up
  a file" mid-Stage-B if it gets gnarly.

- **Functionally-identical builds, not bit-identical**: source-list
  reorganization (Stage C) and configuration-header restructure
  (Stage D) can change symbol order, link order, or default flag
  values in subtle ways. Acceptance criterion is "boots and runs the
  same," not byte-equal.

- **Persistence isn't touched by this plan.** Flash LFS (drive A:)
  and FatFS-on-SD (drive B:) are already feature-flagged at the
  port-config level; SD-less and flash-only variants are existing
  supported configs. This plan only addresses the build-time
  configuration geometry, not the storage stack.

- **`PICOMITEVGA` decomposition is partial.** This plan introduces
  `HAL_PORT_HAS_VGA_PIO` and `HAL_PORT_HAS_HDMI` as siblings, but
  leaves `PICOMITEVGA` itself defined (via cmake or the alias layer)
  for the VGA family of ports. Fully decomposing `PICOMITEVGA` into
  `HAL_PORT_HAS_VGA_FAMILY` (the umbrella for VGA-PIO + HDMI shared
  scanout/mode code) is a follow-on, not in scope here.

## Acceptance criteria

The plan is complete when all of these hold simultaneously:

1. **No build-flag preprocessor sites remain.** This grep returns
   empty across the source tree (excluding `docs/` and stale build
   artifacts):
   ```bash
   grep -rE \
     '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*\b(PICOMITEWEB|HDMI|GUICONTROLS)\b' \
     --include='*.c' --include='*.h' \
     --exclude-dir=build --exclude-dir=docs
   ```
   This pattern catches `#ifdef`, `#ifndef`, `#if defined(...)`,
   `#elif defined(...)`, and compound forms — closing the gap in the
   earlier draft's narrower `ifdef X|ifdef Y` pattern.

2. **No build-flag mentions in CMake.** `CMakeLists.txt` no longer
   references `PICOMITEWEB`, `HDMI`, or `GUICONTROLS` in
   `target_compile_definitions`, `target_compile_options`, library
   linkage blocks, or `if (COMPILE STREQUAL ...)` branches. Verify:
   ```bash
   grep -E 'PICOMITEWEB|HDMI[^_]|GUICONTROLS|COMPILE STREQUAL' CMakeLists.txt
   ```
   returns empty.

3. **Every port builds.** All 12 existing port targets + the 3 new
   F-stage ports (= 15 total) build cleanly via the new `set(PORT
   <name>)` selector.

4. **Every port boots.** F1 (combined HDMI+WiFi) boots on real
   hardware; the other 14 boot in a smoke test (REPL prompt, banner,
   `PRINT 2+3` works).

5. **Host tests pass.** Current count is 239; criterion is
   "no regressions vs. pre-decascade baseline" rather than a hard
   number, since the count drifts as new tests land.

6. **HAL purity gate clean.** The existing `hal_scoreboard.sh` /
   `run_tests.sh --hal-purity` (or whichever gate the rest of the
   Real HAL plan uses) reports no regressions.

7. **Functional-equivalence diff.** For at least one rp2040 port and
   one rp2350 port, capture pre- and post-decascade firmware and
   verify on-device behaviour matches:
   - Boot banner identical (modulo build-date string).
   - `MM.DEVICE$` / `MM.VER` identical.
   - `MEMORY` reports identical heap.
   - Pin map (`OPTION LIST` output) identical.
   - At least one non-trivial test program runs to completion with
     identical output.

8. **CI matrix covers all ports.** `.github/workflows/firmware.yml`
   matrix builds all 15 ports (vs. the pre-plan baseline of 2). New
   ports added in Stage F included.

9. **`docs/adding-a-new-port.md` documents the palette workflow**
   as the only supported way to add a port.
