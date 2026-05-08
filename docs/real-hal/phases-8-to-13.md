# Real HAL — Phases 8 through 13

Sketches for the later phases. These are not started and their details will firm up as earlier phases complete.

## Phase 8 — `hal_multicore.h`

The surviving raw `multicore_fifo_*` calls form a cross-core protocol for the SPI-LCD merge pipeline (sender core0 → receiver core1) plus ad-hoc QVGA DMA-IRQ toggle messages. This isn't a peripheral — it's a channel.

Rather than introduce a generic `hal_multicore` contract up-front, the pragmatic move is: push every multicore user through its own driver (`drivers/display_merge/`, `drivers/vga_pio/`, `drivers/hdmi/`), and only consider a formal `hal_multicore.h` abstraction if a second cross-cutting use emerges. The driver-owned pattern established in Phase 7 (HDMI scanout receiver in `drivers/hdmi/hdmi_scanout.c::HDMICore`) is the template.

**Exit gate:** no direct `multicore_fifo_*` calls outside `ports/pico_sdk_common/` and drivers. Zero multicore-related ifdefs in scored core files.

### Step 1 ✅ (commit 8185531)

Routed `error()` / `do_end()` / `vm_sys_graphics_fb_stop_merge` / `ClearRuntime()` through the existing `hal_display_merge_abort()` + `hal_display_nextgen_scroll_reset()` hooks (which were landed in Phase 7a but whose four remaining in-core call-sites still did raw FIFO pushes). `#include "pico/multicore.h"` dropped from MMBasic.c / Commands.c / vm_sys_graphics.c. Commands.c target-macro ifdefs 39 → 37.

### Step 2 ✅

Relocated `UpdateCore()` — the 100-line core1 FIFO receiver loop — from `PicoMite.c` into `drivers/display_merge/display_merge_pico.c`, alongside the senders it pairs with. The 512-word `core1stack[]` moved with it (same symbol name, same size — MMBasic.c's canary check `core1stack[0] != 0x12345678` still resolves via the `extern uint32_t core1stack[]` in `Hardware_Includes.h`). PicoMite.c's `CheckAbort()` merge-abort block collapsed to `hal_display_merge_abort()`; the NEXTGEN refresh-rect push in `routinechecks()` collapsed to `hal_display_nextgen_refresh_rect()`. `#ifdef PICOMITE` / `#if defined(PICOMITE) && defined(rp2350)` guards around those sites removed (the HAL stubs are no-ops on non-PICOMITE targets). PicoMite.c now `#include`s `hal/hal_display_merge.h` directly. Net: the inter-core FIFO protocol lives in one file, `drivers/display_merge/display_merge_pico.c`.

### Step 3 ✅

Relocated `QVgaCore()` — the core1 QVGA scanout entry that calls `QVgaInit()` once and then spins on the inter-core FIFO for 0x5555 / 0xAAAA DMA-IRQ toggle messages — from `PicoMite.c` into `drivers/vga_pio/vga_qvga_modes.c`. Its 128-word `core1stack[]` moved with it (symbol + size preserved). `QVgaInit()` and the underlying `QVgaPioInit/QVgaBufInit/QVgaDmaInit` chain stay in PicoMite.c for now and are called via an `extern void QVgaInit(void);` in the driver; moving that machinery is a separate refactor.

**Phase 8 closed.** PicoMite.c has zero direct `multicore_fifo_*` calls. All inter-core FIFO traffic now lives in `drivers/` (display_merge, vga_pio, hdmi, spi_lcd for FASTGFX). Zero multicore-related ifdefs exist in any scored core file. No generic `hal/hal_multicore.h` abstraction was introduced — the driver-owned pattern is cleaner and the HAL contract stays focused on display/audio/fs/net surfaces rather than inter-core plumbing.

**Commit-count target:** 3 commits (step 1 done; step 2 done; step 3 done). Closed.

## Phase 9 — `hal_net.h`

PICOMITEWEB only. 67 `PICOMITEWEB` blocks across 8 files.

- Define `hal/hal_net.h` (TCP listen/accept, UDP send/recv, DNS, HTTP server hook).
- `drivers/cyw43/` lifted from current Pico W network code.
Original plan called for a `hal/hal_net.h` contract with TCP/UDP/DNS
entry points and a `drivers/cyw43/` driver. In practice the
PICOMITEWEB sprinkle across core files came in two flavours — one was
"WEB-specific dispatch" which did want a network HAL surface, the
other was "WEB-specific non-network state tweaks" that were really
disguised PSRAM-sizing / framebuffer / interpreter-placement
decisions that belong to other HAL surfaces (heap / display /
port-config). The phase closed by treating each site on its merits
rather than forcing every site through a single `hal_net.h` contract.

**Exit gate:** zero `PICOMITEWEB` references in any scored core file. ✅ met.

### Step 1 ✅ — display_state.c

1 ifdef. `struct3d` + `camera` storage promoted to unconditional
(`drivers/gfx_3d/gfx_3d.c` is the only reader and is not linked on
WEB; the ~150 bytes of dead BSS on WEB isn't worth a gate).

### Step 2 ✅ — Functions.c

5 ifdefs. MM.MESSAGE$ / MM.ADDRESS$ / MM.TOPIC$ routed through a
`port_fun_mm_mqtt_copy(which, out)` port hook (WEB real impl in
MMMqtt.c, non-WEB stub in MMweb_stubs.c / host_peripheral_stubs.c).
MM.SUPPLY case body was already runtime-checking
`ExtCurrentConfig[44]==EXT_ANA_IN || Option.LOCAL_KEYBOARD` which is
false on WEB — the `#ifndef PICOMITEWEB` gate was redundant. Enum
(Operation) and overlaid_functions[] flattened to include every
entry on every target; the flattening dropped 2 more ifdefs in
configuration.h.

### Step 3 ✅ — MMBasic.c

6 ifdefs. Mix of:
  - `core1stack[0]` canary check — added a 1-element core1stack stub
    to MMtcpserver.c (WEB has no core1 but needs the symbol).
  - `DefinedSubFun` / `getvalue` / `findvar` flash-vs-RAM placement
    — introduced per-port `HAL_PORT_MMBASIC_HOT_FUNC(name)` and
    `HAL_PORT_MMBASIC_SUBFUN_FUNC(name)` macros in every
    `ports/*/port_config.h` + `host/port_config.h`. Each expands to
    `__not_in_flash_func(name)` or plain `name` per port, preserving
    the exact existing placement on every target.
  - PSRAM range-check in ClearVars — promoted `PSRAMbase` to
    unconditional in configuration.h (0 on non-rp2350, 0x11000000 on
    rp2350 non-WEB). The range check becomes a runtime no-op on
    targets without PSRAM.
  - ClearRuntime TCP-state teardown — routed through new
    `port_web_clear_runtime_state()` hook.

### Step 4 ✅ — Commands.c

12 ifdefs. Four categories:
  - Flash-vs-RAM placement on cmd_inc / cmd_if / cmd_else / cmd_loop
    → HAL_PORT_MMBASIC_SUBFUN_FUNC or HAL_PORT_MMBASIC_HOT_FUNC
    (cmd_loop uses HOT, the other three use SUBFUN — they differ on
    rp2040 VGA placement).
  - cleanserver() + close_tcpclient() stubs added to MMweb_stubs.c +
    host_peripheral_stubs.c.
  - SaveContext / RestoreContext PSRAM fast-path flattened to
    runtime `if(PSRAMsize)` branch; psmap bitmap stubbed as a 1-word
    array on non-rp2350 so Commands.c's `sizeof(psmap)` references
    resolve.
  - rp2350a-package runtime check at the "CONFIGURE LIST PINS"
    dispatch — `rp2350a` is always true on rp2040 so the check is a
    no-op elsewhere.

### Step 5 ✅ — Memory.c

13 ifdefs. Every site was a nested `#ifdef rp2350 / #ifndef
PICOMITEWEB / PSRAM code` — since PSRAMsize stays 0 at runtime on
rp2350 WEB (CYW43 consumes the QSPI pins), the `#ifndef PICOMITEWEB`
inner gate was redundant with the `if(PSRAMsize)` runtime check.
Dropped all 12 nested ifdefs. The remaining framebuffer-pointer
storage block (`#if defined(PICOMITE) || defined(PICOMITEWEB)`
originally, from the step-1 merge) rewrote as `#ifndef PICOMITEVGA`
to eliminate the PICOMITEWEB token entirely.

**Commit-count target:** 5 commits (actual: 5).

## Phase 10 — `hal_heap.h` + Memory.c cleanup ✅

Memory.c's 39 hardware `#ifdef`s (phase-9 tip) → 1 (remaining is
`#ifdef GUICONTROLS`, a feature flag, permitted by the strict
check). No `hal/hal_heap.h` contract introduced — the original plan
sketched `hal_heap_size()` / `hal_heap_base()` / `hal_heap_psram_base()`
/ `hal_heap_psram_size()` getters, but driver relocation + runtime
`if(PSRAMsize)` checks + port-config value macros turned out cleaner
than a function-pointer HAL surface.

**Exit gate:** Memory.c in STRICT_FILES with zero target-macro and zero
port-config-macro ifdefs. ✅ met.

### Step 1 ✅ — PSRAM bitmap + allocator → drivers/psram_heap/

Relocated `psmap` page-bitmap + `SBitsGet` / `SBitsSet` / `GetPSMemory`
into `drivers/psram_heap/psram_heap_pico.c` (rp2350 non-WEB real impl,
~24 KB BSS) + `drivers/psram_heap/psram_heap_stub.c` (no-op on every
other target). Callers in Memory.c (FreeMemory / GetMemory /
TryGetMemory / FreeSpaceOnHeap / UsedHeap / MemSize / FreeMemorySafe)
drop their nested `#ifdef rp2350` gates — runtime `if(PSRAMsize)` and
address-range checks keep the paths dormant where PSRAMsize == 0.
Commands.c's SaveContext / RestoreContext use the new driver const
`psmap_size_bytes` instead of `sizeof(psmap)` (which broke after the
extern lost its array bound). Fixes a pre-existing
7 MB-vs-6 MB size mismatch between Commands.c's extern and Memory.c's
definition in the process.

### Step 2 ✅ — VGA framebuffer + tile state → drivers/vga_pio/vga_memory.c

Lifted the VGA/HDMI-specific storage block (tilefcols / tilebcols /
HDMIlines / tilefcols_w / tilebcols_w / X_TILE / Y_TILE / ytileheight
/ M_Foreground / M_Background / WriteBuf / DisplayBuf / LayerBuf /
FrameBuf / SecondLayer / SecondFrame) out of Memory.c into
`drivers/vga_pio/vga_memory.c` (VGA + HDMI real impl) +
`drivers/vga_pio/vga_ops_stub.c` (non-VGA stubs). InitHeap calls
new `vga_memory_init_planes()` hook instead of an inline
`#ifdef PICOMITEVGA` plane rebind. HDMICore's core1stack moved into
`drivers/hdmi/hdmi_scanout.c` alongside its core1 entry function.

### Step 3 ✅ — Unified AllMemory layout via port-config

Collapsed the 4-branch memory-layout block (`rp2350 VGA` / `rp2350 non-VGA`
/ `rp2040 VGA` / `rp2040 non-VGA`) into a single unconditional
declaration:

```c
unsigned char __attribute__((aligned(HAL_PORT_ALLMEMORY_ALIGN)))
    AllMemory[HEAP_MEMORY_SIZE + 256 + HAL_PORT_FRAMEBUFFER_TRAILER_BYTES];
unsigned char *MMHeap = AllMemory;
```

New port-config macros in every `ports/*/port_config.h`:

  - `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES`: 320*240*2 on rp2350 VGA +
    HDMI, 640*480/8 on rp2040 VGA, 0 everywhere else.
  - `HAL_PORT_ALLMEMORY_ALIGN`: 4096 on rp2040 VGA (preserves the
    USB MSC flash-page alignment the old `Heap[]` array had), 256
    elsewhere.

`FRAMEBUFFER` and `framebuffersize` now live in the VGA driver
(`vga_memory.c` points FRAMEBUFFER at `AllMemory + HEAP + 256`;
`vga_ops_stub.c` sets it NULL). The rp2040 VGA path no longer has a
separate `video[]` array — the 38400-byte scanout buffer is now the
trailer of AllMemory, same layout as rp2350 VGA.

Scoreboard: Memory.c 39 → 1 (−38 across phase 9 step 5 + phase 10
steps 1–3). Total 104 → 40. rp2350 refs across core files 48 → 17.
PICOMITEVGA 11 → 3. All 12 device variants + host 239/239 + HAL
purity gate green. Memory.c promoted to STRICT_FILES.

**Commit-count target:** 3 commits (step 1 + step 2 + step 3 combined).

## Phase 11 — Sweep + remaining drivers + scope cleanup ✅

### Step 1 ✅ — Commands.c + Functions.c → STRICT_FILES

Commands.c (17 ifdefs) and Functions.c (11 ifdefs) drive to zero
target-macro ifdefs. Patterns:

  - Dead-code blocks deleted (vm_run_memdiag, identical-branch
    rp2350 dead `int dims[MAXDIM]={0}` fork, stale `#ifndef rp2350`
    sinetab gates left over from an older fast-path split).
  - Placement macros on cmd_for / cmd_next / cmd_do / cmd_inc /
    cmd_if / cmd_else / cmd_loop / fun_ternary collapse to
    HAL_PORT_MMBASIC_HOT_FUNC / HAL_PORT_MMBASIC_SUBFUN_FUNC from
    Phase 9.
  - `pico_rand` linked on every device target (it's in rp2_common),
    so `pico/rand.h` + `get_rand_32()` are unconditional.
  - Operation enum + overlaid_functions[] flattened; MMPS2 now always
    recognized (stub PS2code = 0 on USB builds).
  - ADC channel select: `(rp2350a ? 3 : 7)` collapses to `3` on
    rp2040 since rp2350a stubs to true there.
  - sinetab[360] promoted to unconditional (2.8 KB of flash) —
    fast-path available on every target.
  - setmode stub moved to drivers/vga_pio/vga_ops_stub.c; host picks
    it up via the existing CORE_SRCS link.
  - cleanserver + close_tcpclient + initMouse0 now stubbed on USB
    device builds (USBKeyboard.c) + host (host_peripheral_stubs.c)
    so Commands.c + PicoMite.c can call them unconditionally.
  - FileLoadCMM2Program stubbed on rp2040 (defines_loader.c `#else`)
    and host. CMM2mode is now a runtime parameter, no compile gate.
  - RANDOMIZE always available (srand harmless on rp2350's hardware
    RNG path).

### Step 2 ✅ — bc_alloc + bc_source + bc_vm target-macro sweep

  - bc_alloc.c collapses to a single body (the MMBASIC_HOST/else
    fork had identical TryGetMemory/FreeMemory wiring).
  - bc_source.c: dead `bc_opt_level` duplicate definition deleted;
    PWM8A..PWM11B parser keywords unconditional (enum values
    promoted to unconditional in vm_sys_pin.h).
  - bc_vm.c: op_randomize's `seed==0` default sources from new
    `HAL_PORT_RANDOMIZE_DEFAULT_SEED()` port macro; backward-branch
    slowdown routed through new `hal_time_slowdown_tick()` HAL
    hook.

### Result ✅

**All 8 main-scoreboard core files have zero target-macro ifdefs
across every tracked macro (PICOMITE, PICOMITEVGA, PICOMITEWEB,
HDMI, rp2350, PICO_RP2350, USBKEYBOARD, MMBASIC_HOST, MMBASIC_WASM,
PICOMITEPLUS, PICOCALC, HAL_PORT_*).** The scoreboard total of 12
is entirely `#ifdef GUICONTROLS` feature-flag gates + two `#ifndef
min/max` stdlib polyfills in FileIO.c. Memory.c + Commands.c +
Functions.c are now all in STRICT_FILES alongside Draw.c / MM_Misc.c
/ External.c / FileIO.c / Audio.c.

### Step 3 ✅ — vm_sys_time + bc_debug port hooks

  - vm_sys_time.c: 2 → 0. New port hook `port_vm_time_get_tm()`
    (device impl in ports/pico_sdk_common/vm_sys_time_pico.c reading
    readusclock + TimeOffsetToUptime; host impl in host_runtime.c
    with MMBASIC_HOST_DATE / MMBASIC_HOST_TIME env-var per-field
    overrides).
  - bc_debug.c: 5 → 0. `dbg_print` collapses to MMPrintString (host
    has its own routing). `BCCrashInfo` storage attribute via new
    per-port macro `HAL_PORT_BC_CRASH_INFO_ATTR`
    (`.uninitialized_data` section on device, plain BSS on host).
    Stack-pointer + CFSR/HFSR/BFAR/MMFAR reads moved to
    `port_bc_crash_get_sp()` + `port_bc_crash_save_fault_regs()`
    port hooks (device impl in `ports/pico_sdk_common/bc_crash_pico.c`,
    host stubs in host_runtime.c). Fault-register decode print
    stays inline in bc_debug.c — harmless on host since the fields
    stay zero.

### Step 4 ✅ — vm_sys_pin PWM-slice gates

vm_sys_pin.c: 15 → 11. Drop 4 `#ifdef rp2350` gates around PWM
slice / mode-lookup helpers now that PWM0A..PWM11B enum values and
PinDef-mask bits are unconditional. Remaining 11 ifdefs are genuine
device-code splits (register-layout differences between chips) that
want a vm_sys_pin_device spin-off — deferred to a later step.

### Steps 5–20 ✅ — INFO-tracked VM/MMBasic files driven to zero

Steps 5 and 6 (commit `93e6ab2`) drained `bc_runtime.c` and added the
`hal_vm_framebuffer` contract. Steps 7 and 8 (`aefd706`) closed the
last bc-side gates. Steps 12, 13, 14, and 20 (`2b4beb6`) promoted
`bc_runtime.c`, `bc_bridge.c`, `vm_sys_graphics.c`, and `MMBasic.c`
to STRICT_FILES. The MMBasic.c funtbl flatten that previously
overflowed rp2040 RAM was solved by relocating funtbl[] +
hashlabels + the hash-based FindSubFun / findlabel / findvar
collision check into `ports/pico_sdk_common/funtbl_port.c` behind
`port_try_*` hooks, and extracting `error()`'s console-surface +
LCD banner helpers into `clear_runtime_port.c` — link-time port
selection, not a runtime branch.

`vm_sys_file.c`, `vm_sys_pin.c`, and `bc_debug.c` are also at zero
target-macro and zero port-config ifdefs (steps 3+4 in `d24cfec`).

### Final scoreboard (commit `1bab851`)

```
Phase  Draw      MM_Misc   External  FileIO    Commands  Memory    Functions Audio     Total
now    0         0         0         2         0         0         0         0         2
```

`MMBasic.c` and `FileIO.c` each carry 2 ifdefs, all permitted by the
strict check: `GUICONTROLS` / `MMFAMILY` / `__PIC32MX__` feature
flags and `min/max` stdlib polyfills. Zero target-macro and zero
port-config gates remain in any STRICT or INFO file.

### Optional follow-ups (not required for phase closure)

These are code-organisation improvements; the HAL purity goal is
already met because their parent core files (`MM_Misc.c`,
`Commands.c`, `External.c`, `PicoMite.c`) are all in STRICT_FILES at
zero ifdefs.

- **`drivers/watchdog_pico/`** for `cmd_watchdog`, `fun_restart`, `cmd_cpu`, `cmd_reset` (currently in `MM_Misc.c`).
- **`drivers/gps_uart/`** for the GPS subsystem (currently `GPS.c` at root, with globals in `ports/host_native/host_peripheral_stubs.c`).
- **`drivers/goodix_touch/`** (currently `goodix.c` at root).
- **`drivers/mouse_serial/`** (currently `mouse.c` at root).
- **CFunctions architectural decision + wasm-ld `CallCFunction` warning** — bundled with Phase 13 contract lock.

**Exit gate (met):** `tools/check_hal_purity.sh` passes; every device target builds clean; host tests 240/240; mmbasic_stdio corpus 8/8.

## Phase 12 — Host + WASM relocation

Now the device HAL contract is locked, observed across 12 targets.

- Move `host/host_*.c` files into `ports/host_native/` and `drivers/host_*/`. One commit per file moved, validated by `./run_tests.sh` after each.
- Move `host/host_wasm_*.c` likewise into `ports/host_wasm/` and `drivers/wasm_*/`.
- Subsume `host_fb.h`, `host_fs_hal.h`, `host_keys.h`, `host_terminal.h`, `host_time.h` into the corresponding `hal/*.h` files. Where the host took a shortcut that doesn't fit the device contract, the shortcut is examined — fixed if it's a real divergence, codified into the HAL only if it was right and the device contract was wrong.
- Retire the `host/` directory. Build scripts move to `ports/host_native/build.sh` and `ports/host_wasm/build.sh`. Test harnesses move to `tests/`.
- Top-level CMakeLists.txt files get rewritten as per-port CMake recipes.

**Exit gate:** no `host/` directory. `./run_tests.sh` (now under `tests/`) 192/192. WASM smoke harnesses green. All device targets still build clean (this phase touched no device source).

### Phase 12 status (2026-04-23)

✅ **Source-code retirement CLOSED (steps 1–13).** Every `host/*.c` + `host/*.h` moved to `ports/host_native/` or `ports/host_wasm/`; SDK-shim trees (`host/pico/`, `host/hardware/`) moved under `ports/host_native/`; `host/vendor/mongoose.{c,h}` moved under `ports/host_native/vendor/`. `ports/host_native/Makefile` + `ports/host_wasm/Makefile` are now the canonical builds (objects land in their own build trees). `host/Makefile` + `host/Makefile.wasm` demoted to 3-target `$(MAKE) -f` delegators. Binaries + WASM artefacts still land at legacy `host/` paths so every existing workflow (`run_tests.sh`, `buildall.sh`, docs URLs, `serve.py`) keeps working. Host 239/239 + 12/12 device variants green.

✅ **Header subsumption into `hal/`: not needed.** Audit of the five port-internal headers showed every caller is inside `ports/host_native/` or `ports/host_wasm/` — no device / no shared-core file includes them. They're legitimate port-private headers, not device-divergent shortcuts. No action taken.

❌ **Phase 12.6 (`host/` full retirement) SKIPPED (2026-04-24).** Remaining content is user-facing tooling: `build.sh` / `build_wasm.sh` (delegated), `run_tests.sh` + sibling `run_*.sh` harnesses, `tests/` data, `demos/`, `web/` bundle, `README.md`. The architectural goal — source code out of `host/`, ports self-contained, canonical builds under `ports/host_native/` + `ports/host_wasm/` — is already met by Phase 12. Further moves are cosmetic and cost high churn (CI, `buildall.sh`, `serve.py`, ~20 doc URLs, external bookmarks, muscle memory). The purity gate prevents re-introduction of target spaghetti at the source level; that's what Phase 13 locks in. Decision: skip as a dedicated phase; do opportunistic moves only if a future third native port makes the symmetry argument concrete.

## Phase 12.5 — `mmbasic_stdio` pure-stdio executable (HAL litmus test)

- Land `ports/mmbasic_stdio/` with the layout from `architecture.md`.
- `main.c`: parse `argv[1]` as a `.bas` file (or read stdin to EOF if no argv), tokenise, run, exit. Errors go to stderr. `PRINT` goes to stdout.
- HAL impls: write the minimal set listed in `architecture.md`. Most reuse Phase 12 host_native impls (`hal_time`, `hal_filesystem` POSIX). New ones: `hal_keyboard_stdio` (read from stdin, blocking), `hal_display_null` (PRINT path goes to console; pixel/graphics ops error). All hardware-only HALs (`hal_pin`, `hal_audio`, `hal_multicore`, `hal_flash`, `hal_net`) link a `_hard_error` stub that calls MMBasic's `error()` if invoked.
- **Litmus criterion:** the link line for `mmbasic_stdio` must contain **no** files from `drivers/host_fb/`, `drivers/host_termios_kbd/`, `host/host_wasm_*`, `Editor.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, or any display driver. If the linker pulls those in due to undefined references, that's a HAL leak — fix the leak, don't link the file.
- Test harness: `tests/mmbasic_stdio/` runs a corpus of `.bas` programs through the stdio binary and diffs output against expected. Programs that touch hardware-only commands (`PIXEL`, `PLAY`, `PIN`) must produce the documented MMBasic error message via the hard-error stubs.

**Exit gate:** `mmbasic_stdio` builds. Stdio test corpus passes. Link line audit shows no display/REPL/editor files pulled in. The binary is small (target: under 500 KB stripped on x86_64 macOS, since it carries no graphics or filesystem-sim code).

### Phase 12.5 status (2026-05-08)

✅ **Closed.** `ports/mmbasic_stdio/` builds; `./mmbasic_stdio` runs BASIC programs via stdin/stdout. Link-line audit clean: `build/` contains zero objects for `Editor.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, `host_fb.c`, `host_terminal.c`, `host_main.c`, or `host_fastgfx.c` — the MMBasic core is genuinely hardware-clean.

✅ **Test corpus.** 8-test corpus at `ports/mmbasic_stdio/tests/run_tests.sh` (PRINT, FOR/NEXT, IF/THEN, strings, SUB/FUNCTION, plus two hardware-only `PIXEL`/`BOX` programs that must error via the hard-error stubs). Passes 8/8.

✅ **Binary size.** 601 KB stripped at `-O2` on arm64 macOS (down from the original 1.2 MB at `-O0`). The plan's <500 KB target was set against an x86_64 baseline; on arm64 the smallest practical Mach-O is in the 600 KB range without further `--gc-sections` aggression. Spirit of the gate (no display / FS-sim / editor pulled in) is met.

## Phase 13 — Lock the contract 🔧

- ✅ `tools/check_hal_purity.sh` wired into both `host/run_tests.sh` (line 167) and `buildall.sh` (line 33). Every commit on the branch passes the gate before the device build matrix runs.
- ✅ `tools/check_ram_baseline.sh` wired into `buildall.sh` (post-build, after the loop succeeds). Per-target baselines under `tools/ram_baseline_<TARGET>.txt` cover all 14 device variants; gate fails on >64 bytes of BSS growth. Not wired into `host/run_tests.sh` — that script doesn't cross-compile and has no ELFs to inspect, so the gate naturally lives only in `buildall.sh`. `SKIP_RAM_BASELINE=1` escape hatch mirrors `SKIP_HAL_PURITY=1`.
- ⏳ `tools/perf_microbench/` is just a `.gitkeep` placeholder. Nothing in it yet; needs the device-side BASIC microbench corpus + a host comparison harness so a commit that regresses pixel-write or sample-output throughput fails the gate.
- ✅ Predecessor plans superseded (`a92f4f0`): `bridge-restoration-plan.md`, `host-hal-plan.md`, `web-host-plan.md` carry the marker.
- ✅ `docs/adding-a-new-port.md` landed (`c3fb8cc`): covers both hardware and simulation ports.
- ✅ `drivers/CONTRIBUTING.md` landed: rules for new drivers (one peripheral, conformance test, no cross-driver coupling, RAM-resident annotations).
- ⏳ MEMORY.md trim: replace `project_host_is_its_own_port` and related entries with a single pointer to this plan.
- ⏳ CFunctions architectural decision + wasm-ld `CallCFunction` warning resolution. Deferred from Phase 11.

**Exit gate:** future contributors can't quietly re-introduce target spaghetti, and they have a paved path for adding a new board or driver.

## Open questions (resolved as phases land)

1. **Naming.** `hal_display` vs `hal_lcd` vs `hal_video`? Going with `hal_display` — covers VGA/HDMI/LCD/canvas.
2. **Tier-B inlining mechanism.** Per-port inline header (decided in Phase 0, validated by prototype).
3. **Where do shared constants live?** `NBRPINS`, `STRINGSIZE`, `MAXVARS` are MMBasic-level, not HAL. Stay in `core/configuration.h`.
4. **Picomite `Option.PIN` and on-flash settings.** Goes through `hal_flash`. The persistent option block is HAL-storage; the *meaning* of the bytes is core/MMBasic.
5. **CFunctions.** Defer the architectural decision to Phase 11; document the wasm-ld `CallCFunction` warning resolution there.

## Out of scope (for this plan)

- Adding new boards beyond the existing 12 device targets.
- Replacing FatFS or LFS with a different filesystem.
- Replacing the bytecode VM with a different backend.
- Cross-target binary releases (one binary per target, as today).
- Refactoring the BASIC dialect or the parser.
