# Web Console Driver Plan

**Goal:** add a portable network-backed video, keyboard, and sound backend that
lets a device serve a browser UI over WiFi. The browser behaves like the
existing native simulator page: canvas for display, WebAudio for sound, and
keyboard events back to MMBasic.

ESP32-S3 Metro is the first hardware target because it already has a working
WiFi stack, flash filesystem, Web/TCP service surface, and 8 MB ESP-IDF PSRAM.
The design must not become ESP32-only. Shared protocol, browser client, draw
encoding, audio event encoding, and input queue code should live outside the
ESP32 port so other network-capable targets can opt in later.

Reference implementation: [docs/simulator-plan.md](../simulator-plan.md). The
device backend should reuse the native simulator model, not the WASM
shared-memory model.

## Current Status - 2026-05-14

Branch: `web-console-driver`

Checkpoint commit: `452c8b7 checkpoint: web console driver progress`

Current flashed device state:

- ESP32-S3 Metro serves the web console at
  `http://192.168.4.47:18181/__web_console/`.
- The reserved WebSocket endpoint is `GET /__web_console/ws`.
- The page and WebSocket endpoint are served by the existing MMBasic TCP/HTTP
  service, not ESP-IDF `esp_http_server`.
- The browser presents the normal MMBasic console display and feeds keyboard
  input into the normal input path.
- Serial console remains available for recovery.

Implemented on branch:

- Shared web-console virtual display and input queue under
  `drivers/web_console/`.
- ESP32 reserved WebSocket handshake/framing path in the existing TCP server.
- Browser assets embedded in firmware for the reserved web-console page.
- 320x240 ESP32 virtual display backend with PicoCalc-style green-on-black
  console defaults.
- Virtual framebuffer draw hooks for text, `CLS`, pixel, rectangle, bitmap,
  scroll, draw-buffer, and read-buffer paths.
- Dirty framebuffer coalescing and RGB332 RLE `CMDS` blits.
- Browser RGB332 RLE decode and canvas render path.
- Latest-frame-wins behavior under display backlog; stale frame queues are not
  replayed after reconnect.
- Latest-browser-wins reconnect behavior for one active input owner.
- Browser keyboard input for REPL, bad-command handling, `FILES`, `EDIT`, cursor
  keys, Escape, and editor scroll stress.
- `Display_Refresh()` now pumps the web transport at a capped cadence so
  graphics-heavy BASIC loops can flush display changes without making draw code
  transport-aware.
- ESP32 PSRAM is reported by `MEMORY`.
- ESP32 bytecode compiler scratch allocation now prefers PSRAM, so
  `FRUN "A:/mand.bas"` works on-device.
- Speed pass after checkpoint:
  - faster scale-1 bitmap/text drawing;
  - clipped `DrawBuffer` copy;
  - one-pass ESP32 RLE packing with exact-size fallback;
  - browser per-frame debug logging gated behind `?debug=1`.

Validated recently:

- `make -C ports/host_native web-console-protocol-test websocket-test` passed.
- `git diff --check` passed.
- `./buildesp32.sh build` passed.
- `./buildesp32.sh flash` passed.
- On-device HTTP check returned `HTTP 200 OK` for `/__web_console/`.
- On-device all-flags smoke passed before the speed pass:
  display sequence, keyboard sequence, `FILES`, editor open/exit, and editor
  scroll stress.
- After PSRAM compiler allocation fix, `FRUN "A:/mand.bas"` completed:
  `64x48`, `MAX_ITER=96`, checksum `552868`, about `373 ms`.

Current caveats:

- Audio/WebAudio backend is not implemented yet.
- The current implementation has jumped ahead of the original phase order:
  ESP32 display/input MVP exists before full simulator extraction and shared
  audio extraction are complete.
- The speed pass has been built and flashed, but the full on-device web-console
  smoke should be rerun when the browser is not competing for WebSocket
  ownership.
- `RUN "A:/mand.bas"` is expected to appear idle for a long time because the
  file currently on `A:` is a text benchmark with no intermediate output. Use
  `FRUN "A:/mand.bas"` for that benchmark.
- Web console is still LAN/trusted-network only; no TLS/authentication has been
  added.

Phase status summary:

| Phase | Status |
|---|---|
| Phase 0 - Inventory and Contract | Mostly complete; contract and ownership documented, but implementation proceeded before all extraction cleanup. |
| Phase 1 - Extract Shared Protocol | Partially complete; display/input helpers exist under `drivers/web_console/`, but simulator/audio extraction is not complete. |
| Phase 2 - Native Minimal WebSocket Helper | Implemented and host-tested through `shared/net/mm_net_websocket.*` and native websocket tests. |
| Phase 3 - ESP32 Static Page and Minimal WebSocket | Implemented and flashed. |
| Phase 4 - Virtual Display MVP | Implemented and smoke-tested on device. |
| Phase 5 - Browser Keyboard Input | Implemented and smoke-tested on device. |
| Phase 6 - WebAudio Event Backend | Not started. |
| Phase 7 - Portability Cleanup | Partial; shared display/input exist, but ESP32 still owns much of the first working transport integration. |
| Phase 8 - Performance and Resilience | In progress; RGB332 RLE, coalescing, latest-frame-wins, nonblocking send progress, JS logging gate, and text/blit/RLE speed pass are implemented. More measurement is still needed. |

## Product Shape

The feature is a virtual device console, not a web dashboard bolted beside the
serial REPL.

- BASIC `PRINT`, prompts, graphics commands, and framebuffer operations render
  into the virtual display.
- Browser keyboard input feeds the same MMBasic input path as local console
  input.
- `PLAY TONE`, `PLAY SOUND`, `PLAY STOP`, `PLAY VOLUME`, `PLAY PAUSE`, and
  `PLAY RESUME` emit WebAudio events.
- The device serves the browser assets from flash/LittleFS or embedded static
  assets.
- One browser client is enough for the first release. Later clients may receive
  a read-only mirrored display, but input ownership must stay explicit.

The first user-visible mode should be deliberately small:

- 320x240 or 320x320 virtual display.
- PicoCalc-style display console defaults: font, colours, width/height.
- WebSocket connection at a reserved firmware endpoint, separate from BASIC
  user TCP server slots.
- Browser page with canvas, keyboard capture, audio unlock, reconnect, and a
  small status indicator.
- Serial console remains available for recovery and diagnostics.

## Existing Simulator Contract

The native simulator already has the right transport model:

- `FRMB`: one full-frame bootstrap when a browser connects.
- `CMDS`: incremental binary draw command stream.
- JSON text frames for audio events.
- JSON key messages from browser to server.

Current `CMDS` opcodes from `docs/simulator-plan.md`:

| Opcode | Meaning |
|---|---|
| `0x01` | `CLS` with packed colour |
| `0x02` | filled rectangle |
| `0x03` | single pixel |
| `0x04` | scroll by line count with background colour |
| `0x05` | blit rectangle payload |

Do not replace this with raw full-frame streaming for the first device target.
Full frames are acceptable for initial sync and explicit resync only. A
320x240 RGBA frame is about 300 KB; repeated full frames will waste ESP32 WiFi
bandwidth and internal heap.

## Architecture

Target shape:

```text
MMBasic core / VM
  -> shared draw/audio/input HAL contracts
  -> drivers/web_console/ shared protocol + virtual display/audio/input
  -> shared/net/mm_net_websocket minimal WebSocket framing
  -> existing port TCP/HTTP service
      ESP32: current MMBasic TCP server + reserved upgrade path
      host sim: Mongoose adapter, after extraction
  -> browser client
```

Proposed source layout:

```text
drivers/web_console/
  web_console_transport.h     # target-clean backend send/close contract
  web_console_protocol.h
  web_console_protocol.c      # FRMB/CMDS/audio/key framing, no port APIs
  web_console_display.h
  web_console_display.c       # virtual framebuffer + dirty/draw emit helpers
  web_console_audio.h
  web_console_audio.c         # PLAY event encoding
  web_console_input.h
  web_console_input.c         # key queue and ownership rules
  web_console_assets.h        # generated or embedded browser assets

shared/net/
  mm_net_websocket.h
  mm_net_websocket.c          # minimal RFC 6455 handshake/frame helpers

ports/esp32_s3_metro/main/
  web_console_tcp_esp32.c     # reserved MMBasic TCP-server WebSocket endpoint
  hal_audio_esp32_web.c       # links web_console_audio instead of stub
  hal_vm_framebuffer_esp32_web.c or hal_display_esp32_web.c
  hal_keyboard_esp32_web.c    # browser key queue integration

ports/host_native/
  host_sim_web_console_adapter.c  # later: Mongoose adapter over shared protocol

host/web/ or drivers/web_console/web/
  app.js / app.mjs
  audio.js
  style.css
  index.html
```

The shared driver must not include ESP-IDF, Mongoose, POSIX sockets, or
FreeRTOS headers. Transport backends provide only:

- send binary frame to active browser client;
- send text frame to active browser client;
- receive browser text frame;
- report backpressure/closed state;
- serve or expose static browser assets.

The first ESP32 implementation should use the existing MMBasic network server,
not ESP-IDF `esp_http_server`. ESP-IDF's HTTP/WebSocket server remains a
fallback only if the current TCP service cannot safely support a reserved
upgrade connection.

## ESP32 Execution Model

The first ESP32 implementation should be cooperative and poll-driven at the
MMBasic layer, not a dedicated-core design. ESP-IDF WiFi and lwIP already use
their own interrupt/event/task machinery underneath the socket API; the web
console integration should sit above that in the same model as the existing
`ProcessWeb()` services.

Initial implementation rules:

- add a `web_console_poll()` hook to the existing ESP32 network lifecycle;
- keep the reserved WebSocket connection nonblocking;
- treat display and audio producers as queue/dirty-region writers, not as
  synchronous socket senders;
- drain queued WebSocket output opportunistically from the poll hook;
- coalesce or drop display updates before a slow browser can stall the
  interpreter;
- feed inbound browser key/control messages into the normal input queue from
  the poll path.

A separate FreeRTOS task is a later optimization, not part of the MVP. If
profiling proves it is needed, the task should own only transport draining and
input/output queues. It must not touch MMBasic VM, display, or audio globals
directly. Pinning that task to a specific ESP32-S3 core should be considered
only after measuring contention with WiFi/lwIP and the interpreter.

## Protocol Rules

Keep the simulator wire format as the baseline.

- Preserve `FRMB` and `CMDS` magic strings and opcode meanings where practical.
- Keep the browser client tolerant of protocol version/capability fields.
- Add a version/caps message before extending opcodes.
- Use binary frames for display and text frames for control/audio unless a
  measured bottleneck says otherwise.
- Coalesce or drop display updates when the WebSocket send queue is backed up.
  Never let a slow browser stall the interpreter.
- Prefer RGB332 or RGB565 on-device storage; convert to the browser payload only
  at the boundary if the existing browser wants RGBA.
- Keep audio event-based for MVP. PCM streaming, WAV/MP3/MOD, and AudioWorklet
  buffering are later phases.

The WebSocket layer should be deliberately minimal and firmware-owned. It is
not a new BASIC WebSocket feature.

Required MVP WebSocket support:

- intercept `GET /__web_console/ws` before normal BASIC `WEB TRANSMIT` or TCP
  request handling;
- validate `Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key`,
  and WebSocket version `13`;
- compute and return `Sec-WebSocket-Accept`;
- send unmasked server-to-browser binary frames for `FRMB` and `CMDS`;
- send unmasked server-to-browser text frames for audio/status JSON;
- receive masked browser-to-device text frames for key/control messages;
- handle close and ping/pong well enough for browser refresh/reconnect;
- reject fragmented inbound messages in MVP;
- enforce small inbound payload limits;
- chunk or reject outbound payloads that exceed the configured send limit;
- support one active web-console client.

Unit tests should cover the RFC 6455 accept-vector, frame encoding, masked
frame decoding, close/ping handling, and size-limit failures.

Build and test this minimal WebSocket layer natively first. The host-native
test should exercise the shared helper directly, and a small loopback smoke
should prove a real browser/Node WebSocket client can complete the upgrade and
exchange text/binary frames with a tiny native test server. ESP32 integration
starts only after those host tests are green.

## ESP32 First Target

ESP32-specific implementation should use the existing ESP32 port rules:

- ESP-IDF calls stay inside `ports/esp32_s3_metro/main/` or ESP32-specific
  driver files.
- Core and shared protocol code stay target-clean.
- The web console endpoint should be a firmware-reserved path in the existing
  MMBasic TCP/HTTP service. It must not consume BASIC user TCP server slots or
  appear as a user-visible `WEB TCP REQUEST`.
- Use ESP-IDF PSRAM only for explicit large buffers. Do not route generic
  `AllMemory` or `PSRAMsize` through ESP32 PSRAM as part of this feature.
- Keep latency-sensitive queues and small state in internal RAM.
- Use PSRAM for optional full-frame bootstrap buffers or large blit staging if
  measurement shows internal heap pressure.

Suggested ESP32 mode names are implementation details, but a likely user
surface is one of:

- `OPTION WEB CONSOLE ON`
- `OPTION DISPLAY WEB`
- `OPTION CONSOLE WEB`

Pick the final BASIC surface only after inventorying existing `OPTION DISPLAY`,
`OPTION TELNET CONSOLE`, and WebMite conventions. Do not overload ordinary
BASIC user web-server commands.

## Phases

### Phase 0 - Inventory and Contract

Inventory the simulator code that must be shared:

- `host_sim_server.c` WebSocket framing and client lifecycle.
- host framebuffer emitters for `FRMB` and `CMDS`.
- `host_sim_audio.c` JSON audio event queue.
- `web/app.js` or current browser client frame decoder.
- `web/audio.js` WebAudio event handlers.

Write a short contract header for the transport backend and decide where the
browser assets live.

Phase 0 inventory on `web-console-driver`:

- WebSocket framing and client lifecycle owner:
  `ports/host_native/host_sim_server.c`. The simulator uses Mongoose directly:
  `mg_http_listen()` starts the background server, `MG_EV_HTTP_MSG` upgrades
  `/ws` with `mg_ws_upgrade()`, `MG_EV_WS_MSG` accepts browser text JSON and
  pushes key codes, and `MG_EV_POLL` drains display/audio queues. Per-client
  bootstrap state is the file-local `sim_client` stored in `mg_connection.data`;
  every new WebSocket client receives one `FRMB` before shared `CMDS` broadcasts.
  This file must become an adapter in later phases, not the protocol owner.
- Full-frame `FRMB` source owner: `ports/host_native/host_fb.c` owns the host
  visible framebuffer, dimensions, and `host_sim_framebuffer_copy()` /
  `host_sim_framebuffer_dims()`. `ports/host_native/host_sim_server.c` currently
  packs `FRMB` itself in `send_bootstrap_frame()`, including the `"FRMB"` magic,
  little-endian dimensions, and RGB24-to-RGBA8 conversion.
- Incremental `CMDS` emitter owner: `ports/host_native/host_sim_server.c` owns
  the command queue and opcode packing in `host_sim_emit_cls()`,
  `host_sim_emit_rect()`, `host_sim_emit_pixel()`, `host_sim_emit_scroll()`, and
  `host_sim_emit_blit()`. `ports/host_native/host_fb.c` is the producer at draw
  mutation points, including `host_fb_put_pixel()`, `host_fb_fill_rect()`,
  `host_fb_scroll_lcd()`, and front-buffer copy/BLIT presentation. This split
  means Phase 1 should extract the queue and packers before moving any draw
  call sites.
- Audio JSON queue owner: `ports/host_native/host_sim_audio.c` owns JSON event
  construction and queueing for `tone`, `stop`, `sound`, `volume`, `pause`, and
  `resume`. `ports/host_native/host_sim_server.c` drains the queue during
  `MG_EV_POLL` and forwards each JSON string as one WebSocket TEXT frame.
- Browser frame decoder owner: `web/app.js`. It connects to `/ws`, sets
  `binaryType = "arraybuffer"`, decodes `FRMB` full frames, replays `CMDS`
  opcodes `0x01` through `0x05`, sends key JSON as
  `{ "op": "key", "code": <byte> }`, and routes text JSON frames to audio.
- Browser WebAudio owner: `web/audio.js`. It handles the simulator audio JSON
  operations with WebAudio oscillators, noise buffers, per-channel master gain,
  pause/resume, and browser gesture unlock. No server-side audio state beyond
  event queueing is shared today.
- Browser asset location decision: keep the current simulator assets in the
  top-level `web/` directory until extraction starts. The shared device/browser
  assets should ultimately live under `drivers/web_console/web/`, with
  `web/index.html`, `web/app.js`, `web/audio.js`, and `web/style.css` treated as
  the source material to lift or mirror. Host simulator serving from `web/`
  remains unchanged during Phase 0.

Phase 0 contract decision:

- Add `drivers/web_console/web_console_transport.h` as a dependency-free
  backend contract for later phases. It defines only the reserved device
  endpoint, inbound text callback type, and the binary/text send, send-ready,
  and close callback shape. It must not include ESP-IDF, Mongoose, POSIX socket,
  FreeRTOS, or interpreter headers. No build files consume it in Phase 0.

Exit gate:

- no code movement yet unless purely mechanical;
- plan updated with exact source owners;
- host simulator still builds and runs unchanged.

### Phase 1 - Extract Shared Protocol

Move protocol packing/parsing into `drivers/web_console/` without changing
host simulator behavior.

Deliverables:

- shared `FRMB`/`CMDS` packers;
- shared key-message parser;
- shared audio JSON event builders;
- host simulator adapter still uses Mongoose but calls shared protocol code;
- browser client remains compatible.

Exit gate:

- host build and simulator build pass;
- existing browser simulator smoke passes;
- full basic host suite passes;
- `buildall` passes.

### Phase 2 - Native Minimal WebSocket Helper

Implement and test the reusable WebSocket handshake/framing helper on the host
before touching ESP32 transport code.

Deliverables:

- `shared/net/mm_net_websocket.h`;
- `shared/net/mm_net_websocket.c`;
- host-native unit test for:
  - RFC 6455 `Sec-WebSocket-Accept` known vector;
  - outbound text and binary frame encoding;
  - masked inbound text frame decoding;
  - ping, pong, and close handling;
  - fragmented inbound frame rejection;
  - payload size-limit failures;
- small native loopback smoke server using the shared helper, not Mongoose's
  WebSocket implementation, plus a Node/browser WebSocket client smoke.

Exit gate:

- native WebSocket helper unit test passes;
- loopback WebSocket smoke passes;
- host simulator still uses its existing Mongoose WebSocket path unchanged;
- `./host/run_tests.sh` passes;
- `./buildall.sh` passes.

### Phase 3 - ESP32 Static Page and Minimal WebSocket

Use the existing ESP32 MMBasic TCP/HTTP server to serve browser assets, and add
a firmware-reserved minimal WebSocket upgrade path for one web-console client.

Deliverables:

- ESP32 browser assets served from embedded files or A: seed files;
- browser assets served by the current MMBasic HTTP/TCP path, not ESP-IDF
  `esp_http_server`;
- `shared/net/mm_net_websocket.*` minimal handshake/frame helpers;
- reserved `GET /__web_console/ws` interception before normal BASIC request
  handling;
- WebSocket handshake, masked inbound text decode, outbound text/binary encode,
  ping/pong, and close handling;
- protocol caps/hello exchange;
- no display/audio integration yet.

Exit gate:

- WebSocket unit tests pass on host;
- `./buildesp32.sh build` passes;
- flash succeeds;
- browser can load the page through the existing MMBasic web server and connect
  to the reserved WebSocket endpoint;
- existing ESP32 default smoke, PSRAM smoke, flash/VAR smoke, and network
  conformance still pass.

### Phase 4 - Virtual Display MVP

Wire ESP32 display output into the shared `FRMB`/`CMDS` path.

Deliverables:

- virtual display dimensions and console defaults;
- `CLS`, text console output, pixel, rect, scroll, and blit emission;
- browser full-frame bootstrap on connect;
- backpressure policy that drops/coalesces display updates instead of blocking
  MMBasic.

Exit gate:

- `PRINT`, prompt, `CLS`, `COLOUR`, `TEXT`, `PIXEL`, `LINE`, `BOX`, and a small
  graphics demo render in the browser;
- browser reconnect receives a correct `FRMB`;
- serial console remains usable;
- full ESP32 smoke gate still passes.

### Phase 5 - Browser Keyboard Input

Feed browser key events into the ESP32 MMBasic input path.

Deliverables:

- shared key-code mapping reused from simulator where possible;
- focus/capture rules in browser;
- one active input owner;
- clean interaction with serial console and Telnet console.

Exit gate:

- browser can type `PRINT 2+3` and run a short program;
- EDIT and cursor keys work well enough for basic use;
- serial prompt recovery still works;
- network conformance still passes, including Telnet.

### Phase 6 - WebAudio Event Backend

Replace the ESP32 audio stub with a web-console audio backend when the web
console mode is active.

Deliverables:

- `PLAY TONE`, `PLAY SOUND`, `PLAY STOP`, `PLAY VOLUME`, `PLAY PAUSE`, and
  `PLAY RESUME` emit simulator-compatible audio events;
- browser audio unlock UX reused from simulator/WASM browser;
- no PCM streaming in this phase.

Exit gate:

- browser receives expected audio events from BASIC commands;
- a simple melody demo is audible after user audio unlock;
- host audio/simulator smoke still passes;
- ESP32 smoke and network conformance still pass.

### Phase 7 - Portability Cleanup

Make the ESP32 implementation a consumer of the shared web-console driver, not
the owner of the architecture.

Deliverables:

- clear transport backend interface;
- host simulator uses shared protocol code;
- ESP32 uses shared protocol code;
- no ESP-IDF or Mongoose includes in shared driver;
- documentation for adding another target.

Exit gate:

- host simulator and ESP32 web console both pass their browser smokes;
- `tools/check_hal_purity.sh` stays green;
- `./host/run_tests.sh`, `./buildall.sh`, and `./buildesp32.sh build` pass.

### Phase 8 - Performance and Resilience

Optimize only after the MVP is correct.

Candidate work:

- dirty rectangle coalescing;
- RGB332/RGB565 browser decode path;
- command-stream compression or RLE for console-heavy screens;
- send-queue watermarks;
- reconnect/resync hardening;
- optional multi-client read-only mirroring.

Exit gate:

- measured browser FPS/latency documented for text, graphics, and FASTGFX-like
  demos;
- no interpreter stalls under a slow or disconnected browser;
- ESP32 can recover cleanly from browser refresh, WiFi reconnect, and client
  disconnect.

## Test Policy

Cheap gates during implementation:

- `python3.11 -m py_compile` for touched tooling.
- `tools/check_hal_purity.sh`.
- host simulator/browser smoke for protocol changes.
- targeted ESP32 build for transport changes.

Full gate for each accepted phase:

- `./host/run_tests.sh`.
- `./buildall.sh`.
- `./buildesp32.sh build`.
- flash ESP32-S3 Metro.
- ESP32 default smoke.
- ESP32 PSRAM smoke.
- ESP32 flash/VAR smoke.
- ESP32 network conformance.
- new browser web-console smoke for that phase.

For the WebSocket layer specifically, native gates are mandatory before device
work:

- unit test the shared frame/handshake helpers in a host binary;
- run a loopback smoke with a real WebSocket client;
- verify the native simulator still works, since it is the compatibility
  reference for the browser protocol.

If the ESP32 stops producing serial output during an on-device gate, first send
input to distinguish a waiting prompt from a true hang. If input cannot recover
the prompt and no bytes arrive, stop the device attempt and request a physical
reset before rerunning the failed gate.

## Non-Goals

- Native VGA over ESP32 GPIO. That is a separate hardware display backend.
- Native I2S/PWM audio output. That is a separate hardware audio backend.
- PCM/WAV/MP3/MOD streaming in the first release.
- Multi-user remote desktop semantics.
- TLS/authentication for the first LAN-only bring-up. Security policy must be
  revisited before exposing the page beyond a trusted local network.

## Risks

- ESP32 WiFi throughput and heap pressure may make naive RGBA blits choppy.
  Keep `CMDS` incremental and add RGB332/RGB565 browser decode early if needed.
- Browser backpressure can stall the interpreter if send calls block. The
  transport API must report backpressure and allow dropping/coalescing frames.
- MMBasic display globals assume one physical display. Keep the web console as
  a selectable backend and avoid changing core display semantics.
- Audio unlock is browser-policy driven. The page must make it obvious when
  audio is muted until a user gesture.
- Extracting simulator code can regress the native sim. Keep the host simulator
  smoke in every phase gate.

## First Implementation Slice

Start with Phase 0 and Phase 1 only. Do not write ESP32 HTTP/WebSocket code
until the simulator protocol has a shared, port-neutral home. That keeps the
first ESP32 pass small: it should consume an existing protocol package rather
than copying `host_sim_server.c` into the ESP32 port.

When the ESP32 transport phase starts, do not switch ESP32 to ESP-IDF `esp_http_server` by
default. The implementation target is the existing MMBasic TCP/HTTP service plus
a minimal reserved WebSocket upgrade/framing layer.
