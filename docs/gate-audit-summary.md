# HAL gate audit — Stage E1 output

Survey of every `#if HAL_PORT_HAS_*` / `#if HAL_PORT_IS_*` directive
across the source tree, classified by elimination strategy. Per-site
data in `docs/gate-audit.csv`; this file is the rolled-up summary.

## Totals

359 gate sites. The four categories per the (revised) plan:

| Category | Sites | %  |
|---|---:|---:|
| split-driver       | 130 | 36% |
| extract-hook       | 229 | 64% |
| delete             |   0 | revisit during E2 |
| argued-exception   |   0 | populated case-by-case during E2–E7 |

**No "value-in-expression" bucket.** Earlier drafts had 26 sites
tagged for rewrite as `if (HAL_PORT_HAS_*) { ... }`. That's the same
gate as `#if`, just dressed in syntax the purity script doesn't
grep for. Removed from the toolkit. Those 26 sites are now classified
as either split-driver, extract-hook, or delete depending on the
actual hardware coupling at the site. (See per-file table.)

**No exemptions.** The multi-board `OPTION RESET <BOARD>` mechanism in
`ports/{pico,pico_rp2350,vga,vga_rp2350,hdmi_rp2350}/port_defaults.c`
is itself a target for elimination — per-board defaults move to
`drivers/board_profiles/<board>.c` files selected at link time, the
`#if HAL_PORT_HAS_USB_KEYBOARD` ladders inside the multi-board ports
collapse along with them. `configuration.h`'s legacy translation
layer dies along with `-DPICOCALC=true|false` becoming a port-config
flag set in `port_config.h`.

## Per-flag distribution

| Flag | Sites |
|---|---:|
| `HAL_PORT_HAS_PICOMITE`     | 93 |
| `HAL_PORT_HAS_HDMI`         | 76 |
| `HAL_PORT_HAS_USB_KEYBOARD` | 64 |
| `HAL_PORT_IS_VGA`           | 56 |
| `HAL_PORT_HAS_WIFI`         | 54 |
| `HAL_PORT_HAS_I2C_KEYPAD`   | 14 |
| `HAL_PORT_HAS_GUICONTROLS`  | 10 |

## Top files

| File | Sites | Category | Action |
|---|---:|---|---|
| `PicoMite.c`                                       | 67 | extract-hook        | Decompose boot init + main loop into `port_init_displays()`, `port_init_keyboard()`, `port_main_loop_yield()` hooks. |
| `Editor.c`                                         | 50 | extract-hook        | USBKEYBOARD keymap differences → `hal_editor_translate_key()`. |
| `drivers/spi_lcd/spi_lcd.c`                        | 47 | split-driver        | Split per display family (ILI9341, ILI9488, ILI9488WBUFF, ST7789, ST7796, SSD1963). |
| `drivers/sd_spi/mmc_stm32.c`                       | 23 | extract-hook        | I2C-keypad-shared bus init + USBKEYBOARD differences → hooks. |
| `drivers/vga_pio/vga_mode_ops.c`                   | 21 | split-driver        | Split legacy vs NEXTGEN VGA modes. |
| `ports/pico_sdk_common/hal_keyboard_pico.c`        | 16 | extract-hook        | USBKEYBOARD axis → split into `hal_keyboard_usb.c` / `hal_keyboard_ps2.c`. |
| `ports/pico_sdk_common/misc_option_setters.c`      | 15 | extract-hook        | OPTION setters fork by board feature → per-feature hook impls. |
| `AllCommands.h`                                    | 14 | delete              | Token rows go in unconditionally; the dispatcher already errors via the stub-hook surface. |
| `drivers/vga_pio/vga_ops.c`                        | 10 | split-driver        | NEXTGEN + GUICONTROLS gates. |
| `ports/pico_sdk_common/print_display_options.c`    |  9 | extract-hook        | OPTION LIST per feature → hooks. |
| `Hardware_Includes.h`                              |  9 | split-driver        | Replace per-feature includes with port-config-driven include. |
| `Touch.c`                                          |  8 | split-driver        | Gate the entire file by linkage; drop in-file gates. |
| `Custom.c`                                         |  6 | extract-hook        | CSUB / CFunction WiFi gates → hook. |
| `I2C.c`                                            |  5 | extract-hook        | I2C-keypad protocol → `drivers/i2c_picocalc_kbd` hook. |
| `ports/pico/port_defaults.c`                       |  4 | extract-hook        | Per-board defaults → `drivers/board_profiles/<board>.c` linked at port composition; OPTION RESET <BOARD> ladder becomes a runtime board-profile dispatch. |
| `ports/pico_sdk_common/cmd_files_hooks.c`          |  4 | extract-hook        | cmd_files USBKEYBOARD axis → hook. |
| `ports/pico_sdk_common/clear_runtime_port.c`       |  4 | extract-hook        | |
| `drivers/hdmi/hdmi_modes.c`                        |  4 | split-driver        | HDMI vs DVI mode tables. |
| `configuration.h`                                  |  4 | extract-hook        | Move USB-axis FLASH/MagicKey/HEAPTOP + WiFi config + VGA config to flat per-port `port_config.h` values. The legacy translation layer dies. |
| (long tail of 1–3 site files)                      |  … | (mixed)             | |

## Per-category notes

### extract-hook (229 sites, 64%)

The biggest bucket. Most sites are HAL operations that vary per port —
boot sequencing, keyboard input, OPTION setters, `cmd_files` plumbing.
Each maps to a HAL hook in `hal/hal_*.h` plus a real-impl + stub pair
under `drivers/<topic>/` or `ports/pico_sdk_common/`.

Approximate hook count to define: ~35 new hooks across the 229 sites
(many sites collapse to the same hook).

This bucket also includes the legacy multi-board `port_defaults.c`
ladders (16 sites): the per-board profile data moves to
`drivers/board_profiles/<board>.c` files linked unconditionally; the
`OPTION RESET <BOARD>` command becomes a table-driven runtime
dispatch rather than an `#if`-bracketed `checkstring()` ladder.

### split-driver (104 sites, 29%)

`drivers/spi_lcd/spi_lcd.c` alone owns 47 of these; `drivers/vga_pio/`
owns 31; `Hardware_Includes.h` 9; `Touch.c` 8; `drivers/hdmi/` 4;
`SPI-LCD.h` 3. Strategy:

- **`spi_lcd.c`**: split per display-controller family. ~6 new files.
  Display-type dispatch becomes a function-pointer vtable populated
  at boot.
- **`vga_pio/vga_mode_ops.c`**: split legacy QVGA modes vs RP2350-only
  NEXTGEN modes. 2 new files.
- **`vga_pio/vga_ops.c`**: similar split + GUICONTROLS gates collapse
  via existing `gui_controls` hook surface.
- **`Touch.c`**: file is already touch-only; the in-file gates are
  vestigial. Remove the file from non-GUICONTROLS port_sources and
  drop the gates.
- **`drivers/hdmi/hdmi_modes.c`**: split HDMI mode tables vs DVI mode
  tables (shared parent file → two siblings).
- **`Hardware_Includes.h` / `SPI-LCD.h`**: header per-feature
  includes; convert to umbrella-include pattern keyed by linkage,
  not preprocessor.

### Re-categorization of the former "value-in-expression" 26 sites

Each of those sites is now classified as the actual elimination it
needs. Sample re-categorizations (full list in `gate-audit.csv`):

- **`AllCommands.h`** (14 sites, token-table rows). Now
  **delete**: token rows go in the table unconditionally on every
  port; the dispatcher already errors on unknown commands at
  runtime, so a row pointing at a function that lives in a stub
  module just turns into "Not supported on this board" via the
  hook's stub impl. No predicate needed in the table.
- **`bc_vm.c:982`** (single GUICONTROLS gate). Now **delete** — the
  `HideAllControls()` call is already routed through
  `hal_gui_controls_hide_all()` from P3; the stub is a no-op, so
  the call site doesn't need a gate.
- **`MMtcpserver.c`** (single WiFi gate). Now **split-driver**: the
  whole file is already linked only on WiFi ports — drop the
  internal gate, file selection handles it.
- **`drivers/display_merge/display_merge_pico.c`** (single
  PICOMITE-rp2350 gate). Now **split-driver** — the file is the
  rp2350-PicoMite real impl; the gate body is the rp2350-only path,
  so split into `display_merge_pico_rp2040.c` + `display_merge_pico_rp2350.c`
  with each variant linked from the matching port.
- **`drivers/gui_touch/gui_touch.c`** (single gate). Now **delete**
  — the file is itself the real-vs-stub split (`gui_touch.c` real /
  `gui_touch_stub.c` stub); the inner `#if` is redundant once
  linkage selects one.
- **`drivers/spi_lcd/spi_lcd_framebuffer.c`** (single gate). Now
  **delete** — file is already linked only on SPI-LCD ports.
- **`Memory.h`, `Draw.h`** (header-decl gates). Now **delete** —
  unconditional `extern` declarations link cleanly because the
  stub side defines the symbol.
- **`I2C.h:68`** (`SystemI2CTimeout`). Now **extract-hook** — move
  the timeout to a per-port `port_config.h` value (used as a
  *constant in expression*, not a predicate).
- **`ports/host_native/host_peripheral_stubs.c:503`** (single gate).
  Now **delete** — `setBacklight` already has a real impl in
  `picocalc_features.c` when keypad is present; host stub doesn't
  need to gate.
- **`drivers/vga_pio/vga_memory.c`** (single gate). Now
  **split-driver** — fold into the vga_mode_ops split in E4.

### Multi-board mechanism (folded into extract-hook + split-driver)

Earlier drafts of this audit tagged ~22 sites as "legacy-multi-board
exempt." That was wrong. The multi-board `OPTION RESET <BOARD>`
mechanism is the single biggest source of preprocessor coupling in
the legacy ports and must die along with the rest. Specifics:

- `ports/{pico,pico_rp2350,vga,vga_rp2350,hdmi_rp2350}/port_defaults.c`
  (16 sites) — re-categorized as **extract-hook**. Per-board profile
  data moves to `drivers/board_profiles/<board>.c` files (one file
  per profile: gamemite, picocalc, hdmi_usb, pico_computer, etc.).
  Each profile file exports a const struct of OPTION defaults; the
  `OPTION RESET <BOARD>` command becomes a runtime dispatch over a
  registry table built at link time. The dual-shipping legacy port
  directories continue to exist (so existing user `OPTION RESET`
  invocations keep working), but the file body becomes a single
  no-`#if` table walk.
- `configuration.h` (4 sites) — re-categorized as **extract-hook**.
  The USB-axis `FLASH_TARGET_OFFSET_USB` / `MagicKey_USB` /
  `HEAPTOP_USB` siblings become flat per-port `port_config.h`
  values; the WiFi / VGA gates become per-port flat values too.
  When all four sites are eliminated the file becomes a tiny
  unconditional include or disappears entirely.
- `ports/pico_sdk_common/picocalc_features.c` (1 site) —
  re-categorized as **split-driver**: split into `picocalc_features_real.c`
  (linked when `HAL_PORT_HAS_I2C_KEYPAD=1`) + `picocalc_features_stub.c`.
- `ports/pico_sdk_common/port_load_overrides.c` (1 site) —
  re-categorized as **split-driver**: per-board profile files under
  `drivers/board_profiles/` (same target as the per-port `port_defaults.c`
  cleanup above).

The acceptance gate for E8 has no multi-board exemption.

## Next stage entry points

After E1, the next concrete pieces of work in priority order:

1. **E2 — drop dead gates**: re-scan `HAL_PORT_HAS_GUICONTROLS` (10
   sites) and `HAL_PORT_HAS_I2C_KEYPAD` (14 sites). If most are now
   redundant after P3 / P5, finish those flags' source-level
   elimination as a quick win.
2. **E3 — split `drivers/spi_lcd/spi_lcd.c`**: highest single-file
   gate count; sets the pattern for E4.
3. **E5 — extract `Editor.c` keymap dispatch**: 50 sites collapse to
   one hook call per keystroke; high-leverage.
4. **E6 — decompose `PicoMite.c`**: 67 sites; defer until E3/E5
   establish the pattern.
