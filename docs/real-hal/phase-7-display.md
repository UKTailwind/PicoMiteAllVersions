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
- Implement `drivers/ili9341/` from the current `SPI-LCD.c`, lifted with no behaviour change.
- `Draw.c` for ILI9341 ports: replace direct `SPI-LCD.c` calls with HAL calls. `gfx_*_shared.c` calls Tier-B inlines for hot pixel paths.
- 4 Picomite ports (RP2040 + RP2350 × USB / non-USB) switch to `drivers/ili9341/`.

**Exit gate:** Zero `PICOMITE`-only display ifdefs in Draw.c for the ILI9341 path. All 12 device variants + host (239/239) + HAL purity gate green. (Physical `pico_blocks_tilemap` FPS / `mand` wall-time / RAM-baseline verification is desirable post-merge but not a blocker for landing the phase — consistent with Phase 6's close.)

## Phase 7b — `hal_display.h` for VGA (PIO)

- `drivers/vga_pio/` lifted from current VGA scanout code. Multi-mode (SCREENMODE1/2/3) — driver internally handles mode dispatch with local `#ifdef` allowed where it simplifies.
- `Draw.c` for VGA ports: VGA-specific code moves into the driver; the driver implements `hal_display`.
- All `PICOMITEVGA` ifdefs in Draw.c move into `drivers/vga_pio/` — the driver owns the conditional logic, not Draw.c.
- 4 VGA ports (RP2040 + RP2350 × USB / non-USB) switch over.

**Exit gate:** zero `PICOMITEVGA` preprocessor directives in Draw.c. All 12 device variants + host + purity gate green. (Visual scanout / FASTGFX FPS verification: desirable, not a blocker — same policy as 7a.)

## Phase 7c — `hal_display.h` for HDMI

- `drivers/hdmi/` lifted from rp2350 HDMI code. Includes the multicore scrolling/clearing path (uses `hal_multicore`, see Phase 8).
- All `HDMI` ifdefs in Draw.c move into `drivers/hdmi/`.
- 2 HDMI ports switch over.

**Exit gate:** zero `HDMI` preprocessor directives in Draw.c. All 12 device variants + host + purity gate green. (SCREENMODE4/5 visual verification: desirable, not a blocker.)

## Phase 7d — `hal_display.h` for SSD1963 + Web variants

- `drivers/ssd1963/` lifted from current `SSD1963.c`. Web variants pull in `drivers/cyw43/` (Phase 9) at the same time.
- 2 Web ports switch over.

**Exit gate:** Draw.c target-macro + port-config-macro ifdef count → 0. All 12 device ports use `hal_display`. Draw.c promoted to `STRICT_FILES`. All 12 device variants + host + purity gate green. (RAM-baseline verification: desirable, not a blocker.)

## Commit-count target

3–4 commits per sub-phase.
