# Web Host Plan

Compile the macOS host build of PicoMite to WebAssembly so the interpreter + VM run entirely in-browser as a static site. The browser app mirrors the hardware PicoMite: one `<canvas>` renders everything — `PRINT` output, the `> ` prompt, line editor, graphics, all drawn as pixels into the same framebuffer the device uses. Web Audio handles `PLAY`; drag-and-drop / download handle file exchange. No TTY, no terminal emulator, no server round-trip per keystroke, no backend at all.

- **Branch:** `web-host` (off `host-hal-refactor`).
- **Predecessor plan:** [`host-hal-plan.md`](host-hal-plan.md). That refactor turned the host into a proper HAL consumer (shared `core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, `core/mmbasic/MM_Misc.c`) with focused HAL modules (`host_runtime.c`, `host_fastgfx.c`, `host_fs_shims.c`, `host_peripheral_stubs.c`, `host_fb.c`, `host_time.c`, `host_terminal.c`). The web host is the third HAL target — macOS native, `--sim` (Mongoose + WebSocket), and now WASM (emscripten + direct JS bridge).
- **Native host is frozen for this work.** `mmbasic_test` and `mmbasic_sim` keep their current termios TTY REPL path, test harness behavior, and build flags. The web-host branch touches only new files (`host_wasm_*.c`, `host/web/*`, `host/build_wasm.sh`, `host/Makefile.wasm`) and a small set of additive `#ifdef MMBASIC_WASM` gates where a narrow WASM-specific behavior is unavoidable. If a change to a shared or native-host file is tempting, that's a signal the abstraction is wrong — fix the WASM HAL instead.
- **No TTY. No terminal emulator.** The web host behaves like the hardware PicoMite, not like the native macOS host. Character output goes through the existing raster console (`gfx_console_shared.c` → `host_fb.c` framebuffer → canvas blit). `host_terminal.c` is simply not linked in the WASM build; there is no xterm.js, no `<pre>` pane, no VT100 parser. Keyboard input arrives via `wasm_push_key(code)` → ring buffer → `MMInkey`, where "code" is an MMBasic key code (ordinary ASCII plus the same F-key/arrow codes the device expects).

## Invariants

1. **Shared source is untouched.** `core/mmbasic/MMBasic.c`, `core/mmbasic/Commands.c`, `core/mmbasic/Functions.c`, `core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, `core/mmbasic/MM_Misc.c`, the VM (`bc_*.c`, `vm_sys_*.c`), `gfx_*_shared.c`, and `shared/mmbasic/mm_misc_shared.c` compile as-is. The web port is purely a HAL backend swap — anything that required editing a shared file is a bug in the port.
2. **The native host test harness (`mmbasic_test`) stays green.** The WASM target is additive. `cd host/ && ./build.sh && ./run_tests.sh` must pass 201/201 at every phase boundary.
3. **The device build stays green.** No changes to `CMakeLists.txt` / `CMakeLists 2350.txt` or anything under `#ifndef MMBASIC_HOST`.
4. **No server.** The deployable artifact is `index.html` + `picomite.wasm` + `picomite.js` + a preloaded data image. It runs from `file://`, GitHub Pages, or any static host. No COOP/COEP headers required for the MVP (rules out SharedArrayBuffer + pthreads until Phase 7+).
5. **Behavioral parity with the native host's screenshot path.** The native `mmbasic_test` renders `PRINT`, graphics, and the console into an RGB framebuffer via `gfx_console_shared.c` + `host_fb.c` and writes it out as a PPM. The web host displays that same framebuffer on a `<canvas>` in real time. A `.bas` program's canvas should be pixel-identical to the native host's PPM screenshot. The only legitimate divergences are timing-sensitive ones (cooperative scheduling may change `TIMER` resolution) and peripherals that are no-ops on both ports anyway.

## Target architecture

```
┌──────────────────────────────────────────────────────────────┐
│                      Shared core (unchanged)                 │
│   core/mmbasic/MMBasic.c, core/mmbasic/Commands.c, core/mmbasic/Functions.c, core/mmbasic/Operators.c, core/mmbasic/MATHS.c   │
│   core/mmbasic/MMBasic_REPL.c, core/mmbasic/MMBasic_Prompt.c, core/mmbasic/Editor.c                 │
│   core/mmbasic/Draw.c, core/mmbasic/FileIO.c, shared/audio/Audio.c, core/mmbasic/MM_Misc.c, shared/mmbasic/mm_misc_shared.c     │
│   runtime/vm/bc_source.c, runtime/vm/bc_vm.c,                │
│   runtime/vm/bc_runtime.c, runtime/vm/vm_sys_*.c             │
│   gfx_*_shared.c                                             │
├──────────────────────────────────────────────────────────────┤
│                   HAL surface (current)                      │
│   host_runtime.c / host_fs_shims.c / host_fb.c /             │
│   host_fastgfx.c / host_peripheral_stubs.c /                 │
│   host_time.c / shared/mmbasic/mm_misc_shared.c                             │
│   — all compile unchanged under emscripten                   │
├──────────────────────────────────────────────────────────────┤
│             WASM-specific HAL (new, replaces 2 files)        │
│   host_wasm_console.c  ← key-ring input + yield hooks        │
│                          (no output path — console renders   │
│                          into framebuffer via shared code)   │
│   host_wasm_main.c     ← replaces host_main.c test harness   │
│                          (wasm_boot → InitialiseAll → REPL)  │
├──────────────────────────────────────────────────────────────┤
│                     Browser surface (JS/HTML)                │
│   index.html + picomite.mjs (loader, glue)                   │
│   ui/canvas.js         — framebuffer blit, dirty-rect        │
│   ui/audio.js          — Web Audio oscillator/sample pool    │
│   ui/keys.js           — keydown → MMBasic key codes         │
│   ui/fs.js             — drag-drop import, download export   │
└──────────────────────────────────────────────────────────────┘
```

`host_sim_server.c` / Mongoose / WebSockets from the native `--sim` target are **not** ported. The WASM module exposes its C entry points (`wasm_boot`, `wasm_push_key`, `wasm_tick`, `wasm_framebuffer_ptr`, ...) directly to JS via emscripten's `-sEXPORTED_FUNCTIONS`. Browser-side JS calls them with `Module.ccall` / direct wrappers. No in-process HTTP server, no WebSocket protocol.

### What carries over from the native host, unchanged

| Module | Why it's portable |
|---|---|
| `host_runtime.c` | Lifecycle, `Option`/`FontTable`/`PinDef`/`inttbl` backing globals, error-recovery snapshot — pure C, no OS calls. |
| `host_fastgfx.c` | FASTGFX/FRAMEBUFFER back-buffer RAM allocation + memcpy-based SWAP. No OS, no threading. |
| `host_fb.c` | Pixel plane, rectangles, layers, screenshot (screenshot is trivially redirectable to a canvas `toDataURL`). |
| `host_fs_shims.c` | POSIX FS calls (`open`/`read`/`write`/`opendir`/`readdir`/`stat`/`chdir`/`mkdir`/`unlink`/`rename`/`getcwd`) — emscripten libc provides all of these over MEMFS, which is what we use. |
| `host_peripheral_stubs.c` | No-op I2C/SPI/PWM/PIO/UART/GPIO/ADC — pure stubs, no change. |
| `host_time.c` | `clock_gettime(CLOCK_MONOTONIC)` works under emscripten; `nanosleep` works under ASYNCIFY. |
| `shared/mmbasic/mm_misc_shared.c` | Portable sort/longstring/format/date-time/pause — pure C. |
| VM + compiler (`bc_*.c`) | Already C99, no platform calls. |
| Shared commands (`core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, `core/mmbasic/MM_Misc.c`) | Host HAL refactor already gated every device branch with `#ifdef MMBASIC_HOST`. |

### What needs a new backend

| Native host module | WASM replacement | Reason |
|---|---|---|
| `host_terminal.c` (termios raw-mode stdin, unbuffered stdout) | `host_wasm_console.c` — **input-only** ring buffer fed by `wasm_push_key(code)`; no output path (character output already reaches the framebuffer via `gfx_console_shared.c`) | No termios in browsers; no terminal emulator either. |
| `host_main.c` (CLI test harness, argv parsing, oracle compare) | `host_wasm_main.c` — exported `wasm_boot()` / `wasm_tick()` entry points wired to `emscripten_set_main_loop` or pure ASYNCIFY | No argv, no compare-mode; main loop must yield cooperatively. |
| `host_sim_server.c` (Mongoose HTTP + WebSocket server, pthread tick) | **Dropped entirely.** Browser JS calls exported C functions directly; no protocol layer in between. | Not needed without an in-process network server. |
| `vendor/mongoose.c` | **Dropped entirely.** | Same. |
| `host_sim_audio.c` JSON emitter | `host_wasm_audio.c` — same shape, but calls `EM_JS` directly into a Web Audio queue (no JSON marshaling) | Type-safe, zero-copy. |

## How the emscripten build fits in

The WASM port is a new make target alongside `mmbasic_test` and `mmbasic_sim`. All three share `CORE_SRCS`; they diverge only in the HAL modules linked.

- **Toolchain:** emscripten 3.1.x or later (`emcc`, `emmake`, `emrun`). Pinned in `host/build_wasm.sh` and CI.
- **Command shape:**
  ```sh
  emcc -O2 \
    -DPICOMITE -DMMBASIC_HOST -DMMBASIC_WASM \
    -include host_platform.h \
    -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
    -sEXPORTED_FUNCTIONS='["_main","_wasm_boot","_wasm_push_key","_wasm_tick","_wasm_framebuffer_ptr","_wasm_framebuffer_width","_wasm_framebuffer_height","_malloc","_free"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","FS"]' \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web \
    -lidbfs.js \
    --preload-file tests@/sd \
    $(CORE_SRCS) $(WASM_HAL_SRCS) \
    -o web/picomite.mjs
  ```
- **Size target:** ~1.5 MB uncompressed `.wasm`, ~600–800 KB gzipped. Verified in CI.
- **MMBASIC_WASM macro** — new narrow gate for the handful of spots that must differ from native (e.g. `nanosleep` → `emscripten_sleep`, console output → `EM_ASM`). Set in addition to `MMBASIC_HOST`, never instead of it.

### Execution model

The interpreter owns the main loop (`ExecuteProgram` → statement loop → periodic `CheckAbort` / `MMInkey` poll). In a browser, blocking forever freezes the tab.

**MVP: ASYNCIFY.** Emscripten's ASYNCIFY transforms the generated code so that marked "sleep" points unwind the C stack, return to the JS event loop, and resume on the next tick. We mark:

- `host_sleep_us` (already the single entry point for all `PAUSE` / `DELAY` / `nanosleep`)
- `host_sync_msec_timer` (called on every interpreter poll checkpoint — yields 1 tick worth of frame time)
- `MMInkey` when the queue is empty (optional — only if `INPUT$` / `EDIT` need it; most polled reads return immediately)

ASYNCIFY adds ~30% runtime cost and ~15% binary size. Acceptable for MVP. Pay attention to which call sites need `await`-able behavior; over-marking bloats the transform.

**Post-MVP: worker + Atomics.** The interpreter moves to a Web Worker. The main thread owns the DOM (canvas, audio). Communication is `postMessage` for bulk data and `Atomics.wait`/`Atomics.notify` on a `SharedArrayBuffer` ring for sub-millisecond input latency. Requires COOP/COEP response headers, which rules out naive GitHub Pages deployment (need `_headers` on Netlify / Cloudflare Pages / custom hosting). Defer until ASYNCIFY proves inadequate.

## Phased plan

Each phase ends with a green native host build, a green device build, a green WASM build, and a commit. No phase depends on the next.

### Phase 0 — Toolchain scaffolding ✅ (2026-04-18)

**Goal:** `emcc hello.c` builds and serves; the native build still passes.

- Add `host/build_wasm.sh` that installs/activates emscripten (or assumes `EMSDK` env) and invokes `emmake make -f Makefile.wasm`.
- Add `host/Makefile.wasm` — mirrors `host/Makefile` but with the `CORE_SRCS` pared to just enough to prove linking (REPL + interpreter, no graphics, no audio).
- Add `host/web/` with `index.html`, `picomite.css`, and a placeholder `app.mjs` that imports the compiled module and writes "Hello from PicoMite WASM" to a `<pre>`.
- Add a `host/web/serve.sh` that runs `python3 -m http.server 8000 --directory web/` (or equivalent) for local smoke testing.

**Exit gate:** `./build_wasm.sh && ./serve.sh` → opening `http://localhost:8000` shows the banner. Native `./build.sh && ./run_tests.sh` still green.

**Landed:** `host/hello_wasm.c` (trivial `printf` main), `host/Makefile.wasm` (MODULARIZE+ES6, ALLOW_MEMORY_GROWTH, INITIAL_MEMORY=32 MiB, SRCS is just `hello_wasm.c` for now), `host/build_wasm.sh` (sources `~/emsdk/emsdk_env.sh` if emcc isn't already on PATH), `host/web/{index.html,picomite.css,app.mjs,serve.sh,.gitignore}`. Loading the page in headless Chromium renders `Hello from PicoMite WASM` in `#out` with zero console/page errors. Native `./run_tests.sh` stays at 201/201.

### Phase 1 — Canvas + raster REPL ✅ (2026-04-18)

**Goal:** The browser canvas shows a `> ` prompt drawn as pixels. Typing `PRINT 2+3` enter prints `5` on the canvas. `LIST`, `NEW`, simple `FOR`/`NEXT` all work. `PAUSE 100` does not freeze the tab. This phase absorbs the old "graphics to canvas" phase — the prompt *is* graphics.

The key insight: MMBasic's `PRINT` path on device already lands on `DrawPixel`/`DrawChar` via `gfx_console_shared.c`, and `host_fb.c` already backs that path with an RGB plane on the native host (that's where PPM screenshots come from). The WASM port makes the same plane visible to JS in real time.

- Extend `host/Makefile.wasm` SRCS to the full shared/VM/HAL set that the native host uses, minus `host_main.c` / `host_terminal.c` / `host_sim_*.c`. That brings in `core/mmbasic/MMBasic.c`, `core/mmbasic/Commands.c`, `core/mmbasic/Functions.c`, `core/mmbasic/Operators.c`, `core/mmbasic/MATHS.c`, `core/mmbasic/Memory.c`, `core/mmbasic/Editor.c`, `core/mmbasic/MMBasic_Prompt.c`, `core/mmbasic/MMBasic_Print.c`, `core/mmbasic/MMBasic_REPL.c`, all `gfx_*_shared.c`, `shared/mmbasic/mm_misc_shared.c`, `core/mmbasic/Draw.c`, `core/mmbasic/FileIO.c`, `shared/audio/Audio.c`, the full `bc_*` / `vm_sys_*` / FatFs set, and the portable HAL modules (`host_runtime.c`, `host_fastgfx.c`, `host_fs_shims.c`, `host_peripheral_stubs.c`, `host_fb.c`, `host_time.c`, `host_fs.c`, `host_sim_audio.c` (JSON sink stays until Phase 3 audio)).
- Write `host/host_wasm_console.c`:
  - **Input only.** Same exported API as `host_terminal.c` for the read path (`host_raw_mode_enter/exit` as no-ops; `host_read_byte_nonblock`; `host_read_byte_blocking_ms`).
  - Backed by an in-module ring buffer. JS calls `wasm_push_key(code)` on keydown.
  - `host_read_byte_blocking_ms` yields via `emscripten_sleep(1)` when the ring is empty — that's the ASYNCIFY hook that keeps the tab responsive.
  - **No output path.** Character output reaches the framebuffer through `MMputchar` → `gfx_console_shared.c` → `DrawChar`/`DrawRectangle` → `host_fb.c`. We don't add a parallel stdout route.
- Write `host/host_wasm_canvas.c`:
  - Exports `wasm_framebuffer_ptr()` → `host_framebuffer` (24-bpp RGB plane in `host_fb.c`).
  - Exports `wasm_framebuffer_width()` / `wasm_framebuffer_height()`.
  - Exports `wasm_dirty_rect(out_ptr)` → fills 4 ints `{x,y,w,h}`, resets the accumulator, returns 1 if dirty / 0 if clean.
- Extend `host_fb.c` with a minimal dirty-rect accumulator (`host_fb_dirty_add(x,y,w,h)` called from `DrawPixel`, `DrawRectangle`, `ScrollLCD`, `DrawChar`, `DrawBitmap`). Native host ignores it; WASM reads it. This is the single additive `#ifdef MMBASIC_WASM` gate in a shared HAL file we expect to need — the accumulator itself is present on both targets, but only WASM calls `wasm_dirty_rect`.
- Write `host/host_wasm_main.c`:
  - Exports `wasm_boot()` — wraps the native host `main()` body: `InitialiseAll()` → loop of `MMBasic_REPL()` iterations (or `MMBasic_REPL()` directly if its loop already never returns cleanly).
  - Under ASYNCIFY, the blocking reads in `MMBasic_REPL` yield via the `host_wasm_console.c` path.
  - Exports `wasm_break()` that sets `MMAbort = 1` so Ctrl-C from JS can interrupt a running program (full key-polish deferred to Phase 5).
- Rewrite `host/web/app.mjs`:
  - Discards the Phase 0 `<pre>` placeholder.
  - Attaches canvas element. On `Module` resolve, calls `_wasm_framebuffer_*` once to size the canvas, then starts a `requestAnimationFrame` loop that calls `_wasm_dirty_rect` and blits dirty pixels via `ctx.putImageData`.
  - `keydown` listener on `window` (or the canvas with `tabindex`) maps the event to an MMBasic key code (simple pass-through for printable ASCII; minimal arrow/enter/backspace/ctrl-C mapping for MVP — full table is Phase 5) and calls `_wasm_push_key`.
  - Calls `Module._wasm_boot()` once setup is done.
- Rewrite `host/web/index.html`: replace `<pre id="out">` with `<canvas id="screen">` plus a minimal status strip.

**Exit gate:** Browser canvas renders the PicoMite boot banner and `> ` prompt using the device font, as rendered pixels. Typing `PRINT 2+3` ENTER scrolls the console and shows `5` on the next line. `LIST`, `NEW`, a short `FOR I=1 TO 5: PRINT I: NEXT` all produce the expected raster output. `PAUSE 100` → the tab stays responsive throughout. Native `./run_tests.sh` still 201/201. Device build still green (CMakeLists untouched).

**Landed:**
- `host_wasm_console.c` — input-only key ring (2048 slots) drained by the existing `host_read_byte_*` API; `host_raw_mode_is_active` always returns 1 so MMInkey takes the raw-mode branch unchanged; `host_read_byte_blocking_ms` yields via `emscripten_sleep(1)` which is the ASYNCIFY hook.
- `host_wasm_main.c` — owns `flash_prog_buf` + `host_output_hook`, plus the four source-loader helpers (`read_basic_source_file` / `load_basic_source` / `host_read_logical_line` / `host_update_continuation_setting`) cloned from `host_main.c` because LOAD/SAVE callers (bc_runtime.c, host_fs_shims.c) reference them at link time. `wasm_boot()` mirrors `host_main.c::run_repl` with the `--sim` branch's display-console setup folded in: `Option.DISPLAY_CONSOLE=1`, `OptionConsole=2` (screen only), `gui_font=0x01`/8x12, green phosphor palette, `Option.Width/Height` derived from `HRes/VRes` after `host_runtime_begin` wires them.
- `host_wasm_canvas.c` — three JS-facing exports (`wasm_framebuffer_ptr/width/height`). No dirty-rect accumulator yet; full-framebuffer putImageData per rAF is plenty fast for 320×320.
- `host_time.c` — narrow `#ifdef MMBASIC_WASM` swapping `nanosleep` for `emscripten_sleep` in `host_sleep_us` (the only such gate in a shared HAL file for Phase 1).
- `core/mmbasic/MMBasic_REPL.c` — `MMBasic_PrintBanner` gains a `#if defined(MMBASIC_WASM)` banner variant ("MMBasic Web V…"); native host and device branches unchanged.
- `Makefile.wasm` — full shared/VM/HAL source list, `-sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536`, `-sINITIAL_MEMORY=32MiB`, `-sEXPORTED_FUNCTIONS` for the wasm_* entry points. Passes `-Wl,--allow-multiple-definition` so wasm-ld accepts MMBasic's gcc-style tentative-def merging idiom (core/mmbasic/Draw.c / core/mmbasic/FileIO.c declare storage that host_runtime.c also initialises — gcc merges, clang refuses without this flag; `-fcommon` doesn't help because wasm-ld doesn't support common linkage).
- `host/web/` — rewritten `index.html` (one `<canvas id="screen">`), `picomite.css` (pixelated CSS scale), `app.mjs` (keydown → `wasm_push_key` via `cwrap`; `requestAnimationFrame` loop reads `HEAPU32` and writes `putImageData`; MMBasic key-code mapping table for arrows/F-keys/editing keys). Phase 0 `<pre id="out">` placeholder dropped.

**Verified:** Headless Chromium renders the PicoMite boot banner and `> ` prompt as green-phosphor pixels on the canvas. `PRINT 2+3` → ` 5` rendered correctly. `PAUSE 500 : PRINT "AWOKE"` took ~720 ms round-trip (ASYNCIFY sleeping, not busy-looping). Native `./run_tests.sh` still 201/201 (core/mmbasic/MMBasic_REPL.c banner change is WASM-only via `#if defined(MMBASIC_WASM)`). Final `picomite.wasm` = 1.1 MB uncompressed, well under the 1.5 MB budget.

**Deferred to Phase 5 (input polish):** Ctrl-C → `wasm_break`; full keymap; focus edge cases. Phase 1 ships a minimal table that covers printable ASCII + arrows + editing keys + F-keys.

**Deferred to Phase 6 (graphics polish):** FASTGFX SWAP dirty-rect handling, `BLIT`, full end-to-end pixel parity with PPM screenshots.

**Known warning:** `wasm-ld: warning: function signature mismatch: CallCFunction` — core/mmbasic/MMBasic.c declares `uint64_t CallCFunction(...)` and host_runtime.c stubs it as `void CallCFunction(...)`. Pre-existing in the native build (gcc doesn't flag it); CFunctions aren't exercised under host/WASM. Fix belongs in a CFunction-support phase or as follow-up cleanup.

### Phase 2 — Filesystem (preload bundle + drag-drop import + download export) ✅ (2026-04-18)

**Goal:** `FILES`, `LOAD "t001.bas"`, `RUN`, `SAVE` work. User brings their own files in by dragging onto the page; takes them home by downloading.

File access is deliberately transient MEMFS-only. No IDBFS, no OPFS, no File System Access API. Files live in the page's memory for the session; user is responsible for exporting anything they want to keep. Matches how one would use the native host: programs come from disk (here, from drag-drop) and leave to disk (here, via download). Simple, universal, no quotas, no permission prompts.

- Add `--preload-file demos@/sd` to the emscripten command line. The `demos/` directory holds a curated set of bundled examples (Mandelbrot, graphics demos, small games) — read-only from the user's perspective, always present at `/sd/` on boot. Not the full test corpus; we pick maybe 10–20 representative programs.
- `host_sd_root = "/sd"` at boot so `host_fs_shims.c` routes POSIX through emscripten's MEMFS. No C code change — already wired.
- `host/web/ui/fs.js` — three small surfaces:
  - **Drop zone:** the whole page is a `dragover`/`drop` target. Dropped files are read via `FileReader.readAsArrayBuffer`, then `FS.writeFile('/sd/' + file.name, new Uint8Array(buf))`. Multiple files OK. On completion, a toast says "Loaded foo.bas — type `FILES` to list".
  - **Download current program:** a "⬇ Download" button grabs the current program via an exported `wasm_current_program()` (or reads the last-`SAVE`-d file from `/sd/`) and triggers a browser download via `new Blob([bytes]); URL.createObjectURL; <a download>`. Also: any `SAVE "name.bas"` can optionally auto-trigger the download if a "Save also downloads" checkbox is ticked, so power users don't have to click twice.
  - **Download all:** a "⬇ All as .zip" button packs the contents of `/sd/` (minus the preloaded demos, if we track that) into a zip via a tiny zip library (e.g. `fflate`, ~8 KB) and downloads it. Useful for bulk export.
- No C code changes. All logic is JS; from the interpreter's view, `/sd/` is just a POSIX directory.

**Exit gate:** `FILES` lists the bundled demos. `RUN "mandelbrot.bas"` works. Drag a local `.bas` file onto the page → `FILES` shows it → `RUN "mine.bas"` works. `SAVE "new.bas"` → click "Download" → `new.bas` lands in the user's Downloads folder. Refresh the page → drag-dropped files are gone (expected), bundled demos still there (expected).

**Notes:**
- No persistence across reloads is a *feature*, not a bug, for this iteration. It makes the mental model obvious ("the page is a sandbox; save to your disk to keep anything") and avoids a pile of edge cases (quota exceeded, stale OPFS state, browser deleting your files after 60 days of inactivity, etc.).
- If persistence becomes desirable later, it slots in as a post-MVP phase: mount OPFS at `/home/` and add a "Copy to persistent storage" action. The `host_fs_shims.c` routing doesn't care which MEMFS-equivalent backend is at a given path.
- Upload from a `<input type="file" multiple>` picker is a nice addition alongside drag-drop for mobile / touch where drag-drop is awkward. Same underlying path (`FS.writeFile`). Trivial to add; mention in the Phase 2 follow-on list.

**Landed:** `host/Makefile.wasm` adds `--preload-file demos@/sd`, `-sFORCE_FILESYSTEM=1`, and `"FS"` to `EXPORTED_RUNTIME_METHODS`; emscripten packs `host/demos/` (8 curated `.bas` files, 6.8 KB) into `picomite.data` and mounts it at `/sd/` on boot. `host/web/app.mjs` grew three surfaces: an `<input type="file">` upload button, whole-window drag-drop with a visible overlay, and a "Download all" button that packs `/sd/` into a ZIP via a tiny in-line store-only ZIP writer (no compression library dep). The host C side needed zero changes — `host_fs_shims.c`'s existing POSIX routing talks to emscripten MEMFS unchanged.

**Verified:** Headless Chromium sees `FILES` list the 8 preloaded demos, `RUN "demo_hello.bas"` produces the expected FRUN output pixel-on-canvas, drag-drop injects `mine.bas` which `RUN` executes, and `SAVE "saved.bas"` followed by the download button delivers a ZIP containing both bundled + user-added files. Native `./run_tests.sh` still 201/201.

### Phase 2.5 — Hardening and UI polish ✅ (2026-04-18)

Not originally scoped as a phase; a cluster of fixes and quality-of-life features that emerged from actually using the Phase 1+2 build in a browser.

**Cooperative yield in the interpreter hot loop.** ASYNCIFY only yields when the C stack reaches an `emscripten_sleep*` call. The REPL's `MMgetchar` yielded via `host_sleep_us(1000)`, but `CheckAbort` (called on every statement) and `MMInkey` polling (called every `INKEY$`) did not — so any `.bas` that either looped without `PAUSE` or waited for input via `Do While Inkey$="" : Loop` (e.g. `demo_gfx_mandel`'s "key to exit") hung the tab indefinitely. Fix: `wasm_yield_if_due()` in `host_runtime.c`, invoked from `host_runtime_check_timeout` — a throttled `emscripten_sleep(0)` at most every 16 ms of wall clock. Both hot paths converge on that function (interpreter statement loop via `CheckAbort`, busy-wait polls via `MMInkey`, VM back-edges via `bc_vm.c::CheckAbort`), so every hot loop gets a cooperative yield for ~1% overall overhead.

**Red `LCD_error` overlay on every error.** `core/mmbasic/MMBasic.c::error` triggers `LCD_error` (a full-width red-on-black overlay intended for device builds where the serial console and the display are separate) whenever `Option.DISPLAY_CONSOLE == 0`. The WASM path sets it to 1 in `wasm_configure_display_console`, but `host_runtime_begin` snapshots `Option` into `flash_option_buf` *before* the override runs, and `error()` calls `LoadOptions()` on every error to reset `Option` — which reverted `DISPLAY_CONSOLE` to 0 and triggered the overlay. Fix: call `host_options_snapshot()` again in `wasm_boot` *after* `wasm_configure_display_console`, so the flash image holds the correct post-override state.

**Resolution dropdown.** `<select>` with nine common sizes from 320×240 up to 1024×768. Changing the selection sets `?res=WxH` on the URL, persists to `localStorage`, and reloads — the framebuffer plane is allocated inside `host_runtime_begin` (before JS regains control) and the interpreter's `setjmp`/`longjmp` state doesn't tolerate re-init, so a full reload is the only safe path. C side: new `wasm_set_framebuffer_size(w, h)` export wrapping the existing `host_sim_set_framebuffer_size`; JS calls it between `Module` resolve and `wasm_boot`.

**Memory dropdown.** `<select>` offering 128 KB (RP2040-faithful) through 8 MB, defaulting to 2 MB for a generous web-friendly heap. `core/mmbasic/configuration.h` sets `HEAP_MEMORY_SIZE` to 8 MB under `MMBASIC_WASM` (compile-time ceiling for `AllMemory[]` and `mmap[]`); the runtime `heap_memory_size` variable picks any value ≤ ceiling via a new `wasm_set_heap_size()` export that also bumps `bc_alloc`'s VM-heap capacity via a matching `bc_alloc_set_heap_capacity()` setter. `MEMORY` command in BASIC reports the selected size (128 KB selection = 156 KB free after the +28 KB `MAXVARS * sizeof(s_vartbl)` overhead; 2 MB selection = 2076 KB free).

**Canvas sizing.** Pixel doubled at 2× by default, shrinks uniformly if 2× exceeds the viewport budget so aspect ratio is always preserved. `image-rendering: pixelated` keeps nearest-neighbour sampling even at non-integer scales. Canvas re-sizes on window resize. Dropped the old square-forcing CSS (`min(88vw, 88vh - 120px, 640px)` for both width and height) that distorted non-square framebuffers.

**Status line.** Shows live resolution, heap size, and demo count: `Ready — 640×360, 2 MB heap, 8 demos in /sd/. Type FILES.`

**Yield-hook dedup.** `wasm_yield_if_due` was firing once per frame on top of `FASTGFX SYNC`'s own `host_sleep_us` (each yield = ~1 ms of ASYNCIFY unwind overhead, ~5% perf tax on a 50 FPS game). Now `host_sleep_us` stamps a shared `wasm_last_yield_us` after each `emscripten_sleep`, so the periodic check skips when the program has already sleep-yielded recently. Busy-wait loops still get the cooperative yield; cooperative games (e.g. pico_blocks at 50 FPS) pay nothing extra.

**Terminal cleanup on signal death.** `host_terminal.c` installed `atexit` to restore termios, but `atexit` only fires on normal `exit()` — a SIGTERM / SIGINT / SIGHUP / SIGQUIT / SIGPIPE / SIGABRT left stdin in raw mode, which stair-stepped the shell's prompt. Added signal handlers that call the restore hook, then re-raise with the default disposition so the exit status still reflects the signal. Native-host only; WASM doesn't link `host_terminal.c`.

**Persistent /sd/ via IDBFS.** Files the user SAVEs (or the Editor's F1-save path) now survive reloads. `host/demos/` moves from `/sd/` preload to `/bundle/` preload and gets copied into `/sd/` on first boot (gated by a `localStorage` flag, with an "empty /sd/ triggers repopulate" self-heal for stale flags). New **⟲ Reset /sd/** toolbar button wipes every user file and repopulates from the bundle. Flush strategy: `visibilitychange` + `beforeunload` + a 2 s `setInterval`.

**Editor file-load on IDBFS.** `host_fs_posix_try_open` used `fstat(fileno(fp))` to cache the file size that `core/mmbasic/Editor.c::f_size(FileTable[fnbr].fptr)` reads back. On emscripten's MEMFS/IDBFS the fd-backed fstat returns `size=0` for freshly-opened read FILE*s until the first read touches data — only path-backed `stat()` sees the real size. Result: EDIT loaded an empty buffer and the backup copy came out 0 bytes. Fix: `stat(path, ...)` before `fopen()`, regardless of mode. Now FILES and Editor agree on sizes immediately after SAVE.

**Makefile.wasm demo dependency tracking.** `--preload-file demos@/bundle` is a linker flag, not a compile input, so `make` didn't notice when `host/demos/*.bas` changed — `picomite.data` stayed frozen on the previous bundle. Added `$(wildcard demos/*.bas)` as a link prerequisite so touching or adding a demo triggers a relink.

**Bundled `mand.bas` mandelbrot explorer.** Rewrote the interactive mandelbrot renderer (fixed-point inner loop, 16-ply BLINDS interlacing, zoom history stack, palette switcher) to pull `SCREEN_W`/`SCREEN_H` from `MM.HRES`/`MM.VRES` and pick a uniform complex-units-per-pixel scale so both the initial view *and* every zoom stay aspect-correct on any viewport. Zoom cursor is a W×H rectangle matching the viewport ratio, so zooming preserves square pixels through arbitrary depth.

### Phase 3 — Audio via Web Audio ✅ (2026-04-18)

**Goal:** `PLAY TONE 440,1000` emits a 440 Hz beep for 1 s.

- Write `host/host_wasm_audio.c` (parallel to `host_sim_audio.c`):
  - Replace each JSON-emit call with an `EM_ASM` invocation that calls into a JS audio bus (`window.picomiteAudio.tone(freq, ms)`, `.sound(waveform, freq, duration, volume)`, etc.).
  - `PLAY STOP`, `PLAY PAUSE`, `PLAY RESUME` map to matching JS methods.
- Write `host/web/ui/audio.js`:
  - Lazy-create `AudioContext` on first user gesture (otherwise browser blocks).
  - `tone(freq, ms)` → `OscillatorNode` with ramped envelope, scheduled via `audioCtx.currentTime`.
  - `sound(waveform, freq, duration, volume)` → 4 channels multiplexed (matching `PLAY SOUND` semantics).
  - No MOD/WAV/MP3 yet — scope those to Phase 4b or later.
- Display a banner "Click anywhere to enable audio" until the user gesture unblocks the audio context.

**Exit gate:** A test program playing a melody (PLAY TONE + PAUSE sequence) produces audible output. Four-voice `PLAY SOUND` chord sounds right. Audio does not stutter during graphics-heavy programs.

**Landed:**
- `host/host_wasm_audio.c` — new TU, `#ifdef MMBASIC_WASM`-gated, exports the same `host_sim_audio_*` symbols `shared/audio/Audio.c`'s host body calls. Each entry drops through `EM_ASM` into `window.picomiteAudio.{tone,sound,stop,volume,pause,resume}`. The drain API (used by `host_sim_server.c` on the native `--sim` target) is kept as a zero-queue stub for symbol completeness.
- `host/web/ui/audio.js` — WebAudio engine ported from `web/audio.js` (the `--sim` module), stripped of the JSON/WebSocket layer. Exposes `window.picomiteAudio` directly. Keeps the independent `PLAY TONE` graph and 4-slot × {L,R} `PLAY SOUND` voices; "logarithmic" volume map `(v/100)²`; gesture-armed `AudioContext` resume on capture-phase `keydown`/`mousedown`/`touchstart`/`pointerdown`.
- `host/web/index.html` — loads `./ui/audio.js` via plain `<script>` **before** `app.mjs` so `window.picomiteAudio` is installed before the WASM module boots. Adds a small `#audio-hint` banner ("Click or press a key to enable audio") that `ui/audio.js` auto-hides once the context transitions to `running`.
- `host/web/picomite.css` — `#audio-hint` styles (centered bottom pill, hidden by default; `hidden` attribute hides entirely).
- `host/Makefile.wasm` — drops `host_sim_audio.c` from `HOST_PORTABLE_SRCS`, adds `host_wasm_audio.c` to `HOST_WASM_SRCS`. Native `mmbasic_test` / `mmbasic_sim` Makefiles are untouched; they still link `host_sim_audio.c` so the WebSocket JSON path is entirely unchanged.

**Verified:**
- `./build_wasm.sh` clean build, no new warnings (only the pre-existing `CallCFunction` signature mismatch).
- Native `./build.sh && ./run_tests.sh` → 210/210 passed.
- `./build_sim.sh` → `mmbasic_sim` links; `host_sim_audio.o` still present in the link line, so the JSON/WebSocket audio bus is untouched.
- `host/web/smoke_audio.mjs` — headless Chromium boots the page, types `PLAY TONE 440,440,500` / `PLAY SOUND 1,B,Q,220,20` / `PLAY STOP` into the raster REPL, and confirms `window.picomiteAudio` receives each call with the expected args. Exit 0 = pass.

**Deferred (Phase 9 post-MVP):** `PLAY WAV/FLAC/MP3/MOD/MIDI` — need JS-side `decodeAudioData` + a buffering strategy compatible with PAUSE/RESUME, plus C-side wiring since `cmd_play` on host currently rejects these at parse time.

### Phase 4 — Cooperative scheduling & timing ✅ (2026-04-18)

**Goal:** `TIMER`, `PAUSE`, `FASTGFX SYNC`, cursor blink all behave like the native port. Plus a broader brief from using the Phase 2.5 build: `pico_blocks` had visible hiccups during gameplay, so the work also had to eliminate main-thread jitter sources that weren't obvious until a full FASTGFX program was running end-to-end in the browser.

The Phase 2.5 work already handled the groundwork — `host_sleep_us` → `emscripten_sleep` swap and the `wasm_yield_if_due` cooperative hook in `host_runtime_check_timeout`. Phase 4 added the vsync alignment and attacked three main-thread stability issues.

**Landed:**

- **`FASTGFX SYNC` deadline catch-up.** Added a `volatile uint32_t wasm_vsync_counter` bumped from JS every `requestAnimationFrame` — intended initially as a way to pin SYNC-return to a display-refresh boundary. **That spin turned out to halve measured frame rate** on a 60 FPS game running on a 60 Hz display: the coarse `host_sleep_us` lands mid-rAF-interval, then the spin waits ~16 ms more for the next tick → 32 ms between SYNC returns, i.e. 30 FPS. Removed the spin. Kept the counter (exported, useful for smoke tests + potentially future work), kept the "if we fell more than two frames behind, resync deadline to wall clock" catch-up so a one-off hitch doesn't make the game spin indefinitely.
- **Framebuffer generation counter.** `host_fb.c` exposes `volatile uint32_t host_fb_generation` bumped by every path that mutates the visible front plane (`host_fb_put_pixel`, `host_fb_fill_rect`, `host_fb_scroll_lcd`, `bc_fastgfx_swap`, `host_framebuffer_merge`/`copy`/`clear_target`/`reset_runtime`). JS reads the counter via `_wasm_framebuffer_generation()` per rAF and skips `putImageData` entirely when the counter hasn't moved — idle REPL, long `PAUSE`s, and `INKEY$` spin-waits cost zero blit time. FASTGFX back-buffer writes deliberately don't bump; only the SWAP memcpy does, so each visible frame is exactly one blit.
- **Faster blit path.** The per-pixel four-byte-store loop is replaced with a `Uint32Array` view over `imageData.data` and one `0xFF000000 | R | (G<<8) | (B<<16)` store per pixel. Roughly 3× faster on V8 and drops a chunk of main-thread time per rAF on the larger resolutions.
- **Dirty-aware IDBFS flush via `requestIdleCallback`.** The 2 s `setInterval(syncfs…)` from Phase 2.5 was the biggest hidden hitch — walking MEMFS and running an IndexedDB transaction on the main thread every 2 s, regardless of whether anything changed. Now `FS.trackingDelegate` hooks (`onWriteToFile`, `onDeletePath`, `onMovePath`) flip a `sdDirty` flag whenever `/sd/` changes. A poll checks the flag every 2 s and, if dirty, queues a flush via `requestIdleCallback` so it runs between rAFs instead of stepping on one. `visibilitychange` and `beforeunload` still force-flush unconditionally (last-chance boundaries). Net: idle sessions pay nothing; SAVE from BASIC still commits; FASTGFX games no longer see 5–30 ms hitches every 2 s.
- **Dev/test hook.** `host/web/app.mjs` now stashes the WASM instance on `window.picomite = { instance }` so headless smoke tests can peek at `HEAPU32`, `FS.readFile`, etc.

**Verified:** Headless Chromium via `host/web/smoke_phase4.mjs`:
- rAF-driven vsync counter advancing @ ~120 Hz (headless is 120 Hz, real hardware commonly 60 Hz; either works).
- `PAUSE 1000` round-trips in ~1085 ms (ASYNCIFY unwound, not busy-waiting).
- Framebuffer generation counter advances monotonically on every draw path.
- A 500-iteration FASTGFX `FPS 50` loop sampled for 3 s: SWAP rate **50.2 Hz** on the JS side, rAF gap mean **8.3 ms**, worst **10.4 ms** — no dropped or hitched frames. Native `./run_tests.sh` 210/210 green (all `#ifdef MMBASIC_WASM`-gated changes).

**Regression caught + fixed (2026-04-18):** Initial Phase 4 commit introduced two bugs that the smoke test didn't catch:
  1. Uint32 blit swapped R and B — host plane is `(R<<16)|(G<<8)|B` but ImageData u32 on little-endian is `(A<<24)|(B<<16)|(G<<8)|R`, and the first pass mapped them the other way. Fix: swap bytes 0 and 2 only, leave G in place.
  2. rAF-align spin in `bc_fastgfx_sync` halved pico_blocks framerate from 60 to 30 FPS — see the SYNC bullet above for the mechanism. Fix: drop the spin; the real stability win came from the dirty-aware IDBFS flush, not from the rAF alignment.

  Lesson: the smoke test measured "no rAF gap > 40 ms" and "SWAP rate matches nominal FPS" but it ran at FPS 50 on a 120 Hz rAF — those two are consistent with 30 FPS *on a 60 Hz rAF* too. Future FASTGFX perf tests should measure against a specific target FPS and fail if the measured rate is significantly below it.

**Open follow-on:** `setInterval(1e3, wasm_tick)` for `TIMER` advance when no sleep ever fires. The plan floated this but measurement shows `host_sync_msec_timer` gets called plenty via `MMInkey` / `CheckAbort` / `host_sleep_us` in every observed program — including tight no-PAUSE loops, because the cooperative yield in `host_runtime_check_timeout` also touches `host_time_us_64`. Dropping the idea; no JS interval tick needed.

**Exit gate:** `PAUSE 1000` round-trip within 100 ms of nominal ✅. `FASTGFX FPS 50` loop produces 50 ± 1 Hz SWAP rate with worst rAF gap < 40 ms ✅. Cursor blinks at native rate (indirect — `CursorTimer` is driven by the same `host_sync_msec_timer` path and Phase 1 already verified the REPL cursor blinks correctly) ✅. Native `./run_tests.sh` 210/210 ✅.

### Phase 5 — Input polish

**Goal:** Every key MMBasic cares about arrives with the right code; Ctrl-C breaks programs; optional mouse support.

- `host/web/ui/keys.js` — translation table from JS `KeyboardEvent.key` / `.code` to MMBasic key codes. Cover arrows, F1–F12, Home/End/PgUp/PgDn, Ins/Del, Esc, Tab, Backspace, Enter, Ctrl-letter combinations. (Phase 1 ships a minimal table; this phase finishes it.)
- Capture Ctrl-C at the JS level and set an exported `wasm_break_flag = 1` that the interpreter polls during `CheckAbort`. No terminal to compete for it — the canvas has focus, Ctrl-C is unambiguously break.
- Optional: hook `mousemove` / `mousedown` on the canvas to a `host_wasm_mouse_x/y/buttons` global read by a future `MOUSE` function (out of scope for MVP).

**Exit gate:** `EDIT` works with arrow keys. `INPUT$(1)` returns correct codes for Ctrl-A, Esc, Tab, function keys. Ctrl-C breaks a running `FOR I=1 TO 1e9: NEXT`.

### Phase 6 — Graphics polish (FASTGFX + BLIT + SPRITE)

**Goal:** Performance-oriented graphics ops that go beyond "draw directly to the framebuffer" work correctly. Phase 1 already covers `PRINT`, `LINE`, `CIRCLE`, `BOX`, `PSET` — those write straight into `host_fb.c` and are visible via the dirty-rect path. This phase handles the ops that need a back buffer or a sprite pool.

- Verify `FASTGFX` back-buffer setup already works (it's allocated in `host_fastgfx.c`, which is shared with the native host).
- Ensure `FASTGFX SWAP` triggers a full-canvas dirty-rect so the blit loop redraws everything after the flip.
- Ensure `FASTGFX SYNC` yields via `emscripten_sleep` aligned to `requestAnimationFrame`. If 60 Hz alignment is flaky, drive the swap from inside the rAF callback (set a flag in rAF, have `host_sync_msec_timer` spin/yield until the flag is observed).
- Verify `BLIT` between memory pages and from program-supplied buffers works — these are all `host_fb.c` memcpy wrappers that should just function.
- Verify `SPRITE` framework (if the test corpus uses it) is wired.

**Exit gate:** A FASTGFX bouncing-ball demo runs at 60 FPS with no tearing. `BLIT` copies work pixel-identical to the native host. A test that draws concentric circles matches the PPM reference byte-for-byte.

### Phase 7 — Build, package, deploy

**Goal:** `npm run build` (or equivalent) produces a deployable static bundle; CI pushes it on every merge to `web-host`.

- Split `build_wasm.sh` into debug (`-O1 -g -s ASSERTIONS=2`) and release (`-O2 -s ASSERTIONS=0 --closure 1`) variants. Release is what ships.
- Add a GitHub Actions workflow `.github/workflows/wasm.yml`:
  - Sets up emscripten (`mymindstorm/setup-emsdk@v14` or equivalent pinned action).
  - Runs `host/build_wasm.sh release`.
  - Uploads `host/web/` as a Pages artifact.
  - On `main` merges of the eventual feature branch, publishes to GitHub Pages.
- Add a `host/web/README.md` explaining local dev flow (`./build_wasm.sh debug && ./serve.sh`).
- Size budget: gzipped `.wasm` must stay under 1 MB; CI fails if it exceeds.

**Exit gate:** `picomite.github.io/web` (or whatever the URL turns out to be) loads the REPL, runs `tests/t001.bas` successfully, renders a graphics demo, and plays a tone, all from a fresh browser cache.

## Key risks

1. **ASYNCIFY overhead.** +30% runtime cost and +15% binary size, applied globally once any call site is marked. If the interpreter feels sluggish in benchmarks, the escape hatch is the Phase 8 worker rearchitecture (see below). Measure early — by end of Phase 1 — against the native host for a reasonably long program (a 10 s Mandelbrot). If the gap is >2×, escalate.
2. **pthreads / SharedArrayBuffer gated by COOP/COEP.** The MVP deliberately avoids them. GitHub Pages does not send COOP/COEP headers. Moving to Cloudflare Pages or Netlify with a `_headers` file unblocks pthreads whenever needed — just mark it as a deployment change, not a rewrite.
3. **Binary size.** The full VM + interpreter + graphics + audio + VM command table is large. Compile with `-Oz` if `-O2` ships too fat; strip `vm_sys_fft_table.c`-style precomputed tables if they dominate; consider split output (core + async-loaded sample library) if first-load budget is blown.
4. **Audio autoplay gate.** No audio plays until the user clicks. Make the failure mode obvious (visible banner), not silent (tone scheduled, nothing happens).
5. **Timing drift under ASYNCIFY.** `emscripten_sleep(1)` is lower-bounded by the event-loop latency (often 4 ms on throttled tabs). `PAUSE 1` won't be 1 ms. `TIMER` will still advance monotonically via `performance.now()`, so end-to-end behavior stays correct — but busy-wait timing tricks in `.bas` programs may misbehave. Document.
6. **No persistence by design.** User drags files in, saves by downloading. A page reload loses anything they didn't download. Communicate this clearly in the UI (banner on first load, tooltip on the REPL). If users complain, add OPFS as a post-MVP persistence layer — but start simple.
7. **Keyboard focus.** The canvas needs focus to receive keydown events (`tabindex="0"`, visible focus ring). If focus drifts to `<body>` or an overlay panel, input is silently dropped. Mitigate with a click-to-focus hint and a visible focus border.
8. **No terminal conveniences.** Without a terminal emulator there is no native scrollback, no copy/paste of output, no search. Match hardware PicoMite behavior: output scrolls off screen, and the user copies text by reading it. If scrollback demand emerges post-MVP, add it to `gfx_console_shared.c` itself (useful on device too) rather than bolting a browser-only feature on top.

## Open questions (resolve during Phase 0 / 1)

- **ASYNCIFY vs. `emscripten_set_main_loop` as the primary yield mechanism?** ASYNCIFY is easier on existing code; main-loop refactor is cleaner but requires teasing apart `ExecuteProgram` into a re-entrant state machine. Start ASYNCIFY; measure; revisit if needed.
- **Canvas size and scaling.** Hardware PicoMite runs at 320×240 (or 320×320 depending on config). Pick a default for the web build, render at native resolution with `image-rendering: pixelated`, and CSS-scale up to fit the viewport. Allow the user to zoom (CSS transform) but not to resize the framebuffer mid-session.
- **Preload which tests?** The native harness ships 201 `.bas` files; bundling them all is ~400 KB uncompressed. Preload the interesting demos; leave the regression suite as a separate preload file gated by a dev flag.
- **Do we expose the VM-vs-interpreter compare mode in the browser?** Probably not — it's a dev tool. But exposing `?mode=vm` / `?mode=interp` query params for forcing one or the other could help debugging.
- **ES module (MJS) vs. classic script?** Default to MJS (`EXPORT_ES6=1`) for cleaner integration with modern tooling. Plain script is fallback if deployment target rejects modules.

## Post-MVP follow-ons

- **Phase 8: Worker + Atomics.** Move the interpreter to a Web Worker. Drop ASYNCIFY. Main thread owns DOM + audio. `SharedArrayBuffer` ring for input, framebuffer, audio samples. `Atomics.wait`/`notify` for blocking. Requires COOP/COEP — plan the deploy change simultaneously.
- **Phase 9: PLAY WAV / MOD / MP3.** Decode in JS with `decodeAudioData`, schedule via `AudioBufferSourceNode`. Non-trivial — needs a buffering strategy compatible with `PLAY PAUSE`/`RESUME`.
- **Phase 10: Mobile / touch.** Virtual keyboard overlay, pinch-to-zoom on canvas, gesture mapping to MMBasic `MOUSE`.
- **Phase 11: Offline PWA.** Service worker, installable, works without internet once cached. Easy once the static bundle stabilizes.
- **Phase 12: Multi-program tabs.** Multiple VM instances (one per worker) sharing the same canvas or with independent canvases. Mostly a UI exercise.

## Rollout sequence

1. Land this plan document on `web-host` branch. ✅
2. Phase 0 scaffolding. ✅
3. Phase 1 (canvas + raster REPL). ✅
4. Phase 2 (FS). ✅
5. Phase 2.5 (hardening: yield hook, error overlay, resolution + memory dropdowns, canvas scaling, status line). ✅
6. Phase 3 (audio). ✅
7. Phase 4 (scheduling + main-thread jitter fixes). ✅
8. Phase 5 (input), Phase 6 (graphics polish) — one commit each.
9. Phase 7 (deploy) once everything works locally.
8. Merge `web-host` → `main`. Promote the docs / link from the main README.
9. Iterate on post-MVP items as user feedback comes in.

## Success criteria

- Any `.bas` file in the existing test corpus that doesn't touch hardware peripherals or require >32 MB of heap runs in the browser with visually and audibly identical behavior to the native host.
- Cold page load to first prompt: under 2 seconds on a modern laptop over a fast connection.
- Memory footprint: under 64 MB for an idle REPL session.
- Zero backend dependencies: a static bundle dropped in `s3://anywhere` is the complete deployable.

---

**Superseded by [real-hal-plan.md](real-hal-plan.md) (Phase 13 in progress, 2026-04-24).** The WASM port proved the HAL technique composes to a third target; real-hal Phase 12 absorbed `host_wasm_*.c` into `ports/host_wasm/` alongside `ports/host_native/`. Phase-by-phase notes here remain authoritative for the browser behaviour (gesture-armed AudioContext, IDBFS persistence, rAF throttling, ASYNCIFY yield hook) that the WASM port still depends on.
