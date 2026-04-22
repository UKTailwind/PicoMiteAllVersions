# Real HAL — Port-config mechanism, state hoist, combinatorial gates

**Important:** the initial port-config implementation in commits `2c034d7` / `61cb08e` violated the rule "no preprocessor branching in core" by renaming `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` and hiding the original `#ifdef rp2350` inside `hal/hal_port_config.h`. Those commits are scheduled for revert in the fixup plan (`../real-hal-fixup-plan.md`, phase F1). What follows is the design the mechanism was *supposed* to deliver; read it with the fixup plan in hand.

## The problem

Many `#ifdef` blocks in core files don't dispatch to different *code* — they select different *constants*: how many PWM slices, how many PIO blocks, how many pins, which ADC channel is the temperature sensor, what the board name string is, whether PSRAM exists. These don't need HAL functions — they need compile-time constants that vary per port.

## The mechanism (correct shape)

Each port directory provides a `port_config.h` that defines a set of required constants. Core files `#include "port_config.h"` (resolved via include path, not relative path — each port directory is on the include path for its build). The constants replace `#ifdef rp2350` / `#ifdef PICOMITEVGA` / `#ifdef PICOCALC` blocks with direct references.

**Required constants (initial set, grows as phases eliminate ifdefs):**

```c
// port_config.h — provided by each port directory
#define PORT_BOARD_NAME        "PicoMite VGA RP2350"
#define PORT_PWM_SLICE_COUNT   12          // 8 on rp2040, 12 on rp2350
#define PORT_PIO_COUNT         3           // 2 on rp2040, 3 on rp2350
#define PORT_PIN_COUNT         48          // 30 on rp2040, 48 on rp2350
#define PORT_ADC_TEMP_CHANNEL  4           // 4 on both, but rp2350 has more ADC channels
#define PORT_ADC_CHANNEL_COUNT 5           // rp2040=5, rp2350=6+
#define PORT_HAS_PSRAM         1           // 0 or 1
#define PORT_HAS_NETWORK       0           // 1 only on PICOMITEWEB
#define PORT_HAS_DISPLAY       1           // 0 on mmbasic_stdio
#define PORT_MAX_CPU_KHZ       250000      // rp2040=200000, rp2350=250000
```

**Usage in core:** `for (int i = 0; i < PORT_PWM_SLICE_COUNT; i++)` instead of `#ifdef rp2350 ... 12 ... #else ... 8 ... #endif`. The compiler dead-code-eliminates unreachable paths when a constant is 0/1, so `if (PORT_HAS_PSRAM) { ... }` compiles to nothing on ports without PSRAM — same binary effect as `#ifdef`, no preprocessor branching in core.

**What this means in practice:**
- `port_config.h` is one file per port. It may contain `#ifdef` **only** to select between platform variants the port itself spans (rare). Most ports write their constants literally.
- Constants are consumed as **values** in C expressions, never as operands to `#if`. `#if PORT_HAS_PSRAM` in core is forbidden; `if (PORT_HAS_PSRAM) { ... }` is the pattern.
- Port-config is *not* a general shim header for hiding conditionals. If you catch yourself writing `#if PORT_HAS_NETWORK` in core, the right answer is a HAL function call (`hal_net_init()` that's a no-op on non-network ports), not a wrapped conditional.

The host port's `port_config.h` defines `PORT_HAS_DISPLAY 1` (it simulates a display), `PORT_HAS_NETWORK 0`, `PORT_PWM_SLICE_COUNT 0`, etc. The `mmbasic_stdio` port defines `PORT_HAS_DISPLAY 0`, `PORT_HAS_NETWORK 0`, `PORT_PWM_SLICE_COUNT 0` — any BASIC program that tries to use those features gets an error from the hard-error HAL stubs, not from an `#ifdef` in core.

This mechanism is introduced alongside Phase 3b (which needs it most heavily for the rp2350 PWM/PIO/pin-count blocks) and used by every subsequent phase. The first implementation got the pattern wrong — see the fixup plan.

## Combinatorial gates: drivers may have local `#ifdef`s

The "no `#ifdef` in core" rule does **not** mean "no `#ifdef` anywhere." Many `Draw.c` gates are *algorithmic*:

- `Draw.c:90-106` selects fonts based on `PICOMITEVGA && HDMI`.
- `Draw.c:577-606` `ClearScreen` branches on `DISPLAY_TYPE` (SCREENMODE2/3/4/5) with separate paths for `WriteBuf == LayerBuf` vs `SecondLayer`; SCREENMODE4/5 only exist under HDMI.
- `Draw.c:3249-3340` is 4-bit nibble packing for VGA SCREENMODE2 vs 8-bit for HDMI SCREENMODE5.

These are **driver-private**: they belong inside the relevant driver, where local `#ifdef` (or runtime mode dispatch *within* the driver) is fine. The contract is that the gate is local to its driver — nothing outside the driver needs to know. A driver may freely contain `#ifdef HDMI` or `#ifdef rp2350` because the driver only compiles into ports that selected it.

We will **not** create cross-product driver variants (`vga_pio_rp2040`, `vga_pio_rp2350`, `hdmi_rp2350_usb`…). One driver per peripheral; the driver internally handles MCU-version differences.

## Cross-cutting state — hoisted in Phase 0.5

Several globals are referenced by both the interpreter and would-be drivers:

- **Display state:** `HRes`, `VRes`, `FontTable[]`, `gui_font*`, `CursorTimer`, `spritebuff[]`, `struct3d[]`, `layer_in_use[]`, `frameBufferMutex` — currently defined in `Draw.c`, referenced from `MM_Misc.c`, `External.c`, `Editor.c`, `Commands.c`, `GUI.c`, and the VM.
- **Pin state:** `PinDef[]` — defined in `External.c`, written from both interpreter (`cmd_setpin`) and host stub init.
- **Option block:** `Option` struct — touched everywhere; persisted by `hal_flash`.
- **Audio state:** sample buffers, voice slots — currently in `Audio.c`.

If `Draw.c` is split into `drivers/<display>/` files in Phase 7+, every driver that references `HRes` would either re-declare it (multiple-definition link error) or include `Draw.c` indirectly (defeats the split). **Phase 0.5 hoisted these globals into `core/state/` files** (`core/state/display_state.c`, `core/state/pin_state.c`, `core/state/option_state.c`, `core/state/audio_state.c`) before any driver split begins. The current files keep `extern` declarations; the global definitions moved. This was mechanical, low-risk, and unblocks every later phase.

Plan correction landed during Phase 0.5: `PinDef[]` is board-level const in `PicoMite.c`/`host_runtime.c`, not mutable core state — stays where it is.
