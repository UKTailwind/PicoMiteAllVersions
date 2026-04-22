# Real HAL — Architecture

Target layout and composition rules. See `../real-hal-plan.md` for the index and standard.

## Directory layout

```
core/                              ← BASIC interpreter + compiler + VM
   MMBasic.c, Operators.c, Functions.c, Commands.c (interp parts)
   bc_source.c, bc_vm.c, bc_runtime.c, bc_alloc.c
   gfx_*_shared.c                  ← target-agnostic graphics primitives
   mm_misc_shared.c                ← target-agnostic command bodies
   state/                          ← hoisted cross-cutting globals (see port-config.md)
       display_state.c             (HRes, VRes, FontTable, layer_in_use, ...)
       pin_state.c                 (PinDef[], pin mode arrays)
       option_state.c              (Option struct + persistence callbacks)
       audio_state.c               (voice slots, sample buffers)
   No hardware #ifdefs anywhere. Enforced by tools/check_hal_purity.sh.

hal/                               ← HAL interface headers (declarations + Tier-B inlines only)
   hal_time.h, hal_flash.h, hal_pin.h, hal_storage.h, hal_filesystem.h,
   hal_keyboard.h, hal_audio.h, hal_display.h, hal_multicore.h, hal_net.h,
   hal_irq.h
   CONTRACT.md                     ← what each function must guarantee

drivers/                           ← reusable device drivers
   ili9341/, vga_pio/, hdmi/, ssd1963/
   ps2_matrix/, usb_host_kbd/, i2c_picocalc_kbd/
   sd_spi/, pico_flash/
   pwm_synth/, vs1053/
   cyw43/, psram/, goodix_touch/, gui_controls/,
   gps_uart/, watchdog_pico/
   Each driver:
     - includes hal/<corresponding>.h and implements its API
     - knows about its peripheral, not about other peripherals
     - depends on at most one MCU shim (e.g. ports/pico_sdk_common/) for register access
     - may contain local #ifdef gates (HDMI vs VGA inside vga_pio is fine)
     - ships with conformance tests under drivers/<name>/tests/

ports/                             ← target board recipes (device only)
   pico_sdk_common/                ← shared rp2040+rp2350 SDK glue
                                     hal_time/hal_flash impls,
                                     irq dispatch, timer wiring
   rp2040/, rp2350/                ← MCU-specific overrides
   picomite_rp2040/, picomite_usb_rp2040/,
   picomite_vga_rp2040/, picomite_vga_usb_rp2040/,
   picomite_web_rp2040/,
   picomite_rp2350/, picomite_usb_rp2350/,
   picomite_vga_rp2350/, picomite_vga_usb_rp2350/,
   picomite_hdmi/, picomite_hdmi_usb/, picomite_web_rp2350/

   Each port directory contains:
     - port_config.h               ← constants drivers/HAL impls read
     - CMakeLists.txt              ← lists which drivers + hal-impls to link
     - main.c                      ← entry point
     - board_init.c                ← thin glue (pin assignments, clock setup, init order)

host/                              ← UNCHANGED until Phase 12
   host_*.c, host_wasm_*.c         ← present-day code, untouched
   Makefile, Makefile.wasm         ← present-day build, untouched

ports/mmbasic_stdio/               ← Phase 12.5: pure-stdio binary, no display/REPL/editor
   main.c                          ← argv[1] = .bas file, or read stdin to EOF
   CMakeLists.txt                  ← links core + minimal HAL impls only:
                                       hal_time → real libc clock
                                       hal_filesystem → real POSIX
                                       hal_storage → null (no block device)
                                       hal_keyboard → stdin
                                       hal_display → null (PRINT goes to stdout)
                                       hal_audio → hard-error stub
                                       hal_pin/flash/multicore/net → hard-error stubs
   No editor, no REPL, no graphics, no MEMFS, no IDBFS, no canvas.
```

The `mmbasic_stdio` port is structurally distinct from `host/`. The host port simulates a full PicoMite environment (display canvas, MEMFS filesystem, REPL, editor, FASTGFX) for behavioural-equivalence testing. The stdio port simulates *nothing* — it's MMBasic the language, talking to a real OS through narrow HAL surfaces. Together they prove the HAL: the host port shows the contract supports rich device-equivalent behaviour; the stdio port shows the contract supports trivial Unix integration without dragging in any display or interactive code.

## Composition example

```cmake
add_executable(picomite_vga_rp2350
    ports/picomite_vga_rp2350/main.c
    ports/picomite_vga_rp2350/board_init.c
)
target_link_libraries(picomite_vga_rp2350 PRIVATE
    mmbasic_core
    pico_sdk_common
    rp2350
    driver_vga_pio
    driver_ps2_matrix
    driver_sd_spi
    driver_pwm_synth
    driver_psram
    driver_watchdog_pico
)
```

No `target_compile_definitions(... PICOMITEVGA)` poisoning the core. The `vga_pio` driver knows it's VGA; nothing else needs to.

## Tier-B inlining mechanism

The hot-path question — how does `hal_display_put_pixel` inline across the HAL boundary without a function-call overhead — is **the** perf-critical decision and was locked in Phase 0.

**Mechanism: per-port inline header.**

- `hal/hal_display.h` declares the slow-path API and includes a port-specific inline header at the bottom: `#include "hal_display_inlines.h"`.
- Each port directory provides its own `hal_display_inlines.h` containing `static inline` bodies for hot functions. The port's CMake adds the port directory to the include path *before* any driver, so the resolution is unambiguous.
- For non-perf paths (init, mode-set, sync), regular extern functions resolved at link time.

Phase 0 validates this by prototyping `hal_display_put_pixel` for ILI9341 and measuring `pico_blocks_tilemap` SWAP rate on RP2040 hardware. If the inline mechanism costs >2% on FASTGFX, fall back to per-port `.h`-included `.c` (header-only library style) and re-measure. If both lose, the HAL boundary itself moves up the call stack so the hot loop runs entirely inside the driver.

## Host + WASM stay as-is until Phase 12

The host and web-host ports are working test infrastructure. `./run_tests.sh` (192/192), `mmbasic_sim`, and the WASM smoke harnesses (`smoke_audio.mjs`, `smoke_phase4.mjs`) gate every commit on this branch. **Tearing those apart concurrently with HAL definition is gratuitous risk** — if a test fails, we'd be unable to attribute the failure to a HAL design bug vs a Makefile / include-path bug vs a renamed-symbol miss. The `-Wl,--allow-multiple-definition` workaround already in MEMORY.md is a sign the link order is fragile.

The pattern instead:

- **Phases 0–11 add `hal/`, `drivers/`, `ports/` alongside the existing tree.** The new HAL headers are designed to satisfy device targets first. The host port keeps using its current `host_*.h` headers.
- **Where a HAL header naturally subsumes a host header,** the host source either (a) keeps including its old `host_*.h` header which now forwards to the HAL header (one-line shim), or (b) is migrated to include the HAL header directly with a `host/` thin-wrapper for behavioural shortcuts that don't fit the HAL contract.
- **No symbols in `host/` are renamed or moved during Phases 0–11.** A failing test bisects to a single causal change.
- **Phase 12** does the host + WASM relocation in one focused effort, with the device HAL contract already locked. By then the HAL has shaken out across 12 device targets, so any remaining host-isms are real divergences worth examining, not noise from a still-evolving interface.

The CLAUDE.md rule "Never overwrite or delete working code to replace it with something different" applies most strongly to the host port. Phase 12 may rename and relocate; it must not change behaviour, and it lands as its own atomic phase with its own gate.

## Starting state (2026-04-21 survey)

```
Hardware-related #ifdefs in core files (Phase 0 baseline):
  Draw.c        162
  MM_Misc.c     135
  External.c    120
  FileIO.c       75
  Commands.c     46
  Memory.c       37
  Functions.c    17
  Audio.c        14
  Operators.c     0   ← already clean

Per-macro totals across the scored files:
  rp2350           258
  PICOMITE         112    (excluding PICOMITEVGA/WEB/PLUS)
  PICOMITEVGA       88
  PICOMITEWEB       79
  HDMI              62
  USBKEYBOARD       43
  MMBASIC_HOST      41
  PICOCALC          16

12 device targets:
  PicoMite, PicoMite USB, PicoMite VGA, PicoMite VGA USB,
  PicoMite Web (RP2040 + RP2350),
  PicoMite RP2350, PicoMite USB RP2350, PicoMite VGA RP2350,
  PicoMite VGA USB RP2350, PicoMite HDMI, PicoMite HDMI USB.
Plus host (native) and host_wasm (Phase 12).
```

Existing HAL-ish surfaces (informal, host-only, kept as-is): `host_fb.h`, `host_fs_hal.h`, `host_keys.h`, `host_terminal.h`, `host_time.h`; `vm_sys_pin.h`, `vm_sys_file.h`, `vm_sys_graphics.h`, `vm_sys_time.h`, `vm_sys_input.h`. These proved the pattern works without performance loss.
