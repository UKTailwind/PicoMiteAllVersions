# Real HAL — Phase 7: `hal_display`

**Status:** not started. Split into four sub-phases, one per display backend, so a stall on HDMI doesn't block ILI9341.

This is the riskiest phase: display touches the hot path (FASTGFX SWAP, per-pixel draws), spans four very different backends, and owns the biggest ifdef cluster (`Draw.c` = 162 ifdefs at Phase 0 baseline). The two-tier inline mechanism (see `architecture.md`) is the perf-critical tool for keeping `hal_display_put_pixel` fast across the HAL boundary.

## Phase 7a — `hal_display.h` for ILI9341 (the proof)

Pilot one display end-to-end before scaling. ILI9341 SPI LCD is the simplest display backend (no PIO, no multicore, no layer composition), so it pilots the HAL contract.

- Define `hal/hal_display.h` (Tier A slow path: init, set-mode, sync, vsync-wait, scroll, blit-rect; Tier B hot path: `static inline put_pixel` via per-port inline header chosen in Phase 0).
- Define `hal/hal_irq.h` with `HAL_TIME_CRITICAL` macro.
- Implement `drivers/ili9341/` from the current `SPI-LCD.c`, lifted with no behaviour change.
- `Draw.c` for ILI9341 ports: replace direct `SPI-LCD.c` calls with HAL calls. `gfx_*_shared.c` calls Tier-B inlines for hot pixel paths.
- 4 Picomite ports (RP2040 + RP2350 × USB / non-USB) switch to `drivers/ili9341/`.

**Exit gate:** Draw.c `#ifdef` count drops by the ILI9341-specific gates. Zero `PICOMITE`-only display ifdefs for the ILI9341 path. 4 ports build clean. Smoke-boot on physical RP2040 PicoMite + RP2350 PicoMite. `pico_blocks_tilemap` FPS held; `mand` wall time held. RAM baseline check passes.

## Phase 7b — `hal_display.h` for VGA (PIO)

- `drivers/vga_pio/` lifted from current VGA scanout code. Multi-mode (SCREENMODE1/2/3) — driver internally handles mode dispatch with local `#ifdef` allowed where it simplifies.
- `Draw.c` for VGA ports: VGA-specific code moves into the driver; the driver implements `hal_display`.
- All `PICOMITEVGA` ifdefs in Draw.c move into `drivers/vga_pio/` — the driver owns the conditional logic, not Draw.c.
- 4 VGA ports (RP2040 + RP2350 × USB / non-USB) switch over.

**Exit gate:** zero `PICOMITEVGA` references in Draw.c. VGA ports build + boot. Scanout integrity verified visually. FASTGFX FPS held.

## Phase 7c — `hal_display.h` for HDMI

- `drivers/hdmi/` lifted from rp2350 HDMI code. Includes the multicore scrolling/clearing path (uses `hal_multicore`, see Phase 8).
- All `HDMI` ifdefs in Draw.c move into `drivers/hdmi/`.
- 2 HDMI ports switch over.

**Exit gate:** zero `HDMI` references in Draw.c. HDMI ports build + boot. SCREENMODE4/5 verified.

## Phase 7d — `hal_display.h` for SSD1963 + Web variants

- `drivers/ssd1963/` lifted from current `SSD1963.c`. Web variants pull in `drivers/cyw43/` (Phase 9) at the same time.
- 2 Web ports switch over.

**Exit gate:** Draw.c hardware-ifdef count → 0. Zero display-target macros (`PICOMITE`, `PICOMITEVGA`, `HDMI`) in Draw.c. All 12 device ports use `hal_display`. RAM baseline check passes for every target.

## Commit-count target

3–4 commits per sub-phase.
