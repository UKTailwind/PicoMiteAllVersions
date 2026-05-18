# WASM Port Cleanup Plan

**Goal:** bring `ports/host_wasm/` to the same composition standard as device ports — zero `MMBASIC_WASM` ifdefs in shared code, port-owned config, port-owned behavior.

**Current state (as of 2026-05-08):** core is clean. Interpreter, compiler, VM, command/function dispatch, Draw.c — none reference `MMBASIC_WASM`. The remaining coupling sits in `ports/host_native/` (which the WASM build inherits via `-I`) and in two header-level feature gates.

## Inventory

All `MMBASIC_WASM` / `MMBASIC_ANSI` / `MMBASIC_SIM` references outside `ports/host_wasm/`:

| File | Line | What | Disposition |
|---|---|---|---|
| `ports/host_native/host_runtime.c` | 33 | `#include <emscripten.h>` | Step 1 |
| `ports/host_native/host_runtime.c` | 451 | sub-millisecond slowdown accumulator (ASYNCIFY floors `host_sleep_us` at 1 ms; accumulates µs and only sleeps on whole-ms boundaries) | Step 1 |
| `ports/host_native/host_fastgfx.c` | 13 | `#include <emscripten.h>` | Step 2 |
| `ports/host_native/host_fastgfx.c` | 28 | `volatile uint32_t wasm_vsync_counter` storage (read by `ports/host_wasm/host_wasm_canvas.c`) | Step 2 |
| `ports/host_native/host_fastgfx.c` | 65 | vsync resync after >2 frame stalls (GC, tab visibility) | Step 2 |
| `ports/host_native/host_fastgfx.c` | 53 | `#ifdef MMBASIC_SIM` — `host_sim_emit_blit` for native sim's WebSocket emit (defined by host_native, not host_wasm) | Step 4 |
| `MMBasic_REPL.c` | 31 | three-way banner (WASM web / HOST native + trailer / device runtime `banner[]`) | Step 3 |
| `configuration.h` | 154 | `HEAP_MEMORY_SIZE` 8 MB override for WASM | Step 3 |
| `configuration.h` | 164 | `HEAP_MEMORY_SIZE` 2 MB override for `MMBASIC_ANSI` (parallel of WASM, same shape) | Step 3 |
| `configuration.h` | 187 | `MAX_PROG_SIZE` 512 KB cap for WASM (else tracks 8 MB heap → 40 MB static data) | Step 3 |
| `ffconf.h` | 109 | LFN buffer sizing, gated `MMBASIC_HOST \|\| MMBASIC_WASM` | Step 6 — flip to `FF_MAX_LFN_LARGE` feature flag |

Total live ifdef sites to retire: 9. The `ffconf.h` gate is fine — not WASM-specific behavior, "any port with a real POSIX filesystem."

## Step 1 — slowdown hook

`host_sim_apply_slowdown` is called from `ports/host_native/hal_time_host.c:34` and from `bc_vm_poll_interrupts`. Native and WASM need different impls (1-µs sleep vs. ms-boundary accumulator). Today both live in `host_runtime.c` behind an ifdef.

- Move the impl into per-port files: `ports/host_native/host_sim_slowdown.c` (native sleep) and `ports/host_wasm/host_sim_slowdown.c` (µs accumulator). Naming kept as `host_sim_*` because the function is a sim-host-internal utility paired with the existing `host_sim_audio.c` / `host_wasm_audio.c` link-time selection.
- `host_sim_slowdown_us` storage moves to `ports/host_native/host_sim_slowdown.c` (and the extern in `host_wasm_main.c:108` keeps working unchanged).
- `host_runtime.c` drops both impls and the `<emscripten.h>` include. The `int host_sim_slowdown_us` definition migrates with the impl.
- Each port's Makefile lists exactly one slowdown source file.

**`--allow-multiple-definition` risk:** `ports/host_wasm/Makefile:64` passes `-Wl,--allow-multiple-definition` (needed for tentative-definition merging on wasm-ld). If both slowdown sources end up in the WASM link line, wasm-ld silently picks the first symbol it sees and ships the wrong impl — no build error. Mitigation: the WASM Makefile must explicitly exclude the native source from its `CORE_SRCS` list, and Step 5's purity gate addition includes a `nm` symbol-uniqueness check on the WASM artefact for `host_sim_apply_slowdown`. The native build catches the duplicate at link time on its own (no `--allow-multiple-definition`).

## Step 2 — FASTGFX vsync hook

`host_fastgfx.c:65-75` does a wall-clock resync if `host_fastgfx_next_sync_us` falls more than two frames behind real time (GC pause, tab blur). Native doesn't need this. The vsync counter at line 28 is JS-facing storage, read by `ports/host_wasm/host_wasm_canvas.c:33,93-94` via extern.

Hook surface (value-in/value-out, no pointer to private state):

```
uint64_t bc_fastgfx_resync_check(uint64_t next_sync_us, uint64_t frame_us);
```

- Native impl in `ports/host_native/host_fastgfx_resync.c`: `return next_sync_us;` (no-op).
- WASM impl in `ports/host_wasm/host_fastgfx_resync.c`: read wall clock, compare to `next_sync_us + 2*frame_us`, return either the input or `now + frame_us`.
- WASM impl owns `volatile uint32_t wasm_vsync_counter` storage (it's the natural place — JS bridge already lives in `ports/host_wasm/`).
- `host_fastgfx.c` becomes:
  ```
  host_fastgfx_next_sync_us += frame_us;
  host_fastgfx_next_sync_us = bc_fastgfx_resync_check(host_fastgfx_next_sync_us, frame_us);
  ```
  No un-staticing of the variable — the static stays static, the function is pure-value.
- Drop `<emscripten.h>` include from `host_fastgfx.c`.

After this step, `host_fastgfx.c` has zero `MMBASIC_WASM` references. The remaining `#ifdef MMBASIC_SIM` at line 53 is handled separately in Step 4.

## Step 3 — port-owned config (heap, banner, prog size)

The four header gates (`HEAP_MEMORY_SIZE` × 2, `MAX_PROG_SIZE`, banner) all want port-supplied values, not target-identity branches. Reuse the existing `HAL_PORT_HEAP_MEMORY_SIZE` mechanism that `configuration.h:58-62` already keys off; add feature-shaped gates for the others. No new `HAL_PORT_*` prefix sprawl.

**Create `ports/host_wasm/port_config.h`.** WASM Makefile already orders `-I$(WASM_DIR) -I$(NATIVE_DIR)`, so the WASM copy wins. Copy from `ports/host_native/port_config.h` and override the WASM-specific values.

**Heap size (D1 fix).** `configuration.h:58-62` already does `#define HEAP_MEMORY_SIZE HAL_PORT_HEAP_MEMORY_SIZE`. Each port_config.h sets the macro:
- `ports/host_native/port_config.h`: `HAL_PORT_HEAP_MEMORY_SIZE = 128*1024` (existing).
- `ports/host_wasm/port_config.h`: `HAL_PORT_HEAP_MEMORY_SIZE = 8*1024*1024`.
- `ports/mmbasic_stdio/port_config.h` (the `MMBASIC_ANSI` build): `HAL_PORT_HEAP_MEMORY_SIZE = 2*1024*1024`.
- Delete `configuration.h:154-157` and `:164-167` (both `#undef`-then-`#define` blocks) entirely. No `MMBASIC_WASM` or `MMBASIC_ANSI` reference remains in `configuration.h` for heap.

**MAX_PROG_SIZE cap.** Currently `configuration.h:174` defaults `MAX_PROG_SIZE` to `HEAP_MEMORY_SIZE`, then WASM overrides to 512 KB. Convert to feature-shaped default-if-unset:
```
#ifndef MAX_PROG_SIZE
#define MAX_PROG_SIZE HEAP_MEMORY_SIZE
#endif
```
WASM's `port_config.h` defines `MAX_PROG_SIZE (512 * 1024)`. Delete `configuration.h:187-190`.

**Banner (D2 fix).** Three branches, two strings: WASM has just a title, host_native has a title plus a "Ctrl-D to exit" trailer, device uses runtime `banner[]`. Use two optional macros:

```
/* MMBasic_REPL.c */
void MMBasic_PrintBanner(void) {
#ifdef MMBASIC_BANNER_TITLE
    MMPrintString("\r" MMBASIC_BANNER_TITLE "\r\n");
    MMPrintString(MMBASIC_COPYRIGHT);
#ifdef MMBASIC_BANNER_TRAILER
    MMPrintString(MMBASIC_BANNER_TRAILER);
#endif
#else
    extern char banner[];
    MMPrintString(banner);
    MMPrintString(MMBASIC_COPYRIGHT);
#endif
}
```

- `ports/host_wasm/port_config.h`: `#define MMBASIC_BANNER_TITLE "MMBasic Web V" VERSION`.
- `ports/host_native/port_config.h`: `#define MMBASIC_BANNER_TITLE "PicoMite MMBasic Host V" VERSION` + `#define MMBASIC_BANNER_TRAILER "Host REPL — Ctrl-D to exit.\r\n\r\n"`.
- Device port_configs: define neither; fall through to runtime `banner[]`.

Three branches, three configurations. No runtime hook needed for a string difference.

## Step 4 — `MMBASIC_SIM` follow-on in `host_fastgfx.c:53`

`host_sim_emit_blit` is the WebSocket bridge for the native sim (live framebuffer streaming to `serve.py`). It's not in `TARGET_MACROS` today, so the strict purity gate doesn't catch it — but the file should be ifdef-free for Step 5 to lock down `host_native/*.c` cleanly.

- Hook: `void bc_fastgfx_post_swap(int x, int y, int w, int h, const uint32_t *buf);`
- Native impl in `ports/host_native/host_fastgfx_post_swap.c`: calls `host_sim_emit_blit` (only when sim is built — gated by Makefile inclusion, not by ifdef).
- WASM impl in `ports/host_wasm/host_fastgfx_post_swap.c`: empty (browser already has the framebuffer via shared memory; no WS bridge).
- `host_fastgfx.c:53-55` becomes a single unconditional call to `bc_fastgfx_post_swap`.

After Step 4, `host_fastgfx.c` is free of all target-mode ifdefs (`MMBASIC_WASM` and `MMBASIC_SIM`).

## Step 5 — purity gate extension

Once Steps 1–4 land, `MMBASIC_WASM` should appear only in:
- `ports/host_wasm/Makefile` (the `-DMMBASIC_WASM` define itself)
- `ffconf.h:109` (legitimate host-union gate)

Extending `tools/check_hal_purity.sh` is **not** a one-liner. The script (`STRICT_FILES` array at line 99) is a flat list of filenames; there's no glob, no directory-recursion, no per-line whitelist. It also forbids the *full* `TARGET_MACROS` set (line 69) for any file in the list, not a custom subset.

Concrete extension work:

1. Add a `STRICT_DIRS=("ports/host_native")` array next to `STRICT_FILES`. Glob-expand `*.c *.h` at script start, append to `STRICT_FILES`.
2. Add `MMBASIC_SIM` to `TARGET_MACROS` so the gate catches future `host_native/*` regressions covering both `MMBASIC_WASM` and the sim-vs-real distinction.
3. Add a WASM artefact symbol-uniqueness check after the WASM build: `nm ports/host_wasm/web/picomite.wasm | grep ' T host_sim_apply_slowdown\| T bc_fastgfx_resync_check\| T bc_fastgfx_post_swap'` must return exactly one line per symbol. This catches the `--allow-multiple-definition` masking risk from Step 1.

The `ffconf.h:109` host-union gate stays as-is — it's outside `host_native/` and has a legitimate cross-host use, so no whitelist needed.

## Out of scope (deferred)

- **CMake migration.** Both host ports use Make + file-search rules; device ports use CMake. Migrating host_wasm to CMake is strategic but not required for purity. Defer until host_native is also ready to move.
- **Driver-style audio refactor.** `host_wasm_audio.c` already uses link-time selection (no shared ifdef), which is the goal pattern. A more formal `drivers/audio_web/` lift is cosmetic — current shape works.
- **Drop `--allow-multiple-definition`.** Removing the wasm-ld flag would catch duplicate-definition mistakes at link time but requires auditing every tentative-def in MMBasic for explicit ownership. That's a separate effort; Step 5's `nm` check covers the immediate risk.

## Exit gate

- Zero `MMBASIC_WASM` in `MMBasic_REPL.c`, `configuration.h`, `ports/host_native/`.
- Zero `MMBASIC_SIM` in `ports/host_native/host_fastgfx.c`.
- Zero `MMBASIC_ANSI` in `configuration.h`.
- One `MMBASIC_WASM` reference remaining repo-wide outside `ports/host_wasm/`: `ffconf.h:109` (host-union gate).
- `ports/host_native/run_tests.sh` 239/239 (native).
- WASM build via `ports/host_wasm/build.sh` clean.
- Manual browser sanity-check (`ports/host_wasm/web/serve.sh && open http://localhost:8000/`) is on the developer for now. Existing `ports/host_wasm/web/smoke_*.mjs` Playwright scripts are dev-machine-only (hardcoded module path) and intentionally not wired into CI.
- `tools/check_hal_purity.sh` green, including `STRICT_DIRS` expansion and the WASM symbol-uniqueness check.
- `buildall.sh` 12 device variants unchanged.

## Effort

Steps 1, 2, 4 are localized hook splits with the audio link-time-selection pattern as a template — each is ~50–80 lines moved. Step 3 is mechanical header surgery once `port_config.h` lands. Step 5 is the most involved (script extension + Playwright wiring). Total: 2–3 sessions.

## Why this is worth doing

WASM is the only "host" port that meaningfully differs from native at runtime (asyncify, JS vsync, browser memory budget). Every one of those concerns currently bleeds into `host_native` files. Closing the bleed gives `host_native` the same shape as a device port — a self-contained directory readable end-to-end without knowing WASM exists — and lets the purity gate catch regressions automatically.
