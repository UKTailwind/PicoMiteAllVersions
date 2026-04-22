# Real HAL — Phase 5: `hal_keyboard` 🔧

**Status:** infrastructure landed (5a). Ifdef elimination is the remaining work — see the fixup plan (`../real-hal-fixup-plan.md`, F4).

## What landed (Phase 5a)

- `hal/hal_keyboard.h` with full surface: `service`, `clear_repeat_state`, `init`, `keydown_count`, `keydown_slot`, `lock_state`, `set_layout`.
- `ports/pico_sdk_common/hal_keyboard_pico.c` + `host/hal_keyboard_host.c` impls. Device impl dispatches PS/2 vs USB internally.
- `fun_keydown` unified in `vm_sys_input.c`.
- Drivers relocated:
  - `Keyboard.c` → `drivers/ps2_matrix/`
  - `USBKeyboard.c` → `drivers/usb_host_kbd/`
  - `picocalc/i2ckbd.{c,h}` → `drivers/i2c_picocalc_kbd/`

**Ifdef count:** MM_Misc.c went from 135 → 134, not the target of <50. 21 `USBKEYBOARD` blocks in MM_Misc.c are untouched.

**Commits:** `1c24fee`, `95a92e4`, `3be23ac` (Phase 5 hal_keyboard init + keydown + lock_state + set_layout).

## What remains

Every scored core file must have zero `#if*` directives on `USBKEYBOARD` or any other target macro:

- **MM_Misc.c (21 `USBKEYBOARD` blocks):** OPTION output formatting, interrupt config, keyboard buffer handling, repeat-rate setup, PS/2 pin configuration. Bodies move into `hal_keyboard_pico.c` (which already dispatches USB vs PS/2) or into new HAL functions (e.g. `hal_keyboard_get_option_string`, `hal_keyboard_get_config`).
- **FileIO.c (2 `USBKEYBOARD` blocks):** keyboard buffer persistence. Move into HAL.
- **Functions.c (3 `USBKEYBOARD` blocks):** keyboard read functions. Move into HAL.
- **Commands.c (3 keyboard-related blocks):** move into HAL.

## Exit gate

- Zero `USBKEYBOARD` references in any scored core file.
- All keyboard-type dispatch lives in HAL impls.
- `tools/check_hal_purity.sh` passes for all keyboard-touching core files and `hal/hal_keyboard.h`.
