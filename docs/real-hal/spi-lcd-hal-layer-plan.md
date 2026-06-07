# Hardware-Independent SPI LCD HAL Layer Plan

**Status:** future work, skeletal plan.

This note captures the path toward a reusable display HAL boundary that core
graphics, framebuffer, and FASTGFX logic can use unchanged while different
backend drivers handle physical presentation. SPI LCDs are the first target
because the current ESP32 Freenove work exposed the duplication most clearly.
It is based on the current tree:

- `drivers/spi_lcd/` for existing PicoMite SPI LCD controller support,
  framebuffer copy, and FASTGFX.
- `drivers/vm_framebuffer_picomite/` for the current VM `FRAMEBUFFER`
  command implementation.
- `drivers/display_merge/` for the current Pico core1 merge pipeline.
- `ports/esp32_s3/main/esp32_ili9341_lcd.c`, `esp32_fastgfx.c`, and the
  ESP32 framebuffer HAL implementation for the current Freenove ILI9341 path.

## Problem

The ILI9341 is not special. It is one member of a common SPI LCD controller
family. The current code does not yet have a clean hardware-independent SPI LCD
driver layer, so the ESP32 port has local ILI9341, FASTGFX, and framebuffer
presentation code while the PicoMite path carries similar behavior inside
Pico-shaped files.

That duplication makes it too easy for BASIC-level display semantics such as
`FRAMEBUFFER MERGE`, `MERGE ...,R`, `FASTGFX SWAP`, RGB121 packing, dirty
presentation, and panel restore behavior to diverge by port.

## Current Coupling

The existing `drivers/spi_lcd` code has reusable display knowledge, but it is
not a transport-neutral LCD library yet.

Current coupling and assumptions:

- Pico SDK transport APIs: `hardware/spi.h`, `spi_write_blocking`,
  `spi_write_fast`, `spi_finish`, and Pico SPI instance selection.
- Pico DMA APIs and state: `hardware/dma.h`, `fb_dma_chan`, DMA allocation and
  teardown.
- Pico multicore APIs: `pico/multicore.h`, core1 FIFO commands, and
  `UpdateCore()`.
- Pico synchronization: `pico/mutex.h` and the current framebuffer mutex path.
- Port option and pin plumbing mixed into display files: `Option.LCD_*`,
  `Option.SYSTEM_*`, `HAL_PORT_LCD_SPI_CLK_PIN`, backlight and reset pins,
  and controller selection.
- Shared globals from the interpreter/display state: `HRes`, `VRes`,
  `DisplayHRes`, `DisplayVRes`, `WriteBuf`, `FrameBuf`, `LayerBuf`,
  `ShadowBuf`, `Option.DISPLAY_TYPE`, and color tables.
- Runtime controller handling and physical bus handling live in the same files.
- `drivers/display_merge/display_merge_pico.c` mixes SPI LCD merge work with
  Pico core1 mechanics, NEXTGEN hooks, FASTGFX dispatch, and unrelated
  non-VGA option helpers.
- The ESP32 path currently has local ILI9341 transport and presentation code
  under `ports/esp32_s3/main/`, rather than using a common SPI LCD backend.

## Design Principle

The core BASIC graphics surface should not know whether the visible target is
an ILI9341, ST7789, VGA scanout buffer, HDMI mode, host window, or future
display backend. It should draw into the same logical buffer model and call a
generic HAL operation to present pixels, wait for display timing, or query
backend capabilities.

Backend drivers should provide hardware-backed services only:

- configure and reset the physical display
- advertise dimensions, pixel format, timing, readback, and async capability
- present RGB121/RGB565/RGB332 rectangles supplied by common code
- optionally provide acceleration hooks for DMA, dirty rectangles, scanout,
  or background presentation

The SPI LCD layer should therefore be a backend under this generic display
HAL, not a place where BASIC `FRAMEBUFFER` semantics are reimplemented.

## Proposed Layer Boundaries

### 1. Generic Display HAL

Owns the contract between common graphics/framebuffer code and all physical
display backends:

- display dimensions and orientation
- supported logical pixel formats
- present full frame or rectangle
- optional readback/snapshot
- optional wait-for-vblank/scanline/timing
- optional async present/merge capability
- backend restore/shutdown after offscreen modes

The common `Draw.c`, `FRAMEBUFFER`, and `FASTGFX` behavior should target this
HAL and remain unchanged when a port swaps from one backend driver to another.

### 2. SPI LCD Controller Core

Owns controller-level behavior that is independent of MCU transport and BASIC
semantics:

- controller IDs and capabilities for ILI9341, ST7789, ST7796, ILI9488,
  GC9A01, ILI9163, ST7735, SSD1306-class SPI/OLED controllers where practical
- init sequences
- MADCTL/orientation calculation
- inversion and BGR/RGB policy
- pixel format selection
- address-window command construction
- scroll command construction where supported
- read capability metadata where supported

This layer should not call Pico SDK or ESP-IDF APIs directly, and it should
not implement `FRAMEBUFFER` behavior. It should issue controller commands
through a transport interface and expose a display-backend implementation to
the generic display HAL.

### 3. SPI LCD Transport Adapter

Owns the MCU and bus mechanics:

- command/data writes
- bulk pixel writes
- optional reads
- reset and data/command GPIO
- chip-select behavior
- DMA or queued transactions
- maximum transfer sizes
- bus locking and timing constraints

Required adapters:

- Pico SDK adapter using current Pico SPI, GPIO, DMA, and optional core1
  support.
- ESP-IDF adapter using `driver/spi_master.h`, ESP-IDF GPIO APIs, queued or
  polling transactions, PSRAM-aware buffering, and the board profile pin map.

The transport surface should be small enough that controller code can be
shared:

- `write_command(cmd)`
- `write_data(bytes, len)`
- `write_command_data(cmd, bytes, len)`
- `set_addr_window(x0, y0, x1, y1)` as either a controller helper or a
  transport-facing composed operation
- `write_pixels_rgb565(bytes, len)`
- optional `read_response(cmd, bytes, len)`
- optional `flush()` / `wait_idle()`

### 4. Physical Presentation Backend

Owns conversion from common display buffers to physical panel updates:

- RGB121 to RGB565 expansion
- full-screen copy
- rectangle copy
- dirty-row or dirty-rectangle presentation
- optional shadow-buffer comparison
- panel restore after leaving framebuffer/FASTGFX mode

This should be common for SPI LCD panels and sit below the generic display HAL.
It calls the transport/controller layers only to set a window and push pixels.

### 5. Common FRAMEBUFFER Engine

Owns BASIC `FRAMEBUFFER` semantics, not panel transport:

- buffer lifecycle for `FrameBuf`, `LayerBuf`, and `ShadowBuf`
- `FRAMEBUFFER CREATE`, `CREATE FAST`, `LAYER`, `WRITE`, `CLOSE`
- `FRAMEBUFFER MERGE`, `MERGE ...,B`, `MERGE ...,R`, `MERGE ...,A`
- `FRAMEBUFFER COPY`, `SYNC`, and `WAIT`
- layer transparency policy
- RGB121 compositing
- immediate and repeating merge scheduling

The engine should call the generic display HAL for the final RGB121 rectangle
or full-screen result. No per-backend `FRAMEBUFFER` command implementation
should be required for normal display backends.

### 6. Common FASTGFX Engine

Owns FASTGFX command behavior and buffer semantics:

- `FASTGFX CREATE`, `SWAP`, `SYNC`, `CLOSE`, and FPS pacing
- back/front buffer ownership
- interaction with active `FRAMEBUFFER`
- reuse of RGB121 drawing primitives
- dirty presentation through the physical presentation backend

Scanout displays and SPI LCDs may advertise different capabilities and
accelerators through the generic display HAL, but command semantics should
remain common.

## Transport Adapter Requirements

### Pico SDK

- Preserve current SPI LCD behavior for existing PicoMite ports.
- Preserve current performance-critical paths where DMA and core1 merge are
  already used.
- Keep controller init and address-window behavior byte-for-byte compatible
  during the first migration phase.
- Keep current option parsing and pin ownership until the controller layer is
  separated enough to move them safely.
- Continue supporting existing runtime display types, including non-ILI9341
  SPI LCD controllers.

### ESP-IDF

- Use ESP-IDF `spi_master` APIs and board profiles for pin selection.
- Support Freenove ILI9341 first, without baking Freenove assumptions into the
  common controller layer.
- Support PSRAM allocation for frame/layer/front/back buffers.
- Batch RAMWR transfers by rectangle or scanline; avoid per-pixel or
  per-glyph transactions.
- Represent color order, inversion, rotation, and panel quirks as controller
  or board-profile data.
- Provide an async/repeating merge service that does not depend on Pico core1
  FIFO. Candidate mechanisms are an ESP32 task, a VM service hook, or a timer
  that schedules work onto a display task.

## Framebuffer and FASTGFX Reuse Strategy

Near-term reuse should target behavior first, then file structure. The target
is not "make ESP32 use the Pico SPI LCD files directly"; it is "make Pico and
ESP32 use the same core graphics/framebuffer logic through the same HAL
contract."

1. Extract or mirror the PicoMite `FRAMEBUFFER` command semantics into a
   common engine with no Pico SDK includes.
2. Make physical presentation a generic display HAL operation that accepts
   RGB121 source data and a rectangle.
3. Keep Pico's core1 merge pipeline as one implementation of the async merge
   service.
4. Add an ESP32 implementation of the async/repeating merge service using
   ESP-IDF primitives.
5. Move the ESP32 local framebuffer code out of any file named `_stub.c` before
   expanding it further.
6. Share FASTGFX command handling and buffer lifecycle with SPI LCD panels;
   leave only the presentation callback port-specific.
7. Once behavior is common, collapse duplicate RGB121-to-RGB565 expansion and
   dirty comparison code.

## Migration Phases

### Phase 0: Inventory and Naming Cleanup

- Rename any real ESP32 framebuffer implementation currently living in a
  `_stub.c` file.
- Document the public symbols currently required by `drivers/spi_lcd`,
  `drivers/vm_framebuffer_picomite`, and the ESP32 ILI9341 path.
- Identify which behavior is command semantics, which is compositing, and
  which is physical transport.

### Phase 1: Generic Display HAL Sketch

- Define the narrow display HAL that common graphics, framebuffer, and
  FASTGFX code use.
- Include capability flags for scanout, SPI-LCD-style push, readback,
  async/background presentation, and timing wait support.
- Add adapter shims so existing Pico SPI LCD and ESP32 ILI9341 paths can
  expose the HAL without changing BASIC behavior.

### Phase 2: Transport Interface Sketch

- Add a small SPI LCD transport interface header.
- Implement a Pico adapter that wraps the existing Pico SPI calls without
  changing behavior.
- Implement an ESP-IDF adapter that wraps the existing local ILI9341 SPI
  transaction helpers.
- Keep all existing call sites intact until adapter behavior is verified.

### Phase 3: Controller Core Extraction

- Move controller constants, init sequences, orientation handling, address
  windows, inversion, and pixel format policy into a transport-neutral file.
- Keep runtime controller dispatch by `Option.DISPLAY_TYPE` where needed.
- Validate ILI9341 first, then ST7789/ST7796-class panels.

### Phase 4: Physical Presentation Extraction

- Create a shared SPI LCD RGB121 presentation backend.
- Port Pico `copyframetoscreen()` behavior to the shared backend.
- Port ESP32 `present_rgb121_rect` / dirty presentation to the shared backend.
- Keep transport-specific batching and DMA details inside adapters.

### Phase 5: Common FRAMEBUFFER Engine

- Move VM `FRAMEBUFFER` command semantics into a common implementation that
  targets the generic display HAL.
- Preserve Pico behavior for `MERGE ...,B/R/A`, `SYNC`, and `WAIT`.
- Add ESP32 repeating merge support through the new service interface.
- Verify portable framebuffer BASIC programs use the same path on Pico and
  ESP32.

### Phase 6: Common FASTGFX Engine

- Move SPI LCD FASTGFX command handling and buffer lifecycle into a common
  implementation.
- Reuse the same RGB121 presentation backend as framebuffer.
- Keep scanout-only behavior behind display capabilities rather than board or
  MCU names.

### Phase 7: Broader Controller Coverage

- Extend ESP32 support beyond Freenove ILI9341 only when the common SPI LCD
  controller layer is proven.
- Add ST7789/ST7796/ILI9488-class panels by controller data and board profile,
  not by copying another local driver.

## Risks

- Performance regression on Pico if DMA/core1 paths are abstracted too early
  or with too much per-pixel overhead.
- ESP32 PSRAM bandwidth and SPI transaction overhead may make naive full-screen
  merges too slow.
- Existing BASIC programs may rely on timing side effects of Pico core1 merge
  behavior.
- Readback support differs by panel and bus wiring; do not assume every SPI LCD
  can snapshot physical display RAM.
- Controller init tables may contain board-specific quirks currently hidden in
  option handling.
- Runtime display selection may require a dispatcher rather than one linked
  backend if multiple SPI LCD families remain selectable in a single firmware.
- FASTGFX and FRAMEBUFFER interaction can corrupt display state unless buffer
  ownership and panel restore rules are explicit.

## Non-Goals

- Do not refactor VGA, HDMI, DVI, SSD1963 parallel panels, or NEXTGEN MEM332
  as part of the first SPI LCD layer.
- Do not replace the BASIC display command surface.
- Do not require hardware readback for panels that cannot support it reliably.
- Do not make Freenove-specific pin or color quirks part of the generic
  controller layer.
- Do not remove Pico core1/DMA acceleration; wrap it behind a narrower service.
- Do not attempt to support every controller in the first pass. Start with
  ILI9341 and one ST77xx-class panel.
- Do not change behavior in existing PicoMite SPI LCD builds until the adapter
  and controller core have side-by-side validation.

## Initial Validation Targets

- PicoMite ILI9341 build and smoke test still match existing behavior.
- PicoMite FASTGFX and framebuffer demos still run with expected FPS.
- ESP32-S3 Freenove ILI9341 text, `FASTGFX`, and `FRAMEBUFFER` paths use the
  same RGB121 presentation backend.
- Portable framebuffer demo using `FRAMEBUFFER CREATE`, `LAYER`, and
  `MERGE ...,R` runs on both Pico SPI LCD and ESP32 ILI9341.
- Color order, inversion, and orientation are validated separately from
  framebuffer semantics.
