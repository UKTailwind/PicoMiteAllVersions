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

Status: **done — `MMputchar`/`MMPrintString`/`SSPrintString` shared; `myprintf` left per-port (legitimate variation)** · Risk: **HIGH (resolved)**

`MMputchar` was extracted earlier into `runtime/runtime_console_putchar.c`
(byte-identical bodies across all four ports — only depends on the
project-wide symbols `putConsole` and `MMCharPos`).

`MMPrintString` and `SSPrintString` now share `runtime/runtime_console_printstring.c`.
The remaining per-port trailing-flush differences were either equivalent
or absorbed by the `mm_runtime_console_adapter` slots `stdout_flush` and
`telnet_putc`. Specifically:

- The shared body's `bulk_flush()` calls `adapter->stdout_flush()` when an
  adapter is set, or falls back to `fflush(stdout)` otherwise. It also
  calls `adapter->telnet_putc(0, -1)` only when the adapter plugs it.
- host/wasm/stdio/ansi: plug the adapter (host_native already did) — same
  flush AND telnet-drain behaviour as before.
- Pico: no adapter — falls back to `fflush(stdout)`. The previous
  "last-byte fused flush" via `MMputchar(*s, 1)` was equivalent to
  trailing `fflush(stdout)` because `TelnetPutC` ignores `flush=1` (only
  the `-1` sentinel drains) and `console_cdc_putc(c, 1)` is literally
  `putc(c, stdout); fflush(stdout);`. Verified on PicoCalc serial + telnet
  (23/23 console smoke pre- and post-consolidation).
- ESP32: no adapter — falls back to `fflush(stdout)`, matching the prior
  body. Telnet drain remains via the 5 ms timer in `esp32_telnet_poll`
  (adapter's `telnet_putc` left NULL deliberately, so the shared body
  doesn't force a per-call drain). Verified on ESP32-S3 serial + telnet
  (23/23 console smoke).
- pc386: no adapter — falls back to `fflush(stdout)` which is
  `pc386_libc.c`'s no-op shim, preserving the "no flush" behaviour.

Adapter storage + accessors moved out of `runtime_console.c` into
`runtime/runtime_console_adapter.c` so non-host ports (Pico/ESP32/pc386)
that don't link `runtime_console.c` can still resolve the slot.

`myprintf` is intentionally left per-port: host_native routes it through
`host_prints` → `host_print` which bypasses the console-routing machinery
(used for raw-stdout writes only). ESP32 and pc386 keep their
`{ MMPrintString(s); }` 1-liners. No `myprintf` caller exists outside
its own definitions, so the per-port variation has no behavioural impact.

| Site | What remains |
|---|---|
| `runtime/runtime_console_printstring.c` | shared body of `MMPrintString` / `SSPrintString` (the consolidation target) |
| `runtime/runtime_console_adapter.c` | shared `mm_runtime_console_adapter` storage + set/get |
| `ports/pico_sdk_common/pico_console.c:105` | the per-port copy has been removed; pointer comment only |
| `ports/pc386/pc386_runtime.c:176`, `:250` | per-port copies removed; `myprintf` 1-liner stays |
| `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:85` | per-port copies removed; `myprintf` 1-liner + batching-rationale comment stay |
| `ports/host_native/host_runtime.c:627` | `myprintf` → `host_prints` (legitimate override) |

Gate: `porttools/pico_console_smoke.py` (port-agnostic despite the name —
runs against ESP32 serial + telnet too). 23/23 cases on each of {Pico
serial, Pico telnet, ESP32 serial, ESP32 telnet} pre- and
post-consolidation. host `run_tests.sh` 244/244. WASM rebuild clean.

---

## Finding 2 — `MMInkey` / `MMgetchar` + escape-sequence decoder

Status: **done (decoder shared; MMInkey wrappers intentionally per-port)** · Risk: **HIGH (resolved)**

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

Status: **done** · Risk: **HIGH (resolved)**

Consolidated into `runtime/runtime_terminal_hooks_noop.c`. ESP32 supplies
the strong override in `esp32_terminal.c` (emits ANSI escapes); the
three previous byte-identical no-op TUs
(`ports/pico_sdk_common/terminal_hooks_noop.c`,
`ports/host_native/host_terminal_hooks_noop.c`, inline copy in
`ports/pc386/pc386_runtime.c`) are gone. ESP32 deliberately does not
link the shared TU.

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

Status: **pending — superseded by modular stub-driver plan** · Risk: **MEDIUM-HIGH**

~60+ `cmd_X(void) {}` and `int fun_Y(void) { return 0; }` no-op stubs.

| File | Lines | Bodies |
|---|---|---|
| `ports/host_native/host_peripheral_stubs.c` | 646 | 74 stubs |
| `ports/pc386/pc386_peripheral_stubs.c` | 616 | 95 stubs |
| `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c` | 672 | 75 stubs |

**Drift:** identical for the overlapping set (~52 stub names appear
byte-identically in all three ports). ~1900 LOC total.

**Don't tackle as a single-file consolidation.** The naïve approach — one
shared `runtime/runtime_peripheral_stubs.c` with weak defaults — is the
wrong shape. The canonical plan is in
[`real-hal-plan.md` § Modular stub drivers](real-hal-plan.md#modular-stub-drivers-proposed-direction):
break each subsystem into `drivers/<subsystem>/<subsystem>_stub.c`
paired with real driver TUs (`<subsystem>_pico.c`,
`<subsystem>_esp32.c`, …), selectively linked via each port's source
manifest. No weak symbols, no preprocessor gating — the same explicit-
composition discipline as the rest of the real-HAL work. Default
posture is `error("X not supported on this port")` (pc386's existing
posture for `cmd_pwm` / `cmd_servo`); a global `-DHAL_STUBS_SILENT`
flag flips every stub to silent no-op for new-port bringup.

See also [`port-stub-audit.md`](port-stub-audit.md) — the
feature-completeness inventory that catalogues which stubs are
justified, which are real feature gaps, and which are bugs (e.g.
host's `cmd_pin` no-op vs. real `fun_pin`). The two docs share the
same file inventory but have different agendas: this one tracks
duplication, the stub audit tracks coverage.

Migration starts with **I2C as the pattern carve-out**, then the easy
single-subsystem groups (SPI, IR, OneWire, DHT22/DS18B20, RTC,
watchdog, PIO, AES, xregex, PNG), then state-bearing groups (audio,
GPS, mouse, keypad), then display-adjacent groups. The three
`*_peripheral_stubs.c` files get deleted when empty.

---

## Finding 6 — `port_*` no-op stubs (~25 hooks × 3 ports)

Status: **pending — superseded by modular stub-driver plan** · Risk: **MEDIUM-HIGH**

Rides along on the same driver-layout refactor as Finding 5; the canonical
plan is in
[`real-hal-plan.md` § Modular stub drivers](real-hal-plan.md#modular-stub-drivers-proposed-direction).
The empty/identity/fixed-error `port_*` hooks belong with their owning
subsystem driver (e.g. `port_audio_i2s_pio_slice` with `drivers/audio/`,
`port_poke_display_panel` with `drivers/spi_lcd/`), not in a freestanding
shared TU.

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

Status: **done** · Risk: **MEDIUM (resolved)**

The byte-identical defaults — identity `port_drivecheck_remap` and
`"A:"`/`"B:"`-returning `port_filesystem_prefix` — now live in
`runtime/runtime_filesystem_defaults.c`. Pico, host, and ESP32 all
link the shared TU; pc386 keeps its real overrides (FatFs on every
volume, DOS A:/B:/C: drive-letter routing).

| Site | What remains |
|---|---|
| `runtime/runtime_filesystem_defaults.c` | shared identity + canonical "A:"/"B:" bodies |
| `ports/pico_sdk_common/cmd_files_hooks.c` | per-port copies removed; pointer comment only |
| `ports/host_native/host_runtime.c` | per-port copies removed; pointer comment only |
| `ports/esp32_s3_metro/main/esp32_cmd_files_hooks.c` | per-port copies removed; pointer comment only |
| `ports/pc386/pc386_runtime.c:295-297, 326-331` | real DOS A:/B:/C: routing — deliberate override; doesn't link the shared TU |

Gate: `validate_all.sh` green (host 244/244, mmbasic_stdio 8/8,
mmbasic_ansi build clean, 14/14 device variants, RAM baseline holds);
ESP32 build clean; `porttools/pico_console_smoke.py` 23/23 on both
boards (PicoCalc + ESP32) plus `pico_fs_vm_smoke.py fs errors` /
`esp32_fs_vm_smoke.py fs` PASS — covers mkdir/chdir/open/read/write/
append/copy/rename/dir/kill/rmdir, every one of which routes through
both hooks. Explicit `PRINT CWD$` after `CHDIR "A:/"` returns `A:/` on
both boards, confirming `port_filesystem_prefix(0) == "A:"` via the
shared TU.

---

## Finding 8 — `mmbasic_timegm` / `mmbasic_gmtime` shim wrappers

Status: **done — superseded by `hal_calendar` extension** · Risk: **MEDIUM (resolved)**

Rather than consolidating the three shim wrappers behind another rename
trick, the entire wall-clock conversion path is now an explicit HAL:
`hal/hal_calendar.h` exposes `hal_calendar_tm_to_epoch` /
`hal_calendar_epoch_to_tm` with const-correct, reentrant signatures.
One shared driver — `drivers/calendar/calendar_bare.c` — uses Howard
Hinnant's `days_from_civil` / `civil_from_days` algorithms over
`int64_t` throughout, working identically on every port from RP2040
to macOS to emscripten to ESP32-S3 to pc386.

What dissolved:
- `host_platform.h` + `esp32_platform.h` `#define timegm mmbasic_timegm`
  rename macros — gone (the const-vs-non-const collision the rename
  worked around no longer exists because no caller invokes libc
  `timegm` / `gmtime` directly).
- `mmbasic_timegm` / `mmbasic_gmtime` shim wrappers in
  `host_runtime.c`, `esp32_compat.c`, `pc386_runtime.c` — all retired.
- `GPS.h`'s `extern time_t timegm(...)` / `extern struct tm *gmtime(...)`
  declarations — gone (they were the original const-trap).
- `GPS.c`'s Pico-only hand-rolled `timegm` / `gmtime` / `gmtime_r`
  bodies plus their supporting constants — moved into
  `drivers/calendar/calendar_bare.c` with int64-clean math.
- ESP32's `esp32_compat.c::timegm` body — retired (it used `long`
  internally, overflowed past year 2038, **fixed as a free side effect
  of the unified bare driver**).

Caller migration: 22 sites across `mm_misc_shared.c` (13),
`GPS.c` (2), `MMntp.c` (1), `vm_sys_time_pico.c`,
`esp32_default_hooks.c`, `esp32_ntp.c`, `host_web.c`,
`host_wasm_web.c` — all now call `hal_calendar_*`.

Gate: `validate_all.sh` green (host 244/244, mmbasic_stdio **9/9**
including the new `09_datetime.bas` regression test, mmbasic_ansi
build clean, 14/14 device variants, RAM baseline holds); ESP32 build
clean; **WASM build clean** (emcc); both boards pass the new
`porttools/device_datetime_smoke.py` **10/10** (PicoCalc was 10/10
both pre- and post-change; ESP32 was 9/10 pre-change because of the
Y2038-style overflow at year 2099, now 10/10).

| Site | What remains |
|---|---|
| `hal/hal_calendar.h` | HAL contract |
| `drivers/calendar/calendar_bare.c` | shared int64-clean impl |
| every port | links the shared driver (Pico via top-level cmake, ESP32 via its CMakeLists, host/wasm/ansi/stdio/pc386 via Makefiles) |
| smokes | `porttools/device_datetime_smoke.py` (Pico+ESP32), `host/tests/t194_datetime_funs.bas` (host), `ports/mmbasic_stdio/tests/09_datetime.bas` (stdio) |

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

Status: **done** · Risk: **LOW (resolved)**

The byte-identical fallbacks
`int getConsole(void) { return -1; }` and `int kbhitConsole(void) { return 0; }`
now live in `runtime/runtime_console_input_noop.c`. The five non-device
ports that lack a real keyboard / console-input source
(host_native, host_wasm, mmbasic_ansi, mmbasic_stdio, pc386) link the
shared TU; Pico (`pico_console.c`) and ESP32
(`esp32_mmbasic_console_glue.c`) supply real implementations and
deliberately do not link it.

In practice none of the no-op ports actually invoke `getConsole` /
`kbhitConsole` — input arrives through scripted-key hooks (host_native
harness), the web console queue (host_wasm), terminal raw-mode reads
(mmbasic_ansi / mmbasic_stdio), or the PS/2 IRQ-driven ring drained
inside MMInkey (pc386). The shared defaults exist purely to satisfy
the `extern int getConsole(void);` / `extern int kbhitConsole(void);`
declarations in `Hardware_Includes.h`.

| Site | What remains |
|---|---|
| `runtime/runtime_console_input_noop.c` | shared no-op fallbacks |
| `ports/pico_sdk_common/pico_console.c:15, :65` | real impl (untouched) |
| `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:94, :102` | real impl (untouched) |
| `ports/host_native/host_runtime.c:626, :655` | per-port copies removed; pointer comments only |
| `ports/pc386/pc386_runtime.c:243-244` | per-port copies removed; pointer comment only |

Gate: `validate_all.sh` green (host 244/244, mmbasic_stdio 8/8,
mmbasic_ansi build clean, 14/14 device variants, RAM baseline holds);
ESP32 build clean; `porttools/pico_console_smoke.py` 23/23 +
`porttools/pico_input_smoke.py` 21/21 on both PicoCalc (WebMite
RP2350B) and ESP32-S3 Metro.

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

Done so far: Findings 1, 2, 3, 4, 7, 8, 10, telnet IAC parser; live
bugs (pc386 escape decoder, pc386 `timegm` const, Pico DEL→BKSP,
ESP32 `timegm` Y2038-style overflow at year 2099).

**Finding 5 is intentionally not next.** Its scope (~1900 LOC of
peripheral stubs) is owned by the modular stub-driver carve-out in
[`real-hal-plan.md` § Modular stub drivers](real-hal-plan.md#modular-stub-drivers-proposed-direction),
not by this audit's single-file consolidation pattern. Same for
Finding 6 (`port_*` no-op stubs) — those ride along on the same
driver-layout refactor. Both wait until that carve-out begins (I2C
first).

Next dedup-style work, in order of payoff:

1. **Finding 9** — `cmd_files_*` lifecycle hooks (~80 lines, partial
   overlap, includes a stale copy-paste comment to clean up).
