# Preprocessor-gate elimination plan

## Premise

After the post-decascade work (P1–P7), every port-config gate has been
*renamed* into the `HAL_PORT_HAS_*` / `HAL_PORT_IS_*` palette. None are
gone. 361 preprocessor sites remain across 30 files. The next stage is
to actually eliminate them — at every site, either:

- **Split** the surrounding source file into per-flavor driver files
  selected by `port_sources.cmake` (the file-selection model gui_controls,
  vga_ops_stub, audio_mp3 etc. already use), OR
- **Extract** the gated block into a HAL hook with a real-impl + stub
  pair under `drivers/<feature>/`, OR
- **Reformulate** as a value-in-expression (`if (HAL_PORT_HAS_X) { … }`)
  so the compiler dead-codes the unreachable branch but the source has
  no `#if`, OR
- **Delete** if the gate is dead / a flag has no remaining preprocessor
  consumers.

The flags themselves stay (they remain useful for CMake-level
composition and runtime-value-in-expression checks; see "What the flags
are for" at the bottom). What goes away is `#if HAL_PORT_HAS_*` /
`#if HAL_PORT_IS_*` directives in C source.

## Current state (2026-05-02)

361 preprocessor gates total. Top files by gate count:

| File | Gates | Notes |
|---|---:|---|
| `PicoMite.c` | 67 | Core boot + dispatch — heavy WiFi / USBKEYBOARD / VGA / HDMI / I2C-keypad gating |
| `Editor.c` | 50 | Built-in editor — USBKEYBOARD-axis keymap differences |
| `drivers/spi_lcd/spi_lcd.c` | 47 | One file mutating per-display-family at preprocess time |
| `drivers/sd_spi/mmc_stm32.c` | 23 | I2C-keypad-shared bus + USBKEYBOARD differences |
| `drivers/vga_pio/vga_mode_ops.c` | 21 | NEXTGEN-display gates inside the VGA driver |
| `ports/pico_sdk_common/hal_keyboard_pico.c` | 16 | USBKEYBOARD axis dispatching |
| `ports/pico_sdk_common/misc_option_setters.c` | 15 | OPTION setters per board/feature |
| `AllCommands.h` | 15 | Token-table conditional rows |
| `Hardware_Includes.h` | 10 | Includes by feature |
| `drivers/vga_pio/vga_ops.c` | 10 | NEXTGEN-display + GUICONTROLS gates |
| `ports/pico_sdk_common/print_display_options.c` | 9 | OPTION LIST per feature |
| `Touch.c` | 8 | Touch panel gating |
| `Custom.c` | 6 | CSUB / CFunction gating |
| `I2C.c` | 5 | I2C-keypad protocol |
| (long tail) | … | 4 sites or fewer in 16 more files |

Per-flag distribution:

| Flag | Sites |
|---|---:|
| `HAL_PORT_HAS_PICOMITE` | 93 |
| `HAL_PORT_HAS_HDMI` | 76 |
| `HAL_PORT_HAS_USB_KEYBOARD` | 64 |
| `HAL_PORT_IS_VGA` | 56 |
| `HAL_PORT_HAS_WIFI` | 54 |
| `HAL_PORT_HAS_I2C_KEYPAD` | 14 |
| `HAL_PORT_HAS_GUICONTROLS` | 10 |

## Stages

Each stage independently committable, leaving `buildall.sh` green.

### Stage E1 — Audit + classify (1–2 days, no code changes)

Walk every site and assign a category. Output: `docs/gate-audit.csv`
with one row per site:

```
file,line,flag,category,notes
PicoMite.c,2045,HAL_PORT_HAS_I2C_KEYPAD,extract-hook,init delay -> hal_i2c_keypad_boot_delay()
drivers/spi_lcd/spi_lcd.c,1031,HAL_PORT_HAS_PICOMITE,split-driver,ILI9488WBUFF only on rp2350 PicoMite
Editor.c,*,HAL_PORT_HAS_USB_KEYBOARD,extract-hook,keymap dispatch -> hal_keyboard_*
…
```

Five categories:

1. **split-driver** — file is shared across hardware variants but the
   gated body is hardware-specific. Action: split into per-flavor
   driver file selected by `port_sources.cmake`.
2. **extract-hook** — gated block is logically a HAL operation that
   varies by port. Action: define a hook in `hal/hal_<topic>.h`,
   provide real + stub impls.
3. **value-in-expression** — gate can be rewritten as an `if (FLAG)`
   so the compiler dead-codes one branch. Works when both branches
   reference symbols that exist on every port.
4. **delete** — the gated block is dead, the flag has zero remaining
   consumers, or the surrounding code can be unconditionally compiled.
5. **legacy-multi-board** — file lives under `ports/<legacy>/`
   (port-impl scope per the post-decascade rule). Excluded from
   elimination — the post-decascade plan tags these as "leave legacy
   multi-board ports as they are." E1 still records them but the
   action is "skip".

The audit gives a real worklist. Without it, the rest of the plan is
hand-waving.

### Stage E2 — Drop dead gates (smallest payoff first)

From the E1 audit, attack everything in the **delete** category. Some
candidates already visible:

- `HAL_PORT_HAS_GUICONTROLS` only has 10 sites. After P3 most should
  already be hooks. Remaining sites are likely in `Touch.c` /
  `gui_touch.c` and may be legitimately gated (Touch.c is itself
  GUI-only and may not need to compile at all on non-GUICONTROLS
  ports). If `Touch.c` isn't linked on non-GUICONTROLS ports, the
  internal `#if HAL_PORT_HAS_GUICONTROLS` gates are vestigial — drop
  them.
- `HAL_PORT_HAS_I2C_KEYPAD` only has 14 sites — small enough to fully
  decompose into hooks in one stage.

### Stage E3 — Split `drivers/spi_lcd/spi_lcd.c` by display family

The single biggest source file (47 gates) is a 1000+ line driver
shared across half a dozen display-controller families (ST7789, ILI9341,
ILI9488, ILI9488W, ILI9488WBUFF, SSD1963, ST7796, etc.) with many
`#if HAL_PORT_HAS_PICOMITE && defined(rp2350)` branches.

Split into per-family driver files:

```
drivers/spi_lcd/
    spi_lcd_common.c        — bus init, CS, gpio_put helpers
    spi_lcd_ili9341.c
    spi_lcd_ili9488.c
    spi_lcd_ili9488w.c
    spi_lcd_ili9488wbuff.c  — only on rp2350 PicoMite
    spi_lcd_st7789.c
    spi_lcd_st7796.c
    …
```

`port_sources.cmake` lists exactly the families that port supports.
Display-type runtime dispatch (the `if (Option.DISPLAY_TYPE == ILI9488)`
ladders) becomes function-pointer dispatch through a vtable populated
at boot.

This is a substantial refactor — ~1 week of focused work. Defer to a
later stage if E1 reveals smaller wins available first.

### Stage E4 — Split `drivers/vga_pio/vga_mode_ops.c`

21 sites, mostly `HAL_PORT_HAS_NEXTGEN_DISPLAY` gating around the
RP2350-only NEXTGEN display modes. Same shape as E3: split the file
into `vga_mode_ops_legacy.c` (always linked) and `vga_mode_ops_nextgen.c`
(linked only on ports with NEXTGEN support).

### Stage E5 — Extract `Editor.c` USBKEYBOARD keymap dispatch (50 gates)

The built-in editor's per-key handlers fork sharply on USBKEYBOARD vs
PS/2 (different scancodes, different modifier handling). Today this is
50 `#if HAL_PORT_HAS_USB_KEYBOARD` gates inline. Extract to a HAL hook:

```c
hal/hal_editor_keys.h:
  int hal_editor_translate_key(int raw_scancode);
  // returns the editor's internal key code, or 0 if unmapped

drivers/editor_keys/editor_keys_usb.c   — USBKEYBOARD impl
drivers/editor_keys/editor_keys_ps2.c   — PS/2 impl
```

`Editor.c` calls `hal_editor_translate_key()` once per keystroke; the
50 inline gates collapse to a single call.

### Stage E6 — Decompose `PicoMite.c` (67 gates)

`PicoMite.c` is the boot file + main dispatch loop + interrupt
handlers. Most gates are around boot-init sequences (which peripherals
to init, what order, how long to wait). Each maps to a port hook:

- `port_init_displays()` — runs SSD/I2C/SPI-LCD/VGA/HDMI init in the
  right order for this port.
- `port_init_keyboard()` — USB host vs PS/2 vs I2C keypad.
- `port_main_loop_yield()` — different ports have different work to
  do on the idle path (lwIP poll, USB host poll, etc.).

Extract per-section. Plan ~2 stages over the file.

### Stage E7 — Reformulate value-in-expression sites

After E2–E6, what's left should be small per-file residue that's
"both branches link cleanly, just one is dead at compile time."
Rewrite from:

```c
#if HAL_PORT_HAS_HDMI
    init_hdmi_clocks();
#else
    init_pwm_backlight();
#endif
```

to:

```c
if (HAL_PORT_HAS_HDMI) init_hdmi_clocks();
else                    init_pwm_backlight();
```

This requires both `init_hdmi_clocks` and `init_pwm_backlight` to
*link* on every port — usually means adding stub functions for the
inactive side. Compiler folds the constant; same machine code.

### Stage E8 — Acceptance: zero `#if HAL_PORT_*` in core / drivers

Final gate:

```bash
grep -rE '^\s*#\s*(if|ifdef|ifndef|elif).*\bHAL_PORT_(HAS|IS)_' \
    --include='*.c' --include='*.h' . | \
    grep -v '^./ports/[a-z_]*/port_config\.h' | \
    grep -v '^./build'
```

returns empty. `tools/check_hal_purity.sh` extends to enforce this
across all source files (not just the strict-scope set).

The legacy multi-board port_defaults.c files (E1's category 5) are
exempted explicitly — they're allowed `#if` on the multi-board axis.

## What the flags are still for after E8

If every source-level `#if` goes, the flags don't disappear. They keep
three jobs:

1. **CMake-side composition.** `port_sources.cmake` reads each port's
   palette flags (or their CMake-variable twins) to decide which driver
   file to link, which library to pull in, which `target_compile_options`
   to add. Files are selected, not gated.
2. **Runtime values in C expressions.** `if (HAL_PORT_HAS_SSD1963 &&
   Option.DISPLAY_TYPE >= SSDPANEL)`, array sizes, `static_assert`
   invariants, function-pointer table sizing. The flag is a constant
   value the compiler dead-codes around.
3. **Self-documenting port_config.h.** `ports/dvi_wifi_rp2350/port_config.h`
   reads as the inventory: HDMI=1, WiFi=1, USB-host=1, GUI controls=0,
   I²C keypad=0. New-port authors copy and adjust.

If a flag has zero CMake consumers AND zero runtime-value consumers
AND its `port_config.h` definition is the only place it appears, it's
dead documentation — delete in E2.

## Risks

- **Driver-flavor proliferation.** E3 alone may add 10+ new driver
  files. CMake snippets get longer. Trade-off: a longer
  `port_sources.cmake` is per-port composition (the goal); a 1000-line
  preprocessor-shaped driver is the anti-goal.
- **Function-pointer dispatch overhead.** Replacing compile-time
  branches with vtable dispatch adds an indirect call per
  display-driver call. Negligible on a 250 MHz rp2350; benchmark E3
  before / after to confirm no regression on the slower rp2040 ports.
- **Legacy port `#if` exemptions.** The post-decascade plan keeps
  legacy multi-board ports' `#if`s. E8's gate has to grandfather
  those files explicitly — without that, the gate fails on day 1.

## Estimated effort

| Stage | Effort |
|---|---|
| E1 — audit | 1–2 days |
| E2 — drop dead gates | 1 day |
| E3 — split spi_lcd.c | 1 week |
| E4 — split vga_mode_ops.c | 2 days |
| E5 — extract Editor.c keys | 2 days |
| E6 — decompose PicoMite.c | 3–4 days |
| E7 — reformulate residue | 2 days |
| E8 — gate + acceptance | 1 day |

Total: ~3 weeks of focused work to drive `#if HAL_PORT_*` to zero in
core + drivers.
