# Real HAL — Phase 3: `hal_pin` (pins / PWM / ADC) 🔧 (93% — F2 fixup)

**Status:** infrastructure landed (3a). The first elimination attempt (old "3b") was reverted. The corrected elimination (fixup plan F2, seven sub-steps) has driven External.c target ifdefs from **120 → 8** — a 93% reduction. Eight ifdefs remain, deferred: six KEYPAD extended-vs-legacy gates, one PicoCalc keymap data block, and one ADC-RUN dims-type split (the last blocked on MATHS.c `parseintegerarray`/`parsefloatrarray` signature unification).

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

## F2a sub-step summary (landed)

| step | commit | what | Δ External.c target |
|------|--------|------|-------|
| 3a-infra | earlier | hal_pin surface + 107 call sites | 0 |
| F2a 1 | `7dabd7f` | per-port `port_config.h` + first cull | −15 |
| F2a 2 | `5c01e35` | unconditional globals + case arms | −47 |
| F2a 3a | `1d136fb` | `hal_fast_timer` + `HAL_PORT_HAS_HEARTBEAT` | −8 |
| F2a 3b | `8543db1` | keyboard HAL ext + `HAL_PORT_ADC_CHANNEL_MAX` | −15 |
| F2a 3c | `778956e` | `rp2350a` runtime ADC split + camera stubs | −13 |
| F2a 3d | `62925da` | `PINMAP` + `codemap` to per-port `.c` | −5 |
| F2a 3e | `e59f02d` | ADC OPEN `rp2350a` + MOUSE/GAMEPAD stubs | −3 |
| F2a 3f | `1b9ce12` | MQTT + `CollisionFound` unconditional | −3 |
| F2a 3g | `30f4f84` | `setBacklight` unified + NEXTGEN unconditional | −6 |
| **total** | | | **−115 (120 → 8 → deferred = 5?)** see note |

Ongoing target-macro count at HEAD: **8 in External.c** (down from the F1 baseline of 120).

## What's deferred (F2 close blockers)

The last 8 ifdefs all need broader refactors that would cascade beyond External.c:

- **KEYPAD extended-vs-legacy mode** (6 ifdefs at External.c:2377–2504). RP2350 has a runtime 64-pin keypad with `keypadrows`/`keypadcols`/`PadLookup` globals and a new `argc==13` parse path; RP2040 is a fixed 4×4 keypad with `#define keypadcols 4`. Unification requires either moving `cmd_keypad` / `KeypadClose` / `KeypadCheck` into a per-port `.c` or pushing everything through a `hal_keypad_*` surface. User-directed deferral.
- **PicoCalc keymap / asciimap data block** (External.c:2533, 128 lines of `const unsigned char localkeymap[][]` + `cmd_keyscan`). Whole block should move to `ports/pico_rp2350/picocalc_keypad.c` or `ports/pico_rp2350/pin_tables.c`.
- **ADC RUN `dims` type split** (External.c:3428). `parseintegerarray` and `parsefloatrarray` take `short *dims` on RP2040 and `int *dims` on RP2350. Unifying requires touching `MATHS.h`/`MATHS.c` signatures + the variable-table struct's `dims[MAXDIM]` field in `MMBasic.h` (from `short` to `int` everywhere). That costs ~10 bytes per variable × MAXVARS, which is a real RAM hit on RP2040. Needs explicit RAM-budget sign-off before the switch.

## Exit gate

- External.c has zero `#if*` directives referencing target macros (`rp2350`, `PICOMITE`, etc.) OR port-config macros (`HAL_PORT_*`, `PORT_*`).
- All pin/PWM/ADC differences between rp2040 and rp2350 live in HAL impls, not in External.c.
- `tools/check_hal_purity.sh` passes for External.c, `vm_sys_pin.c`, and `hal/hal_pin.h`.
- `tools/hal_scoreboard.sh` (rewritten per fixup plan F1 to count *all* preprocessor conditionals in core, regardless of macro name) shows a real reduction.
