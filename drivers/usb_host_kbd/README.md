# drivers/usb_host_kbd — TinyUSB host HID keyboard driver

Drives a USB-host HID keyboard attached to the PicoMite's OTG port via
TinyUSB's `tuh_*` host stack. HID report callbacks decode scancodes
into MMBasic key codes and push into `ConsoleRxBuf`; the 1 kHz
`tuh_task()` + `hid_app_task()` pump lives in `PicoMite.c::routinechecks`
for now (migration to `hal_keyboard_service()` pending — see
`docs/real-hal-plan.md` Phase 5 follow-ups).

Exposes:
- `clearrepeat()` — reset the auto-repeat state machine (wired through
  `hal_keyboard_clear_repeat_state()`).
- `KeyDown[]`, `caps_lock`, `num_lock`, `scroll_lock` — HID state
  globals read by `fun_keydown()` (MM_Misc.c). These stay
  `#ifdef USBKEYBOARD`-gated in core until a state-accessor HAL lands.

Linked only when `COMPILE` is one of the USB host targets (PICOUSB,
VGAUSB, PICOUSBRP2350, VGAUSBRP2350, HDMIUSB per CMakeLists.txt).

## Lifted from

`USBKeyboard.c` (repo root, pre-Phase-5 refactor). No behavioural
change on the move — source was relocated and the CMake reference
updated.

## Future work

- Migrate the 1 kHz `tuh_task()` pump out of `PicoMite.c` into
  `hal_keyboard_service()`.
- Add a state-accessor HAL surface (`hal_keyboard_keydown_scan`,
  `hal_keyboard_lock_state`) so `fun_keydown()` stops reading
  `KeyDown[]` / `caps_lock` directly.
- Driver conformance tests per `docs/real-hal-plan.md`.
