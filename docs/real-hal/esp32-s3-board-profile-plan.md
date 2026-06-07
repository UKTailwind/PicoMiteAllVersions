# ESP32-S3 Board Profile Plan: Generic Port + Board Profiles

## Goal

Build one ESP32-S3 port that boots sanely over USB Serial/JTAG on ordinary
ESP32-S3 hardware, keeps the existing Adafruit Metro ESP32-S3 support as a board
profile, and adds Freenove FNK0104A/B 2.8 inch ILI9341 Display support as
another board profile.

The shared ESP32-S3 runtime should remain common:

- USB Serial/JTAG console and MMBasic REPL.
- LittleFS-backed `A:`.
- SD-backed `B:` where the board has a socket.
- ESP-IDF WiFi/network stack.
- Existing keyboard stack, unchanged unless the Freenove profile proves a pin
  conflict on hardware.
- Shared audio synth/decoder path, with board-specific I2S wiring.
- Future local LCD support selected by board profile and `OPTION` state, not by
  forking the ESP32-S3 port.
- A generic serial-first profile that does not assume board peripherals. On
  unknown ESP32-S3 boards it should still boot, expose the REPL, mount `A:`,
  and allow WiFi configuration without touching SD, LCD, touch, audio, or LED
  pins.

## Source Facts

Freenove repository:
<https://github.com/Freenove/Freenove_ESP32_S3_Display>

Relevant Freenove ILI9341 files:

- `Tutorial_No_Touch/Sketches/Sketch_07.1_Music/Sketch_07.1_Music.ino`
- `Tutorial_No_Touch/Sketches/Sketch_11.1_LVGL/display.cpp`
- `Tutorial_With_Touch/Sketches/Sketch_11.1_Touch/Sketch_11.1_Touch.ino`
- `Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3.zip`

The Freenove README identifies four board variants. This plan targets only the
ILI9341 variants first:

- `FNK0104A`: 2.8 inch ILI9341, no touch.
- `FNK0104B`: 2.8 inch ILI9341, FT6336U touch.

The ST77922 and 4.0 inch ST7796 variants should be later board profiles because
their display and pin shape differs.

## Port And Board Profiles

Rename the ESP32-S3 port directory away from the Metro board name:

```text
ports/esp32_s3_metro -> ports/esp32_s3
```

The port name should describe the MCU family, not the first board used for
bring-up. Board behavior belongs in board profiles.

Build one ESP32-S3 firmware image. Do not create separate Metro and Freenove
firmware builds. The firmware should include a small board-profile table and
boot with conservative generic defaults until the user selects a profile from
MMBasic.

Use the existing MMBasic configuration surface:

```basic
CONFIGURE LIST
CONFIGURE METRO
CONFIGURE FREENOVE ILI9341
```

`CONFIGURE <profile>` is already an alias for `OPTION RESET <profile>` in the
shared command surface. The ESP32-S3 port should implement
`port_print_supported_boards()` and `port_factory_reset_board()` so this works
the same way as existing Pico/VGA factory profiles.

Use generic as the reset/default profile. The default must be conservative:
serial REPL, `A:`, and WiFi only. It should not claim board-specific SD, LCD,
touch, audio, or LED pins. `OPTION RESET` returns to this generic profile.

Create `ports/esp32_s3/esp32_board_profile.h` with profile macros for:

- Human-readable profile/device name.
- Stable profile id used by persisted options and driver lookup.
- Board-owned SD socket pins.
- Board-owned LCD pins.
- Touch controller pins.
- Audio sink type and pins.
- WS2812 / LED pins if needed.
- Pins that should be hidden or reserved from BASIC GPIO use while onboard
  peripherals are enabled.
- Whether each onboard peripheral exists at all on the selected profile.

Avoid duplicating the whole port. The selected board profile should provide
constants and optional peripheral setup data at runtime; it should not clone the
ESP32-S3 runtime or require a different firmware build.

## Pin Matrix

### Generic ESP32-S3

The generic profile is intentionally sparse.

| Peripheral | Default |
|---|---|
| Console | USB Serial/JTAG |
| `A:` | LittleFS |
| `B:` | disabled until a board profile supplies SD pins/backend |
| WiFi | enabled |
| LCD | disabled |
| Touch | disabled |
| Audio | off unless user configures generic audio pins explicitly |
| Onboard LED / RGB | disabled unless a profile supplies a pin |

Acceptance for unknown ESP32-S3 hardware: firmware boots to the REPL without
requiring SD, LCD, audio, touch, or board LED wiring.

### Metro

Existing SD SPI backend:

| Signal | GPIO |
|---|---:|
| SD SCLK | 39 |
| SD MOSI | 42 |
| SD MISO | 21 |
| SD CS | 45 |

Existing default generic I2S:

| Signal | GPIO |
|---|---:|
| BCLK | 5 |
| WS | 6 |
| DOUT | 7 |

### Freenove ILI9341

LCD from Freenove TFT_eSPI setup:

| Signal | GPIO |
|---|---:|
| LCD SCLK | 12 |
| LCD MOSI | 11 |
| LCD MISO | 13 |
| LCD CS | 10 |
| LCD DC | 46 |
| LCD RST | -1 |
| LCD Backlight | 45 |
| SPI frequency | 40 MHz |

Touch from Freenove FT6336U example:

| Signal | GPIO |
|---|---:|
| I2C SDA | 16 |
| I2C SCL | 15 |
| INT | 17 |
| RST | 18 |

SD socket from Freenove examples:

| SD Contact | GPIO | SPI-mode role |
|---|---:|---|
| CLK / SCK | 38 | SCLK |
| CMD | 40 | MOSI |
| D0 | 39 | MISO |
| D1 | 41 | unused in SPI mode |
| D2 | 48 | unused in SPI mode |
| D3 | 47 | CS |

Freenove examples use `SD_MMC.setPins(...)`, but the physical microSD card can
also be driven in SPI mode. First implementation should reuse the existing
SDSPI FatFs backend with the SPI-mode mapping above. If hardware smoke shows the
card does not initialize reliably, add a board-profile-selected SDMMC diskio
backend as a second step.

Audio from Freenove music example:

| Signal | GPIO |
|---|---:|
| I2S MCLK | 4 |
| I2S BCLK | 5 |
| I2S DIN | 6 |
| I2S DOUT | 8 |
| I2S WS | 7 |
| Amp enable | 1, active low in Freenove sketch |
| Codec I2C SDA | 16 |
| Codec I2C SCL | 15 |
| Codec I2C address | ES8311 `0x18` |

The touch controller and ES8311 share the same I2C pins on Freenove ILI9341.

## Implementation Phases

### Phase 1: Board Profile Scaffold

- Rename `ports/esp32_s3_metro` to `ports/esp32_s3`.
- Rename build output and CI artifact names away from `metro` where they refer
  to the port.
- Add `esp32_board_profile.h`.
- Add one compiled board-profile table containing generic, Metro, and Freenove
  ILI9341 entries.
- Add `port_set_default_options()` generic defaults: USB Serial/JTAG REPL,
  `A:`, WiFi-capable, no board-owned peripheral pins.
- Add `port_print_supported_boards()` entries for:
  - `GENERIC`
  - `METRO`
  - `FREENOVE ILI9341`
- Add `port_factory_reset_board()` support for `METRO` and `FREENOVE ILI9341`
  using the existing `CONFIGURE <profile>` / `OPTION RESET <profile>` path.
- Store the selected profile in persisted options using a stable id, not by
  guessing from pin numbers. Keep `Option.platform` as the human-readable name
  if that remains the local convention.
- Move current Metro SD and audio defaults into the Metro factory profile.
- Add a generic profile with no board-owned SD/LCD/touch/audio pins.
- Add Freenove ILI9341 profile constants.
- Update `HAL_PORT_DEVICE_NAME` and banner/profile strings from the selected
  profile.

Acceptance:

- Single ESP32-S3 build boots as generic ESP32-S3 over USB Serial/JTAG.
- Generic defaults expose the REPL, `A:`, and WiFi without requiring known board
  peripherals.
- `CONFIGURE LIST` shows generic, Metro, and Freenove profiles.
- `CONFIGURE METRO` produces the same defaults as the current Metro build.
- `CONFIGURE FREENOVE ILI9341` applies Freenove board-specific constants.
- `OPTION RESET` returns to generic board defaults.
- No local display or audio behavior changes yet.

### Phase 2: SD `B:` Drive

- Replace Metro-specific SD macros in `esp32_sd_diskio.c` with profile macros.
- Keep the existing SDSPI backend initially.
- For Freenove ILI9341, map:
  - `SCLK=38`
  - `MOSI=40`
  - `MISO=39`
  - `CS=47`
- Consider enabling pull-ups on CMD/D0/D3 and possibly D1/D2 if the IDF SDSPI
  path does not do enough for this board.

Acceptance:

- Generic profile leaves `B:` disabled or reports it unsupported cleanly.
- Metro `B:` still mounts on current hardware.
- Freenove `B:` mounts and passes:
  - `FILES "B:"`
  - write/read/delete small BASIC file.
  - run a program from `B:`.
- If SDSPI fails on Freenove, add an SDMMC diskio backend behind the same board
  profile instead of changing BASIC filesystem behavior.

### Phase 3: Keyboard

- Keep the existing keyboard stack unchanged.
- Audit Freenove profile pins against the keyboard pins used by the target
  carrier setup.
- If this is being used in a PicoCalc-style assembly, prefer keeping the same
  I2C keyboard path and only change pins if the hardware requires it.
- Reserve keyboard pins at runtime through the existing option/pin reservation
  mechanism rather than baking keyboard policy into the generic ESP32 pin table.

Acceptance:

- Generic profile leaves keyboard behavior serial-first and does not claim
  keyboard pins.
- Existing keyboard behavior is unchanged for Metro.
- Freenove profile does not reserve or consume keyboard pins accidentally.
- If keyboard is attached, console input works before display integration.

### Phase 4: WiFi

- Leave WiFi common. ESP32-S3 WiFi is on-chip, not board-wired like CYW43.
- Keep current ESP-IDF WiFi stack, NVS credentials, TCP/UDP/NTP/MQTT/Telnet/Web
  command implementation.
- Verify that added display/SD/audio drivers do not starve internal DRAM enough
  to break WiFi.

Acceptance:

- `WEB SCAN`
- `OPTION WIFI ...`
- `WEB CONNECT`
- `PRINT MM.INFO$(IP ADDRESS)`
- Existing network conformance smoke still passes with generic defaults and
  after `CONFIGURE METRO` / `CONFIGURE FREENOVE ILI9341`.

### Phase 5: ILI9341 Display

Add a local SPI LCD backend for ESP32-S3 rather than pulling in Arduino
TFT_eSPI.

Recommended path:

- Use ESP-IDF SPI master for the LCD bus.
- Use a small ILI9341 init sequence derived from existing repo display drivers
  where possible.
- Use profile pins for SCLK/MOSI/MISO/CS/DC/RST/BL.
- Treat backlight as a board-controlled output, default on.
- Integrate with existing `Draw.c` function pointers:
  - `DrawPixel`
  - `DrawRectangle`
  - `DrawBitmap`
  - `ReadBuffer` if practical.
  - `ScrollLCD`
- Prefer a framebuffer or line-buffered flush path first. Optimize later.
- Add `OPTION LCDPANEL` support only after the physical driver works from a
  profile default. The first board bring-up can auto-configure the built-in
  panel for `freenove_ili9341`.

Important constraint:

- The existing SPI-LCD drivers are Pico-centric in places. Use their controller
  knowledge, but do not assume RP2040 SPI/DMA APIs work on ESP32.

Acceptance:

- Freenove boots to a usable local LCD console or local display surface.
- `CLS`, `TEXT`, `LINE`, `BOX`, `PIXEL`, `COLOUR` render correctly.
- Web console framebuffer remains available and does not conflict with the
  local LCD.
- Generic and Metro remain serial/web-console only unless a display profile is
  selected.

### Phase 6: Touch

Do not block the initial display port on touch.

For FNK0104B:

- Add FT6336U I2C driver on `SDA=16`, `SCL=15`.
- Use `INT=17`, `RST=18` if needed.
- Share the I2C bus with ES8311.
- Integrate through existing GUI/touch abstractions only after basic display
  output is stable.

Acceptance:

- Touch init does not break ES8311.
- Touch coordinates can be read and transformed for display rotation.
- GUI/touch BASIC surface can be enabled later without changing board profile.

### Phase 7: Audio

Freenove ILI9341 audio is I2S into an ES8311 codec. Treat that as a named
audio profile selected by BASIC:

```basic
OPTION AUDIO FREENOVE
```

Do not make plain generic I2S implicitly initialize the Freenove codec. Generic
I2S and the Freenove onboard codec path are different user intents even though
they use the same ESP32 I2S transport.

Implementation:

- Add an ESP32 audio profile interface compiled into the ESP32 port:
  - profile name for `OPTION AUDIO`.
  - transport kind (`I2S` for Freenove).
  - I2S pins, including optional `MCLK` and explicit `WS`.
  - extra pins to reserve.
  - `init()` hook for codec / amplifier board bring-up.
  - optional `deinit()` hook for shutdown or `OPTION AUDIO DISABLE`.
- Keep the PCM generation, decoder path, ring buffer, and I2S writer in the
  existing ESP32 audio backend. The Freenove driver should only provide setup
  and pin/config data.
- Add a compiled profile table selected by board config/source list:
  - Generic profile has no board-specific audio profile entries.
  - Metro profile has no `FREENOVE` entry.
  - Freenove ILI9341 profile includes `FREENOVE`.
- Avoid runtime board checks such as `if (board == freenove)` in the main audio
  backend. The main backend asks the selected audio profile for hooks and pin
  configuration.
- Extend persisted audio options enough to distinguish:
  - generic `OPTION AUDIO I2S ...`
  - generic `OPTION AUDIO PDM ...`
  - named `OPTION AUDIO FREENOVE`
- Add explicit stored fields or equivalent for `audio_profile`, `audio_i2s_ws`,
  and `audio_i2s_mclk`. Do not infer `FREENOVE` from matching pin numbers.
- Extend ESP32 I2S backend to support explicit `WS`, not only `WS=BCLK+1`.
- Add optional MCLK support for the I2S std config.
- For `OPTION AUDIO FREENOVE`, use:
  - `MCLK=4`
  - `BCLK=5`
  - `WS=7`
  - `DOUT=8`
  - reserve `DIN=6` only if later microphone/input support needs it.
- Add `esp32_audio_profile_freenove_es8311.c`:
  - initialize I2C port 0 on `SDA=16`, `SCL=15`, 400 kHz.
  - initialize ES8311 at address `0x18`.
  - reset/configure clocks and I2S format.
  - power up DAC/output.
  - set volume and unmute.
  - drive `AP_ENABLE=1` to the level Freenove uses (`LOW`) during codec
    bring-up.
- Keep microphone/input support out of scope initially even though `DIN=6`
  exists.

Acceptance:

- `OPTION AUDIO FREENOVE` is accepted only when the Freenove audio profile is
  compiled in.
- `OPTION AUDIO FREENOVE` followed by `PLAY TONE`, `PLAY SOUND`, and file
  playback produces audio on the Freenove speaker output.
- `OPTION AUDIO I2S ...` remains generic and does not initialize ES8311.
- Generic ESP32-S3 profile audio remains off unless configured by explicit generic
  audio options.
- Metro external-I2S/PDM behavior is unchanged.
- `MM.INFO$(AUDIO)` distinguishes `FREENOVE`, `I2S`, `PDM`, and `OFF`.
- `OPTION AUDIO DISABLE` does not initialize ES8311 and releases or avoids
  claiming the onboard audio pins.

### Phase 8: Pin Ownership And Options

Do not permanently mark every Freenove onboard pin `UNUSED` in `PinDef[]` unless
the feature is always enabled.

Policy:

- Pins for enabled fixed onboard peripherals should be reserved at runtime.
- Pins for disabled optional peripherals should remain available.
- `OPTION AUDIO DISABLE` should release or avoid claiming audio pins.
- `OPTION LCDPANEL DISABLE` or equivalent should release display pins if that
  option exists for the ESP32 display path.
- Shared I2C pins for touch/audio must be handled as a shared bus, not as two
  independent pin owners.

Acceptance:

- `SETPIN` rejects pins currently owned by board peripherals.
- Disabling a board peripheral leaves pins usable where safe.
- `MM.INFO$(PIN GPn)` or equivalent reports a useful owner state if the current
  pin state machinery supports it.

## Build And Configuration Smoke Matrix

There should be one firmware build:

```sh
idf.py build
```

The matrix below is a runtime configuration smoke matrix for that one image.

### Generic ESP32-S3 Defaults

Smoke:

- Boot banner and USB REPL.
- `PRINT MM.VER`
- `FILES "A:"`
- `FILES "B:"` fails cleanly or reports unavailable.
- WiFi scan/join if antenna/hardware allows normal ESP32-S3 WiFi operation.
- No SD/LCD/touch/audio pins are reserved by default.

### Metro

Smoke:

- `CONFIGURE METRO`
- Reboot into Metro profile.
- Boot banner and REPL.
- `PRINT MM.VER`
- `FILES "A:"`
- `FILES "B:"` with SD inserted.
- `PLAY TONE` on existing external I2S/PDM setup.
- WiFi/web-console smoke.

### Freenove ILI9341

Smoke order:

1. `CONFIGURE FREENOVE ILI9341`.
2. Reboot into Freenove profile.
3. Boot banner and USB REPL.
4. `FILES "A:"`.
5. SD `B:` mount over SDSPI.
6. WiFi join and `MM.INFO$(IP ADDRESS)`.
7. LCD clear/text/graphics.
8. Audio tone through onboard speaker output.
9. Touch read, if FNK0104B.

## Open Questions

- Does Freenove ILI9341 SD socket behave reliably in SPI mode on these pins, or
  does it require SDMMC mode for this PCB?
- Is `AP_ENABLE=LOW` always the required speaker-output enable state, or does it
  vary across FNK0104A and FNK0104B?
- Does ES8311 need 16 kHz init as in Freenove's `EXAMPLE_SAMPLE_RATE`, or should
  we initialize it directly for MMBasic's 44.1 kHz output path?
- Should `OPTION AUDIO FREENOVE` expose a volume default distinct from MMBasic's
  existing playback volume, or should the ES8311 profile set one fixed codec
  gain and leave user volume in the existing software path?
- Should the Freenove display auto-enable as console on first boot, or should it
  require an `OPTION LCDPANEL` command?
- Should CI publish one generic ESP32-S3 image only, or also include generated
  helper text showing the `CONFIGURE` commands for supported boards?

## Recommended First PR

Keep the first PR small:

- Rename the port directory and build target from Metro-specific naming to
  generic ESP32-S3 naming.
- Add the board profile table and profile header.
- Add the conservative generic default profile.
- Add `CONFIGURE LIST`, `CONFIGURE METRO`, and
  `CONFIGURE FREENOVE ILI9341` through `port_print_supported_boards()` and
  `port_factory_reset_board()`.
- Move Metro SD/audio constants into the Metro runtime profile.
- Add Freenove ILI9341 constants.
- Make SD backend use profile pins in SDSPI mode.
- Make default I2S WS use profile macro rather than `BCLK+1`.
- Add the audio-profile abstraction and `OPTION AUDIO FREENOVE` parser path,
  but it can initially error cleanly if the ES8311 profile source is not linked.
- Document Freenove display/audio/touch pins.

Do not include LCD rendering, FT6336U, or the full ES8311 register driver in
that first PR. Those should follow once the profile split is proven by clean
generic, Metro, and Freenove builds.
