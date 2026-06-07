# Port-config palette: prune + rename recommendations

Inventory taken on `hal-decascade` after batch 18. The 17 `HAL_PORT_HAS_*`
macros and friends fall into four buckets. This file is the running
recommendation list — execute as one or two separate batches when ready.

## Bucket 1 — Delete (chip-family redundant with `#ifdef rp2350`)

These macros encode "is this an rp2350-class chip?" twice — once in the
SDK's `rp2350` target macro, once in port_config. The driver-pair linkage
already carries the rest of the discrimination. Zero non-port-config
gate uses today.

| Macro | Active gate uses | Action |
|---|---|---|
| `HAL_PORT_HAS_PSRAM` | 0 | Delete from all 9 port_config.h |
| `HAL_PORT_HAS_PIO2` | 0 (last gate dropped batch 18) | Delete |
| `HAL_PORT_HAS_DEFINES` | 1 (defines_loader.c — rp2350-only file) | Replace with `#ifdef rp2350` inside the file, delete macro |
| `HAL_PORT_HAS_UPNG` | 0 (LoadPNG promoted to universal in batch 18) | Delete |
| `HAL_PORT_HAS_FAST_TIMER` | 1 (External.c — comment only) | Drop comment ref, delete macro |
| `HAL_PORT_HAS_VGA_PIO` | 0 | Delete |

**Net: -6 macros × 9 ports = -54 port_config lines.**

## Bucket 2 — Delete (zero active gates after recent batches)

The flag still exists in port_config.h but no code reads it as a gate.
Pure relics.

| Macro | Notes | Action |
|---|---|---|
| `HAL_PORT_HAS_HEARTBEAT` | Replaced by `drivers/heartbeat/` driver pair in batch 18 | Delete |
| `HAL_PORT_HAS_SSD1963` | Replaced by runtime `Option.DISPLAY_TYPE>=SSDPANEL` in batch 18 + universal `SetBacklightSSD1963` stub | Delete |
| `HAL_PORT_HAS_INT5` | Used only in External.c comments / dead-code suppression | Delete after a sweep that drops the comments + INT5 setter inlines |

**Net: -3 macros.**

## Bucket 3 — Rename (current name is misleading)

| Macro | Problem | Proposed name |
|---|---|---|
| `HAL_PORT_HAS_PICOMITE` | Misleading — every port is "a PicoMite". The macro really means "this port has the original PicoMite SPI-LCD framebuffer board layout (Geoff Graham PGA2350 + ILI9488BUFF/NEXTGEN displays)". | `HAL_PORT_HAS_SPI_LCD_FB` |
| `HAL_PORT_HAS_USB_KEYBOARD` | Implies `0` means "no keyboard"; really means "USB-host backend selected (vs. PS/2 matrix backend)". Both backends always exist via driver-pair linkage. | `HAL_PORT_KEYBOARD_USB_HOST` |
| `HAL_PORT_HAS_PICOMITE` (uses) | 2 sites: 1 vacuous (file-already-gated-by-linkage), 1 redundant (Option.DISPLAY_TYPE>=NEXTGEN runtime guard suffices) | Drop both gates first, then delete the macro entirely |

`HAS_PICOMITE` is the worst offender — recommend deleting outright since
both call sites are removable. `HAS_USB_KEYBOARD` should be renamed
because deletion would force the rename anyway (driver-pair linkage
needs *some* discriminator).

## Bucket 4 — Keep (true port-shape axes)

These earn their keep — each drives a real driver-pair linkage decision
in `port_sources.cmake` and reflects an axis orthogonal to chip family.

| Macro | Axis |
|---|---|
| `HAL_PORT_KEYBOARD_USB_HOST` *(renamed)* | USB-host vs PS/2-matrix keyboard backend |
| `HAL_PORT_HAS_WIFI` | CYW43 + lwIP stack vs no networking |
| `HAL_PORT_HAS_HDMI` | HSTX HDMI scanout vs VGA-PIO scanout (within VGA family) |
| `HAL_PORT_IS_VGA` | Provisional legacy VGA-family discriminator only. It currently covers VGA-PIO + HDMI + DVI and is badly named for non-Pico VGA implementations. Drain it into display-driver capability bits before treating new VGA backends as "VGA" ports. |
| `HAL_PORT_HAS_I2C_KEYPAD` | PicoCalc keypad-MCU axis |
| `HAL_PORT_HAS_GUICONTROLS` | GUI widget suite (rp2350 only by RAM budget) |
| `HAL_PORT_HAS_MP3` | MP3 decoder (rp2350 only by RAM/CPU budget) |
| `HAL_PORT_HAS_NEXTGEN_DISPLAY` | NEXTGEN framebuffer panels — **but** all 7 active uses are runtime folds in print_display_options.c that need batch-18-style hook treatment first |

## Recommended batch sequence

**Batch 19a — bucket 1 + 2 deletions** (no behaviour change):

1. Drop `HAS_PSRAM`, `HAS_PIO2`, `HAS_UPNG`, `HAS_FAST_TIMER`,
   `HAS_VGA_PIO`, `HAS_HEARTBEAT`, `HAS_SSD1963`, `HAS_INT5` from all
   9 port_config.h files.
2. Replace `HAS_DEFINES` use in `defines_loader.c` with `#ifdef rp2350`,
   then drop the macro.
3. Drop `HAS_FAST_TIMER` comment reference in External.c.
4. -9 macros × 9 ports ≈ 80 lines deleted from port_config.h files.

**Batch 19b — `HAS_PICOMITE` extinction**:

1. Drop the always-true gate at `display_merge_pico.c:190` (file is
   only linked when the flag is 1 — vacuous).
2. Drop the runtime-equivalent gate at `clear_runtime_port.c:26`
   (Option.DISPLAY_TYPE>=NEXTGEN handles it).
3. Delete `HAL_PORT_HAS_PICOMITE` from all 9 port_config.h files.

**Batch 19c — `HAS_USB_KEYBOARD` rename**:

1. Mechanical rename to `HAL_PORT_KEYBOARD_USB_HOST` across:
   port_config.h × 9, configuration.h, port_tokens.h × 9 (USB-axis
   gates), all driver and ports/ source files using it.
2. Update CMakeLists.txt's `target_compile_options` USB-axis switch.

**Batch 19d — drain `HAS_NEXTGEN_DISPLAY`**:

1. Convert the 7 runtime folds in `print_display_options.c` to
   per-display-driver hooks (display_merge_pico.c real impl, vga + hdmi
   stubs).
2. Delete the macro.

**Batch 19e — drain `HAL_PORT_IS_VGA` into display capabilities**:

1. Audit every `HAL_PORT_IS_VGA` site and classify whether it really means
   VGA-family scanout, Pico PIO VGA implementation, BASIC screen modes,
   QVGA/tile-array layout, HDMI/DVI sharing, or non-SPI-LCD option storage.
2. Add a display-driver capability surface populated by the linked/initialized
   display backend.
3. Replace shared code checks with capability queries or operation hooks.
4. Rename any remaining narrow build-time selector to the exact thing it means,
   or delete it if source-list composition already expresses the choice.

After batches 19a through 19d: **17 → 7 macros**. After 19e, the remaining
display macros better match the actual driver capabilities instead of the
legacy VGA-family bucket. port_config.h files shrink by ~50%. The "is this
PicoMite" naming embarrassment is gone.
