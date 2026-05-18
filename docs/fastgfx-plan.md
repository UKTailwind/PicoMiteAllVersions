# FASTGFX Implementation Plan

## Overview

New double-buffered display API for PicoMite that uses scanline diffing and DMA to minimize SPI transfers. Runs the diff+transfer on core1 so BASIC can overlap computation. Completely separate from the existing FRAMEBUFFER/BLIT API.

## BASIC API

### `FASTGFX CREATE`
- Allocates two buffers in RAM, each `HRes * VRes / 2` bytes (4-bit indexed color, 2 pixels/byte):
  - **back**: the drawing target. All drawing commands write here.
  - **front**: shadow of what's currently on the display. Never drawn to by BASIC.
- Both buffers start zeroed (black).
- Sets `WriteBuf` to point to the back buffer so all drawing commands target it.
- Errors if FASTGFX is already active, or if FRAMEBUFFER/LAYER is active.

### `FASTGFX SWAP`
- If a previous SWAP is still running on core1, blocks until it finishes (implicit sync).
- Sends a command to core1 via multicore FIFO to start the swap operation.
- Returns immediately to BASIC (non-blocking).
- Core1 swap operation (detailed below).

### `FASTGFX SYNC`
- Blocks until core1 has finished the current swap operation.
- After SYNC returns, the back buffer is valid and safe to draw on.
- Must be called before any drawing commands that modify the back buffer.
- No-op if no swap is in progress.

### `FASTGFX CLOSE`
- Waits for any in-progress swap to finish.
- Frees both buffers.
- Restores `WriteBuf` to normal display buffer.
- Resets FASTGFX state.

## Core1 Swap Operation

When core1 receives the swap command, it performs these steps:

### 1. Tear Sync
- `while(GetLineILI9341() != 0) {}` — wait for display scanline 0.
- Same as existing merge behavior. Prevents visible tearing.

### 2. Scanline Diff + Transfer
For each scanline `y` from 0 to `VRes-1`:

1. **Compare**: `memcmp(back + y * HRes/2, front + y * HRes/2, HRes/2)`
   - If identical, skip this scanline entirely.

2. **Find dirty range**: Scan the scanline bytes to find the leftmost and rightmost byte that differs. This gives the minimal dirty pixel range `[x_left, x_right]` (each byte = 2 pixels).

3. **Batch consecutive dirty scanlines**: If the next scanline is also dirty, extend the region vertically rather than issuing a separate `DefineRegionSPI` per scanline. Track the union of dirty x-ranges across batched scanlines.

4. **DefineRegionSPI**: Set the display window to the dirty rectangle once for the batch.

5. **Convert + DMA transfer**: For each scanline in the batch:
   - Convert the dirty pixel range from 4-bit indexed color to RGB565 using the existing `map[]` lookup table, writing into a line buffer.
   - DMA the line buffer to SPI.
   - Use ping-pong line buffers: while DMA sends buffer A, CPU converts the next scanline into buffer B.
   - Wait for DMA completion before reusing a buffer.

6. **Copy to front**: `memcpy` the dirty byte range from back to front for each processed scanline (can be done per-scanline as we go, after DMA has read the data).

### 3. Signal Done
- Set a flag (e.g., `fastgfx_done = true`) and memory barrier (`__dmb()`).
- SYNC polls/waits on this flag.

## DMA Details

- Use one of the RP2040's DMA channels (claim at FASTGFX CREATE, release at CLOSE).
- Configure for 8-bit transfers from line buffer to `spi_get_hw(spi0)->dr`.
- DREQ paced by SPI TX FIFO (`DREQ_SPI0_TX`).
- Two line buffers for ping-pong (max `HRes * 2` bytes each = 640 bytes for RGB565 at 320 wide).
- After starting DMA, CPU is free to convert the next scanline or copy back→front.
- Wait for DMA completion: poll `dma_channel_is_busy()` or use the transfer-complete interrupt.

## Memory Budget

- Back buffer: `320 * 240 / 2` = 38,400 bytes
- Front buffer: `320 * 240 / 2` = 38,400 bytes
- Two line buffers: `320 * 2 * 2` = 1,280 bytes
- Total: ~78 KB (same as existing FrameBuf + LayerBuf)

## Build Environment

- **Pico SDK**: `~/pico/pico-sdk` (SDK 2.2.0). NOT `~/git/pico-sdk` (stale env var).
- **SDK customizations**: `gpio.c` and `gpio.h` are symlinked from the SDK into `PicoMiteAllVersions/gpio.c` and `gpio.h`. Changes are RP2040 pad isolation compat, IRQ handler in RAM, `gpio_acknowledge_irq` de-inlined, added `gpio_get_out_level_all64()`. Originals backed up as `.bak` in the SDK tree.
- **Build cache**: `build2350/CMakeCache.txt` has `PICO_SDK_PATH=/Users/joshv/pico/pico-sdk`. For RP2040 builds, create a new build directory and pass `-DPICO_SDK_PATH=~/pico/pico-sdk`.
- **DMA library**: `hardware_dma` already linked in CMakeLists.txt (line 254).

## Files to Modify

### Draw.c
- New function `cmd_fastgfx()` — parses `FASTGFX CREATE|SWAP|SYNC|CLOSE` subcommands.
- New function `fastgfx_swap_core1()` — the core1 swap operation (diff, convert, DMA, copy).
- New globals: `FastGFXBackBuf`, `FastGFXFrontBuf`, `fastgfx_done`, `fastgfx_active`.
- DMA channel management (claim/release).

### PicoMite.c
- Add new FIFO command ID for FASTGFX SWAP in `UpdateCore()` on core1.
- `UpdateCore()` calls `fastgfx_swap_core1()` when it receives the command.

### Commands.c / CommandTable
- Register `cmd_fastgfx` as the handler for the `FASTGFX` command token.

### SPI-LCD.c
- No changes needed. Reuse existing `DefineRegionSPI()`, `spi_finish()`, and `map[]` table.
- DMA writes directly to the same SPI peripheral that `spi_write_fast()` uses.

## Interaction with Existing API

- FASTGFX and FRAMEBUFFER are mutually exclusive. `FASTGFX CREATE` errors if FrameBuf or LayerBuf is allocated. `FRAMEBUFFER CREATE` errors if FASTGFX is active.
- FASTGFX does not use LayerBuf, FrameBuf, DisplayBuf, or any existing merge/blit paths.
- Drawing commands (BOX, CIRCLE, LINE, TEXT, etc.) work unchanged — they write to whatever `WriteBuf` points to, which FASTGFX sets to the back buffer.

## BASIC Usage Pattern

```basic
FASTGFX CREATE

' Draw initial scene
CLS RGB(BLACK)
' ... draw background, sprites, HUD ...

FASTGFX SWAP       ' send first frame
FASTGFX SYNC       ' wait for it

DO
  ' Physics + input (can overlap with previous swap if desired)
  ' ...

  FASTGFX SYNC     ' ensure back buffer is safe to modify

  ' Erase old sprite positions (black boxes)
  ' Draw sprites at new positions
  ' Update HUD if changed

  FASTGFX SWAP     ' kick off diff+send on core1

  IF INKEY$ = CHR$(27) THEN EXIT DO
LOOP

FASTGFX CLOSE
```

## Performance Expectations

For a game like pico_blocks with ~5-10% of pixels changing per frame:
- **Diff**: 240 scanlines * 160 bytes memcmp ≈ 0.3 ms
- **Convert**: ~3,800 changed pixels * map lookup ≈ 0.1 ms
- **DMA SPI transfer**: ~7,600 bytes at 50 MHz ≈ 1.2 ms
- **Copy back→front**: ~24 changed scanlines * 160 bytes ≈ negligible
- **DefineRegionSPI**: 1-3 calls ≈ 0.1 ms
- **Total swap time**: ~2 ms (vs ~50 ms for full-screen merge today)
- **Expected FPS**: Limited by BASIC interpreter speed (~25 ms) ≈ 40 FPS
