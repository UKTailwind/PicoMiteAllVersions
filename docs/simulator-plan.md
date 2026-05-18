# MMBasic Simulator Plan

> **User-facing docs:** [`simulator.md`](simulator.md) — quick start, build/run scripts, demos, troubleshooting.
> This file is the design log (phases, decisions, trade-offs, debt).

## Status (as of 2026-04-17)

- **Phase 0 — host REPL on terminal: ✅ DONE.** Interactive MMBasic over a real TTY using the device's `EditInputLine`. LOAD / SAVE / FILES / RUN / FRUN route through the host FS (`--sd-root`). 192/192 tests pass. Commits: `51bb8ce`, `da6d070`.
- **Phase 1 — HTTP + WS framebuffer streaming: ✅ DONE.** `./build_sim.sh` builds `mmbasic_sim` with vendored Mongoose + vanilla-JS frontend. Streams a **`CMDS` opcode stream** (CLS / RECT / PIXEL / SCROLL / BLIT) pushed immediately on every draw, with a one-shot `FRMB` full-frame bootstrap for new clients.
- **Phase 2 — keyboard input: ✅ DONE.** Browser `keydown` → JSON `{op:"key",code}` → server key queue → host `MMInkey`. Tracks held keys by `ev.code` (not `ev.key`) so Shift+char releases cleanly. Paces auto-repeat (150 ms initial, 70 ms interval) so games like `pico_blocks` don't overshoot. F1–F12, arrows, Home/End/PgUp/PgDn, Ctrl-<letter>, Insert/Delete all mapped.
- **Phase 3 — audio commands: ✅ DONE.** `PLAY TONE / STOP / SOUND / VOLUME / PAUSE / RESUME` translate to JSON TEXT frames on the same `/ws` socket and drive a WebAudio engine (`web/audio.js`). Both the interpreter's `cmd_play` (host copy) and the VM's `vm_sys_audio_play_tone/stop` syscalls share the emitter in `host/host_sim_audio.c`. File-based playback (`PLAY WAV / FLAC / MP3 / MODFILE`) is deferred to Phase 5.
- **Phase 4 — polish: partially delivered** (see [Phase 4 progress](#phase-4--polish) below). Remaining: auto-open browser, file upload, FPS cap, WS-typing char drops past ~16 chars.
- **Landed out of plan:**
  - `--slowdown N µs` throttle firing on every interpreter CheckAbort and VM back-edge so `RUN` and `FRUN` pace evenly without distorting `PAUSE` / `TIMER`.
  - `FRAMEBUFFER COPY F, N` now emits a BLIT CMDS op (was a silent memcpy — non-FASTGFX graphics demos appeared static).
  - Real host `fun_time` / `fun_date` via `localtime_r`, with `MMBASIC_HOST_DATE` / `MMBASIC_HOST_TIME` env-var overrides for deterministic tests.
  - VM compiler accepts `Rnd()` and `Rnd(expr)`, matching interpreter semantics. Regression test `t185_rnd_parens.bas`.
  - `FileLoadProgram` errors via `error()` on missing file so "Cannot find file" reaches the framebuffer console, not just stderr. (Still a bespoke host stub — see below.)
  - 13 bundled demos: `demo_gfx_*`, `demo_draw_*`, `demo_sound_*`, `demo_melody`.
- **Known debt — file I/O divergence.** Host's `FileLoadProgram`, `cmd_load`, `cmd_save`, parts of `cmd_files` are bespoke reimplementations in `host/host_stubs_legacy.c` that bypass the HAL we already have (`BasicFileOpen` / `FileGetChar` / `SaveProgramToFlash`). Next reclamation target: extract `FileLoadProgram` (and friends) into shared sources so host uses the same code path as device. Analysis: every dependency already resolves on host.

See [References](#references) for where things live.

### Delivered since the original plan

**Architecture / philosophy**
- Reframed `host/` as **its own MMBasic port**, peer to PicoMite / Maximite / MMBasic-DOS / STM32H743, not "Pico for x86". See `memory/project_host_is_its_own_port.md`. When host/device diverge, the question isn't "make them match" — it's "is this the HAL (legitimate) or the core leaking into host-only code (extract to shared)?"
- Extracted `DisplayPutC` / `GUIPrintChar` / `ShowCursor` from `core/mmbasic/Draw.c` → `gfx_console_shared.c`, shared by host and device. Driven by "why aren't we running device code as-is?"
- Confirmed **PicoMite is resolution-independent**: set `HRes`/`VRes` + framebuffer dims before first draw and everything else (Option.Width, clipping, console geometry) follows. See `memory/project_resolution_independent.md`.

**Wire protocol v3 — command stream**
- `CMDS` opcode stream replaces per-frame RGBA blast. Opcodes: `0x01 CLS`, `0x02 RECT`, `0x03 PIXEL`, `0x04 SCROLL`, `0x05 BLIT`. Pushed immediately on draw, not on a timer.
- New clients receive a one-shot `FRMB` full-frame bootstrap so they don't need the prior command history.
- Server poll loop is `mg_mgr_poll(1ms)`; no throttling.
- `FASTGFX` double-buffering works via a host back buffer (`host_fastgfx_back`); `bc_fastgfx_swap` memcpys to front and emits a single `BLIT`.

**Host simulator behavior**
- `--sim` entry in `host_main.c` configures PicoCalc-style defaults: 320×320, 8×12 font, green phosphor palette (`0x00FF00`), `OptionConsole=3` (UART + screen), `Option.DISPLAY_CONSOLE=1`, `Option.ColourCode=1`, `Option.Tab=4`.
- `--resolution WxH` CLI option (default 320×320). Clamped to `[80×60, 2048×2048]`. Sets `HRes`/`VRes` + `host_fb_width`/`host_fb_height` before framebuffer allocation.
- **1 ms tick thread** (`host_sim_tick_start`) mirrors device `timer_callback` (PicoMite.c:826): bumps `mSecTimer`, `CursorTimer`, `PauseTimer`, `Timer1..5`, `TickTimer[]`, `ScrewUpTimer`, etc. Makes PAUSE, TIMER, ON INTERRUPT TICK, and cursor blink work without per-hook patching.
- `MMputchar` / `putConsole` / `SerialConsolePutC` now match device dispatch exactly. `SSPrintString` is serial-only (never hits `DisplayPutC`), so VT100 escapes stop polluting the framebuffer console.
- `MMfputs` / `MMfputc` for `filenbr=0` route through `MMputchar` (matches `core/mmbasic/FileIO.c:3254/3386`), so PRINT output reaches both stdout *and* the framebuffer console.
- `MMInkey` sleeps 1 ms when empty in `--sim` mode so the Editor poll loop doesn't pin a CPU.

**Banner + terminal hygiene**
- Boot banner matches device (`PicoMite MMBasic Host V6.01.00b10` + copyright lines + "Bytecode VM by Josh V") plus a short "Host REPL — Ctrl-D to exit." line.
- `SerialConsolePutC` translates `\n` → `\r\n` when raw mode is active (OPOST is disabled there), fixing the stair-step of every prompt / error / editor message.

**Web frontend**
- Canvas pixel buffer matches server-reported dimensions (from FRMB/CMDS header). CSS sizing is set from JS via `DISPLAY_SCALE = 2` — **every framebuffer pixel renders as exactly 2×2 CSS px** regardless of resolution. Aspect ratio preserved.
- `image-rendering: pixelated` keeps the scaling sharp.
- Green-phosphor glow preserved via CSS `box-shadow`.

## Goal

Browser-based desktop simulator for the PicoCalc/PicoMite. Same interpreter and VM as the device — only the I/O backend changes. The host binary embeds an HTTP + WebSocket server; a plain-JS frontend in the browser is the display, keyboard, and audio output.

Design principles:
- The interpreter and VM stay untouched. Even the device's main interactive loop stays where it is — we only redirect its inputs and outputs.
- **Prefer wrapping over modifying existing MMBasic sources.** New behavior lives in `host/` as thin shims, hooks, and function overrides. Touch `core/mmbasic/MMBasic.c`, `PicoMite.c`, `core/mmbasic/Commands.c`, etc. only when there is no reasonable wrapper alternative, and keep any change minimal and local.
- The framebuffer is the source of truth. Pixels stream out, key events stream in, audio is sent as commands.
- The web frontend is a thin shell: receive pixels, draw to canvas, send keys back. No business logic in the browser.

## Architecture

```
┌──────────────────────────┐    WebSocket     ┌──────────────────────────┐
│  mmbasic_sim (C)         │ ◄─────────────►  │  Browser (vanilla JS)    │
│  ┌────────────────────┐  │                  │  ┌────────────────────┐  │
│  │ MMBasic interpreter│  │  binary frames → │  │ <canvas> + app.js  │  │
│  │   + bytecode VM    │  │  ← key events    │  │ window keydown →   │  │
│  └────────────────────┘  │  audio cmds  →   │  │ audio.js (WebAudio)│  │
│  host_stubs (existing)   │                  │  └────────────────────┘  │
│  ┌────────────────────┐  │                  │                          │
│  │ host_sim_server.c  │  │  HTTP: index.html, app.js, style.css        │
│  │ (Mongoose)         │  │                  │                          │
│  └────────────────────┘  │                  │                          │
└──────────────────────────┘                  └──────────────────────────┘
```

The REPL renders into the framebuffer using the existing on-screen text renderer — no separate text panel. The browser is intentionally dumb: receive pixels, draw to canvas, send keys back.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| HTTP + WS library | Mongoose (vendored, single .c/.h) | Designed for embedding in C apps; WS built in; battle-tested |
| Frontend | Vanilla JS, no build step | Single `index.html` + ES modules. Zero toolchain. The frontend is a thin shell (canvas + WS) — React/Vite is overkill. The C server is the end-all-be-all; HTML is just the window. |
| Framebuffer wire format | **CMDS opcode stream** + one-shot `FRMB` bootstrap for new clients | Originally raw RGBA full-frame; switched to immediate command push so idle frames cost nothing and animated frames avoid tearing / timer jitter |
| Display resolution | `--resolution WxH`, default 320×320 | PicoMite is resolution-independent; keep it configurable at launch |
| Client scaling | Integer pixel-doubling (2× via CSS) | Sharp edges, aspect-preserving, resolution-agnostic |
| Audio | Commands → WebAudio | Avoids PCM streaming and underrun handling; matches MMBasic's tone-centric audio model |
| REPL UI | Rendered into framebuffer | PicoCalc-faithful; reuses device's text-on-screen renderer; one source of truth |
| Bind address | `127.0.0.1` by default | Safe default; `--listen 0.0.0.0` to share |
| Auto-open browser | Yes, on `--sim` start | Better DX |
| Frontend asset serving | `web/` from disk in dev | Phase 5: `xxd -i` bundle into binary for single-file deploy |
| Build target | Separate `mmbasic_sim` binary, new `make sim` | Existing CI / test harness untouched |
| Terminal REPL | Permanent feature, not just scaffolding | Useful for CI, scripting, SSH; can coexist with `--sim` for incremental testing |

## Phases

### Phase 0 — Host REPL on terminal ✅ DONE

Delivered:

- `./host/mmbasic_test --repl [--sd-root DIR]` enters an interactive MMBasic session. Default `sd-root` is cwd.
- Uses the device's real `EditInputLine` (moved to `core/mmbasic/MMBasic_Prompt.c` — see below). UP/DOWN history, LEFT/RIGHT cursor edit, HOME/END, INSERT, F1–F12, TAB, backspace — all working on a real terminal via VT100 escape codes.
- Terminal raw mode and escape-sequence decoding in `host/host_terminal.{c,h}`. Terminal size auto-detected via `TIOCGWINSZ`. Cooked-mode fallback when stdin is piped (CI / scripts still work).
- `EDIT` runs over VT100 and calls through to the full device editor. F1 saves edits back to ProgMemory.
- `LOAD "foo.bas"`, `SAVE "foo.bas"`, `FILES`, `RUN "foo.bas"`, `FRUN "foo.bas"` route through the host filesystem at `host_sd_root`. `cmd_save` detokenises via `llist()`. `cmd_files` uses POSIX `opendir`/`readdir` in `host/host_fs.{c,h}` (isolated because POSIX `DIR` collides with FatFS `DIR`).
- `MEMORY`, `NEW`, error recovery all work. Ctrl-D at the prompt exits cleanly (gated on `editactive == 0` so it's still cursor-right inside EDIT).
- Simulated flash (`flash_range_erase`, `flash_range_program`, `FlashWriteByte`-family stubs) backs writes to `flash_prog_buf` so NEW / SAVE / EDIT-save actually persist. `load_basic_source` erases ProgMemory before writing, matching device behavior.
- Test harness (`mmbasic_test program.bas`) unchanged. All 191 tests still pass.

**PicoMite.c monolith split** (checkpointed in this phase because the host REPL wanted to reuse these):

| New file | Contents | Notes |
|---|---|---|
| `core/mmbasic/MMBasic_Prompt.c` | `EditInputLine`, `InsertLastcmd`, `lastcmd[]`, `MMPromptPos` | Host + device share the same line editor |
| `core/mmbasic/MMBasic_Print.c` | `PRet`/`PInt`/`PFlt`/`SRet`/`SInt`/`SIntComma`/… (12 print formatters) | Deleted 11 duplicate host stubs |
| `core/mmbasic/MMBasic_REPL.c` | `MMBasic_RunPromptLoop()`, `commandtbl_decode`, `transform_star_command` | Host's `run_repl()` calls the same loop device's `main()` calls |

PicoMite.c: 5065 → 4131 lines (−934). Both `CMakeLists.txt` and `CMakeLists 2350.txt` updated.

**Deferred from Phase 0 (known limitations):**

- `PLAY MODFILE` / `PLAY WAV` — no audio at all on host yet. Phase 3.
- `OPTION LIST` (prints nothing on host), `AUTO`, and assorted device-specific commands remain no-op stubs in `host_stubs_legacy.c`.
- Graphics commands (`BOX`/`LINE`/`CIRCLE`/`PIXEL`/`POLYGON`/`TEXT`) use simplified scalar-only bespoke host implementations; device uses `gfx_*_shared.c` with array-form support. Phase 4 polish: wire host into the shared path with host draw callbacks.
- HDMI/VGA display pipeline (`QVga*`, `HDMIloop0-3`, `HDMICore`, `dma_irq_handler0`) is still in `PicoMite.c`. Device-only; low priority to extract since host will never touch it.

### Phase 1 — HTTP + WS server, framebuffer streaming ✅ DONE

1. **Vendor Mongoose** at `host/vendor/mongoose.{c,h}` — single-file embeddable HTTP + WS server.
2. **Scaffold `web/`** — plain `index.html`, `app.js`, `style.css`. No build step. A single `<canvas>` element opens a WebSocket to `ws://<host>/ws`, receives binary RGBA frames, and draws each via `putImageData`. CSS `image-rendering: pixelated` + 2-3× scale.
3. **New `host/host_sim_server.c`** — Mongoose event loop. HTTP serves `web/` static files directly from disk. WS endpoint at `/ws` handles connections and pushes frames.
4. **Framebuffer-flip hook** in `host_stubs_legacy.c` — when `host_framebuffer` content changes (or on a 60Hz timer), call into the server to broadcast a binary WS message. The host framebuffer already exists as `uint32_t host_framebuffer[]` — it's what the test harness captures pixels from. No restructure needed, just a hook.
5. **New `--sim [--port N] [--listen ADDR]` flag** in `host_main.c`. `--sim` implies `--repl` behavior (interactive session, not one-shot); pairing with `--repl` means terminal I/O + web display coexist — a useful intermediate state for testing before keyboard-over-WS exists.
6. **Build target** — new `make sim` produces `mmbasic_sim` binary. `make` / `make test` stay on `mmbasic_test`. CI untouched.

**Acceptance:** `./mmbasic_sim --sim sample.bas` opens browser, shows running graphics at up to 60fps. Can open multiple browsers (one broadcasts-to-all is fine — multi-client support is out-of-scope, last connection wins or server broadcasts to all; TBD during implementation).

**Size check:** 320×240×4 = 307 KB/frame × 60 fps = ~18 MB/s. Loopback handles that trivially. If we ever care about WAN performance, switch to RGB565 (halves it) or dirty-rect deltas — but not in v1.

**Delivered (Phase 1 notes):**
- Mongoose 7.21 vendored at `host/vendor/mongoose.{c,h}`. License is GPL-2.0-or-commercial — fine for the host-only dev tool but would need a swap if we ever ship the sim in a closed-source context.
- Server runs on a pthread; MMBasic stays on the main thread. Framebuffer broadcast is unlocked — the worst visible artifact is a torn frame for 16 ms.
- Wire format: 8-byte header (`"FRMB"` + u16 width + u16 height, little-endian) followed by RGBA8 pixels. Browser decodes in `web/app.js`.
- `--sim` currently pairs with the terminal REPL (like `--repl`) — typing BASIC at the terminal updates the browser. No auto-run of a `.bas` file yet; the `sample.bas` arg in the acceptance criteria is implemented via `RUN "sample.bas"` typed at the prompt. Dedicated `--sim file.bas` auto-run can be added when useful.
- Multi-client: server broadcasts to every open WS connection; opening two tabs shows the same frames in both.
- 191/191 existing tests still pass — `make` (test harness) and `make sim` share source but build into separate object dirs.

### Phase 2 — Keyboard input ✅ DONE

Delivered:
- `web/app.js` attaches `keydown`/`keyup` listeners, translates to PicoMite keycodes (device codes from `core/mmbasic/Editor.h`: `BKSP=0x08`, `ENTER=0x0d`, `ESC=0x1b`, `UP=0x80`, `DOWN=0x81`, `LEFT=0x82`, `RIGHT=0x83`, `HOME=0x86`, `END=0x87`, `INSERT=0x84`, `PGUP=0x88`, `PGDN=0x89`, `F1..F12=0x91..0x9c`, `Ctrl-<letter>` = 1..26), and sends `{op:"key", code}` as JSON over WS.
- Server parses JSON, drops byte into a pthread-mutex-protected key queue; `MMInkey` drains it in `--sim` mode.
- **Held-key tracking by `ev.code` (physical key)** rather than `ev.key` (character) — fixes Shift+char sticky-key bug where `keydown` fires with `'"'` but `keyup` arrives with `"'"` (shift released first).
- **Paced auto-repeat**: browser keydown auto-repeat (~30/s on macOS) is faster than device keyboard (~6/s). Simulator uses 150 ms initial delay, 70 ms interval (~14/s) via `setTimeout` chain. Games like `pico_blocks` paddle no longer overshoot.
- `blur` handler drops all held-key timers so keys don't stick on tab-switch mid-press.
- Acceptance met: REPL fully usable in the browser. EDIT works, line editor works, arrow keys + F-keys all functional.

### Phase 3 — Audio commands ✅ DONE

Delivered:

- **`host/host_sim_audio.{c,h}`** — JSON event emitter. One thread-safe queue of JSON strings; producers are `cmd_play` (interpreter path) and `vm_sys_audio_play_tone/stop` (VM path); consumer is the Mongoose poll loop in `host_sim_server.c`, which pushes each string as a WebSocket TEXT frame to every client. In non-sim host builds all emitters compile as no-ops so `mmbasic_test` links unchanged.
- **Real `cmd_play` on host** (replaces the Phase 0 stub in `host_stubs_legacy.c`). Handles `STOP / PAUSE / RESUME / CLOSE / VOLUME / TONE / SOUND`. Shares argument validation shape with `shared/audio/Audio.c` (same error messages for ranges / counts). `NEXT / PREVIOUS / LOAD SOUND` silently no-op; file-based forms (`WAV / FLAC / MP3 / MODFILE / …`) error out with "Unsupported on host".
- **Wire protocol (server → client)** — JSON TEXT frames on the same `/ws` socket as the graphics CMDS:
  - `{op:"tone", l, r, ms?}` — ms omitted = play forever until next STOP/TONE; ms=0 is a no-op (matches device).
  - `{op:"sound", slot:1..4, ch:"L"|"R"|"B", type:"S|Q|T|W|O|P|N", f, vol:0..25}`.
  - `{op:"volume", l, r}` (0..100), `{op:"stop"}`, `{op:"pause"}`, `{op:"resume"}`.
- **`web/audio.js`** — WebAudio engine, imported as a module by `app.js`. Master L/R `GainNode`s feeding a `ChannelMerger` into `destination`. `PLAY TONE` uses a pair of `OscillatorNode`s (sine, one per side, 0 Hz = no-tone). `PLAY SOUND` keeps 4 slots × {L, R} voices; S/Q/T/W map to oscillator wave types, P = short looping random buffer (periodic-noise approximation), N = 1 s white-noise buffer, O = disconnect that side. Master gain is `(v/100)²` for perceptually-smooth volume changes. AudioContext creation deferred until the first keydown / click so browsers don't block startup.
- **Demo programs** (`demo_sound_tones.bas`, `demo_sound_waves.bas`, `demo_sound_chord.bas`, `demo_melody.bas`, `demo_sound_sfx.bas`) exercise every code path. `demo_sound_waves` / `demo_sound_chord` / `demo_sound_sfx` require `RUN` (interpreter) because the VM's `source_compile_play` only natively handles TONE / STOP.

**Deferred to Phase 5:** `PLAY MODFILE` / `PLAY WAV` / `PLAY FLAC` / `PLAY MP3` (needs binary streaming, not commands), `PLAY LOAD SOUND` (user-defined waveforms), VS1053 MIDI family.

### Phase 4 — Polish

**Delivered:**
- **Graphics primitives now share `gfx_*_shared.c`** — `gfx_box_shared.c`, `gfx_circle_shared.c`, `gfx_line_shared.c`, `gfx_pixel_shared.c`, `gfx_cls_shared.c`, `gfx_text_shared.c`, `gfx_console_shared.c`. Host wires its own `DrawRectangle` / `DrawBitmap` / `ScrollLCD` / `DrawPixel` backings into the same function-pointer table the device uses.
- **Configurable resolution** (`--resolution WxH`) — arbitrary dimensions between 80×60 and 2048×2048. Pixel-doubled on client.
- **Status indicator in UI** — `<div id="status">` shows "connecting…" / "connected" (green) / "disconnected — retrying…" (red). Auto-reconnect on drop.
- **Command-stream wire protocol** — immediate push instead of full-frame broadcast; reduces bandwidth for mostly-static frames, eliminates tearing.
- **FASTGFX double-buffering** on host — `host_fastgfx_back` + `bc_fastgfx_swap` for flicker-free animated games.

**Remaining:**
- Auto-open browser on `--sim` start (`open` / `xdg-open` / `start`).
- Window title shows running program / interpreter status.
- Configurable frame rate cap (`--fps N`) — currently uncapped, which is mostly fine since CMDS is event-driven.
- File upload: drag `.bas` onto the page, server saves to `host_sd_root`.

### Phase 5 — Optional extras

- GPIO panel: side panel showing `host_pin_value[]` and `host_pin_mode[]` state. Could be interactive (click a pin to drive it).
- Single-binary deploy: `xxd -i` the `web/` files into a C header, embed in binary. `mmbasic_sim` becomes a single self-contained executable.
- Serial console panel: separate `<Console/>` that mirrors anything written to the simulated USB CDC. Useful for serial-port-style interaction without disturbing the main display.
- `PLAY MODFILE` / `PLAY WAV` via PCM streaming (binary WS frames + AudioWorklet).
- HDMI/VGA display pipeline extraction from `PicoMite.c` (only if there's appetite for larger device testing).

## Wire format (v3, current)

**Server → client:** binary WS, two variants (see `web/app.js` comment header for full opcode byte layouts).

1. **`FRMB`** (full frame, sent once per client on connect to bootstrap):
   - 4 bytes magic `"FRMB"`
   - u16 width, u16 height (LE)
   - RGBA8 pixel data, row-major, `w*h*4` bytes

2. **`CMDS`** (command stream, the default path for all subsequent drawing):
   - 4 bytes magic `"CMDS"`
   - u16 canvas width, u16 canvas height (LE)
   - Sequence of opcodes:
     - `0x01 CLS` — u32 color (5 bytes total)
     - `0x02 RECT` — i16 x, i16 y, u16 w, u16 h, u32 color (13 bytes)
     - `0x03 PIXEL` — i16 x, i16 y, u32 color (9 bytes)
     - `0x04 SCROLL` — i16 lines (+down / -up), u32 bg color (7 bytes)
     - `0x05 BLIT` — i16 x, i16 y, u16 w, u16 h, RGBA[w\*h] (9 + w\*h\*4 bytes)

Future JSON messages for audio commands will live alongside binary frames (Phase 3).

**Client → server:** JSON.
- `{op:"key", code: N}` — one byte, device-style codes (see Phase 2 section for the full table).
- `{op:"upload", name:"...", data:"<base64>"}` — Phase 4, not yet implemented.

The protocol is small enough that neither side needs a schema file — one comment block in `app.js` and matching code in `host_sim_server.c` is enough.

## Build integration

- `./host/build_sim.sh` (or `make sim`) builds `mmbasic_sim` separately from `mmbasic_test`. Existing `host/build.sh` and `host/run_tests.sh` untouched.
- `./host/run_sim.sh` launches with sensible defaults (port 5150, web-root `../web`, sd-root repo root, 320×320).
- Object files live in `host/sim_obj/` so the simulator build doesn't stomp on the test-harness build.
- Mongoose adds no dependencies to the test harness. The web frontend has no build step and no dependencies at all — just static files.
- CI continues to build and run only `mmbasic_test`.

## Keymap

PicoCalc keys are mostly ASCII. Function keys, Fn-modifier combinations, and special keys will be mapped on demand by reading the device's keyboard driver source — not pre-investigated. Phase 2 will produce a small `host/picocalc_keymap.h` and mirror it in `web/keymap.js`.

## File layout (current)

```
host/
  host_main.c                # --sim / --repl / --sd-root / --listen / --port / --resolution / --web-root
  host_sim_server.c          # DONE: Mongoose HTTP+WS, FRMB bootstrap + CMDS broadcast, key queue, audio TEXT drain
  host_sim_server.h          # DONE: host_sim_server_start/stop
  host_sim_audio.{c,h}       # DONE Phase 3: PLAY* → queued JSON TEXT frames for WS broadcast
  host_stubs_legacy.c        # HAL: framebuffer, draw primitives, CMDS emitters, tick thread, key queue, cmd_play
  host_framebuffer_backend.h # DONE: FASTGFX back-buffer recognition, fill helpers
  host_terminal.{c,h}        # DONE: termios raw mode, escape decoding
  host_fs.{c,h}              # DONE: POSIX directory listing for FILES/LOAD/SAVE
  vendor/mongoose.{c,h}      # DONE: vendored Mongoose 7.21, GPL-2.0 (host-only, not shipped on device)
  Makefile                   # DONE: `make sim` target
web/
  index.html                 # canvas + script tag
  app.js                     # WS client, FRMB/CMDS decode, key send, paced repeat (ES module)
  style.css                  # canvas styling (sizing is set from JS)
  audio.js                   # DONE Phase 3: WebAudio engine (tone / sound / stop / volume / pause / resume)
gfx_box_shared.c             # DONE: shared with device
gfx_circle_shared.c          # DONE: shared with device
gfx_line_shared.c            # DONE: shared with device
gfx_pixel_shared.c           # DONE: shared with device
gfx_cls_shared.c             # DONE: shared with device
gfx_text_shared.c            # DONE: shared with device
gfx_console_shared.c         # DONE: DisplayPutC / GUIPrintChar / ShowCursor, shared with device
core/mmbasic/MMBasic_Prompt.c             # DONE: EditInputLine + lastcmd + MMPromptPos
core/mmbasic/MMBasic_Print.c              # DONE: PRet/PInt/PFlt/...
core/mmbasic/MMBasic_REPL.c               # DONE: MMBasic_RunPromptLoop() + helpers
docs/
  simulator-plan.md          # this doc
  bridge-restoration-plan.md # separate (FRUN bridge work)
```

## Out of scope (for v1)

- I2C / SPI peripheral emulation (RTC, expansion boards)
- USB host emulation
- Real-time GPIO interaction (PWM frequency monitoring, etc.) — basic value display only
- Multi-client support (one browser at a time; second connection replaces first or broadcasts)
- Save state / snapshots
- Network sharing beyond `--listen 0.0.0.0` (no auth, no TLS)

## Phase 0 findings (read before Phase 1)

Notes from building Phase 0 that a fresh conversation should know before touching Phase 1:

- **`host_sd_root`** is the REPL's filesystem root, defined in `host_stubs_legacy.c`. `NULL` in the test harness (falls through to in-memory FatFS). Non-NULL in `--repl` mode (cwd by default, overridden by `--sd-root`). Phase 1's server should inherit this — serving `web/` is a separate concern but the file-upload path in Phase 4 writes into `host_sd_root`.
- **Framebuffer is `host_framebuffer`** — uint32_t RGBA buffer, allocated in `host_stubs_legacy.c`. Drawing primitives (`host_fill_rect_pixels`, `host_draw_line_pixels`, `host_draw_triangle_pixels`, `host_draw_char`, `host_draw_text`) write directly. No existing "flip" event; Phase 1 must introduce one. Simplest: a dirty flag + a 60Hz timer, or hook every framebuffer-writing primitive.
- **Text rendering already goes through the framebuffer.** `MMPrintString` → host stdout, but device-style graphics PRINT and `TEXT` command draw into `host_framebuffer` via `host_draw_text`. So as soon as the canvas is hooked up, text-mode programs show correctly *and* the REPL text display (if `Option.DISPLAY_CONSOLE` is enabled) would appear.
- **`Option.DISPLAY_CONSOLE`** is currently 0 in the REPL (console text goes to terminal, not framebuffer). Phase 1 should decide: keep as-is (browser shows only graphics, terminal shows text), or set to 1 (browser shows a PicoCalc-faithful combined console+graphics). The plan says "framebuffer is source of truth" — argues for setting it to 1 in `--sim` mode.
- **Piped-input mode** is handled separately from raw-mode in `host/host_terminal.c`. Phase 1's server is a third input source. Keep them orthogonal — the server path shouldn't go through `host_terminal.c` at all.
- **Simulated flash is 256 KB** (`flash_prog_buf`); first half is program area, second half is CFunction/Font area (0xFF-filled). `load_basic_source` now erases the program area before writing; Phase 1 doesn't need to worry about this.
- **`editactive` flag** is defined in `core/mmbasic/Editor.c` only under `#ifdef PICOMITEVGA`; host redefines it in `host_stubs_legacy.c`. Used by the REPL to gate Ctrl-D-exit. Phase 1's server may want to check it too if it intercepts keys.
- **`.claude/` and `build_rp2040/`** are gitignored-adjacent (the former is this session's local state, the latter is a build artifact). Don't commit either.

## Risks and unknowns (for Phase 1+)

- **Framebuffer flip granularity.** Currently every pixel-level primitive writes directly; there's no "present" step. Naive approach: 60Hz timer broadcasts the buffer regardless of changes. Better: dirty flag set by each primitive, cleared on broadcast. Best: track dirty rectangles — but raw full-frame at 18 MB/s on loopback is plenty fine.
- **Mongoose event loop vs main thread.** Mongoose is single-threaded event-loop based; so is the REPL (blocks on stdin). Phase 1 needs to decide: spawn Mongoose on a background thread (then framebuffer hook must be thread-safe), or reverse — put MMBasic interpreter on a background thread and run Mongoose on main. A background Mongoose thread is simpler for MVP.
- **EDIT in a browser.** Full-screen editor with arrow keys, page up/down — the browser will want to scroll/select instead. Need `preventDefault()` on captured keys, possibly a "capture keyboard" toggle.
- **Audio timing.** WebAudio scheduling is sample-accurate but the WS hop adds jitter. Fine for tones; non-issue once we accept it isn't a music engine.
- **Auto-open browser.** On macOS `open http://localhost:PORT` works. Make sure the HTTP server is listening before opening, or the browser gets "connection refused" and the user has to refresh.

## References

- **Commits**:
  - `51bb8ce` — Add host REPL and begin breaking up PicoMite.c
  - `da6d070` — Fix REPL state bugs: load stale tail, F1 save, Ctrl-D, inpbuf echo
- **Existing host build**: `host/host_main.c`, `host/host_stubs_legacy.c`, `host/Makefile`, `host/build.sh`
- **VM syscall ABI**: `vm_sys_*.h`, `bytecode.h`
- **Framebuffer primitives**: `host_stubs_legacy.c` (search for `host_fill_rect_pixels`, `host_draw_*`)
- **Mongoose**: https://github.com/cesanta/mongoose
