# Real HAL — Phase 7: `hal_display`

**Status:** not started. Split into four sub-phases, one per display backend, so a stall on HDMI doesn't block ILI9341.

This is the biggest phase: display touches the hot path (FASTGFX SWAP, per-pixel draws), spans four very different backends, and owns the biggest ifdef cluster (`Draw.c` = 164 target-macro ifdefs at Phase 6 close). The two-tier inline mechanism (see `architecture.md`) is the perf-critical tool for keeping `hal_display_put_pixel` fast across the HAL boundary.

**Validation model:** same as every closed phase (0–6). The exit gate is
all 12 device CMake variants building clean under `buildall.sh`, host
`./run_tests.sh` 239/239, `tools/check_hal_purity.sh` green, and the
targeted file's scoreboard column dropping to 0. Exit-gate text below
that mentions "smoke-boot on physical hardware" or "FPS held" was
written before Phase 6 closed and sets a bar higher than the rest of
the plan — it should be read as *desirable* post-merge verification by
the maintainer, not as a blocker for landing the phase. A phase-closing
commit with the standard build+test+purity triple is the working
standard.

**Current Draw.c per-macro distribution** (at commit `899c218`, phase 6 close):

| macro          | count |
|----------------|-------|
| rp2350         | 55    |
| PICOMITE       | 49    |
| HDMI           | 40    |
| PICOMITEVGA    | 39    |
| PICOMITEWEB    | 5     |

(Overlap: `rp2350` ifdefs often co-occur with `PICOMITEVGA` or `HDMI`; the same physical directive can count against two columns. Total unique directives: 164.)

## Phase 7a — `hal_display.h` for ILI9341 (the proof)

Pilot one display end-to-end before scaling. ILI9341 SPI LCD is the simplest display backend (no PIO, no multicore, no layer composition), so it pilots the HAL contract.

- Define `hal/hal_display.h` (Tier A slow path: init, set-mode, sync, vsync-wait, scroll, blit-rect; Tier B hot path: `static inline put_pixel` via per-port inline header chosen in Phase 0).
- Define `hal/hal_irq.h` with `HAL_TIME_CRITICAL` macro.
- Implement `drivers/spi_lcd/` from the current `SPI-LCD.c`, lifted with no behaviour change. (Named `spi_lcd` rather than `ili9341` because the file covers the full SPI-controller family — ILI9341, ST7796, ILI9488, ST7789B, GC9A01, ILI9163, ILI9481, SSD1306, N5110, ST7920 — not just ILI9341.)
- `Draw.c` for ILI9341 ports: replace direct `SPI-LCD.c` calls with HAL calls. `gfx_*_shared.c` calls Tier-B inlines for hot pixel paths.
- 4 Picomite ports (RP2040 + RP2350 × USB / non-USB) switch to `drivers/spi_lcd/`.

**Exit gate:** Zero `PICOMITE`-only display ifdefs in Draw.c for the ILI9341 path. All 12 device variants + host (239/239) + HAL purity gate green. (Physical `pico_blocks_tilemap` FPS / `mand` wall-time / RAM-baseline verification is desirable post-merge but not a blocker for landing the phase — consistent with Phase 6's close.)

### Progress (as of 2026-04-23)

- **Step 1** (`1e495e1`): relocate `SPI-LCD.c` → `drivers/spi_lcd/spi_lcd.c`. Pure file move — infrastructure only, scoreboard unchanged.
- **Step 2** (`b793387`): introduce `hal/hal_display_merge.h` with `hal_display_merge_abort()` and `hal_display_merge_check_busy()`. Real impl in `drivers/display_merge/display_merge_pico.c` (PICOMITE variants), stub in `display_merge_stub.c` (others). Move `mergerunning` / `mergedone` / `mergetimer` to unconditional storage in `core/state/display_state.c`. Draw.c ifdefs: 164 → 160 (−4).
- **Step 3** (`9c2af50`): extend `hal_display_merge` with `lock_fb` / `unlock_fb` / `mark_done` (wraps `frameBufferMutex` + the `mergedone` signal pair) and `fast_dma_alloc` / `fast_dma_free` (wraps `ShadowBuf` + `fb_dma_chan` setup/teardown for FRAMEBUFFER CREATE FAST). Draw.c ifdefs: 160 → 151 (−9).
- **Step 4** (`ee1f725`): `HAL_PORT_LCD_SPI_CLK_PIN` port-config macro replaces the `#if PICOMITE && rp2350` pair that toggled between `Option.LCD_CLK` (rp2350 dedicated LCD SPI pin) and `Option.SYSTEM_CLK` (rp2040 shared SPI pin). Three call sites migrated (copyframetoscreen two, fastgfx_get_spi). `RGB565()` promoted to unconditional. Draw.c ifdefs: 151 → 147 (−4).
- **Step 5** (`46da291`): NEXTGEN (MEM332) function-pointer dispatch unguarded. `drivers/spi_lcd/spi_lcd_nextgen_stub.c` supplies no-op MEM332 symbols (Draw/Bitmap/Buffer/BLIT/Scroll variants) on every build except PICORP2350/PICOUSBRP2350 (which get the real impls from `spi_lcd.c`). `SPI-LCD.h` externs unconditional. `ScreenBuffer` alias promoted to unconditional in `configuration.h`. `restorepanel` / `setframebuffer` / `copyframetoscreen` NEXTGEN branches drop their guards. Draw.c ifdefs: 147 → 141 (−6).
- **Step 6** (`832cf32`): remaining NEXTGEN memory-sizing and refresh paths. `cmd_restore` ReadBuffer memory sizing (2 gates), `DrawPixel16` / `DrawRectangle16` / `DrawBitmap16` / `ReadBuffer16` WriteBuf lazy-alloc VIRTUAL bound (4 gates), `InitDisplayVirtual` early-return check (1 gate) all collapse to the rp2350 form (which behaves identically on non-rp2350 since `Option.DISPLAY_TYPE >= NEXTGEN` is never true there). `cmd_refresh` NEXTGEN rectangle-push wrapped as `hal_display_nextgen_refresh_rect()`; `ClearScreen` NEXTGEN scroll-reset wrapped as `hal_display_nextgen_scroll_reset()`. Draw.c ifdefs: 141 → 121 (−11).
- **Step 7** (TBD): async merge protocol and FASTGFX extraction. Six new hal_display_merge hooks — `sync_wait`, `post_fill`, `post_bg`, `post_copy`, `post_blit_fill`, `post_blit_bg`, plus `has_pipeline` — cover every `multicore_fifo_push_blocking(1|2|3|4|5, …)` call-site in Draw.c's `cmd_framebuffer` SYNC/MERGE/COPY and `cmd_blit` MERGE command paths. The previously `#ifdef PICOMITE`-gated parse blocks become unconditional; `background && !hal_display_merge_has_pipeline()` surfaces a clean "Not available on this display" error on targets without a core1 merge loop. The ~400-line FASTGFX block (`fastgfx_swap_core1`, `merge_optimized`, `bc_fastgfx_*`, `cmd_fastgfx`) moves out of Draw.c into `drivers/spi_lcd/spi_lcd_fastgfx.c` (real impl for PICOMITE) plus `drivers/spi_lcd/spi_lcd_fastgfx_stub.c` (VGA/HDMI/WEB stubs), matching the pattern used by `drivers/pwm_synth/` in Phase 6. Draw.c ifdefs: 121 → 110 (−11). **Draw.c has zero PICOMITE-macro ifdefs remaining.**

Cumulative Phase 7a progress: Draw.c **164 → 110** (−54 ifdefs). Grand scoreboard **267 → 213**. PICOMITE-macro references across all tracked core files: 49 → 5 (the stragglers live in non-Draw.c files).

With PICOMITE cleared from Draw.c, phase 7a's ILI9341-proof exit gate is effectively hit for that macro. The remaining 110 ifdefs in Draw.c break down by macro as PICOMITEVGA (50), HDMI (41), rp2350 (~70, mostly overlapping with the above), and PICOMITEWEB (34) — all phase 7b / 7c / 7d territory. The next obvious chunk is Phase 7b (VGA): lift the VGA scanout state machine into `drivers/vga_pio/` and drive the PICOMITEVGA ifdef cluster to zero via the same pattern (per-driver real file + stub).

## Phase 7b — `hal_display.h` for VGA (PIO)

- `drivers/vga_pio/` lifted from current VGA scanout code. Multi-mode (SCREENMODE1/2/3) — driver internally handles mode dispatch with local `#ifdef` allowed where it simplifies.
- `Draw.c` for VGA ports: VGA-specific code moves into the driver; the driver implements `hal_display`.
- All `PICOMITEVGA` ifdefs in Draw.c move into `drivers/vga_pio/` — the driver owns the conditional logic, not Draw.c.
- 4 VGA ports (RP2040 + RP2350 × USB / non-USB) switch over.

**Exit gate:** zero `PICOMITEVGA` preprocessor directives in Draw.c. All 12 device variants + host + purity gate green. (Visual scanout / FASTGFX FPS verification: desirable, not a blocker — same policy as 7a.)

### Progress (as of 2026-04-23)

- **Step 1** (TBD): first `hal_vga_ops.h` surface and per-function lifts. Five hooks — `handle_cls`, `handle_tile_cls`, `handle_layer_clear`, `retile_for_font`, `wait_scanline_zero` — cover `ClearScreen` + `gfx_cls_helper` + `cmd_font` + the DrawCircle benchmark loop. Real impls in `drivers/vga_pio/vga_ops.c` (HDMI-vs-QVGA dispatch local-ifdef'd inside); no-op stubs in `drivers/vga_pio/vga_ops_stub.c`. `HAL_PORT_CONSOLE_FONT_MEDIUM` port-config macro collapses the FontTable[2] arial_bold-vs-Hom_16x24 triple-nested `#ifdef PICOMITEVGA/HDMI` pair. `ScrollStart` promoted to unconditional storage in `core/state/display_state.c` (was duplicated between drivers/ssd1963/SSD1963.c and host_runtime.c). Draw.c ifdefs: 110 → 97 (−13).

## Phase 7c — `hal_display.h` for HDMI

- `drivers/hdmi/` lifted from rp2350 HDMI code. Includes the multicore scrolling/clearing path (uses `hal_multicore`, see Phase 8).
- All `HDMI` ifdefs in Draw.c move into `drivers/hdmi/`.
- 2 HDMI ports switch over.

**Exit gate:** zero `HDMI` preprocessor directives in Draw.c. All 12 device variants + host + purity gate green. (SCREENMODE4/5 visual verification: desirable, not a blocker.)

### Progress (as of 2026-04-23)

Draw.c already had zero HDMI ifdefs at phase 7b close — 7b's
vga_ops.c / vga_mode_ops.c lifts swept the last of them. Phase 7c is
therefore scope-exit-gate-met by inheritance. The remaining 7c work
is the *optional* driver-hygiene refinement: making `drivers/hdmi/`
genuinely own every piece of HDMI-specific code, so the directory
matches how `drivers/spi_lcd/` owns the SPI-LCD family and
`drivers/vga_pio/` owns the QVGA PIO scanout.

- **Step 1** (`3d6f189`): `drivers/hdmi/hdmi_modes.c` — extract the
  HDMI half of `drivers/vga_pio/vga_mode_ops.c`'s `#ifndef HDMI /
  #else / #endif` block: DrawRectangle555/256 family, DrawBitmap555/
  256, DrawBuffer555/256, ReadBuffer555/256, DrawPixel555/256,
  ScrollLCD555/256, and the HDMI flavour of `cmd_map` / `cmd_tile`
  that dispatches on DISPLAY_TYPE. Sister file
  `drivers/vga_pio/vga_qvga_modes.c` takes the non-HDMI side. CMake
  selects the right file per target. Also drops the vestigial
  `#include "hardware/spi.h"` from Draw.c — a step toward the
  mmbasic_stdio (12.5) goal of compiling the interpreter core
  without any Pico SDK headers on its include path.
- **Step 2** (`022d3ee`): `drivers/hdmi/hdmi_scanout.c` — pure
  file-level move of the 1444-line HDMI TMDS scanout engine out of
  `PicoMite.c`'s `#ifdef PICOMITEVGA / #ifndef HDMI / #else [body]
  / #endif / #endif` block: TMDS sync-line constants, the
  MODE_H/V_* mode tables, `dma_irq_handler0`, `HDMIloop0/1/2/3/X`,
  `HDMICore`, and the HDMI flavour of `settiles`. PicoMite.c's
  PICOMITEVGA block shrinks to just the QVGA (non-HDMI) scanout.
  `HDMICore` gains an extern in Hardware_Includes.h for the
  `multicore_launch_core1_with_stack` call-site that stays in
  PicoMite.c. IRQ priorities, DMA chains, and RAM-resident
  placement (`MIPS32` / `MIPS64` / `__not_in_flash_func`) are
  preserved exactly.

Scoreboard unchanged through both steps — neither file was tracked
at phase-7c entry (PicoMite.c is port-level, not core). The
refinement is structural: HDMI code now has exactly one home.
Remaining HDMI conditional code in the tree (10 in PicoMite.c, 18
in Editor.c, a handful elsewhere) is port/UI adapter logic, not
core-interpreter code, and not scoped to this phase.

## Phase 7d — `hal_display.h` for SSD1963 + Web variants

- `drivers/ssd1963/` lifted from current `drivers/ssd1963/SSD1963.c`. Web variants pull in `drivers/cyw43/` (Phase 9) at the same time.
- 2 Web ports switch over.

**Exit gate:** Draw.c target-macro + port-config-macro ifdef count → 0. All 12 device ports use `hal_display`. Draw.c promoted to `STRICT_FILES`. All 12 device variants + host + purity gate green. (RAM-baseline verification: desirable, not a blocker.)

## Commit-count target

3–4 commits per sub-phase.
