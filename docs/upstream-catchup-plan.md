# Upstream Catch-Up Plan

Bring the fork up to parity with `UKTailwind/PicoMiteAllVersions` on language and portable features, **without** merging. Each feature is ported by hand: read the upstream implementation, adapt it into our tree, gate it behind our HAL layout, verify tests green, commit. Upstream is treated as a **reference implementation**, not a merge target.

- **Fork point:** `c0f666f` (2025-08-21), version 6.01.00b10.
- **Upstream HEAD:** `04f81d0` (2026-04-05), version 6.02.02B0.
- **Our lead branch:** `main` — but **no catch-up work lands here directly.** See "Merge target" below.
- **Integration branch:** `catchup-integration`. Every feature port commits here.
- **Reference remote:** `upstream` (`UKTailwind/PicoMiteAllVersions`). Kept up to date with `git fetch upstream` but never merged into our history.

## Merge target — READ THIS BEFORE COMMITTING

**No catch-up commit goes on `main` until the entire plan is landed on `catchup-integration` and validated on real device hardware.** Running the host + wasm + firmware-build gates locally proves that the tree compiles and passes compare-mode tests, but it does not prove that a rp2040 or rp2350 with a real PicoCalc LCD, SD card, keyboard, and audio DAC actually runs the ported feature correctly. The PC simulator elides timing, peripheral behaviour, and concurrency that only the device exposes.

The workflow is therefore:

1. Every feature port and supporting infrastructure (bridge fixes, plan rewrites, test imports) commits to `catchup-integration`, not `main`. Per-feature working branches (`catchup/<feature>`) fast-forward into `catchup-integration` when their own gate is green.
2. `catchup-integration` accumulates commits until the plan's exit criteria (Phase A complete, Phase B complete — whatever the current milestone is) are met.
3. A `.uf2` from `catchup-integration` is flashed onto physical PicoCalc hardware (both rp2040 and rp2350) and the acceptance test programs are run end-to-end on device — not just `ports/host_native/tests/acceptance/struct_full.bas` through the simulator, but the actual programs on real glass. Anything that looks wrong gets fixed on `catchup-integration` before main sees a single commit.
4. Only after device validation does `catchup-integration` merge (or rebase-and-fast-forward) into `main` as a single coherent set.

Corollaries:

- The simulator gates (host `./run_tests.sh`, firmware `.uf2` builds, `./ports/host_wasm/build.sh`) remain required at every per-feature boundary — they catch regressions early — but they are necessary, not sufficient.
- Sub-plans (e.g. `docs/type-struct-port-plan.md`) follow the same rule. Their phases commit to `catchup-integration` (or to feature branches that fast-forward into it).
- Docs-only updates to the catch-up plan itself (clarifications, status, scope notes) also land on `catchup-integration` so `main` doesn't see "catch-up is happening" until the work is actually validated.
- The earlier catch-up commits on `main` (TRIM$ `291d525`, initial plan doc `55548fb`) predate this policy. They stay where they are rather than get force-rewritten out of published history; the policy applies from the next commit forward.

## Why manual, not merge

1. **Upstream has no real git history.** 108 of 172 post-fork commits are `"Add files via upload"` — whole-tree zip drops. A three-way merge sees them as one giant conflict per file per drop with no rationale to resolve against.
2. **Our architectural direction is different.** We are splitting interpreter from HAL (`docs/host-hal-plan.md`). Upstream is piling more `#ifdef`-gated chip variants onto the same monolith. Merging would reintroduce hardware coupling into files we have already cleaned up (`Draw.c`, `FileIO.c`, `shared/audio/Audio.c`, `MM_Misc.c`, `shared/mmbasic/mm_misc_shared.c`).
3. **We have three targets upstream does not.** Host (`MMBASIC_HOST`) and WebAssembly (`MMBASIC_WASM`) must keep compiling through every feature we take. A straight merge breaks both until repaired, with no way to test incrementally.
4. **Much of the upstream churn is formatting.** Upstream reformatted brace style (`cmd_foo(void){` → `cmd_foo(void)\n{`) across the whole tree; a merge would surface that as thousands of spurious conflicts mixed in with real work.
5. **Per-feature ports can be tested.** Each feature lands as one or a few commits behind the full `./run_tests.sh` gate. A merge cannot.

## Invariants

1. **Host test gate** — `cd ports/host_native && ./build.sh && ./run_tests.sh` (default compare mode, interp vs VM) must pass at every feature-boundary commit. `--interp` and `--vm` modes are diagnostic only.
2. **Firmware gate** — `./build_picocalc_firmware.sh` (both rp2040 and rp2350) must produce `.uf2` artifacts at every feature boundary. The script mirrors `.github/workflows/firmware.yml` exactly; if it passes locally, CI passes on push.
3. **Web gate** — `./ports/host_wasm/build.sh` must produce `ports/host_wasm/web/picomite.{mjs,wasm}` cleanly. Pages deploy on main must stay green.
4. **No new hardware `#ifdef` gates** added to core interpreter files (`MMBasic.c`, `Commands.c`, `Functions.c`, `Operators.c`). If an upstream feature requires a gate, the gate goes behind a HAL entry point, not into the core.
5. **Bridge-first for new commands.** New `cmd_*` entries go to the interpreter path via `AllCommands.h`; VM gets a native opcode only when profiling shows it matters. Pattern per `docs/bridge-restoration-plan.md`.
6. **No SDK mutation.** The Pico SDK at `$PICO_SDK_PATH` must stay stock. The historical `gpio.c`/`gpio.h` patches were eliminated on the `sdk-patch-removal` branch (GPIO IRQ dispatcher now lives in `picomite_gpio_irq.c`). Any future change that would require patching SDK sources is a signal to refactor the dependency out, not to reintroduce the patch.
7. **Attribution preserved.** Per the MMBasic license, the `Version.h` copyright line must stay intact. Port notes in commit messages cite upstream commit / file / approximate line range for each ported feature so future maintainers can audit.

## Feature inventory

Ranked by value-to-cost ratio. "Value" = how much user-facing capability we gain. "Cost" = lines to port, HAL touch-points, device-vs-host surface.

### Tier 1 — language features (highest value, lowest hardware risk)

These are interpreter-core changes that benefit every target (device, host, web) equally. They land in files we already share across HALs.

| feature | upstream anchor | size | notes |
|---|---|---|---|
| `CONST` declaration | `Commands.c:cmd_const` (~6610) | small | Pure interpreter. Adds to variable table with immutable flag. **Already present at fork point — `Commands.c:cmd_const` line 3516, registered in `AllCommands.h`, covered by `ports/host_native/tests/frontend/t009_const_assign.bas`. Confirmed matches upstream logic; no port needed.** |
| `TYPE` / `END TYPE` / `STRUCT` | `Commands.c:cmd_type` (~6663), `cmd_struct` (~6704), `cmd_endtype` | medium | User-defined record types. Structure definition happens in `PrepareProgramExt`; runtime `cmd_type` just skips to `END TYPE`. Check variable-table and DIM machinery. |
| `REDIM` | `Commands.c:cmd_redim` (~5613) | small | Runtime array resize. **DONE 2026-04-20. Works in both engines via bridged `cmd_redim` + a post-bridge array-pointer rebinding loop in `bc_bridge.c` (commit `ef9fc5d` + follow-up `catchup/bridge-rebind`). One known limitation: string-array REDIM (`REDIM s$(n)`) is still interp-only until `bc_source.c` compiles the `DIM s$(n) LENGTH n` clause natively. Covered by `t205_redim.bas` (compare mode) and `t206_redim_string.bas` (interp only).** |
| `EXECUTE` | `Commands.c:cmd_execute` | small | Runs a BASIC statement from a string. Essentially `tokenise(string) → ExecuteProgram`. Check stack/recursion safety. **Already present at fork point — `Commands.c:execute()` line 3755 + `cmd_execute()` line 3814. Matches upstream behavior; no port needed.** |
| `fun_trim` | `Functions.c:fun_trim` | tiny | String trim (whitespace on both sides). **DONE 2026-04-20 (291d525).** |
| `INC` command | already in ours | — | Already shared. No action. |

### Tier 2 — portable commands (new drawing / computation)

Pure compute or pure graphics primitives, so they run identically on device + host + web once the graphics path reaches them.

| feature | upstream file | size | notes |
|---|---|---|---|
| `Turtle` graphics | `Turtle.c` (1107 lines) | medium | Uses existing line/point primitives. Self-contained module; goes straight into our shared graphics path. |
| `Mandelbrot` command | `Draw.c` (inside upstream's rewritten file) | small | Wraps the math in a command; can reuse our existing `mand.bas` logic or take upstream's. |
| `Tilemap` | `Draw.c` | medium | Tile-based sprite drawing. Check memory model for tile atlas. |
| `Bezier`, `Star`, `Bitstream`, `Fill` | `Draw.c` | small each | Drawing primitives. Port one at a time. |
| `Astro`, `planet.c` | `planet.c` (309 lines) | small | Astronomical calculation command. Pure math. |
| `re.c` / `re.h` regex library | 1501 lines, replaces `regex.c`/`xregex.h`/`xregex2.h` | medium | Library swap. Keeps existing regex surface but swaps implementation. Worth doing for maintenance but not urgent unless we hit a bug in the old one. |

### Tier 3 — platform-dependent (defer or selectively adapt)

These touch hardware or peripheral bus code. They make sense for device builds only and won't improve ports/host_wasm/web at all.

| feature | upstream file | notes |
|---|---|---|
| `Stepper` | `stepper.c` (5965 lines) | Stepper motor control. Large, hardware-bound (GPIO + timers). Low priority unless a user needs it. |
| `I2CLCD` | `drivers/i2c_bus/I2C.c` | HD44780-over-I²C driver. Useful if requested. |
| `YModem` | `External.c` / `FileIO.c` | File transfer protocol. Moderate value; needs serial console hooks. |
| `OneShot` | `External.c` / `MM_Misc.c` | Single-shot IRQ trigger. Small, chip-specific. |
| `IRQ NEXT` / `IRQ PREV` | `External.c` | IRQ routing helpers. |
| `Location` | `drivers/gps/GPS.c` | GPS command. Depends on GPS module support. |
| `Raycaster` | `Raycaster.c` (1732 lines) | Wolfenstein-style rendering. Pure compute + our framebuffer, so actually portable, but large — move to Tier 2 only if someone asks. |
| `Mode`, `Fill` variants | display-specific | Tied to upstream's new framebuffer layout; adapt only if we port the underlying feature. |

### Explicit non-goals

- **New display drivers.** `VGA222.c`, `RGB121.c`, expanded `drivers/ssd1963/SSD1963.c`, HDMI variants. Our target hardware list is PicoCalc (single LCD), host, web. Adding these expands the `#ifdef` matrix we are trying to shrink.
- **New chip variants.** PICORP2350 etc. are already building from our RP2350 CMake file; we do not adopt upstream's `set(COMPILE …)` taxonomy.
- **USB stack rework.** `USBKeyboard.c` churn upstream is mostly refactoring. Low-value, high-risk to touch.
- **Formatting churn.** Do not reformat files to match upstream brace style. Each port carries its own minimal diff against our current file.
- **Upstream's `mmc_stm32.c` / PSRAM changes** unless we hit a bug on our build.
- **Any change to `CMakeLists.txt` that reorganizes our variants.** Our CMake files are load-bearing for CI — isolate upstream changes from that file.

## Per-feature workflow

For every feature ported, follow this loop exactly:

1. **Branch off current `catchup-integration`.** Name the branch `catchup/<feature>` (e.g. `catchup/type-struct`, `catchup/fun-trim`). Never branch off `main` for catch-up work.
2. **Fetch fresh upstream.** `git fetch upstream` so the reference is current.
3. **Read, don't copy blindly.** Open the upstream implementation in its new location. Read the full function, every helper it calls, every new field on `Option`, every new entry in `AllCommands.h` / command table.
4. **Port source changes.** Apply the minimum diff to our files (`Commands.c`, `Functions.c`, `AllCommands.h`, `MMBasic.h` as needed). Keep our surrounding code untouched.
5. **Update HAL split if needed.** If the feature touches `Draw.c` / `FileIO.c` / `shared/audio/Audio.c` / `MM_Misc.c`, mirror the split pattern already established there (device body vs. shared body). Do NOT introduce bare `#ifdef PICOMITE` gates into logic — route through the existing HAL entry points or add a new one.
6. **Bridge, don't native-VM.** New commands land as interpreter entries + `OP_BRIDGE_CMD` bridging so `FRUN` still reaches them. Native VM opcodes only after profiling.
7. **Write a test.** Every ported feature gets at least one `ports/host_native/tests/*.bas` that exercises it. Regressions in tier-1 features bite hardest, so cover edge cases (empty string, negative index, nested TYPE, etc.).
8. **Run the simulator gate — all three must pass:**
   - Host: `cd ports/host_native && ./build.sh && ./run_tests.sh` (default compare mode).
   - Firmware: `./build_picocalc_firmware.sh` — both rp2040 and rp2350 must produce a `.uf2`. Mirrors CI exactly; if this passes locally, `firmware.yml` will pass on push.
   - Web: `cd ports/host_wasm && ./build.sh` — `ports/host_wasm/web/picomite.{mjs,wasm}` must produce cleanly.

   If any gate fails, the port is not done — do not commit. These are necessary but **not** sufficient; see "Merge target" above.
9. **Commit with reference.** Commit message cites upstream: *"Upstream 6.02 parity: TYPE/STRUCT — ported from UKTailwind/PicoMiteAllVersions Commands.c cmd_type (@04f81d0)"*.
10. **Fast-forward `catchup-integration` when green.** No merge commits on feature branches; rebase-and-ff or squash. **Do not touch `main`.**

A feature may span more than one commit if it's large. Each commit must still pass the simulator gate on its own.

When the plan's current milestone is complete (all Phase A features landed on `catchup-integration`, for example), the catch-up milestone ships via the device-validation step described in "Merge target" — flash the `.uf2`, run the acceptance programs on real hardware, fix whatever the simulator missed, then and only then merge to `main`.

## Ordered plan

Do tier 1 first, tier 2 after. Within each tier, do smaller features first to build confidence in the workflow.

### Phase A — tier 1 language features

1. **`fun_trim`** — smallest possible port. Proves the workflow end-to-end against an extremely low-risk surface. **DONE 2026-04-20 (291d525).**
2. **`CONST`** — one command, adds one flag to variable table. Dependency for Tier 1 feature interactions (`CONST TYPE x AS INTEGER = 5` style). **Already present at fork point; no port needed.**
3. **`EXECUTE`** — runs a tokenized string. **Already present at fork point; no port needed.**
4. **`REDIM`** — operates on an existing array. Touches DIM machinery, so needs careful testing against our array code which has been extended for VM slots. **DONE 2026-04-20. Interpreter-only — VM array slots are statically sized.**
5. **`TYPE` / `STRUCT` / `END TYPE`** — the centerpiece of 6.02. Ported via `docs/type-struct-port-plan.md` as 19 phases on `catchup/type-struct`, each passing host `./run_tests.sh` compare + rp2040 + rp2350 + wasm gates. **DONE 2026-04-20** — all phases landed, 233/233 host tests green. Two narrow follow-ups documented in-plan: (a) string-array EXTRACT/INSERT in compare mode (interp vs VM layout mismatch — interp-only for now), (b) struct-array-member.field as BYREF user-SUB arg (interp error; PRINT/BOX/expression evaluation works). Neither blocks the `v6.01-parity-plus-lang` tag.

Gate at end of Phase A: host test suite green, device build green, web build green. Tag `v6.01-parity-plus-lang`.

### Phase B — tier 2 portable commands

**Next up: `TILEMAP` + `SPRITE` (item 6)** — prioritized over the smaller tier-2 items because it's the single most game-enabling port left. Owner picked this as the next step on 2026-04-20 after confirming it slots cleanly into our shared graphics path and noting that sprites alone would meaningfully improve `pico_blocks.bas` (explosion animation, ball rendering, collision tests). Afterward return to size-ordered items (7-12).

6. **`TILEMAP` + `FLASH LOAD IMAGE` + `blit121`** — 4 co-dependent sub-ports, ~900 lines total. Research findings (2026-04-20) clarified the upstream structure versus the earlier inventory:
   - **RGB121 is 4 bpp / 16 colours** (1 bit R : 2 bits G : 1 bit B — palette-indexed via `RGB121map[16]`), **not** 3 bpp / 8 colours. Storage layout is two pixels per byte, packed as nibbles. This is the format `FLASH LOAD IMAGE` writes into flash; `blit121` reads it back.
   - **`TILEMAP SPRITE` is a sub-dispatch under `cmd_tilemap`, not a top-level `SPRITE` command.** Top-level `cmd_sprite` in upstream is the older full-RGB VGA sprite system (LOAD/SHOW/HIDE/SCROLL with `spritebuff[]`); that's a separate piece and not in scope for this port. The new tile-based actors are accessed as `TILEMAP SPRITE CREATE / MOVE / SET / DRAW / DESTROY / CLOSE`.
   - **`MAX_SPRITES = 64`** in upstream (earlier inventory said 32 — wrong). `MAX_TILEMAPS = 4`.
   - Upstream gates `cmd_tilemap` on `DISPLAY_TYPE == SCREENMODE2/3` (PicoMiteVGA) so `blit121` writes nibbles straight into the RGB121 framebuffer. Our PicoCalc LCD, host, and wasm framebuffers are all RGB565 — we need an RGB121→RGB565 destination path in `blit121` that expands via `RGB121map[]` on write. This is ~20 lines inside the blit loop, not a separate renderer.
   
   Sub-steps (each is its own commit behind the full four-gate check — host `./run_tests.sh`, rp2040 `.uf2`, rp2350 `.uf2`, wasm `./build_wasm.sh`):
   - 6a. **`FLASH LOAD IMAGE slot, file$ [,O/OVERWRITE]`** (~90 lines, upstream FileIO.c:947). Missing prerequisite — our `cmd_flash` has ERASE/OVERWRITE/LIST/DISK LOAD but not LOAD IMAGE. Reuses existing `decodeBMP` + `writetoflashcallback`. Output writes into `flash_target_contents` (RAM-backed on host/wasm, real flash on device) with an 8-byte `[width uint32][height uint32]` header followed by RGB121-packed pixels. Host note: the `LOAD IMAGE` we already have (`cmd_LoadImage` in FileIO.c:984) is the *screen-blit* variant — decodes BMP and draws to the framebuffer. FLASH LOAD IMAGE is a different command that writes to a flash slot for later reuse. Don't conflate the two.
   - 6b. **`blit121` + `blit121_self`** (~200 lines, upstream `RGB121.c` — a file we don't currently carry). Pure byte-array C, no hardware dependencies, compiles unchanged on host + wasm. Extended with a 16 bpp destination mode that expands RGB121 nibbles to RGB565 via `RGB121map[]`. Gated by `DISPLAY_TYPE` (or equivalent) to pick nibble-write vs RGB565-expand at runtime. Unit-testable in isolation — blit a known source into a host framebuffer and compare bytes.
   - 6c. **`cmd_tilemap` + `fun_tilemap` + `TILEMAP SPRITE` subsystem** (~600 lines, upstream Draw.c:14980-15850). Includes the DATA-statement walker (`tilemap_read_data`), the four tilemap-slot array, the 64-sprite actor array, and the embedded sprite sub-dispatch. Subcommands: `CREATE / ATTR / DESTROY / SET / DRAW / SCROLL / VIEW / CLOSE / SPRITE {CREATE,MOVE,SET,DRAW,DESTROY,CLOSE}`. Function queries: `TILEMAP(TILE / COLLISION / ATTR / VIEWX / VIEWY / COLS / ROWS / SPRITE {X,Y,TILE,HIT,W,H}, …)`. Bridged via `OP_BRIDGE_CMD` — no VM opcodes. Acceptance tests: compute-only (`TILEMAP(COLLISION …)`, `TILEMAP(TILE …)`) needs no display; render test framebuffer-compares a known output.
   - 6d. **Demo: rework `ports/host_wasm/demos/pico_blocks.bas`** to use `TILEMAP SPRITE` for ball + explosion, plus a tilemap backdrop. Ship as a `/sd/` bundled web-host demo so the feature is visible on page load.
   - Feature branch: `catchup/tilemap-sprite` off `catchup-integration`. No direct commits to `main` — device validation on real rp2040 + rp2350 hardware is required before `catchup-integration` merges (see "Merge target" at top).
7. `Mandelbrot` command (if we want it — the bundled `mand.bas` already demonstrates the capability, this is the built-in form).
8. `planet.c` / `Astro` — small, pure math.
9. Individual drawing primitives: `Bezier`, `Star`, `Bitstream`, `Fill`.
10. `Turtle` — self-contained new file, bulk port. Low priority unless someone asks.
11. `re.c` regex swap — **partially DONE.** Imported `re.c` + `re.h` in TYPE/STRUCT Phase 15 (@commit `1a97b33`) for `STRUCT(FIND ... regex)`. Legacy `xregex.c` still handles `cmd_regex` / `fun_regex`. Full swap (delete xregex, migrate legacy callers, ~2000 lines saved) is opportunistic cleanup, not blocking anything.

Gate at end of Phase B: same as Phase A. Tag `v6.02-core-parity`.

### Phase C — tier 3 selective

Triggered by user demand, not by upstream completeness. Pick individual commands off the tier-3 list when someone files a request or a use case appears. Each follows the same per-feature workflow.

No end-of-phase tag; tier 3 is open-ended.

## Tracking

- Keep this file updated: mark each tier-1 and tier-2 feature `DONE <date>` with the commit hash when landed.
- Per-feature branches may be kept as references or deleted after merge — not prescribed.
- When upstream ships a new 6.02.x or 6.03.x release, `git fetch upstream`, scan the diff for newly-added tier-1/tier-2 items, and append them to the inventory. Do not re-baseline against the new upstream — each feature still comes over as its own port.

## Exit criteria

Phase A complete = feature parity with upstream on language. Practical meaning: a BASIC program using `TYPE`/`STRUCT`/`CONST`/`REDIM`/`EXECUTE` from an upstream 6.02 example runs on our builds with identical output.

Phase B complete = portable-command parity. Practical meaning: graphics demos from upstream that stay within the tier-2 command set run on all three of our targets (device, host, web).

We do not target "byte-for-byte upstream parity" — that would require re-monolithing the tree. Tier 3 items are adopted piecemeal on demand; everything in "Explicit non-goals" stays out permanently. The long-term direction remains the core/HAL split (`docs/host-hal-plan.md` and its successors); upstream catch-up is a parallel track, not a substitute.
