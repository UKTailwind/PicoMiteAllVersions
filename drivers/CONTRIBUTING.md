# drivers/ — Contributing

Each subdirectory under `drivers/` is a reusable peripheral driver. Ports compose them at link time; there is no runtime plugin model.

## What counts as a driver

A driver encapsulates **one peripheral**: a display controller, an audio backend, a storage medium, a keyboard decoder. If you find yourself wanting to split behaviour across two peripherals (e.g. "graphics + touch"), that's two drivers with a thin coordinator in core, not one.

Existing drivers for reference:

- **Input**: `drivers/ps2_matrix/`, `drivers/usb_host_kbd/`, `drivers/i2c_picocalc_kbd/`, `drivers/gui_touch/`
- **Display**: `drivers/spi_lcd/`, `drivers/vga_pio/`, `drivers/hdmi/`, `drivers/display_merge/`
- **Audio**: `drivers/pwm_synth/`, `drivers/vs1053/`, `drivers/audio_mp3/`
- **Storage / memory**: `drivers/sd_spi/`, `drivers/pico_flash/`, `drivers/psram_heap/`
- **VM-side framebuffer**: `drivers/vm_framebuffer_picomite/`, `drivers/vm_framebuffer_unsupported/`
- **Graphics helpers**: `drivers/gfx_3d/`, `drivers/upng_sprite/`

## Shape of a driver directory

```
drivers/<name>/
    <name>.c           — device implementation
    <name>_stub.c      — no-op stub for targets without the peripheral
    <other>.c          — additional files if the driver has multiple
                         cleanly-separated concerns (e.g. vga_pio's
                         mode-ops vs blit-ops vs memory layout)
```

The **real-and-stub pattern** is load-bearing. Core files call the driver's entry points unconditionally — `Memory.c` calls `GetPSMemory()`, `Audio.c` calls `hal_audio_*`, `FileIO.c` calls `port_mount_sd_drive()`. The device target builds link the real `*.c`; targets without the peripheral link the `*_stub.c`. The stubs satisfy the linker and rely on runtime guards (`if (PSRAMsize)`, empty-body no-ops, error-returning fallbacks) to stay dormant. `psram_heap_real.c` + `psram_heap_stub.c` is the canonical pair — read them first if you're adding a new driver with a device/non-device split.

## Rules

### 1. One peripheral

A driver owns one peripheral end-to-end. "End-to-end" means: init, teardown, runtime operations, and any IRQ or DMA handlers for that peripheral. Don't split the init of a peripheral across two drivers. Don't have a driver manage a peripheral it doesn't own (e.g. if the SSD1963 driver needs a GPIO reset pin, it uses the `hal_pin_*` API — it does not link `drivers/gpio_whatever/` directly).

### 2. No cross-driver includes

A driver includes:

- `MMBasic_Includes.h` + `Hardware_Includes.h` (the core MMBasic surface).
- Its own `drivers/<name>/<name>.h` if one exists.
- The relevant `hal/hal_*.h` contract if it implements one.
- Pico SDK headers (`hardware/*.h`, `pico/*.h`) via the MCU shim.

A driver **does not** `#include "drivers/<other>/…"`. If two drivers need to talk, they talk through a HAL surface, a core callback, or a shared symbol declared in core. `grep -r '#include "drivers/' drivers/` must stay empty.

### 3. At most one MCU shim

A driver depends on `ports/pico_sdk_common/` (shared rp2040+rp2350 SDK glue) for register access. It does **not** depend on `ports/<specific_board>/` — per-board concerns belong in that board's port directory, not leaking into shared drivers.

### 4. Conditionals stay local

A driver may contain local `#ifdef rp2350 / #ifdef PICOMITEVGA / …` gates where the register layout, PIO program, or peripheral wiring genuinely differs between targets that all link this driver. The purity gate does not apply inside `drivers/*/` — that's the whole point of having the directory.

What's **not** OK: using a driver-local `#ifdef` to decide whether to emit a call to a different driver, or to satisfy a linker-error by conditionally compiling out an entry point that core references unconditionally. If core calls you on a target where you can't do anything, your stub goes under `drivers/<name>/<name>_stub.c` — the build system picks the right file.

### 5. RAM-resident annotations honoured

Device hot paths — per-pixel writes, per-sample audio kernels, DMA IRQ handlers, PIO feed loops — are annotated `__not_in_flash_func(name)` on pico targets so the XIP flash access doesn't stall the hot loop. When you copy code that already has these annotations, keep them. When you write a new hot path, profile before deciding it doesn't need one.

Examples in-tree:
- `drivers/spi_lcd/spi_lcd.c:103` — `spi_write_fast` pushes bytes through the SPI block one at a time; must stay RAM-resident.
- `drivers/pwm_synth/pwm_synth.c:590` — `iconvert` runs inside the audio IRQ callback every sample.
- `drivers/spi_lcd/spi_lcd_fastgfx.c:55` — `fastgfx_swap_core1` is the core1 FIFO receiver for the FASTGFX scanline-diff swap.

### 6. Conformance tests

If the peripheral admits an off-board test (anything that can be exercised under `mmbasic_test` without hardware), ship a `drivers/<name>/tests/` subdirectory with `.bas` fixtures. Currently aspirational — no driver has a conformance suite yet. The first contributor to add one sets the pattern.

## How your driver gets linked

Device targets: the top-level `CMakeLists.txt` / `CMakeLists 2350.txt` has driver-selection blocks gated on `COMPILE STREQUAL`. Add your driver's `.c` file(s) to the branches for targets that need the real impl, and add the `_stub.c` to the remaining branches.

Simulation ports: each port's standalone `Makefile` lists its `CORE_SRCS` + `NATIVE_SRCS` directly. Host simulation ports typically link the `_stub.c` variants of hardware drivers since the host has no real peripheral. If your driver has a host implementation (for simulation), name it `<name>_host.c` to match the `_pico.c` / `_stub.c` convention.

## Further reading

- `docs/real-hal-plan.md` — the plan index.
- `docs/real-hal/architecture.md` — where drivers sit in the layered layout.
- `docs/real-hal/contracts.md` — HAL contract sketches that some drivers implement.
- `docs/adding-a-new-port.md` — how ports compose drivers.
