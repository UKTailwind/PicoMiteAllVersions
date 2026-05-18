# Mechanical Renames

A backlog of pure-rename cleanups — identifier or macro names that carry historical noise (`HAL_PORT_` prefix sprawl, misleading suffixes, dead-vintage stems) but no semantic change.

Each entry is a self-contained `sed`-able pass. No behavior change, no recategorization, no signature changes. If a rename would require thinking about what something *means* or whether it's in the right bucket — that's a different kind of work and belongs elsewhere (e.g. a fixup plan).

Workflow per entry:

1. Branch `mechanical-renames` off `main` (one branch, sequential commits — one commit per entry).
2. Run the rename with a single global `sed` / `grep -rl … | xargs sed -i`.
3. Build host + one device target as smoke; full `buildall.sh` only if the macro is in a code path that varies per port.
4. Commit, tick the entry below to **done**, push.

---

## 1. `HAL_PORT_MMBASIC_*_FUNC` → `MMB_*_FUNC`

**Status:** done

**Rationale.** Both macros are pure function-placement attributes (expand to `__not_in_flash_func(name)` on Pico targets, plain `name` elsewhere). They neither belong to a HAL contract (no `hal/` header) nor to a port-config gate. The `HAL_PORT_` prefix is exactly the sprawl pattern flagged in `feedback_port_prefix_pollution.md`. The `MMBASIC_` middle segment is also redundant — these macros are only ever used on MMBasic interpreter functions.

**Rename:**

| Old | New |
|---|---|
| `HAL_PORT_MMBASIC_HOT_FUNC` | `MMB_HOT_FUNC` |
| `HAL_PORT_MMBASIC_SUBFUN_FUNC` | `MMB_DISPATCH_FUNC` |

`MMB_` prefix to mark "MMBasic interpreter internal" (vs a generic `RAM_FUNC` that would collide with peripheral-driver naming). `HOT` and `DISPATCH` describe the two existing categories — per-statement hot path vs the giant once-per-SUB/FUNCTION dispatch.

**Scope.** ~85 sites total:

- `.c` call sites (~70): `Commands.c`, `Functions.c`, `Operators.c`, `MMBasic.c`, `Memory.c`, `MM_Misc.c`, `shared/mmbasic/mm_misc_shared.c`, `Draw.c`, `FileIO.c`, `runtime/runtime_abort.c`, `runtime/runtime_interrupt.c`.
- Port definitions (12): `ports/*/port_config.h` — one pair each in `pico`, `vga`, `web`, `pico_rp2350`, `vga_rp2350`, `vga_wifi_rp2350`, `hdmi_rp2350`, `dvi_wifi_rp2350`, `web_rp2350`, `host_native`, `esp32_s3_metro`. (`host_wasm`, `mmbasic_ansi`, `pc386` don't define the macros today.)
- Runtime fallback `#ifndef` guards (2): `runtime/runtime_abort.c`, `runtime/runtime_interrupt.c`.
- Docs (3): `docs/adding-a-new-port.md`, `docs/real-hal/phases-8-to-13.md`, `docs/real-hal/scoreboard.md`.

**Out of scope (deliberate).**

- `HAL_PORT_RAM_FUNC` — separate macro with separate users; will get its own entry.
- Rebucketing miscategorized call sites (`cmd_null` in RAM, `cmd_inc` tagged `SUBFUN`). That's a semantic fix, not a rename — separate work.
- Deleting the orphan `/** @endcond */` near `cmd_null`. Doc cleanup, separate.

**Smoke gate.** `ports/host_native/build.sh && ports/host_native/run_tests.sh` + at least one Pico build (`pico` or `pico_rp2350` via `buildall.sh`) to confirm `__not_in_flash_func` still resolves.

---

## 2. `HAL_PORT_RAM_FUNC` → `PORT_RAM_FUNC`

**Status:** done

**Rationale.** Same shape as entry 1 — `HAL_PORT_RAM_FUNC(name)` expands to `__not_in_flash_func(name)` on Pico targets, plain `name` elsewhere. Not a HAL contract, not a configuration value — it's a function-placement attribute used by External.c GPIO fast paths. The `HAL_` prefix is the same noise as entry 1. Keep `PORT_` so the name doesn't collide with a more generic `RAM_FUNC` (peripheral drivers, Pico SDK conventions). Held separate from entry 1's `MMB_HOT_FUNC` because these wrap GPIO/peripheral code, not MMBasic interpreter functions — same mechanism, different conceptual category.

**Rename:**

| Old | New |
|---|---|
| `HAL_PORT_RAM_FUNC` | `PORT_RAM_FUNC` |

**Scope.** ~27 sites:

- Call sites (16): all in `External.c` — `PinSetBit`, `ExtSet`, `cmd_sync`, and other GPIO pin-toggle helpers.
- Port definitions (11): every `ports/*/port_config.h` except `host_wasm`, `mmbasic_ansi`, `pc386`.

**Out of scope.** Merging with entry 1's `MMB_HOT_FUNC` is *not* an option — although both expand to `__not_in_flash_func` on most ports, the rp2040 VGA, rp2040 WEB, vga_wifi_rp2350 and web_rp2350 port configs deliberately set them to *different* placements to fine-tune SRAM usage (e.g. rp2040 VGA keeps `MMB_HOT_FUNC` in RAM but pushes `PORT_RAM_FUNC` to flash). The four placement macros are a four-way SRAM-budgeting knob; merging would lose that flexibility.

**Smoke gate.** Same as entry 1.

---

## 3. `HAL_PORT_MMINKEY_DECL` → `MMINKEY_DECL`

**Status:** done

**Rationale.** Same shape again — `__not_in_flash_func(name)` on Pico, plain `name` elsewhere. One call site (`MMInkey` in `ports/pico_sdk_common/pico_console.c`). Named specifically enough (`MMINKEY_`) that the `HAL_PORT_` prefix adds zero information.

**Rename:**

| Old | New |
|---|---|
| `HAL_PORT_MMINKEY_DECL` | `MMINKEY_DECL` |

**Scope.** 10 sites:

- Call site (1): `ports/pico_sdk_common/pico_console.c` — `MMInkey` declaration.
- Port definitions (9): every `ports/*/port_config.h` that builds against pico_sdk_common.

**Out of scope.** Folding `MMInkey` into entry-1 `MMB_HOT_FUNC` is *not* an option — the rp2040 VGA port sets `MMINKEY_DECL` to plain `name` (flash) while keeping `MMB_HOT_FUNC` as `__not_in_flash_func` (RAM). Distinct knob, deliberate. Mechanical rename only.

**Smoke gate.** Pico builds only — host/esp32/wasm don't compile pico_sdk_common.

---

## 4. `HAL_PORT_BC_CRASH_INFO_ATTR` → `BC_CRASH_INFO_ATTR`

**Status:** done

**Rationale.** Linker-section attribute — `__attribute__((section(".uninitialized_data.bc_crash_info")))` on Pico targets, empty elsewhere. Tags the `BCCrashInfo bc_crash_info` global so it survives across resets for crash-report recovery. The `BC_` prefix already namespaces it (bytecode); the `HAL_PORT_` adds nothing.

**Rename:**

| Old | New |
|---|---|
| `HAL_PORT_BC_CRASH_INFO_ATTR` | `BC_CRASH_INFO_ATTR` |

**Scope.** 13 sites:

- Call sites (2): `bc_debug.c` — declaration of `bc_crash_info` plus a doc comment referencing the macro.
- Port definitions (11): every `ports/*/port_config.h` except `host_wasm`, `mmbasic_ansi`, `pc386`.

**Smoke gate.** Same as entry 1.

---

## TBD — bigger judgment calls (not queued)

- **`HAL_PORT_*` → `PORT_*` mass rename** for the ~55 legitimate per-port configuration macros (`HAL_PORT_HEAP_MEMORY_SIZE`, `HAL_PORT_FILES_MAX`, `HAL_PORT_HAS_WIFI`, the `_CMD_TOKEN[S]` / `_FUN_TOKEN[S]` family, etc.). These are real port-shape values; the prefix at least signals "see port_config.h." Worth doing for consistency but it's not BS, it's verbosity — separate decision.
- **`HAL_PORT_RANDOMIZE_DEFAULT_SEED()` → `PORT_RANDOM_SEED()`**. Genuine port-supplied hook (entropy source). Defensible name today; rename only if the mass-rename above goes through.
- **`HAL_PORT_ASSERT_H` / `HAL_PORT_CONFIG_INCLUDED`**: header guard + sanity marker. Private bookkeeping, low value to touch. Leave alone.
