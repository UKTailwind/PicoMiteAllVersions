# ESP32-S3 GUI Controls Adapter Plan

## Current State

- Shared PicoMite GUI control code still exists in `drivers/gui_controls/GUI.c`.
- The shared HAL-facing wrapper exists in `drivers/gui_controls/gui_controls_real.c`.
- The ESP32-S3 general port currently links `gui_controls_stub.c` and `gui_touch_stub.c`.
- ESP32-S3 Freenove touch support exists as direct `TOUCH()` functions backed by the FT6336U controller.
- The current ESP32-S3 touch path is suitable for raw touch coordinates, but it is not yet wired into the legacy GUI control stack.

## Goal

Enable the shared PicoMite GUI controls on the ESP32-S3 general port without copying the GUI implementation.

The intended command/function surface is:

- `GUI BUTTON`
- `GUI SWITCH`
- `GUI CHECKBOX`
- `GUI RADIO`
- `GUI LED`
- `GUI FRAME`
- `GUI NUMBERBOX`
- `GUI TEXTBOX`
- `GUI FORMATBOX`
- `GUI SPINBOX`
- `GUI DISPLAYBOX`
- `GUI CAPTION`
- `GUI GAUGE`
- `GUI BARGAUGE`
- `GUI AREA`
- `CTRLVAL`
- `MSGBOX`

## Design Principles

- Do not copy or fork `drivers/gui_controls/GUI.c`.
- Keep hardware details behind adapter or HAL boundaries.
- Reuse shared GUI control parsing, state management, drawing, and event handling.
- Keep ESP32-specific code limited to touch, display, timing, audio click, and option-state adapters.
- Prefer a generic adapter shape where possible so other mapped capacitive-touch ESP32-S3 boards can reuse it.

## Required Adapter Work

- Link `GUI.c` and `gui_controls_real.c` for the ESP32-S3 general port instead of `gui_controls_stub.c`.
- Replace `gui_touch_stub.c` on ESP32-S3 with an adapter that bridges FT6336U reads into the legacy GUI touch expectations:
  - `GetTouch(GET_X_AXIS)`
  - `GetTouch(GET_Y_AXIS)`
  - `TouchState`
  - `TouchDown`
  - `TouchUp`
  - `TOUCH_DOWN`
- Decide how calibration should behave for capacitive touch panels that already return mapped display coordinates.
  - Preferred starting behavior: bypass resistive calibration for Freenove FT6336U and report already-mapped coordinates.
  - Preserve enough `Option.TOUCH_*` state to keep shared GUI code and option validation stable.
- Define click/beep behavior.
  - Initial implementation may be silent or route through the existing ESP32 audio adapter when audio is configured.
  - The adapter should not require audio for GUI controls to work.
- Audit timer and routine hooks used by the shared GUI stack.
  - Ensure touch polling or interrupt processing updates the expected shared state.
  - Keep GUI event dispatch compatible with the interpreter scheduler.
- Confirm display interaction.
  - GUI drawing should use the existing shared graphics/framebuffer paths.
  - Console echo and editor behavior should remain stable after GUI commands run.

## Implementation Phases

### Phase 1: Inventory and Build Wiring

- Identify every symbol required when ESP32 links `GUI.c`, `gui_controls_real.c`, and the real touch helper code.
- Replace GUI control stubs with shared GUI control sources in the ESP32-S3 build.
- Add only the minimal ESP32 adapter symbols needed to compile.

Exit criteria:

- ESP32-S3 firmware builds with shared GUI control code linked.
- Existing serial, SD, display, framebuffer, touch, WiFi, and telnet smoke tests still pass.

### Phase 2: Touch Adapter

- Bridge Freenove FT6336U reads to the legacy GUI touch API.
- Map down/up transitions into `TouchState`, `TouchDown`, and `TouchUp`.
- Confirm coordinate orientation matches the active display rotation.
- Keep direct `TOUCH()` behavior unchanged.

Exit criteria:

- A small BASIC program can print `TOUCH(X)`, `TOUCH(Y)`, `TOUCH(DOWN)`, and GUI touch state consistently.
- Touch down/up transitions are detected once per gesture, not continuously retriggered.

### Phase 3: Basic Controls

- Enable simple controls first:
  - `GUI BUTTON`
  - `GUI SWITCH`
  - `GUI CHECKBOX`
  - `GUI RADIO`
  - `GUI LED`
  - `GUI FRAME`
  - `GUI CAPTION`
- Verify that drawing, redraw, touch hit testing, and `CTRLVAL` work.

Exit criteria:

- A smoke program creates each basic control, updates it, touches it, and reads expected values with `CTRLVAL`.
- The program runs from both `RUN` and `FRUN`.

### Phase 4: Text and Numeric Controls

- Enable:
  - `GUI NUMBERBOX`
  - `GUI TEXTBOX`
  - `GUI FORMATBOX`
  - `GUI SPINBOX`
  - `GUI DISPLAYBOX`
- Check keyboard interaction over USB serial, telnet, and any attached USB keyboard path.

Exit criteria:

- A smoke program enters and edits values in text/numeric controls.
- Control focus, redraw, and value retrieval behave consistently across console backends.

### Phase 5: Gauges, Areas, and Dialogs

- Enable:
  - `GUI GAUGE`
  - `GUI BARGAUGE`
  - `GUI AREA`
  - `MSGBOX`
- Verify screen repaint behavior and interaction with framebuffer/FASTGFX where applicable.

Exit criteria:

- A smoke program creates gauges and areas, updates them repeatedly, and displays a message box.
- No display corruption occurs after exiting the GUI program back to the REPL.

### Phase 6: Regression and Documentation

- Add ESP32 GUI control smoke tests under the existing port tooling.
- Document the ESP32-S3 GUI support status and any unsupported legacy calibration commands.
- Add a short example program for Freenove touch GUI controls.

Exit criteria:

- ESP32-S3 GUI smoke tests pass on the Freenove board.
- Existing non-GUI ESP32-S3 smoke tests still pass.
- The docs clearly distinguish direct `TOUCH()` support from full GUI control support.

## Smoke Tests

- `gui_basic_controls.bas`: creates button, switch, checkbox, radio, LED, frame, and caption controls; verifies touch and `CTRLVAL`.
- `gui_text_controls.bas`: creates numberbox, textbox, formatbox, spinbox, and displaybox; verifies keyboard/touch editing.
- `gui_gauges_msgbox.bas`: creates gauge, bargauge, area, and message box; verifies redraw and REPL recovery.
- `touch_transition_smoke.bas`: verifies raw FT6336U touch down/up transitions and mapped coordinates.
- Run each smoke program through `RUN` and `FRUN` where supported.
- Run console smoke after GUI tests to catch display-console side effects.

## Risks and Open Questions

- The shared GUI stack may contain Pico-era assumptions about display state, touch calibration, timers, or keyboard focus.
- `Option.TOUCH_*` calibration state may be required even when capacitive touch is already mapped.
- Display console, editor, framebuffer, and GUI redraw paths may compete for screen state.
- Some GUI controls may need more RAM than expected; PSRAM use and fragmentation should be monitored.
- Touch interrupt handling may be preferable, but polling is simpler for the first adapter.
- The local reference docs mention an Advanced Graphics PDF, but that PDF is not currently present under `docs/reference`.
- `MSGBOX` and text-entry controls may need careful testing across USB serial, telnet, and attached keyboard input.
