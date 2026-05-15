# Port Duplication & Drift Audit

Captures functions duplicated across port files where the bodies are
effectively port-agnostic (depend only on already-virtualised symbols).
Each finding is in the same shape as `MMgetline` was before consolidation
(see commit history around `runtime/runtime_getline.c`): a function whose
behaviour should be shared but where each port has its own copy, and those
copies have drifted in non-obvious ways.

The CLAUDE.md rule "When extracting a function/module into a new file,
transplant it verbatim …" addresses how these get *created*. This document
tracks the backlog of *existing* duplication that wasn't extracted cleanly.

Status legend: `pending`, `in-progress`, `done`.

---

## Live bugs surfaced by the audit

These are real defects, not just duplication. Fix before / during the
relevant finding's consolidation.

| Bug | File | Status |
|---|---|---|
| ~~pc386 `MMPrintString` never flushes~~ — **not a bug** on pc386: `SerialConsolePutC` ignores flush and writes byte-immediate to the UART; pc386_libc.c's `fflush` is a no-op shim. Output is never buffered, so there's nothing to flush. The "missing flush" *will* matter once Finding 1's shared `MMPrintString` lands and stdout-buffered ports start running pc386 builds — so the absence is a `console_adapter.stdout_flush` slot to leave NULL on pc386, not a literal `fflush(stdout)` call. | `ports/pc386/pc386_runtime.c:181` | n/a |
| pc386 has no escape-sequence decoder at all — serial-attached terminals can't send arrow keys / F-keys | `ports/pc386/pc386_runtime.c` (MMInkey region) | **done — Finding 2 consolidation** (pc386 now calls `mmbasic_escdecode_run` on `0x1b` from the serial path) |
| pc386 `mmbasic_timegm` const-violation — passes `tm` straight to libc `timegm` instead of copying first like host_native/esp32 | `ports/pc386/pc386_runtime.c:501-506` | **done** |
| Pico `MMInkey` doesn't normalise `0x7F` (DEL) → `BKSP` — backspace silently broken at end-of-line over USB-CDC serial (macOS Terminal / iTerm2 / `screen` / `picocom` all send `0x7F` for the Backspace key; REPL editor's `case DEL` only does forward-delete, no-op at end-of-line). host_native and ESP32 already normalise; Pico was the odd one out. Found by user, smoke gap (`pico_input_smoke.py` tested `0x08` instead of `0x7F`). | `ports/pico_sdk_common/pico_console.c` (MMInkey) | **done** — added 0x7F → BKSP, added `backspace_del` smoke case |

---

## Finding 1 — `MMputchar` / `MMPrintString` / `SSPrintString` / `myprintf`

Status: **pending** · Risk: **HIGH**

The byte-level console output routines.

| File | Lines |
|---|---|
| `runtime/runtime_console.c` | 251-271 |
| `ports/pico_sdk_common/pico_console.c` | 51-71, 157-171 |
| `ports/pc386/pc386_runtime.c` | 172-187, 243 |
| `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c` | 75-99 |

**Drift:** semantic. Same logical body in every port, but the trailing
flush differs:
- `runtime_console.c` (host/wasm/stdio/ansi): flush stdout AND telnet
- `pico_console.c`: per-byte flush=1 ordering quirk
- `esp32_mmbasic_console_glue.c`: `fflush(stdout)`; telnet drain happens
  per-byte in `SerialConsolePutC`
- `pc386_runtime.c`: **no flush at all** (live bug above)

**Why shareable:** body only depends on `MMputchar` and a port flush hook,
both already port-virtualised via `console_adapter` (`stdout_flush` +
`telnet_putc` slots already exist).

---

## Finding 2 — `MMInkey` / `MMgetchar` + escape-sequence decoder

Status: **partial — escape decoder shared** · Risk: **HIGH (decoder portion resolved)**

The escape-sequence decoder portion is now consolidated in
`runtime/runtime_console_escdecode.c`, exposing
`mmbasic_escdecode_run(read_byte_ms)` + `mmbasic_escdecode_pop_pushback()`.
All four ports' MMInkey now call into it; pc386 gained an escape decoder
where previously it had none. The shared decoder is the UNION of all
prior versions: legacy PicoMite (F1-F12, arrows, navigation, Shift-F3..F10
via both ESC[25~..34~ and ESC O 2 R) + xterm (ESC[H/F) + esp32
(ESC[Z → SHIFT_TAB, ESC[n;m~ modifier-parameter skip). 43/43 keymap-smoke
sequences pass on Pico hardware (`porttools/pico_keymap_smoke.py
--features any`).

The MMInkey *core* (buffer drain + scripted-key + sim-key + raw-mode
handling around the escape call) still lives per-port — each port's
input-source plumbing genuinely differs (web-console pre-decoded keys
on esp32, scripted-key harness on host, PS/2 IRQ on pc386, USB-CDC ring
on Pico). Those wrappers stay; the decoder body doesn't.

| File | Lines |
|---|---|
| `runtime/runtime_console.c` | 112-238 (`mmbasic_runtime_console_decode_escape_sequence`, `MMInkey`, `MMgetchar`) |
| `ports/pico_sdk_common/pico_console.c` | 69-155 (full F1-F12 + Shift-F3..F10 decoding) |
| `ports/pc386/pc386_runtime.c` | 198-239 (no escape decoder — live bug above) |
| `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c` | 127-294 (SHIFT_TAB, modifier-parameter skip, web-console pre-decoded keys) |

**Drift:** semantic — each port supports a different subset of terminal
escape sequences.
- pico_console: Shift-F3..F10 modifiers, `ESC O T → F5`, Pico-style polled timer
- esp32: `ESC [ Z → SHIFT_TAB`, modifier-parameter skipping (`ESC [ n ; m ~`)
- runtime_console (host/wasm/stdio): no Shift-F* modifiers, no SHIFT_TAB
- pc386: no escape decoder at all

**Why shareable:** escape decoding only needs a byte-read primitive with a
ms timeout, which every port exposes. Port-specific extras (e.g. web-console
pre-decoded keys) can route through the existing `console_adapter`.

`runtime/runtime_console.c` already has a shared escape decoder
(`mmbasic_runtime_console_decode_escape_sequence`) — but neither pico nor
esp32 uses it.

---

## Finding 3 — `port_terminal_handle_cls` / `port_terminal_emit_colour` no-op pair

Status: **pending** · Risk: **HIGH**

Three byte-identical no-op bodies in separate translation units.

| File | Lines |
|---|---|
| `ports/pico_sdk_common/terminal_hooks_noop.c` | 14-17 |
| `ports/host_native/host_terminal_hooks_noop.c` | 15-18 |
| `ports/pc386/pc386_runtime.c` | 490-493 |

(ESP32 provides real bodies in `esp32_terminal.c` — legitimate override.)

**Drift:** none — identical. Easiest consolidation win.

**Why shareable:** function bodies have zero port-dependent state. A single
shared `runtime/runtime_terminal_hooks_noop.c` plus ESP32's override is the
existing pattern.

---

## Finding 4 — `cmd_ireturn` + `check_interrupt`

Status: **done** · Risk: **HIGH (resolved)**

Both functions now share `runtime/runtime_interrupt.c`:
`mmbasic_runtime_check_interrupt(adapter)` and
`mmbasic_runtime_cmd_ireturn(adapter)` take a per-port
`mmbasic_runtime_interrupt_dispatch_adapter` (declared in
`runtime/runtime.h`) that names the port's service hook, TCP/UDP
pending hooks (optional), commandtbl_decode wrapper, error-state
save buffers, and the IRET trampoline token buffer.

host_native and esp32 each declare a single static adapter pointing
at their own state and replace their previous ~60-line bodies with
one-line forwarders. The UDP `udp_pending` slot is now wired on
both ports (esp32 plugs `esp32_udp_interrupt_pending`, host plugs
NULL because its UDPreceive flag is already driven by the shared
`shared/net/mm_net_service.c` path — but the slot exists for any
port that wants the extra pending poll). Pico's check_interrupt
in `MM_Misc.c` is intentionally untouched (it's a much richer
dispatcher with keyboard/GUI/pin checks beyond the TCP/UDP/MQTT
flat dispatch). pc386's stubs are also untouched.

Smoke gate: `porttools/host_basic_network_conformance.py` drives
`WEB TCP INTERRUPT` / `WEB UDP INTERRUPT` / `WEB MQTT SUBSCRIBE`
callbacks against the host build, exercising every dispatch branch
through the new shared function. Also fixed the test's embedded
BASIC to use B: (the host-available drive) instead of A: — that
was a pre-existing bug masking the test's runnability on host.

| File | Lines |
|---|---|
| `ports/host_native/host_runtime.c` | 486-550 |
| `ports/esp32_s3_metro/main/esp32_runtime.c` | 92-167 |

**Drift:** partial-overlap. `cmd_ireturn` is line-for-line identical except
for the static-var name prefix (`host_` vs `s_`). `check_interrupt` differs
in: service-function name, ESP32 has an extra `esp32_udp_interrupt_pending()`
check (host lacks it — **likely latent bug**), and the abort-adapter wiring
(ESP32 wires `before_abort` / `after_poll`, host doesn't).

**Why shareable:** `runtime/runtime_interrupt.c` already provides the
heavy lifting. The remaining state (3 ints + 1 short buffer) belongs in a
per-port adapter struct; the divergent pieces (TCP/UDP pending hooks,
yield/wdt-reset hooks) are exactly what adapters are for.

---

## Finding 5 — Peripheral-stub command/function table

Status: **pending** · Risk: **MEDIUM-HIGH**

~60+ `cmd_X(void) {}` and `int fun_Y(void) { return 0; }` no-op stubs.

| File | Lines | Bodies |
|---|---|---|
| `ports/host_native/host_peripheral_stubs.c` | 646 | 74 stubs |
| `ports/pc386/pc386_peripheral_stubs.c` | 616 | 95 stubs |
| `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c` | 672 | 75 stubs |

**Drift:** identical for the overlapping set. ~1900 LOC total.

**Why shareable:** bodies are empty / `error("Not supported")` patterns.
Whichever TU provides the symbol wins; a shared
`runtime/runtime_peripheral_stubs.c` could supply weak-default no-ops, with
ports overriding the few they actually implement.

---

## Finding 6 — `port_*` no-op stubs (~25 hooks × 3 ports)

Status: **pending** · Risk: **MEDIUM-HIGH**

| File | Range |
|---|---|
| `ports/host_native/host_runtime.c` | 310-311, 811-863, 870, 877, 883-933, 941- |
| `ports/host_native/host_peripheral_stubs.c` | 595, 599-600, 621, 642-643 |
| `ports/pc386/pc386_runtime.c` | 289, 364-365, 397-446, 450-468 |
| `ports/esp32_s3_metro/main/esp32_default_hooks.c` | 33-94 |
| `ports/esp32_s3_metro/main/esp32_globals.c` | 25, 31 |

Hooks include: `port_usb_count`, `port_usb_hid_field`,
`port_print_supported_boards`, `port_factory_reset_board`,
`port_display_option_setter`, `port_print_display_options`,
`port_print_lcd_spi`, `port_print_keyboard_heartbeat`,
`port_print_usb_kb_repeat`, `port_clear_lcd_spi_if_shares_system`,
`port_pinno_alias_for_name`, `port_pin_is_reserved_alias`,
`port_pin_reserved_label`, `port_lcd320_option_setter`,
`port_misc_option_setter`, `port_pico_pins_option_setter`,
`port_heartbeat_option_setter`, `port_system_lcd_spi_option_setter`,
`port_audio_i2s_pio_slice`, `port_poke_display_panel`,
`port_select_error_prompt_font`, `port_clear_runtime_display_reset`,
`port_error_restore_console_surface`, `port_error_show_lcd_banner`,
`port_try_find_subfun_hash`, `port_try_find_label_hash`,
`port_try_check_var_subfun_collision`,
`port_prepare_program_finalize_subfun`,
`port_bc_bridge_clear_subfun_hash`, `port_bc_bridge_rehash_subfun`,
`port_bc_crash_save_fault_regs`, `port_bc_crash_get_sp`,
`port_set_default_options`, `port_runtime_abort_dma`,
`port_runtime_disable_watchdog`, `port_repl_wifi_arch_init_and_connect`.

**Drift:** mostly identical, occasional cosmetic differences (host says
`error("Not supported on host")` for picocalc hooks; pc386 returns 0
silently; esp32 omits some entirely).

**Why shareable:** empty / identity / fixed-error bodies — no port state.
Same `runtime/runtime_default_hooks.c` weak-linkage default pattern as
Finding 5.

See also: feedback memory entry `feedback_port_prefix_pollution.md`.

---

## Finding 7 — `port_drivecheck_remap` / `port_filesystem_prefix` defaults

Status: **pending** · Risk: **MEDIUM**

| File | Notes |
|---|---|
| `ports/pico_sdk_common/cmd_files_hooks.c:80` | default identity |
| `ports/host_native/host_runtime.c:789, 791-794` | default identity |
| `ports/esp32_s3_metro/main/esp32_cmd_files_hooks.c:36, 38-40` | default identity |
| `ports/pc386/pc386_runtime.c:294-296, 325-330` | real DOS A:/B:/C: routing — legitimate override |

**Drift:** identical for the three default copies.

**Why shareable:** pure function of an integer arg, returns input or
compile-time string. No port state.

---

## Finding 8 — `mmbasic_timegm` / `mmbasic_gmtime` shim wrappers

Status: **pending** · Risk: **MEDIUM** (one live bug — see top)

| File | Lines |
|---|---|
| `ports/host_native/host_runtime.c` | 715-725 |
| `ports/esp32_s3_metro/main/esp32_compat.c` | 41-47 |
| `ports/pc386/pc386_runtime.c` | 501-506 |

**Drift:** semantic. host_native and esp32 copy the input `tm` before
calling `timegm` (POSIX `timegm` may mutate). pc386 passes through —
**const-violation** (live bug, fix during consolidation).

**Why shareable:** the header rename trick (`host_platform.h`) is
project-wide; the wrapper bodies are just `timegm(&tmp)` / `gmtime(timer)`.

---

## Finding 9 — `cmd_files_*` lifecycle hooks

Status: **pending** · Risk: **MEDIUM**

`cmd_files_save_program_context`, `cmd_files_restore_program_context`,
`cmd_files_pump_console_key`, `cmd_load_post_cleanup`.

| File | Lines |
|---|---|
| `ports/pico_sdk_common/cmd_files_hooks.c` | 38-65 (real bodies) |
| `ports/host_native/host_runtime.c` | 730-763 |
| `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c` | 141-207 |
| `ports/pc386/pc386_runtime.c` | 336-349 |

**Drift:** partial-overlap, stale copy-paste comments
(`esp32_peripheral_stubs.c:159-164` contains a comment saying "**Host** can't
SaveContext + InitHeap mid-FRUN" — copy-pasted from host_runtime.c without
updating).

**Why shareable:** `mmbasic_runtime_post_load_longjmp` is already the
shared helper. `pump_console_key` differs by which input source to poll —
exactly the `console_adapter` pattern.

---

## Finding 10 — `getConsole` / `kbhitConsole` defaults

Status: **pending** · Risk: **LOW**

`int getConsole(void) { return -1; }` and `int kbhitConsole(void) { return 0; }`.

| File | Lines |
|---|---|
| `ports/host_native/host_runtime.c` | 661, 690 |
| `ports/pc386/pc386_runtime.c` | 241-242 |

(pico_console.c and esp32 supply real implementations.)

**Drift:** identical. Trivial — can ride along on Finding 1's consolidation.

---

## Also worth investigating (lower priority)

- **`host_runtime_finish` / `host_runtime_timed_out`** — pc386 reuses the
  `host_*` namespace verbatim (`pc386_runtime.c:117-118`). Naming smell;
  non-host port shouldn't export `host_*` symbols.
- **`port_apply_default_console_colors`** — implemented separately in
  `pico/port_defaults.c`, `pico_rp2350/port_defaults.c`, `vga/port_defaults.c`,
  `vga_rp2350/port_defaults.c`, `web/port_defaults.c`,
  `web_rp2350/port_defaults.c`, `dvi_wifi_rp2350/port_defaults.c`,
  `hdmi_rp2350/port_defaults.c`, `vga_wifi_rp2350/port_defaults.c`,
  `host_runtime.c`, `pc386_runtime.c`, `esp32_terminal.c`. Per-board
  diffs may be legitimate; needs a side-by-side review.
- **`uSec`** — host_native (`{ (void)us; }`), pc386
  (`{ hal_time_sleep_us((uint32_t)us); }`), esp32_compat.c (similar).
  Three one-liners over the HAL.
- **`ClearExternalIO`, `SoftReset`, `closeframebuffer`, `clear320`,
  `initMouse0`, `restorepanel`** — empty no-op bodies repeated in
  host_runtime.c, pc386_runtime.c, esp32_globals.c. Same shape as
  Finding 6, can ride along.

## Telnet IAC parser consolidation — DONE (retried successfully)

Status: **done** · Risk: **HIGH (resolved — buggy Pico parser replaced
with canonical 5-state machine)**

The three RFC 854 inbound parsers
(`MMtelnet.c:pico_telnet_receive_bytes`,
`esp32_telnet.c:esp32_telnet_receive_bytes`,
`host_web.c:host_telnet_receive_bytes`) handle the same protocol with
different code. Pico is buggy — drops the entire TCP segment if it
starts with IAC (`MMtelnet.c:115`), losing keystrokes that arrive in
the same segment as a negotiation reply. ESP32 and host both
implement the correct 5-state machine (DATA / IAC / OPT / SB / SB_IAC).

**Attempt** (rolled back): created `shared/net/mm_net_telnet_rx.{c,h}`
with the canonical 5-state machine and replaced each port's
`*_receive_bytes` with a one-line wrapper that calls
`mm_net_telnet_rx_feed`. Build was clean on all four targets but the
Pico telnet smoke regressed badly — 43/43 keymap → 14/43, flaky
between runs, and the device sometimes wedged in the editor.

**Root cause**: I worked around `MM_Misc.h:104`'s `time_t` reference
(no `<time.h>` include) by hand-rolling `extern` decls in the shared
parser instead of including `MMBasic_Includes.h` / `Hardware_Includes.h`.
But `MMAbort` is `volatile int` (`PicoMite.c:141`) — 4 bytes — and I
declared it `volatile bool`. C linkage doesn't check types across
TUs; my bool-sized stores left 3 bytes of stale data in MMAbort, so
BASIC saw spurious aborts (INKEY$ exiting early, INPUT bailing,
longjmps into the editor). Same shape of bug for any other type
I'd silently mismatched.

**Retry (commit `2e8cad7`)**: with `MM_Misc.h` now self-contained,
the shared parser file safely uses `MMBasic_Includes.h` +
`Hardware_Includes.h`, picking up the canonical types
(`volatile int MMAbort`, `volatile bool Keycomplete`, etc). Full
12-cell smoke matrix passes on both boards (Pico WebMite RP2350B +
ESP32-S3 Metro, USB-CDC + telnet, input + console + keymap). The
state-machine body was correct all along — only the type discipline
was wrong.

**Lesson** (now in CLAUDE.md): when sharing a function across ports,
use `MMBasic_Includes.h` + `Hardware_Includes.h` even when the header
chain has annoying transitive dependencies. C linkage doesn't validate
types across TUs; a one-byte mismatch on a global silently corrupts
adjacent state in ways that look like flaky tests hours later.

---

## Recommended sequence

1. **pc386 live bugs** (top of doc) — small, isolated fixes that
   eliminate two real defects (flush + timegm const). Defer the escape
   decoder until Finding 2 lands.
2. **Finding 3** — three identical no-op TUs → one shared. Trivial; good
   warmup for the consolidation pattern.
3. **Finding 1** — `MMputchar` / `MMPrintString` / `SSPrintString` into
   the existing `console_adapter` pattern. Already 60% done in
   `runtime/runtime_console.c`; just need to delete the three copies and
   wire each port's adapter slots.
4. **Finding 2** — `MMInkey` + escape decoder, with the union of feature
   sets. Biggest payoff (fixes pc386 arrow-key bug; brings web-console
   pre-decoded keys, SHIFT_TAB, Shift-F* into the shared spine).
5. Findings 4 / 5 / 6 — bigger but more mechanical.
6. Findings 7 / 8 / 9 / 10 — small follow-ups.
