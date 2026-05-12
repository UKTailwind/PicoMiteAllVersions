# Stage 5 — VGA mode 13h graphics

**Goal:** Turn the pc386 display from a placeholder geometry shim into a real
graphics target. BASIC drawing commands write to the live graphics surface,
and `PIXEL(x,y)` reads back the live pixel value.

## Non-negotiables

1. **No core target gates.** The port wires existing Draw.c function pointers
   and HAL implementations. No `#if PORT_PC386` in core or VM.
2. **VGA is the default REPL.** The VGA window is the local console, using the
   same `DisplayPutC`/`DrawBitmap` path as device builds. COM1 remains an ANSI
   serial mirror and test-harness path.
3. **The VGA surface is scanout-only.** `FRAMEBUFFER N/F/L` stays unsupported
   because the VGA surface is already the live scanout buffer, not an
   off-screen layer model.

## Delivered

- **Mode programming.** `drivers/vga_mode13h/` uses classic IBM VGA mode
  13h as the default. Runtime `MODE` switches call BIOS INT 10h through the
  protected-mode thunk: cold mode 1 is VGA 13h, and modes 2..6 are VBE
  linear framebuffer modes when the BIOS exposes them. After entering a VBE
  mode, `MODE 1` keeps BASIC at logical 320x200 but uses the VBE 640x480
  surface as a doubled compatibility scanout; QEMU/SeaBIOS does not reliably
  make legacy A0000 scanout visible again after LFB modes. The driver checks
  the VBE BIOS return status; on QEMU/Bochs only, if a repeated BIOS mode set
  fails after one VBE mode is active, it falls back to the documented Bochs
  DISPI ports for the same linear framebuffer mode.
- **Resolution modes.** `MODE` lists the active mode and available modes:
  `MODE 1` = 320x200, `MODE 2` = 640x480, `MODE 3` = 800x600,
  `MODE 4` = 1024x768, `MODE 5` = 480x480 letterboxed in 640x480,
  and `MODE 6` = 320x320 pixel-doubled and letterboxed in 1024x768.
  Modes 2..6 require VBE; without it only mode 1 is advertised.
- **Scanout backends.** Mode 1 draws one byte per pixel at `0xA0000` with an
  RGB 3:3:2 DAC palette. VBE modes draw through the BIOS-provided linear
  framebuffer with the same Draw.c-facing pixel path.
- **Exact readback.** The driver keeps a 24-bit shadow plane, so
  `PIXEL(x,y)` returns the exact BASIC RGB value that was drawn, while the VGA
  card receives the nearest 8-bit palette index.
- **Draw.c integration.** `pc386_runtime_begin()` calls `vga_mode13h_init()`,
  which sets `HRes/VRes` and wires `DrawPixel`, `DrawRectangle`, `DrawBitmap`,
  `ScrollLCD`, `DrawBuffer`, and `ReadBuffer`.
- **Graphics console.** `pc386_runtime_begin()` enables
  `Option.DISPLAY_CONSOLE`, sets `OptionConsole=3` (screen + serial), selects
  the normal font 1 (`8x12`), and derives the real console geometry from
  `HRes/VRes` (`40x16` at 320x200, `80x40` at 640x480). The serial REPL
  mirror remains active.
- **HAL readback path.** pc386 now links
  `drivers/display_pixel_readbuffer/display_pixel_readbuffer.c` instead of the
  host pixel-read stub.
- **Tests.** `ports/pc386/tests/repl_expect.py` gained a `graphics` case:
  `MM.HRES/MM.VRES`, `CLS`, `PIXEL`, `BOX`, `LINE`, `CIRCLE`, `PIXEL(x,y)`
  readback, `MODE` switching, and the expected `FRAMEBUFFER` error.
- **Screen probe.** `ports/pc386/tests/screen_probe.py` drives BASIC over
  COM1, asks QEMU/QMP for a `screendump`, parses the PPM, and samples pixels
  from the actual displayed surface.
- **Run modes.** `./run_floppy.sh` exercises the BIOS/FDC boot path, starts in
  VGA mode 13h, and exposes VBE modes 2..6 when the BIOS supports them.
  `./run.sh` is the direct QEMU `-kernel` development path. Both interactive
  paths use QEMU Cocoa `zoom-to-fit` by default; `unscaled` leaves the QEMU
  window at raw guest pixels.

## Validation

Stage close was validated with:

1. `./ports/pc386/build.sh`
2. `python3 ports/pc386/tests/repl_expect.py graphics graphics_vbe`
3. `python3 ports/pc386/tests/screen_probe.py` — actual QEMU screen pixels pass,
   including serial and PS/2 `aa` + Backspace redraw checks plus a
   `MODE 2` → `MODE 1` visible-screen round trip
4. `python3 ports/pc386/tests/screen_probe.py --vbe-all` — displayed pixels and
   scanout dimensions pass for all advertised VBE modes

Interactive run:

1. `cd ports/pc386 && ./run_floppy.sh`
2. Use `./run_floppy.sh unscaled` when inspecting raw guest pixels.

Follow-on regression checks:

1. `tools/check_hal_purity.sh`
2. `host/run_tests.sh`

## Open questions

- **Text-mode fallback.** Stage 4 mirrored output to VGA text memory. Stage 5
  switches to a graphics console once the framebuffer driver is up. A pure
  VGA-text fallback could be useful for debugging failed graphics init, but it
  is no longer the default console.
- **Palette policy.** The current RGB 3:3:2 palette is simple and predictable.
  If real hardware testing shows a better compatibility profile with the BIOS
  mode-13h default palette, revisit the DAC setup in Stage 8.
