# drivers/ps2_matrix — PS/2 keyboard driver

Drives a PS/2 matrix keyboard wired up on the PicoMite / VGA
PicoMite's PS/2 clock and data GPIOs. Scan codes are decoded to
MMBasic key codes and pushed into `ConsoleRxBuf`; `hal_keyboard`
(via `ports/pico_sdk_common/hal_keyboard_pico.c`) invokes
`CheckKeyboard()` on the interrupt-hook path to drive the scan loop.

Layout selection (US / UK / FR / DE / IT / ES / BE / BR) lives in
this file; the user selects via `OPTION KEYBOARD <code>` which
writes `Option.KeyboardConfig`. The layout decode tables are local
to `Keyboard.c`.

Linked only when `COMPILE` is one of the non-USB targets (PICO, VGA,
PICORP2350, VGARP2350, WEB, WEBRP2350 per CMakeLists.txt). USB
variants link `drivers/usb_host_kbd/USBKeyboard.c` instead.

## Lifted from

`Keyboard.c` (repo root, pre-Phase-5 refactor). No behavioural change
on the move — source was relocated and the CMake reference updated.
`PS2Keyboard.h` stays at repo root because it's the interface header
read by core files.

## Future work

The HAL contract expects a `drivers/<name>/tests/` conformance suite
(see `docs/real-hal-plan.md`). Not yet authored.
