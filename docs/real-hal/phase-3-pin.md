# Real HAL — Phase 3: `hal_pin` (pins / PWM / ADC) 🔧

**Status:** infrastructure landed (3a). Ifdef elimination is unfinished and the first attempt (3b) was a rename, not an elimination — see the fixup plan (`../real-hal-fixup-plan.md`, F2) for the corrected approach.

HAL API surface and call-site migration landed. The HAL contract is complete and both device and host impls exist. What remains is eliminating the `#ifdef` blocks from core files — the conditional bodies need to move *into* HAL implementations and driver files so core calls a single function with no target branching.

## What landed (Phase 3a)

- `hal/hal_pin.h` with full API surface (26 functions + Tier-B inlines):
  - `set_mode`, `read`, `write`, `toggle`, `read_output_latch`, `set_drive_mA`, `set_pulls`, `set_dir`, `set_input_enabled`, `select_digital`, `init_digital`, `deinit`, `set_function`
  - `adc_select`, `adc_init`, `adc_set_temp_sensor`, `adc_read`
  - `set_input_hysteresis`, `set_slew_fast`
  - `irq_set_edge`
  - `bank_read_all`, `bank_read_out_latch`, `bank_{set,clr,xor}_mask`
  - Tier-B fast inlines (`read_fast`, `write_fast`, `toggle_fast`) via per-port `hal_pin_inlines.h`.
- `ports/pico_sdk_common/hal_pin_pico.c` + `host/hal_pin_host.c` impls.
- 107 `hal_pin_*` call sites in External.c replacing direct `gpio_*` SDK calls.
- Direct `gpio_put`/`gpio_get`/`gpio_init`/`gpio_set_function`/`gpio_set_pulls` calls eliminated.
- `vm_sys_pin.c` adapted to call the HAL.

**Ifdef count unchanged:** External.c is still 120 (baseline was 120). The 37 rp2350 PWM-slice/PIO-count blocks and other structural `#ifdef`s were not moved into HAL impls in 3a.

**Commits:** `67b4092`, `bbbb4ec`, `344def0`, `2b374da`, `dca6ba2`, `8919341`, `67c1313`, `e733405`.

## Phase 3b attempt (reverted — see fixup plan)

Commits `2c034d7` and `61cb08e` claimed to eliminate 79 ifdefs via a port-config mechanism. What they actually did: renamed `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` in core, and relocated the original `#ifdef rp2350` into `hal/hal_port_config.h`. The scoreboard only grepped for old macro names, so renamed conditionals were invisible to the metric. Conditional compilation was moved, not eliminated. Both commits are scheduled for revert.

## What remains (corrected — replaces old "3b")

Per the fixup plan's F2, External.c must have **zero** `#if` / `#ifdef` / `#ifndef` / `#elif` directives on target OR port-config macros. Allowed: plain `#include`, function-body `if (...)` runtime checks against a global like `rp2350a`.

Work items:

- **rp2350 PWM slice / PIO / pin-count blocks (~37 in External.c):** bodies move into `hal_pin_pico.c`. Core calls e.g. `hal_pin_init_extended_pwm()` unconditionally; impl file dispatches internally on hardware. If a constant is needed for array sizing, each port's `port_config.h` defines it as a plain `#define`; core uses the value in C expressions, never in `#if`.
- **Single-shot ADC calls** (`adc_select_input`, `adc_read` at External.c:1050–1052, `adc_hw` register access): go through `hal_pin_adc_*`.
- **15 `picomite_gpio_irq_set_enabled` calls:** go through `hal_pin_irq_set_edge` or equivalent.
- **`sio_hw->gpio_hi_in` direct register read:** needs a HAL accessor.
- **ADC DMA-streaming path** (`adc_set_round_robin`, `adc_fifo_setup`, `adc_run`, `adc_set_clkdiv`): consider `hal_adc_stream_*` or leave in a driver.

## Exit gate

- External.c has zero `#if*` directives referencing target macros (`rp2350`, `PICOMITE`, etc.) OR port-config macros (`HAL_PORT_*`, `PORT_*`).
- All pin/PWM/ADC differences between rp2040 and rp2350 live in HAL impls, not in External.c.
- `tools/check_hal_purity.sh` passes for External.c, `vm_sys_pin.c`, and `hal/hal_pin.h`.
- `tools/hal_scoreboard.sh` (rewritten per fixup plan F1 to count *all* preprocessor conditionals in core, regardless of macro name) shows a real reduction.
