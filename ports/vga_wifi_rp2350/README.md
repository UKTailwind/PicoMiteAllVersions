# ports/vga_wifi_rp2350 — F2 validation port

Stage-F validation port from `docs/hal-decascade-plan.md`. Combines
`HAL_PORT_HAS_VGA_PIO=1` + `HAL_PORT_HAS_WIFI=1` on RP2350. First port
shape to coexist VGA-PIO scanout with the CYW43 + lwIP polled stack.

## Status

Builds clean via the per-port `port_sources.cmake` infrastructure and
is gated by `buildall.sh`. Hardware target is `pico2_w` (RP2350A +
CYW43). Real-hardware boot has not been smoke-tested by this branch.

## What this port validates

- `HAL_PORT_HAS_VGA_PIO=1` and `HAL_PORT_HAS_WIFI=1` coexist in one
  `port_config.h` without ifdef gymnastics.
- Per-port PIO claim flags (`HAL_PORT_PIO0_CLAIMED`,
  `HAL_PORT_PIO1_CLAIMED`, `HAL_PORT_PIO2_CLAIMED`) cleanly OR-merge
  hardware claims when the QVGA scanout (PIO1+PIO2) and CYW43 SPI
  (PIO0) coexist.
- The unified `MES_SIGNON` template
  `"\r" HAL_PORT_DEVICE_NAME " MMBasic " CHIP " Edition V" VERSION`
  produces a sensible boot banner ("VGAMiteWiFi MMBasic …") for the
  combined shape; the previous per-target `#define` cascade would
  have produced a redefinition error here.
- `core1stack[]` (Stage C3) sized via `HAL_PORT_CORE1_STACK_WORDS=128`
  works with CYW43 polled (which doesn't claim core1).
- Adding a new port to the build needed only:
    `ports/vga_wifi_rp2350/port_config.h`
    `ports/vga_wifi_rp2350/port_sources.cmake`
    `ports/vga_wifi_rp2350/pin_tables.c`
    `ports/vga_wifi_rp2350/port_defaults.c`
  + one `elseif (COMPILE STREQUAL …) set(PORT_DIR …)` entry in
  `CMakeLists.txt` + adding the target to the `pico2_w` board branch
  + appending the target name to `buildall.sh`. No central
  driver-selection ladder edits, no new `-D` defines, no
  configuration.h cascade entries.

## Latent bugs F2 surfaced and the decascade fixed

These would have remained latent until somebody tried this port shape:

- **External.c::setBacklight** → `spi_write_command` undefined when the
  dispatch table includes `cmd_backlight` (because of `HAL_PORT_HAS_WIFI`)
  AND the SPI-LCD primitives aren't compiled (because of `PICOMITEVGA`).
  Gated the SSD1306SPI branch on `#if !HAL_PORT_IS_VGA` — small SPI
  OLEDs aren't physically present on a VGA port anyway, so this is
  pure compile-time dead-code elimination.
- **MMtcpserver.c::closeall3d** stub multi-defined against
  `gfx_3d.c::closeall3d` real impl on PICOMITEVGA WiFi ports. Gated
  the stub on `#if !defined(PICOMITEVGA)`.
- **gfx_3d.c** must be linked on PICOMITEVGA ports because the
  dispatch table references `fun_3D` / `fun_map` / `fun_getscanline`
  inside `#ifdef PICOMITEVGA` in AllCommands.h. F2's
  `port_sources.cmake` includes it.

## Memory layout notes

RP2350A on pico2_w is RAM-tight after CYW43:

  HEAP_MEMORY_SIZE          = 80 KB    (smaller than vga_rp2350's 184 KB)
  FRAMEBUFFER_TRAILER_BYTES = 38400    (1bpp QVGA, rp2040 layout — RP2350's
                                        16-bit QVGA layout would not fit)
  HAL_PORT_HAS_PSRAM        = 0        (CYW43 owns the QSPI pins)
  HAL_PORT_HAS_HEARTBEAT    = 0        (CYW43 owns the LED)

Real-hardware boot will need pin-map and OPTION RESET profile work
specific to the target board. The current `port_defaults.c` has a
single generic profile.
