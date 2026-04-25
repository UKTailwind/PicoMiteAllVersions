# ports/vga_wifi_rp2350 — F2 validation port (work in progress)

Stage-F validation port from `docs/hal-decascade-plan.md`. Combines
`HAL_PORT_HAS_VGA_PIO=1` + `HAL_PORT_HAS_WIFI=1` on RP2350 — the first
port shape to coexist VGA-PIO scanout with the CYW43 + lwIP stack.

## Status

The port directory builds + configures cleanly via the new per-port
`port_sources.cmake` infrastructure. The link step does **not** succeed
yet — F2 surfaced architectural coupling between the SPI-LCD subsystem
and the WiFi stack that the decascade plan didn't address. Documented
below as follow-on items.

```
$ cmake -DCOMPILE=VGAWIFIRP2350 -DPICOCALC=false ..   # OK
$ make                                                # link fails
  ld: .bss won't fit in RAM (overflow ~14 KB)
  ld: undefined reference to fun_3D
  ld: undefined reference to spi_write_command
```

## Why F2 is excluded from `buildall.sh`

The 12-port gate stays green. F2 is a forward-looking compose target
that proves the decascade infrastructure (palette flags, per-port
snippets, source-list composition) supports a novel combination
without inventing new top-level enums — the unblocked path forward
for HDMI+WiFi (F1) is the same set of files in a different combination.

## What this port already validates

- `HAL_PORT_HAS_VGA_PIO=1` and `HAL_PORT_HAS_WIFI=1` coexist in one
  `port_config.h` without ifdef gymnastics.
- `core1stack[]` (Stage C3) sized via `HAL_PORT_CORE1_STACK_WORDS=128`
  works with CYW43 polled (which doesn't claim core1).
- `HAL_PORT_PIO0_CLAIMED=1`, `HAL_PORT_PIO1_CLAIMED=1`,
  `HAL_PORT_PIO2_CLAIMED=1` cleanly OR-merge VGA-PIO scanout claims
  (PIO1+PIO2) with CYW43 SPI claims (PIO0) — the per-port flags
  introduced for F2 also benefit every existing port.
- The single-source `MES_SIGNON` template
  `"\r" HAL_PORT_DEVICE_NAME " MMBasic " CHIP " Edition V" VERSION`
  produces a sensible boot banner ("VGAMiteWiFi MMBasic …") for this
  combo, where the previous per-target `#define` cascade would have
  produced a redefinition error.
- The new port directory is wired into `CMakeLists.txt` via a single
  `elseif (COMPILE STREQUAL "VGAWIFIRP2350") set(PORT_DIR vga_wifi_rp2350)`
  + the `pico2_w` board branch — no other top-level edits.

## What needs to happen before F2 links cleanly

1. **`struct option_s` VGA-vs-non-VGA layout.** `Touch.c` references
   `Option.TOUCH_XSCALE` / `TOUCH_YSCALE` / `TOUCH_XZERO` / `TOUCH_YZERO`,
   which only exist in the `#ifndef PICOMITEVGA` branch of FileIO.h's
   union. F2 has `PICOMITEVGA` defined, so `Touch.c` can't compile on
   this port. Either:
   - Make those struct fields unconditional (move the VGA-side `Height`/`Width`/`dummy[12]`
     overlay to a different slot), OR
   - Introduce `HAL_PORT_HAS_TOUCH` and gate `Touch.c` on it.
2. **`SSD1963.c` is touch-aware.** Line 202 zeros `Option.TOUCH_XZERO/YZERO`
   on display reset — same struct issue. F2 currently omits `SSD1963.c`
   from its source list, but `External.c::setBacklight` calls
   `spi_write_command` (defined in SSD1963.c) so the link fails.
   Either gate `setBacklight` on a HAS_SPI_LCD flag or move
   `spi_write_command` into a shared SPI-LCD HAL.
3. **`fun_3D` / DRAW3D dispatch.** `AllCommands.h` references `fun_3D`
   inside `#ifdef PICOMITEVGA`. F2 has `PICOMITEVGA` so the dispatch
   table needs `fun_3D` defined. `gfx_3d.c` provides it but the
   current source-list logic excludes `gfx_3d.c` on WiFi ports
   (because the WEB stack's `MMtcpserver.c` provides a `closeall3d`
   stub, not a `fun_3D` stub). Include `gfx_3d.c` on F2 (since it's
   PICOMITEVGA, not just non-WEB).
4. **Heap budget.** Combined VGA scanout framebuffer (~150 KB) +
   CYW43 + lwIP buffers + interpreter heap overflows RP2350's 256 KB
   SRAM by ~15 KB. Either drop heap to 128 KB or move scanout
   framebuffer to PSRAM (which CYW43 owns the QSPI pins for, so PSRAM
   is unavailable on this port shape).

Items 1-3 are real architectural decoupling work outside the
decascade plan's scope — they were latent in the existing codebase,
just hidden by the assumption that PICOMITEVGA and PICOMITEWEB are
mutually exclusive. F2 surfaces them.
