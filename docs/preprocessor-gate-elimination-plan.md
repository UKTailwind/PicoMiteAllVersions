# Gate elimination plan

## Goal

**Zero `#if HAL_PORT_*` AND zero `if (HAL_PORT_HAS_*)` in C source.**
Modularity comes from file selection at link time, not from gates of
any kind. The two are the same coupling — `#if` versus runtime `if`
is just syntax.

The bar for keeping any gate, in either form, is "I tried to
eliminate it, here's the concrete reason that didn't work" —
documented at the site, with the alternative considered. Default is
elimination, exceptions are argued individually.

## Premise

After the post-decascade work (P1–P7), every port-config gate has been
*renamed* into the `HAL_PORT_HAS_*` / `HAL_PORT_IS_*` palette. None are
gone. 361 preprocessor sites remain across 30 files. Each site gets
eliminated by one of:

- **Split** the surrounding source file into per-flavor driver files
  selected by `port_sources.cmake` (the file-selection model
  `gui_controls`, `vga_ops_stub`, `audio_mp3` etc. already use). The
  whole file's body is unconditional inside; the linker picks one
  variant per port. **Preferred default — this is what "modular"
  means here.**
- **Extract** the gated block into a HAL hook (function declared in
  `hal/<topic>.h`, real impl + stub pair under `drivers/<topic>/`).
  Use when the gated body is genuinely shared logic with a small
  per-port variation point. Don't use as a one-call-site rebrand of
  an `#if` — that's the same gate dressed up.
- **Delete** if the gate is dead, the body is vestigial, or the
  surrounding code can be unconditionally compiled with the symbols
  it references already linking on every port.
- **Argued exception** — last resort. The gate stays in source,
  with an in-source comment explaining what was tried and why
  elimination is impractical at that site. Tracked as a known
  technical-debt entry.

What's NOT in the toolkit:

- ~~Value-in-expression (`if (HAL_PORT_HAS_X) { … }`)~~ — deleted
  from the toolkit. It's the same coupling as `#if`, dressed in
  syntax the purity script doesn't grep for. That's gaming, not
  elimination. Earlier drafts of this plan listed it as a category;
  it's gone now.

The flags themselves stay (they remain useful for CMake-side file
selection and as port_config.h self-documentation). What goes away
is *every* form of `HAL_PORT_*` consultation in compiled source.

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

Four categories (down from five — the value-in-expression bucket is
gone):

1. **split-driver** — file is shared across hardware variants but the
   gated body is hardware-specific. Action: split into per-flavor
   driver file selected by `port_sources.cmake`. Body inside each
   file is unconditional. **Preferred default**.
2. **extract-hook** — gated block is genuinely shared logic with a
   small per-port variation point. Hook lives in `hal/hal_<topic>.h`,
   real + stub impls live under `drivers/<topic>/`. Reserved for
   cases that aren't trivially file-splittable.
3. **delete** — the gate, or the body it guards, is vestigial.
   Removing it changes nothing because the symbols already link on
   every port (or the body was dead).
4. **argued-exception** — site-by-site documented "this stays
   because <concrete reason>, here's what I tried." Last resort,
   not a default bucket.

No file-level exemptions. The legacy multi-board `OPTION RESET <BOARD>`
ladders in `ports/{pico,pico_rp2350,vga,vga_rp2350,hdmi_rp2350}/port_defaults.c`
are NOT exempt — earlier drafts of this plan tagged them so, that was
wrong. Per-board profile data moves to `drivers/board_profiles/<board>.c`
files registered at link time; the `OPTION RESET <BOARD>` command
becomes a runtime registry walk over a string-keyed table, not an
`#if`-bracketed `checkstring()` ladder. The dual-shipping legacy port
directories continue to exist (so existing user `OPTION RESET`
invocations keep working), but their `port_defaults.c` files lose all
preprocessor gates.

The audit gives a real worklist. Without it, the rest of the plan is
hand-waving.

### Status (2026-05-03)

Tree gate count: **361 → 224** (137 eliminated, 38%).

| Stage | Status | Eliminated | Cumulative |
|---|---|---:|---:|
| E1 — audit + classify                            | DONE     | 0   | 0   |
| E2 — GUICONTROLS (10) + I2C_KEYPAD (14)          | DONE     | 24  | 24  |
| E3 — split spi_lcd.c into MEM332 + sender hooks  | DONE     | 47  | 71  |
| E4 — split vga_mode_ops.c HDMI / IS_VGA          | DONE     | 21  | 92  |
| E5 — Editor.c keymap                             | deferred | —   | —   |
| E5 — hal_keyboard_pico split (USB / PS2)         | DONE     | 16  | 108 |
| E5b–E5h — PicoMite.c WiFi+heartbeat+keyboard+misc| DONE     | 26  | 134 |
| E5b — board-profile registry                     | TODO     | —   | —   |
| E5i — PinDef[] split into per-port pin_tables.c  | DONE     | TBD | TBD |
| E5j — InitReservedIO ownership untangle          | DONE     | 5   | TBD |
| E5j-tail — drain remaining InitReservedIO gates  | DONE     | 2   | TBD |
| E5k — Editor.c (49 sites, 5 flag types)          | TODO     | —   | —   |
| E6 — finish PicoMite.c (41 remaining)            | TODO     | —   | —   |
| E7 — argue/resolve any leftovers                 | TODO     | —   | —   |
| E8 — purity-script enforcement                   | TODO     | —   | —   |

Top remaining offender files (post-E5j-tail sites):

  Editor.c                                    49
  PicoMite.c                                  29
  drivers/sd_spi/mmc_stm32.c                  14   (InitReservedIO is fully gate-free; remaining gates live in other functions)
  ports/pico_sdk_common/misc_option_setters.c 15
  AllCommands.h                               11
  drivers/vga_pio/vga_ops.c                   10
  Hardware_Includes.h                          9

Pattern observed: gates accumulate at *chokepoint* functions — any
boot-time hook (`InitReservedIO`), giant lookup table (`PinDef[]`),
or "runs every tick" handler becomes a magnet for unrelated
port-specific code. The structural fix is per-subsystem ownership
splits (E5i, E5j) rather than more conditional compilation.

### Stage E2 — Drop dead gates + finish the small flags

From the E1 audit, attack the smallest-flag elimination first:

- `HAL_PORT_HAS_GUICONTROLS` (10 sites). After P3 most should already
  be hooks. Remaining sites are likely in `Touch.c` / `gui_touch.c`.
  `Touch.c` is itself GUI-only and may not need to compile at all on
  non-GUICONTROLS ports — gate it via `port_sources.cmake` linkage
  rather than in-file `#if`s. Drop the internal gates.
- `HAL_PORT_HAS_I2C_KEYPAD` (14 sites). Small enough to fully
  decompose. Targets: I2C.c keypad protocol → real-vs-stub split,
  PicoMite.c boot delay → hook, drivers/sd_spi/mmc_stm32.c
  shared-bus init → hook, port_load_overrides.c + picocalc_features.c
  → split.

Both flags should drop to zero source-level gates by end of E2. CMake
keeps the flag for linkage selection; runtime keeps it for
value-in-expression checks.

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

### Stage E5b — Convert multi-board mechanism to a board-profile registry

The dual-shipping legacy ports' `port_defaults.c` files (16 sites
across 5 ports) plus `port_load_overrides.c` (1 site) are the densest
preprocessor coupling in the codebase. The multi-board axis itself —
runtime selection of *which physical board* this firmware was burned
to — is not the problem; it's a real user-facing feature
(`OPTION RESET PICOCALC` etc.). The problem is that today it's
implemented as `#ifdef`-bracketed `checkstring()` ladders inside one
`port_defaults.c` per port directory.

Replacement design:

```
drivers/board_profiles/
    board_profile.h            — struct board_profile { name, defaults_fn, ... }
    board_profile_registry.c   — table of registered profiles + dispatch
    profile_picocalc.c         — sets OPTION defaults for the PicoCalc board
    profile_gamemite.c         —      …      Game*Mite
    profile_pico_computer.c    —      …      Pico Computer
    profile_picoresttouch.c    —      …      Pico-ResTouch
    profile_hdmi_usb.c         —      …      HDMI-USB
    …
```

Each profile file is linked from `port_sources.cmake` only on ports
that physically support that board. `OPTION RESET <BOARD>` walks the
registry table, matches by name, calls the profile's `defaults_fn`,
saves, soft-resets. The `port_defaults.c` files in legacy ports
collapse from 100+ lines of `#if`-laden ladder to a one-line
"register the profiles this port knows about and delegate to the
registry."

This is a real refactor — ~3 days of work — but eliminates 17 `#if`
sites and turns the multi-board mechanism into a proper data-driven
system.

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

### Stage E7 — Argue and resolve every remaining gate

After E2–E6, walk the remaining sites one at a time. The default
disposition is "split or extract"; the alternatives are "delete" or
"argued exception." For each "argued exception" site, write a
comment at the source that names:

  1. What was tried (split, extract, delete) and why each failed.
  2. What the linkage / cost trade-off is.
  3. The concrete reason the gate stays.

Sites without all three notes don't survive E7.

The E7 pattern that's NOT in the toolkit: rewriting `#if FLAG` as
`if (FLAG)` while keeping the same coupling. That's gaming the
purity script — same gate, different syntax. If a gate genuinely
needs to stay, it stays as `#if` with a documented reason. If it
can move out of source, it moves all the way out.

### Stage E8 — Acceptance: zero `HAL_PORT_*` consultation in source

Final gate. `tools/check_hal_purity.sh` extends to enforce both:

1. Zero `#if HAL_PORT_*` directives:
   ```bash
   grep -rE '^\s*#\s*(if|ifdef|ifndef|elif).*\bHAL_PORT_(HAS|IS)_' \
       --include='*.c' --include='*.h' . | \
       grep -v '^./ports/[a-z_]*/port_config\.h' | \
       grep -v '^./build'
   ```
   returns empty.

2. Zero runtime `if (HAL_PORT_HAS_*)` / `if (HAL_PORT_IS_*)`
   expressions in C source:
   ```bash
   grep -rE '\bif\s*\([^)]*\bHAL_PORT_(HAS|IS)_' \
       --include='*.c' --include='*.h' . | \
       grep -v '^./ports/[a-z_]*/port_config\.h' | \
       grep -v '^./build'
   ```
   returns empty.

The flags themselves still appear in `port_config.h` files (their
home), in `CMakeLists.txt` / `port_sources.cmake` (CMake-side
composition reads them), and in array sizes / `static_assert`
invariants where the flag is used as a *value* the compiler needs
at compile time, not as a control-flow predicate. Those are the
three legitimate uses; everything else is gone.

The "argued-exception" sites from E7 (if any survive) are listed
explicitly in `tools/check_hal_purity.sh` as named site exceptions,
each with a comment pointing to the in-source justification. No
file-level blanket exemptions.

## What the flags are still for after E8

If every source-level gate goes, the flags don't disappear. They
keep three jobs:

1. **CMake-side composition.** `port_sources.cmake` reads each port's
   palette flags (or their CMake-variable twins) to decide which
   driver file to link, which library to pull in, which
   `target_compile_options` to add. Files are selected, not gated.
2. **Compile-time values in array sizes / `static_assert` invariants
   / table sizing.** The flag appears as a *value* the compiler
   needs at compile time (`uint8_t buf[HAL_PORT_KBD_BUF_SIZE]`,
   `static_assert(HAL_PORT_PIO_COUNT >= 2, "…")`). The flag is not
   a control-flow predicate — it's an integer constant being used
   as a number. This is a different thing from `if (FLAG) { ... }`.
3. **Self-documenting `port_config.h`.** `ports/dvi_wifi_rp2350/port_config.h`
   reads as the inventory: HDMI=1, WiFi=1, USB-host=1, GUI
   controls=0, I²C keypad=0. New-port authors copy and adjust.

A flag that has zero CMake consumers, zero array-size / static_assert
consumers, and its `port_config.h` definition is the only place it
appears, is dead documentation — delete it.

A flag that's used only as a control-flow predicate (`if (FLAG)` or
`#if FLAG`) is failing the goal of this plan. Either eliminate the
predicate (split / extract / delete) or document the argued
exception.

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
| E5b — board-profile registry | 3 days |
| E6 — decompose PicoMite.c | 3–4 days |
| E7 — reformulate residue | 2 days |
| E8 — gate + acceptance | 1 day |

Total: ~3.5 weeks of focused work to drive `#if HAL_PORT_*` to zero
across core, drivers, and legacy port impl files (no exemptions).
