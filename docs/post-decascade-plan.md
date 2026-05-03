# Post-decascade plan: clean mix-and-match ports

## Goal

The `hal-decascade` plan eliminated three target macros (`PICOMITEWEB`,
`HDMI`, `GUICONTROLS`) as build flags but stopped there. The actual
target is bigger:

- **Any subset of hardware capabilities composes into a port without
  silent breakage.** Pick from {SPI-LCD, VGA-PIO, HDMI/DVI, WiFi,
  GUICONTROLS / touch panel, PSRAM, I²S, USB-host keyboard, PS/2
  keyboard, multicore scanout, …}, set the matching `HAL_PORT_HAS_*`
  flags in `port_config.h`, and the build either produces a working
  firmware or fails loudly with a localised error. No silent struct
  layout corruption, no link-time symbol roulette.
- **Core code has zero `#ifdef` on hardware concerns.** Everything
  hardware-shaped lives behind a `HAL_PORT_HAS_*` flag, a port hook,
  or a per-port driver in `drivers/<feature>/`.
- **One port = one PCB.** A "port" is the firmware for a specific
  physical board. New ports don't ship with `OPTION RESET <BOARD>`
  menus that pick between alternative wirings — the firmware was
  compiled for that PCB and already knows the wiring.
- **OPTION setters stay** as a debug / bring-up surface. Changing the
  HDMI pin assignment via `OPTION HDMI PINS` should still work for
  the rare case where the silkscreen is wrong and the user wants to
  test a workaround without rebuilding. Persistence still works.
  But the *factory-reset-to-board-profile* mechanism dies for new
  ports — there is no profile to reset to.
- **Existing legacy multi-board ports stay as they are.**
  `ports/pico/` ships with Game*Mite / PicoCalc / Pico-ResTouch / etc.
  profiles in its `port_factory_reset_board`. `ports/hdmi_rp2350/`
  ships with HDMIUSB / PICO COMPUTER / HDMIUSBI2S. Real users
  `OPTION RESET` against those today; don't break them. Just don't
  propagate the pattern to new ports.

The decascade plan's opening line — *"a custom build can mix any
subset of hardware features without inventing a new top-level macro
and a new `COMPILE=` enum value"* — was a sales pitch, not a spec.
This plan makes it the spec.

## Current state

After `hal-decascade`'s 11 commits + the F1 / pico_stretch port (in
flight as uncommitted changes against the same branch):

- `PICOMITEWEB`, `HDMI`, `GUICONTROLS` are gone from source as
  preprocessor gates and from CMake as build flags. ✓
- Per-port `port_sources.cmake` snippets own source-list inclusion +
  per-port build config. ✓
- Per-port `port_config.h` files own heap / flash / CPU / MMBasic-cap /
  PSRAM / pin counts / PIO claims. ✓
- 14 device variants build clean via `buildall.sh` including F1
  `DVIWIFIRP2350`. ✓

What's still wrong (the audit's findings):

- **Four target macros remain in core** as `#ifdef` gates: `PICOMITEVGA`
  (~80 sites), `PICOMITE` (~30 sites), `USBKEYBOARD` (~20 sites),
  `PICOCALC` (~10 sites). Plus `rp2350` chip-level (~100 sites) which
  is a different concern (chip variant, not port-config) and arguably
  legitimate. Total: ~239 directives.
- **`struct option_s` layout is gated on `PICOMITEVGA`** — `FileIO.h`
  uses `#ifndef PICOMITEVGA / #else` to overlay
  `TOUCH_XSCALE/YSCALE/XZERO/YZERO` (non-VGA) with
  `Height/Width/dummy[12]` (VGA). A future port shape combining
  WiFi + VGA + touch would silently corrupt
  `Option.TCP_PORT/ServerResponceTime` (which sit at the same offsets
  as the touch fields).
- **`PICOMITEVGA` conflates four orthogonal capabilities**: the
  VGA-PIO scanout family, the QVGA tile-array structure, the
  display-mode RESOLUTION command set, and the absence of SPI-LCD
  touch fields in `Option`. They should each be their own
  `HAL_PORT_HAS_*` flag.
- **`PICOMITE` similarly conflates** the SPI-LCD subsystem, the
  SSD1963 backlight path, the merge-pipeline core1 loop, and the
  FASTGFX scanline-diff DMA path.
- **`USBKEYBOARD` is the last COMPILE-name string-match in
  `port_sources.cmake`** (`if (COMPILE STREQUAL "*USB")`). Every port
  duplicates the pattern. Should be `HAL_PORT_HAS_USB_KEYBOARD`.
- **`PICOCALC` is a per-board feature flag treated as a global
  CMake variable** (`-DPICOCALC=true|false`). For new single-board
  ports it should be just another port-config flag.
- **`port_pinno_alias_for_name` and friends have no HAL contract.**
  Each port reinvents them; the `hdmi_rp2350` version even has
  WiFi-flavoured aliases despite the port not having WiFi (leftover
  cruft).
- **No `set(PORT pico_stretch)` selector.** The CMake top-level still
  picks ports via the `COMPILE` enum + a `COMPILE → PORT_DIR` map.
  Stage E2e was supposed to drop this; deferred.

## Stages

Each stage independently committable, leaving `buildall.sh` green at
every step.

### Status (2026-05-02)

| Stage | Subject | Status | Commit |
|---|---|---|---|
| P1 | `struct option_s` layout independence | DONE | `8789d29` |
| P2 | `PICOMITEVGA` → `HAL_PORT_IS_VGA` (+ allies) | DONE | `d761d2d` |
| P3 | `PICOMITE` → `HAL_PORT_HAS_PICOMITE` (+ GUICONTROLS HAL) | DONE | `4b62a1f` |
| P4 | `USBKEYBOARD` → `HAL_PORT_HAS_USB_KEYBOARD` | DONE | `eb629c9` |
| P5 | `PICOCALC` → `HAL_PORT_HAS_I2C_KEYPAD` | DONE | `ea74496` |
| P6 | `PORT` replaces `COMPILE` as the canonical selector | DONE | `5695f85` |
| P7 | Docs + buildall.sh accept direct `-DPORT` names | DONE | `8f9cfb0` |
| P8 | Specify the `port_*()` HAL contract | TODO | — |
| P9 | Validation matrix (novel feature combinations) | partial | F1 + F2 |

P9 is partial: F1 (`dvi_wifi_rp2350` — HDMI + WiFi + USB-host kbd +
I²S + PSRAM) and F2 (`vga_wifi_rp2350` — VGA-PIO + WiFi) already
validate two of the three combinations the plan calls for. The
third (SPI-LCD + WiFi + USB-host keyboard) hasn't been tried yet
but the F1/F2 ports prove the per-port composition machinery works
in practice.

### Stage P1 — `struct option_s` layout independence

The single most dangerous coupling — a wrong feature combination
silently corrupts persisted Option values. Fix first.

P1.1. **Survey every union / `#ifdef`-gated field in `struct option_s`**
      (`FileIO.h`). The known offenders:
      - `#ifndef PICOMITEVGA` overlay of touch (XSCALE/YSCALE/XZERO/YZERO)
        vs VGA tile (Height/Width/dummy[12]) at offset ~80
      - `#if defined(PICOMITE) && defined(rp2350)` extra LCD_CLK/MOSI/MISO
        fields at offset ~64
      - `#ifdef PICOMITEWEB` (now decascaded but the gates are still
        there as `HAL_PORT_HAS_WIFI`-equivalent) for TCP_PORT /
        ServerResponceTime at offset ~96
      - `#ifdef PICOMITEVGA` again for X_TILE / Y_TILE
      - `#ifdef PICOCALC` for KEYBOARDBL

P1.2. **Decide layout policy.** Two options:
      a) Make every field always present. Cost: ~30 bytes per port of
         wasted persisted `Option` storage. Benefit: guaranteed
         layout-stability across any feature combination.
      b) Keep the unions but version the `Option` blob — store a
         `layout_version` byte and migrate / refuse on mismatch.
         Cost: persistence-format complexity.
      Recommendation: option (a) for simplicity. ~30 bytes per port
      is negligible against `MagicKey` already being a full 32-bit
      cookie.

P1.3. **Rewrite `struct option_s` as a flat layout with no `#ifdef`s.**
      Bump every existing port's `HAL_PORT_MAGIC_KEY` so old persisted
      Option blobs are auto-replaced by defaults on first boot.

P1.4. **Acceptance**: `grep -E '^#(if|ifdef|ifndef)' FileIO.h` returns
      empty inside `struct option_s`. All 14 ports build clean. Boot
      a representative rp2040 + rp2350 target on real hardware,
      verify `OPTION LIST` output is preserved.

### Stage P2 — Decompose `PICOMITEVGA`

`PICOMITEVGA` is ~80 sites across ~25 files conflating four things:
the VGA-PIO scanout family, QVGA tile-array runtime structure, the
RESOLUTION/VGA PINS/DEFAULT MODE OPTION setters, and the absence of
SPI-LCD touch in `Option`. After P1, the touch issue is gone. The
remaining three need to split.

P2.1. **Replace `#ifdef PICOMITEVGA`** with the appropriate
      `HAL_PORT_HAS_VGA_PIO` / `HAL_PORT_HAS_NEXTGEN_DISPLAY` /
      `HAL_PORT_HAS_QVGA_TILE_GRID` (new flag) check, file by file.
      Most sites collapse to `HAL_PORT_HAS_VGA_PIO`.
P2.2. **Drop `-DPICOMITEVGA`** from per-port `port_sources.cmake`
      snippets. Ports declare the underlying capabilities instead.
P2.3. **Acceptance**: `grep -rE '\bPICOMITEVGA\b' --include=*.c
      --include=*.h` returns empty outside `PicoCFunctions.h` (user
      template) and historical comments. All 14 ports build clean.

### Stage P3 — Decompose `PICOMITE`

`PICOMITE` is ~30 sites conflating the SPI-LCD subsystem,
SSD1963/SSD1306 backlight, merge-pipeline core1 loop, and FASTGFX
DMA path. Symmetric with P2.

P3.1. **Add palette flags**: `HAL_PORT_HAS_SPI_LCD`,
      `HAL_PORT_HAS_FASTGFX`, `HAL_PORT_HAS_DISPLAY_MERGE_CORE1`.
      (The merge core1 loop and FASTGFX both today gate on PICOMITE
      because they're SPI-LCD-only, but they could in principle ship
      independently.)
P3.2. **Replace `#ifdef PICOMITE`** site by site.
P3.3. **Drop `-DPICOMITE`** from per-port snippets where possible.
      Some sites in `drivers/spi_lcd/spi_lcd.c` may need to stay
      until `spi_lcd.c` itself splits into per-display-family driver
      files (e.g. drivers/ili9341/, drivers/st7789/) — that's a
      follow-on.
P3.4. **Acceptance**: `grep -rE '\bPICOMITE\b'` returns only
      historical comments and non-source uses.

### Stage P4 — Promote `USBKEYBOARD` to a port-config flag

The last COMPILE-name string-match.

P4.1. **Add `HAL_PORT_HAS_USB_KEYBOARD`** to every `port_config.h`
      (0 for PS/2, 1 for USB-host).
P4.2. **Replace `if (COMPILE STREQUAL "*USB")` in every
      `port_sources.cmake`** with `if (HAL_PORT_HAS_USB_KEYBOARD)`.
      This requires CMake to know the port-config flag value. Cleanest:
      have each port_sources.cmake set a CMake-level
      `set(HAL_PORT_HAS_USB_KEYBOARD true)` at its top, mirroring its
      port_config.h value. Two sources of truth, kept in sync by hand.
P4.3. **Replace `#ifdef USBKEYBOARD` in port-impl files** with
      `#if HAL_PORT_HAS_USB_KEYBOARD`. Core never sees USBKEYBOARD.
P4.4. **Drop `-DUSBKEYBOARD`** from snippets — set it implicitly
      from the port-config flag.
P4.5. **Existing dual-shipping ports** (`hdmi_rp2350` ships HDMI +
      HDMIUSB, `pico` ships PICO + PICOUSB, etc.) split into separate
      directories: `hdmi_rp2350/` (PS/2) and `hdmi_usb_rp2350/` (USB).
      ~5 new directories, mostly pin_tables.c + port_defaults.c
      duplicated with the keyboard backend changed.
P4.6. **Acceptance**: `grep -rE '\bUSBKEYBOARD\b'` returns empty
      outside port_config.h definitions.

### Stage P5 — Promote `PICOCALC` to a port-config flag

PicoCalc is a board feature, treated as a CMake-global today.

P5.1. **Decide scope.** PICOCALC drives:
      - The i2c-keypad driver inclusion (`drivers/i2c_picocalc_kbd/`)
      - `HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C` runtime constant
      - A few PicoCalc-specific factory-reset profiles in `port/pico/`
      All three are board-specific, not feature-specific.

P5.2. **For new ports**: PICOCALC is a port-config flag
      `HAL_PORT_IS_PICOCALC`. Each port_config.h says yes/no.

P5.3. **For legacy `ports/pico/`**: keep the global `-DPICOCALC=true`
      override mechanism for backwards compatibility, but route core
      decisions through `HAL_PORT_IS_PICOCALC` (set from `-DPICOCALC=true`
      via port_sources.cmake).

P5.4. **Acceptance**: `grep -rE '\bPICOCALC\b' --include=*.c
      --include=*.h` returns empty outside port_config.h /
      port_sources.cmake / port impl files.

### Stage P6 — Drop the COMPILE enum

After P4, the only consumer of `if (COMPILE STREQUAL …)` left is the
top-level `COMPILE → PORT_DIR` map in CMakeLists.txt. Replace with a
direct `set(PORT <port_dir_name>)` selector.

P6.1. **Top-level CMakeLists.txt**: change
      `set(COMPILE PICO)` → `set(PORT pico)`. The `if (COMPILE
      STREQUAL …)` ladder collapses to `include(${CMAKE_SOURCE_DIR}/
      ports/${PORT}/port_sources.cmake)`. The board / chip-platform
      branches (`pico_w` vs `pimoroni_pga2350` etc.) move into each
      port_sources.cmake (they already pick the board file at the
      top of every snippet via the CMake `set(PICO_BOARD …)`
      mechanism — just need to lift them).
P6.2. **buildall.sh, build_firmware.sh, firmware.yml**: switch
      `-DCOMPILE=…` to `-DPORT=…` and update target lists to be
      lowercase port-directory names.
P6.3. **Backwards-compat shim** (one release): if `COMPILE` is set
      but `PORT` isn't, infer `PORT` from the legacy `COMPILE` →
      directory map, log a deprecation warning, and continue.
P6.4. **Acceptance**: `grep -E 'COMPILE STREQUAL' CMakeLists.txt
      ports/*/port_sources.cmake` returns empty.

### Stage P7 — Convention: new ports are single-board

P7.1. **Update `docs/adding-a-new-port.md`** to make single-board the
      default workflow. Move the multi-board pattern to a separate
      "Legacy multi-board ports" section that says "if you're
      maintaining `ports/pico/` or `ports/hdmi_rp2350/` you'll see
      this pattern; don't propagate it."
P7.2. **`port_factory_reset_board`**: for new ports, the body is
      `return 0;`. The `OPTION RESET <BOARD>` command then errors
      with "Unknown board" — which is the right answer for a port
      that's already been built for one specific board.
P7.3. **`port_print_supported_boards`**: for new ports, the body is
      `MMPrintString("`HAL_PORT_DEVICE_NAME`\r\n");` (just the one
      name, for diagnostic visibility).
P7.4. **OPTION setters stay**. `OPTION HDMI PINS`, `OPTION VGA PINS`,
      `OPTION RESOLUTION`, `OPTION SDCARD`, `OPTION I2S`, `OPTION
      LCDPANEL`, `OPTION TOUCH`, `OPTION KEYBOARD PINS`, `OPTION
      PSRAM PIN`, etc. are all still wired up and persisted. Useful
      for bring-up and silkscreen-typo workarounds.

### Stage P8 — Specify the `port_*()` HAL contract

The audit found `port_pinno_alias_for_name` and friends have
diverged across ports. They should be a documented HAL contract.

P8.1. **Write `hal/CONTRACT.md` entries** for each `port_*()` entry
      point: pre-conditions, post-conditions, what core does with
      the return value, edge cases.
P8.2. **For pin-alias hooks specifically**: define what "reserved
      alias" means (boot-reserved GPIO that's still addressable by
      virtual pin number) vs "absent alias" (return 0). Audit every
      port's implementation against the contract; fix the leftover
      WiFi-flavoured aliases in `hdmi_rp2350` (which doesn't have
      WiFi).
P8.3. **Acceptance**: `tools/check_hal_contract.sh` (new) verifies
      every port defines every entry point and returns the right
      shape. Falls into the same gate as `check_hal_purity.sh`.

### Stage P9 — Validation matrix

After P1–P8, add new validation ports for novel feature combinations
that didn't compose before:

- VGA + WiFi + GUICONTROLS + touch (the original "would silently
  corrupt Option" case from the audit)
- SPI-LCD + WiFi + USB-host keyboard
- HDMI + WiFi + GUICONTROLS (combines the most flags)

Each should compose by setting the right palette flags only — no
source patches required, no novel coupling discovered. If any does
require a source patch, that's a P1–P8 regression.

## Risks / things this plan handwaves

- **Persistence-format break.** P1's `Option` layout change requires
  bumping every `HAL_PORT_MAGIC_KEY`. Users with saved-options
  blobs lose their settings on first boot of the new firmware.
  Acceptable cost; document in release notes.

- **Driver-flavour proliferation.** P3's "split spi_lcd.c into
  per-display-family driver files" is a follow-on with its own
  scope. Until it lands, P3.3's `-DPICOMITE` cleanup may stop short.

- **PICOCALC global vs port-config tension.** P5 keeps the global
  `-DPICOCALC=true` mechanism for backwards compatibility while new
  ports use a port-config flag. Two patterns coexisting is messy
  but pragmatic. Eventually pick one.

- **USB-port directory split (P4.5)** doubles the rp2040+rp2350
  device port count. The `buildall.sh` matrix grows accordingly.
  CI build time goes from ~10 min to ~15 min.

- **Estimated effort.** P1 is ~1 day (struct surgery + magic-key
  bump + smoke). P2/P3 are ~2 days each (mechanical decascade per
  the original pattern). P4 is ~1 day for the flag + ~1 day for
  the directory splits. P5 is ~half day. P6 is ~half day. P7 is
  ~half day (mostly docs). P8 is ~1 day (contract spec + audit + new
  gate script). P9 is ~half day per validation port. Total: ~9 days
  of focused work.

## Acceptance criteria

The plan is complete when all of these hold:

1. **Zero `#ifdef` on PICOMITE / PICOMITEVGA / PICOMITEWEB / HDMI /
   GUICONTROLS / USBKEYBOARD / PICOCALC** in core, drivers (excluding
   per-driver-internal driver-flavour gates), or `configuration.h`.
   Verified by `tools/check_hal_purity.sh`.
2. **`struct option_s` is layout-stable** across all
   `HAL_PORT_HAS_*` combinations. Persisted Option blobs from one
   port can't silently corrupt under another port.
3. **No `if (COMPILE STREQUAL …)`** anywhere in CMakeLists.txt or any
   `port_sources.cmake`. The selector is `set(PORT <dir>)`.
4. **Adding a new port requires four files** in `ports/<board>/`:
   `port_config.h`, `port_sources.cmake`, `pin_tables.c`,
   `port_defaults.c`. No top-level edits except adding the port name
   to `buildall.sh` (and equivalent CI matrix). New port author writes
   no `#ifdef`s, defines no new `-D` flags.
5. **The validation matrix (P9)** — three novel combinations
   compose with palette flags only, no source patches.
6. **`docs/adding-a-new-port.md`** describes single-board ports as
   the default; multi-board pattern documented as legacy maintenance.
7. **OPTION setters** for hardware pins (HDMI, VGA, SDCARD, I²S,
   TOUCH, LCDPANEL, KEYBOARD, PSRAM) still work as a debug surface.
   Removed only `OPTION RESET <BOARD>` for new ports (it's a no-op
   when `port_factory_reset_board` returns 0).
