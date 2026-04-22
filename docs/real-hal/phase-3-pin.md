# Real HAL — Phase 3: `hal_pin` (pins / PWM / ADC) ✅ (F2 closed)

**Status:** infrastructure landed (3a); the first elimination attempt (old "3b") was reverted; the corrected elimination (fixup plan F2, eight sub-steps) drove External.c target-macro ifdefs from **120 → 0**. External.c is promoted to `STRICT_FILES` in `tools/check_hal_purity.sh` — the purity gate now rejects any future target-macro or port-config-macro ifdef added to External.c.

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
| F2a 3h | (this session) | KEYPAD unification + PicoCalc keymap → per-port + MATHS.c dims widening | −8 |
| **total** | | | **−120 (120 → 0)** |

Ongoing target-macro count at HEAD: **0 in External.c**. Two `#ifdef GUICONTROLS` blocks remain — not counted (GUICONTROLS is a feature flag, not a target macro).

## F2a step 3h (final) — how the last eight landed

- **`parseintegerarray` / `parsefloatrarray` / `parsestringarray` / `parsenumberarray` signature unification** (MATHS.c). All four functions now take `int *dims` on every target. The function body's memcpy from `g_vartbl[].dims` (still `short` on RP2040, `int` on RP2350) into the caller's dim buffer is guarded by a compile-time `sizeof(g_vartbl[0].dims[0]) == sizeof(int)` check that dead-code-eliminates into a straight memcpy on RP2350 and an element-by-element widening copy on RP2040. Zero RAM impact — var-table field stays `short` on RP2040. Small, bounded CPU cost (a 5-iteration widening copy in a function already doing variable lookup and type checks). No preprocessor gate.
- **All callers** (`External.c`, `Commands.c`, `MATHS.c`, `Custom.c`) now declare `int dims[MAXDIM]` unconditionally. Matching `array_comp` / `parse_and_strip` signatures in Commands.c unified too.
- **KEYPAD extended mode** (cmd_keypad / KeypadClose / KeypadCheck) now runs on every target. `keypad_pins[64]`, `keypadrows`/`keypadcols` (runtime int), `PadLookup` (MMFLOAT*), and `PadLookupDefault[16]` are all unconditional globals. Legacy-mode path in cmd_keypad sets `keypadrows=keypadcols=4` + `PadLookup=PadLookupDefault` — identical behaviour to the old RP2040 macros. Static RAM cost: 56 extra bytes for `keypad_pins[]` + 132 bytes for `PadLookupDefault[]` const in flash.
- **PicoCalc keymap + cmd_keyscan** (128 lines of `localkeymap[][]` / `asciimapl/u/fl/fu[]` + `cmd_keyscan`) moved to `ports/pico_rp2350/picocalc_keypad.c`. Only `COMPILE=PICORP2350` and `COMPILE=PICOUSBRP2350` link it; the one caller in PicoMite.c is already gated so no link surprise on other targets.

## Exit gate — PASSED

- `External.c`: zero `#if*` directives on target or port-config macros. (Two `#ifdef GUICONTROLS` remain — not in scope.)
- `External.c` added to `STRICT_FILES` in `tools/check_hal_purity.sh`; gate green.
- Host `mmbasic_test` 239/239, all 12 device CMake variants build clean.
- Scoreboard grand total: **602 → 476** (−126 since F1 baseline).

## Exit gate

- External.c has zero `#if*` directives referencing target macros (`rp2350`, `PICOMITE`, etc.) OR port-config macros (`HAL_PORT_*`, `PORT_*`).
- All pin/PWM/ADC differences between rp2040 and rp2350 live in HAL impls, not in External.c.
- `tools/check_hal_purity.sh` passes for External.c, `vm_sys_pin.c`, and `hal/hal_pin.h`.
- `tools/hal_scoreboard.sh` (rewritten per fixup plan F1 to count *all* preprocessor conditionals in core, regardless of macro name) shows a real reduction.
