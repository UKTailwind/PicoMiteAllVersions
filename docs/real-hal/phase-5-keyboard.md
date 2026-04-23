# Real HAL — Phase 5: `hal_keyboard` ✅

**Status:** closed. Infrastructure landed in 5a; the long tail of MM_Misc.c
target-macro ifdefs (21 `USBKEYBOARD` blocks at the time of writing, plus the
rest) was eliminated as fixup phase F4 — see `../real-hal-fixup-plan.md` and
the F4 step rows in `scoreboard.md`. F4 closed at commit `38cb691`.

## What landed (Phase 5a)

- `hal/hal_keyboard.h` with full surface: `service`, `clear_repeat_state`,
  `init`, `keydown_count`, `keydown_slot`, `lock_state`, `set_layout`.
- `ports/pico_sdk_common/hal_keyboard_pico.c` + `host/hal_keyboard_host.c`
  impls. Device impl dispatches PS/2 vs USB internally.
- `fun_keydown` unified in `vm_sys_input.c`.
- Drivers relocated:
  - `Keyboard.c` → `drivers/ps2_matrix/`
  - `USBKeyboard.c` → `drivers/usb_host_kbd/`
  - `picocalc/i2ckbd.{c,h}` → `drivers/i2c_picocalc_kbd/`

## What F4 added on top

Thirty sub-steps drove MM_Misc.c from 140 → 4 ifdefs (target-macro 135 → 0).
The four leftovers are `#ifdef GUICONTROLS`, a feature flag (not a target or
port-config macro) — out of scope for the HAL purity gate. MM_Misc.c is in
`STRICT_FILES` and the gate passes.

The keyboard-shaped work specifically:
- USBKEYBOARD blocks in MM_Misc.c: replaced by `port_misc_option_setter()`
  (KEYBOARD REPEAT / PS2 PINS / MOUSE), `port_keyboard_option_setter()`, and
  `port_usb_count` / `port_usb_hid_field` HID accessors. See F4 steps 3, 8,
  9, 16, 23 in `scoreboard.md`.
- Per-port hooks for everything that wasn't strictly keyboard but had been
  living in MM_Misc.c gates (display setters, OPTION setters, MM.INFO
  fields, audio I2S PIO map, PIO interrupt-poll lookup, POKE DISPLAY raw
  panel writes) live in `ports/pico_sdk_common/misc_option_setters.c`,
  `print_display_options.c`, `spi_lcd_options.c`, `picocalc_features.c`,
  and per-port `port_defaults.c` files.

## Exit gate (met)

- Zero target-macro and zero port-config-macro `#if*` directives in MM_Misc.c.
- `tools/check_hal_purity.sh` passes with MM_Misc.c in STRICT_FILES.
- Build sweep green on all 12 device variants + host (239/239) + WASM.
