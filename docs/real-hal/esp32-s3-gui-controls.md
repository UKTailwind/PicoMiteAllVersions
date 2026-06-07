# ESP32-S3 GUI Controls

This page records the current ESP32-S3 general-port GUI control status. It
pairs with the implementation plan in
[esp32-gui-controls-adapter-plan.md](esp32-gui-controls-adapter-plan.md).

## Status

The ESP32-S3 general port now links the shared PicoMite GUI control
implementation from `drivers/gui_controls/GUI.c` through the shared
`gui_controls_real.c` wrapper. ESP32-specific code supplies adapters for the
Freenove display, FT6336U capacitive touch controller, timing, and option
state. The GUI parser, control state, drawing commands, redraw behavior, and
`CTRLVAL` handling are shared; there is no ESP32 fork of `GUI.c`.

The Freenove 2.8 inch capacitive touch board has smoke coverage for:

- `GUI BUTTON`
- `GUI SWITCH`
- `GUI CHECKBOX`
- `GUI RADIO`
- `GUI LED`
- `GUI FRAME`
- `GUI CAPTION`
- `GUI NUMBERBOX`
- `GUI TEXTBOX`
- `GUI FORMATBOX`
- `GUI SPINBOX`
- `GUI DISPLAYBOX`
- `GUI GAUGE`
- `GUI BARGAUGE`
- `GUI AREA`
- `CTRLVAL`
- `MSGBOX` as a manual touch test

The automated suites run the control programs through both `RUN` and `FRUN`
where the control can complete without a physical finger press. `MSGBOX` is
kept as a manual test because the shared implementation blocks until a real
button touch is received.

## Touch Model

There are two related surfaces:

- Direct `TOUCH()` functions report the FT6336U state. This is useful for
  free-form touch programs and for checking raw down/up/ref transitions.
- GUI controls consume the same FT6336U data through the shared GUI touch
  adapter. `TOUCH(LASTREF)` reports the last GUI control reference touched.

On the Freenove board, the FT6336U reports coordinates that are already mapped
to the active display orientation. The ESP32 adapter therefore bypasses the
legacy resistive calibration workflow used by older PicoMite SPI touch panels.

Do not treat `GUI CALIBRATE` as a required setup step on this ESP32 port. The
current ESP32 Freenove path does not persist or apply legacy calibration
coefficients, and `OPTION TOUCH ...` pin calibration setup from the PicoMite
manual is not the configuration mechanism for this board profile.

## Smoke Tests

The phase-specific smoke runners are:

- `porttools/esp32_gui_basic_controls_smoke.py`
- `porttools/esp32_gui_text_controls_smoke.py`
- `porttools/esp32_gui_gauges_msgbox_smoke.py`

`porttools/esp32_gui_controls_smoke.py` runs them in order:

```sh
python3.11 porttools/esp32_gui_controls_smoke.py \
  --target /dev/cu.usbmodem2101
```

Use `--dry-run` to print the commands without opening serial, telnet, or the
board:

```sh
python3.11 porttools/esp32_gui_controls_smoke.py --dry-run
```

The text and gauge suites also support `--target telnet:<host>[:port]`. The
basic-controls suite currently uses the serial-only helper.

Each suite uploads small BASIC programs to the selected drive. By default the
manual touch programs are left in place for hand testing; pass `--cleanup` to
remove generated files after the automated checks.

## Example

`examples/freenove_touch_gui_controls.bas` is a short Freenove touch drawing
program using `GUI BUTTON`, `GUI SWITCH`, `GUI LED`, `GUI AREA`, direct
`TOUCH()`, and `TOUCH(REF)`.

Before running the example, make sure the board has enough GUI slots:

```basic
OPTION GUI CONTROLS 12
```

Then upload the file to `A:` or `B:` and run it from the BASIC prompt.

## Known Gaps

- Legacy resistive touch calibration is not a Freenove setup path.
- `MSGBOX` completion is manual in the smoke suite because there is no
  synthetic touch injection hook.
- Attached USB keyboard coverage for text controls remains a manual hardware
  check; serial and telnet console edit paths are covered by smoke tests.
