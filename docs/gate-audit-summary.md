# HAL gate audit — Stage E1 output

Survey of every `#if HAL_PORT_HAS_*` / `#if HAL_PORT_IS_*` directive
across the source tree, classified by elimination strategy. Per-site
data in `docs/gate-audit.csv`; this file is the rolled-up summary.

## Totals

360 gate sites, classified as:

| Category | Sites | %  |
|---|---:|---:|
| extract-hook        | 209 | 58% |
| split-driver        | 102 | 28% |
| value-in-expression |  26 |  7% |
| legacy-multi-board  |  22 |  6% |
| delete (E1 result)  |   1 | (none flagged in this pass; revisit during E2) |

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
| `AllCommands.h`                                    | 14 | value-in-expression | Token-table conditional rows; convert to runtime gating in the lookup. |
| `drivers/vga_pio/vga_ops.c`                        | 10 | split-driver        | NEXTGEN + GUICONTROLS gates. |
| `ports/pico_sdk_common/print_display_options.c`    |  9 | extract-hook        | OPTION LIST per feature → hooks. |
| `Hardware_Includes.h`                              |  9 | split-driver        | Replace per-feature includes with port-config-driven include. |
| `Touch.c`                                          |  8 | split-driver        | Gate the entire file by linkage; drop in-file gates. |
| `Custom.c`                                         |  6 | extract-hook        | CSUB / CFunction WiFi gates → hook. |
| `I2C.c`                                            |  5 | extract-hook        | I2C-keypad protocol → `drivers/i2c_picocalc_kbd` hook. |
| `ports/pico/port_defaults.c`                       |  4 | legacy-multi-board  | Skip — OPTION RESET handlers in legacy multi-board port. |
| `ports/pico_sdk_common/cmd_files_hooks.c`          |  4 | extract-hook        | cmd_files USBKEYBOARD axis → hook. |
| `ports/pico_sdk_common/clear_runtime_port.c`       |  4 | extract-hook        | |
| `drivers/hdmi/hdmi_modes.c`                        |  4 | split-driver        | HDMI vs DVI mode tables. |
| `configuration.h`                                  |  4 | legacy-multi-board  | Skip — translation layer for legacy multi-board ports. |
| (long tail of 1–3 site files)                      |  … | (mixed)             | |

## Per-category notes

### extract-hook (209 sites, 58%)

The biggest bucket. Most sites are HAL operations that vary per port —
boot sequencing, keyboard input, OPTION setters, `cmd_files` plumbing.
Each maps to a HAL hook in `hal/hal_*.h` plus a real-impl + stub pair
under `drivers/<topic>/` or `ports/pico_sdk_common/`.

Approximate hook count to define: ~30 new hooks across the 209 sites
(many sites collapse to the same hook).

### split-driver (102 sites, 28%)

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

### value-in-expression (26 sites, 7%)

Mostly scattered single-site files (`bc_vm.c`, `MMtcpserver.c`,
`drivers/display_merge/display_merge_pico.c`, etc.) plus
`AllCommands.h`'s 14 token-table rows.

Pattern:

```c
// before
#if HAL_PORT_HAS_X
    do_x();
#else
    do_y();
#endif

// after
if (HAL_PORT_HAS_X) do_x();
else                do_y();
```

Requires both `do_x()` and `do_y()` to *link* on every port. Most of
these already do — these are sites where the `#if` is preprocessor
laziness rather than a hard linkage requirement.

`AllCommands.h`'s token rows are different: the table is a sparse
const array; conditional rows can become runtime predicates inside
the dispatch function rather than build-time inclusions.

### legacy-multi-board (22 sites, 6%)

Exempt from elimination. These are:

- `ports/{pico,vga,vga_rp2350,pico_rp2350,hdmi_rp2350}/port_defaults.c`
  — `OPTION RESET <BOARD>` handlers in the legacy multi-board ports
  (16 sites total).
- `configuration.h` (4 sites) — translation layer that maps the
  legacy `-DPICOCALC=true|false` CMake variable to the right
  `HAL_PORT_*` constants for the legacy multi-board ports.
- `ports/pico_sdk_common/picocalc_features.c` (1 site) — already a
  real-vs-stub split inside the file.
- `ports/pico_sdk_common/port_load_overrides.c` (1 site) — board
  profile defaults override.

The post-decascade plan tags these as "leave legacy multi-board ports
as they are." The acceptance gate for E8 will skip these files
explicitly.

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
