# Display HAL Refactor Plan

## Why This Exists

The WebRP2350 PicoCalc graphics regression exposed a structural problem in
the display layer. WebRP2350 needed the SPI-LCD core1 merge pipeline and
`spi_lcd_fastgfx.c`, but linking `drivers/display_merge/display_merge_pico.c`
also pulled in RP2350 NEXTGEN-only behavior. The immediate fix narrowed the
old `rp2350` gates to `HAL_PORT_HAS_NEXTGEN_DISPLAY`, but that is still not the
desired HAL shape: source files are still making build-time display-family
decisions internally.

The long-term goal is a display stack where the port's source list determines
which display capabilities exist. Driver files should implement one coherent
capability and should not contain `#if HAL_PORT_*`, target-macro, or
chip-family gates to decide which unrelated display family they are today.

This plan covers the two display files that currently carry the most mixed
responsibility:

- `drivers/display_merge/display_merge_pico.c`
- `drivers/spi_lcd/spi_lcd_framebuffer.c`

It also records the adjacent source-list, option-hook, and validation work
needed to keep PicoMite, PicoMiteRP2350, WebMite, WebRP2350/PicoCalc,
VGA, HDMI, and DVI-WiFi aligned.

## Invariant Goals

1. **Build-time file inclusion is the determinant.**
   A port enables display behavior by linking the correct driver
   implementation or stub in `ports/<port>/port_sources.cmake`. It should not
   inherit a broad implementation and depend on `#ifdef` branches inside that
   implementation to disable unrelated features.

2. **Display HAL boundaries are capability-shaped.**
   HAL entry points should describe operations the rest of the system needs:
   merge-pipeline control, physical framebuffer copy, NEXTGEN dirty-rect
   refresh, display option hooks, VGA/HDMI scanout operations, and FASTGFX
   swap. They should not encode board names or MCU names.

3. **No target or port-config preprocessor gates in shared display drivers.**
   `drivers/display_merge/`, `drivers/spi_lcd/`, and `drivers/vga_pio/` should
   not use `#if HAL_PORT_*`, `#ifdef rp2350`, `#ifdef PICOMITEWEB`, etc. to
   choose between unrelated driver families. Local controller-specific
   constants and normal C `if` statements on runtime display type are fine
   where they are genuinely part of one driver implementation.

4. **One translation unit owns one display capability.**
   A file named for SPI-LCD merge should not also own NEXTGEN option setters,
   PicoCalc keyboard backlight, and non-VGA option printer stubs. A file named
   `spi_lcd_framebuffer.c` should not be the long-term home for SSD1963
   parallel-panel transfer loops and NEXTGEN MEM332 shadow-buffer writes unless
   those are explicitly treated as backends behind a common framebuffer-copy
   contract.

5. **Real and stub implementations have identical external surfaces.**
   Core and shared command code calls hooks unconditionally. Unsupported
   displays either no-op or raise the same user-facing MMBasic error from the
   hook. Callers should not need compile-time checks to know whether a backend
   exists.

6. **Chip family is not a display family.**
   RP2350 is not equivalent to NEXTGEN. WebRP2350 is RP2350 but does not have
   the PicoMite NEXTGEN display path. PicoMiteRP2350 has NEXTGEN support.
   Source composition must express that distinction directly.

7. **Hardware smoke tests must cover the build axis that triggered the change.**
   Any refactor here must test at least PicoMiteRP2350 NEXTGEN compile coverage
   and WebRP2350/PicoCalc SPI-LCD FASTGFX behavior. A source-only clean build is
   not sufficient.

## Current Structure

### Source Inclusion Matrix

The display stack is already partly source-list driven:

| Port family | Merge driver | Framebuffer driver | FASTGFX | NEXTGEN MEM332 | VGA/HDMI scanout |
| --- | --- | --- | --- | --- | --- |
| `pico` | `display_merge_pico.c` | `spi_lcd_framebuffer.c` | `spi_lcd_fastgfx.c` | `spi_lcd_mem332_stub.c` | `vga_ops_stub.c` |
| `pico_rp2350` | `display_merge_pico.c` | `spi_lcd_framebuffer.c` | `spi_lcd_fastgfx.c` | `spi_lcd_mem332.c` | `vga_ops_stub.c` |
| `web` | `display_merge_stub.c` | `spi_lcd_framebuffer.c` | `fastgfx_minimal.c` | `spi_lcd_mem332_stub.c` | `vga_ops_stub.c` |
| `web_rp2350` | `display_merge_pico.c` | `spi_lcd_framebuffer.c` | `spi_lcd_fastgfx.c` | `spi_lcd_mem332_stub.c` | `vga_ops_stub.c` |
| `vga*` | `display_merge_stub.c` | unsupported/stub | `fastgfx_minimal.c` | `spi_lcd_mem332_stub.c` | `vga_ops.c` family |
| `hdmi*` / `dvi_wifi*` | `display_merge_stub.c` | unsupported/stub | `fastgfx_minimal.c` | `spi_lcd_mem332_stub.c` | `vga_ops.c` + HDMI/DVI |

The matrix is the right mechanism, but the units being selected are too broad.
`web_rp2350` is the proof: it wants the SPI-LCD merge and FASTGFX path, but not
the NEXTGEN MEM332 path.

### `display_merge_pico.c`

Current responsibilities:

- Implements `hal_display_merge_*` sender hooks.
- Owns the core1 FIFO receiver, `UpdateCore()`.
- Decodes merge commands 1 through 5.
- Decodes FASTGFX command 8.
- Decodes NEXTGEN commands 6 and 7.
- Implements `port_main_launch_core1()`.
- Implements non-VGA print/option stubs.
- Implements SD card/system SPI option helper behavior.
- Implements PicoCalc keyboard backlight handling behind `#ifdef rp2350`.
- Implements NEXTGEN option accessors and `OPTION LCD SPI` handling behind
  `HAL_PORT_HAS_NEXTGEN_DISPLAY`.

The first five are a coherent SPI-LCD merge-pipeline capability. The rest are
separate capability axes. Keeping them in one file forces every port that needs
the merge pipeline to inherit unrelated display policy.

### `spi_lcd_framebuffer.c`

Current responsibilities:

- Restores display function pointers after framebuffer mode.
- Opens/closes the framebuffer and layer buffers.
- Pushes 4-bit framebuffer data to physical displays.
- Composites layer and framebuffer (`merge`, `blitmerge`).
- Parses `FRAMEBUFFER` commands.
- Implements 4-bit framebuffer draw primitives.
- Contains SPI-LCD serial-panel transfer code.
- Contains SSD1963/IPS parallel-panel transfer code.
- Contains NEXTGEN MEM332 shadow-buffer writes.
- Contains an internal `#if !HAL_PORT_IS_VGA` around `FRAMEBUFFER SYNC` and
  `FRAMEBUFFER MERGE`.

This file is currently "non-VGA physical framebuffer support", not just
SPI-LCD. Some runtime dispatch on `Option.DISPLAY_TYPE` is expected because
MMBasic supports many panel controllers at runtime. The issue is that it also
uses compile-time display-family gates and embeds multiple physical-transfer
backends in one large command file.

The smallest improvement is to remove the redundant VGA gate by source
composition. The larger improvement is to split the physical copy backend from
the common framebuffer command/layer-composition logic.

## Desired Architecture

### Layer 1: Command/Common Framebuffer Logic

Create a driver file that owns the command and buffer lifecycle logic without
knowing the physical display bus:

- `drivers/display_framebuffer/display_framebuffer_common.c`

Responsibilities:

- `cmd_framebuffer()`
- `restorepanel()`
- `setframebuffer()`
- `closeframebuffer()`
- shared 4-bit framebuffer draw primitives
- calls to merge HAL hooks for async operations
- calls to a physical-copy HAL/backend hook for `copyframetoscreen()`

This file may branch on runtime `Option.DISPLAY_TYPE` only where the BASIC
language semantics require it. It must not contain target or port-config
preprocessor gates.

### Layer 2: Physical Framebuffer Copy Backends

Split the current `copyframetoscreen()` responsibility by physical backend:

- `drivers/spi_lcd/spi_lcd_framebuffer_copy.c`
  - serial SPI LCD panels
  - uses `DefineRegionSPI`, `spi_write_fast`, `HAL_PORT_LCD_SPI_CLK_PIN` as a
    normal selected-port value, not as a preprocessor gate

- `drivers/ssd1963/ssd1963_framebuffer_copy.c` or
  `drivers/spi_lcd/parallel_lcd_framebuffer_copy.c`
  - SSD1963, IPS_4_16, 8/16-bit parallel panel transfer loops
  - owns `SSD1963data`, `WriteComand`, `SetAreaSSD1963`, GPIO strobes

- `drivers/spi_lcd/spi_lcd_mem332_framebuffer_copy.c`
  - NEXTGEN MEM332 shadow-buffer copy into `ScreenBuffer`
  - linked only with the real MEM332 backend

- `drivers/display_framebuffer/display_framebuffer_copy_stub.c`
  - unsupported target stub

The common layer should call a selected backend through one C surface. The
first pass can keep the historical `copyframetoscreen()` symbol and select one
implementation by source inclusion. If multiple physical families must coexist
inside one firmware image, use a backend table or a small runtime dispatcher
whose cases call backend functions supplied by linked files. That dispatcher
must not use compile-time gates.

### Layer 3: Merge Pipeline

Replace `display_merge_pico.c` with narrower units:

- `drivers/display_merge/display_merge_spi_lcd_core1.c`
  - `hal_display_merge_abort/check_busy/lock/unlock/init/mark_done`
  - `hal_display_fast_dma_alloc/free`
  - async merge post hooks for commands 1 through 5
  - `UpdateCore()` receiver for commands 1 through 5
  - FASTGFX command 8 dispatch, or calls a separate FASTGFX hook
  - `port_main_launch_core1()`

- `drivers/display_merge/display_merge_nextgen.c`
  - real `hal_display_nextgen_refresh_rect()`
  - real `hal_display_nextgen_scroll_reset()`
  - command 6 and 7 receiver handling, if command dispatch remains FIFO-based

- `drivers/display_merge/display_merge_nextgen_stub.c`
  - no-op NEXTGEN refresh/scroll implementation

The command receiver can be structured in either of two acceptable ways:

1. The core1 receiver calls a hook for unknown/extension commands:
   `display_merge_handle_extension_command(command)`. The real NEXTGEN file
   consumes commands 6 and 7; the stub ignores or drains nothing because those
   commands are never posted by the stub senders.

2. The sender/receiver are split into a NEXTGEN-specific merge receiver linked
   only for NEXTGEN ports and a non-NEXTGEN receiver linked for the rest.

The first option has less duplication and keeps the FIFO protocol central. The
second option is stricter source inclusion. Either is acceptable if it removes
target and port-config gates from the source.

### Layer 4: Display Option Hooks

Move option and print hooks out of the merge driver:

- `drivers/display_options/display_options_non_vga.c`
  - non-VGA print stubs currently in `display_merge_pico.c`
  - SD card/system SPI option helpers that are shared by Pico/Web non-VGA

- `drivers/display_options/display_options_nextgen.c`
  - `port_setter_scroll_start()`
  - `port_setter_screenbuff()`
  - NEXTGEN-specific `OPTION SYSTEM SPI` auto-population of LCD pins
  - `OPTION LCD SPI`

- `drivers/display_options/display_options_nextgen_stub.c`
  - returns "not handled" for non-NEXTGEN ports

PicoCalc keyboard backlight does not belong in display merge. Move it to the
keyboard or PicoCalc port-hook area:

- `drivers/picocalc/picocalc_keyboard_backlight.c`, or
- `ports/web_rp2350/port_keyboard_backlight.c` plus corresponding PicoCalc
  source inclusion.

The selected file should reflect the capability: "has PicoCalc keyboard
backlight", not "is RP2350".

## Refactor Phases

### Phase 0: Baseline and Audit

- Record current modified display files and whether the WebRP2350 stopgap is
  present.
- Run an audit for display-related preprocessor gates:
  - `drivers/display_merge/`
  - `drivers/spi_lcd/`
  - `drivers/vga_pio/`
  - `drivers/hdmi/`
  - display-related `ports/*/port_sources.cmake`
- Classify each gate as:
  - source-composition bug
  - acceptable driver-local controller detail
  - legacy port file exception
  - unrelated future cleanup

Exit criteria:

- This document's source matrix is updated if the current branch differs.
- A short list of must-fix gates for this refactor exists.

### Phase 1: Split `display_merge_pico.c`

- Extract common SPI-LCD merge/core1 behavior into
  `display_merge_spi_lcd_core1.c`.
- Extract NEXTGEN refresh/scroll behavior into a real NEXTGEN unit plus stub.
- Extract non-VGA option stubs and SD/system SPI option helpers out of the
  merge driver.
- Move keyboard backlight handling out of the merge driver.
- Update `ports/pico*/port_sources.cmake` and `ports/web_rp2350/port_sources.cmake`
  to link the common merge driver plus the correct NEXTGEN real/stub file.
- Keep `display_merge_stub.c` for VGA/HDMI/Web paths that do not have the
  SPI-LCD merge pipeline.

Exit criteria:

- No `#ifdef rp2350` or `#if HAL_PORT_HAS_NEXTGEN_DISPLAY` remains in
  `drivers/display_merge/`.
- WebRP2350 links SPI-LCD merge and FASTGFX without linking real NEXTGEN code.
- PicoMiteRP2350 links real NEXTGEN code.
- PicoMiteRP2040 links NEXTGEN stubs.

### Phase 2: Clean `spi_lcd_framebuffer.c`

- Remove the internal `#if !HAL_PORT_IS_VGA` around `FRAMEBUFFER SYNC` and
  `FRAMEBUFFER MERGE`; the file is not linked into VGA/HDMI targets.
- Decide whether the first pass keeps `copyframetoscreen()` in the same file
  or introduces a physical-copy backend surface immediately.
- If keeping the first pass small, add a follow-up TODO in the file header
  explaining that it is currently "non-VGA physical framebuffer" rather than
  only SPI-LCD.
- If doing the full split, extract:
  - common framebuffer command/lifecycle logic
  - SPI serial LCD copy backend
  - SSD1963/parallel copy backend
  - NEXTGEN MEM332 copy backend
  - unsupported stub
- Preserve the existing runtime support for all panel types that can coexist
  in one firmware image.

Exit criteria:

- No port-config preprocessor gates remain in `spi_lcd_framebuffer.c`.
- `FRAMEBUFFER SYNC`, `MERGE`, `COPY B`, and `CREATE FAST` behavior is
  unchanged on PicoMite and WebRP2350.
- The resulting file names accurately describe their responsibilities.

### Phase 3: Align FASTGFX

- Confirm whether `fastgfx_swap_core1()` is a merge-pipeline command or should
  be a separate display-swap HAL hook.
- Keep `spi_lcd_fastgfx.c` linked only where the SPI-LCD scanout exists.
- Keep `fastgfx_minimal.c` linked only where FASTGFX is syntax-compatible but
  has no SPI-LCD scanout backend.
- Avoid tying FASTGFX support to PICOMITE, WEB, or RP2350 target macros.

Exit criteria:

- WebRP2350/PicoCalc links `spi_lcd_fastgfx.c` because it has SPI-LCD scanout,
  not because it shares a target family with PicoMite.
- VGA/HDMI/Web remain on the appropriate minimal/stub path unless a real
  backend is intentionally added.

### Phase 4: VGA/HDMI Follow-Up

This plan's first priority is the SPI-LCD/WebRP2350 breakage, but the same
pattern exists in the VGA/HDMI display family.

- Audit `drivers/vga_pio/vga_mode_ops.c`, `vga_ops.c`,
  `vga_qvga_modes.c`, and `vga_memory.c` for target/chip gates.
- Split QVGA, HDMI/DVI, and RP2350-only acceleration where source inclusion
  can express the difference.
- Keep local controller/timing details inside the selected driver files.

Exit criteria:

- VGA/HDMI source lists express scanout family selection.
- Remaining gates, if any, are documented as local driver implementation
  details rather than HAL-boundary decisions.

### Phase 5: Tooling and Regression Gates

- Extend the HAL purity tooling to include the refactored display files.
- Add a focused display-gate audit command to CI or developer docs.
- Add a link/build assertion where possible so a port cannot link both a real
  and stub implementation of the same display capability.

Exit criteria:

- New target or port-config preprocessor gates in the display HAL surface fail
  the audit.
- Source-list mistakes fail early as duplicate symbols or missing required
  symbols, not as latent hardware regressions.

## Validation Matrix

Minimum build validation:

- `COMPILE=PICO`
- `COMPILE=PICORP2350`
- `COMPILE=WEB`
- `COMPILE=WEBRP2350`
- `COMPILE=VGA`
- `COMPILE=HDMI`
- `COMPILE=DVIW`

Minimum hardware/smoke validation:

- WebRP2350/PicoCalc:
  - `FASTGFX CREATE`
  - `FASTGFX FPS 50`
  - draw a rectangle or BASIC graphics primitive
  - `FASTGFX SWAP`
  - `FASTGFX SYNC`
  - `FASTGFX CLOSE`

- PicoMiteRP2350:
  - build coverage for NEXTGEN MEM332 symbols
  - `OPTION LCD SPI` syntax still accepted where supported
  - `MM.INFO(SCREENBUFF)` and scroll-start option access still work where
    supported

- PicoMiteRP2040:
  - standard SPI-LCD framebuffer create/write/copy/merge path
  - NEXTGEN options remain unavailable

- Web/WebRP2350 networking regression smoke:
  - web server still serves files
  - TFTP conformance still passes large-file cases
  - telnet console still negotiates and remains interactive

## Non-Goals

- Do not remove runtime support for multiple LCD controller types from one
  firmware image.
- Do not move display pin tables out of port files in this pass.
- Do not redesign the BASIC `FRAMEBUFFER`, `FASTGFX`, or `OPTION LCDPANEL`
  language surface.
- Do not force a generic multicore HAL. The display driver may own its
  multicore implementation as long as the ownership is selected by source
  inclusion and hidden behind display HAL hooks.

## First Concrete Patch

The first patch should replace the current stopgap in
`display_merge_pico.c` with source composition:

1. Create a common SPI-LCD merge/core1 file.
2. Create real and stub NEXTGEN merge extension files.
3. Move option/backlight hooks out of the merge file.
4. Update `ports/pico`, `ports/pico_rp2350`, and `ports/web_rp2350` source
   lists.
5. Build `PICO`, `PICORP2350`, `WEB`, and `WEBRP2350`.
6. Re-run the PicoCalc FASTGFX smoke that caught the regression.

Only after that should `spi_lcd_framebuffer.c` be split. That sequencing keeps
the highest-risk broken boundary small and testable before tackling the larger
framebuffer file.
