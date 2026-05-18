# WASM Network Proxy Compliance Plan

**Goal:** make the host-WASM build capable of full BASIC network conformance
when it is served with a trusted host proxy, while preserving the current
static GitHub Pages behavior for deployments that have no server-side code.

The browser build cannot open raw TCP, UDP, TFTP, Telnet, or arbitrary
cross-origin HTTP sockets directly. That is a browser sandbox constraint, not
a BASIC or shared-network-stack design choice. The proxy mode should treat the
browser as a UI/runtime and tunnel the network backend through the local or
server-side host that served the app.

## Operating Modes

Host-WASM must support two explicit modes.

### Static Browser Mode

This is the current GitHub Pages-compatible mode.

- No proxy is assumed.
- `WEB TCP CLIENT REQUEST` uses browser `fetch` for same-origin and
  CORS-approved HTTP requests.
- MQTT uses browser WebSocket when the broker exposes an MQTT WebSocket
  endpoint.
- Raw TCP stream/server, UDP, TFTP, Telnet, and non-CORS HTTP fail with clear
  unsupported errors.
- This mode remains the default for `ports/host_wasm/web/` when no proxy is detected.

### Proxy Mode

This mode is active when the app is served by a companion proxy process.

- The browser talks to the proxy over same-origin HTTP/WebSocket.
- The proxy opens real host sockets and implements network services on behalf
  of the WASM runtime.
- BASIC-visible semantics should converge with host-native and hardware
  behavior wherever the proxy can faithfully represent the transport.
- Proxy mode must be opt-in or positively detected; it must not make static
  Pages deployments appear more capable than they are.

## Compliance Target

Proxy mode should pass the same behavioral suites covered by
`porttools/network_conformance.py all`, adapted to drive the browser app
instead of a serial console:

- TCP client HTTP request.
- TCP client stream.
- TCP server listener, request/read/send/close, transmit helpers, and listener
  preservation across `RUN`.
- UDP send/receive and listener preservation across `RUN`.
- TFTP write/read round trip.
- Telnet console round trip.
- NTP request/response.
- MQTT connect, subscribe, receive, publish, unsubscribe, close.
- Shared option/lifecycle behavior: configured services survive `RUN`; active
  sessions are cleaned consistently.

Static browser mode keeps its existing smoke coverage:

- Browser fetch-backed HTTP behavior.
- MQTT-over-WebSocket behavior.
- Explicit unsupported errors for raw socket features.

## Architecture

The proxy is a host-network HAL backend for WASM, not a new BASIC API. The
proxy process is a native C executable built by the repo toolchain. Do not add
a Python or Node server as the implementation path; JavaScript may still be
used by browser smoke tests, but the server users run should be C.

```
BASIC command surface
  -> shared/net command parsers + lifecycle policy
  -> ports/host_wasm/host_wasm_web.c
  -> JS proxy transport in ports/host_wasm/web/app runtime
  -> same-origin WebSocket/HTTP C proxy server
  -> host OS sockets/filesystem/network
```

Keep policy in shared code:

- BASIC parsing stays in existing shared `shared/net` modules where it already
  exists.
- Lifecycle decisions stay in `mm_net_lifecycle`.
- `ports/host_wasm/host_wasm_web.c` should only select the browser/proxy
  backend and bridge bytes/events.
- The proxy server owns host sockets, DNS, timers, and service listeners.

## Native C Proxy Server

Build a standalone C binary, likely from the native host area, because the repo
already has a C HTTP/WebSocket server stack for `mmbasic_sim`.

Preferred shape:

- New target in `ports/host_native/Makefile`: `wasm-proxy`.
- Output binary: `ports/host_native/build/wasm_network_proxy`.
- Reuse `ports/host_native/vendor/mongoose.{c,h}` for HTTP/WebSocket serving.
- Serve static files from `ports/host_wasm/web/` with the same COOP/COEP headers that
  `ports/host_wasm/web/serve.py` adds today.
- Add a proxy WebSocket endpoint and capability endpoint in the same process.
- Keep the proxy independent from `mmbasic_test` / `mmbasic_sim`; it is a
  companion service for the WASM page, not another interpreter process.

Implementation files should be structured so protocol and socket code are
testable without a browser:

- `ports/host_native/wasm_proxy_main.c`: CLI, static-file root, bind address,
  security flags, Mongoose event loop.
- `ports/host_native/wasm_proxy_protocol.{c,h}`: framed WebSocket protocol,
  request ids, caps, serialization.
- `ports/host_native/wasm_proxy_net.{c,h}`: host socket operations for TCP,
  UDP, listener state, DNS, timeouts.
- Optional `ports/host_native/wasm_proxy_http.{c,h}` if arbitrary HTTP/HTTPS
  proxying needs separate parsing/TLS handling.

`ports/host_wasm/web/serve.py` may remain as a tiny static fallback for developers, but
proxy compliance work should use `ports/host_native/build/wasm_network_proxy`.

## Proxy Transport

Use one persistent WebSocket control channel from browser to proxy:

- Endpoint: `/__picomite_proxy/ws`.
- First message from JS: `hello` with protocol version and requested features.
- First message from proxy: `caps` with supported transports and limits.
- Messages use a small framed protocol: JSON control header plus optional
  binary payload, or a compact binary header once behavior stabilizes.
- Every operation has a monotonically increasing request id so async replies
  can be matched without blocking the browser event loop.

Required capabilities:

- `tcp_client`
- `tcp_stream`
- `tcp_server`
- `udp`
- `tftp`
- `telnet`
- `ntp`
- `mqtt_plain`
- `http_proxy`

The proxy should also expose a simple HTTP capability endpoint:

- `GET /__picomite_proxy/caps`
- Returns JSON with protocol version, capability bits, default limits, and
  whether loopback-only security is active.

## Feature Mapping

### HTTP / TCP Client Request

Proxy mode should support both:

- Fetch-like HTTP request proxying for arbitrary `http://` and `https://`
  URLs.
- Raw TCP request behavior compatible with hardware `WEB TCP CLIENT REQUEST`.

Implementation path:

1. Keep current browser `fetch` implementation for static mode.
2. In proxy mode, forward the parsed host/port/request bytes to the proxy.
3. Proxy opens a socket, writes the request bytes, reads the response, and
   returns bytes into the BASIC long-string buffer.
4. Support TLS only when the request target requires it and the proxy can
   verify certificates through the host OS. Plain HTTP remains required.

### TCP Stream

Proxy mode maps `WEB OPEN TCP STREAM` and `WEB TCP CLIENT STREAM` to a proxy
TCP socket id.

- `open_tcp_stream(host, port, timeout)` returns a stream id.
- `tcp_stream_write_read(id, write_bytes, read_limit)` returns newly available
  bytes and stream status.
- `WEB CLOSE TCP CLIENT` closes the stream id.

### TCP Server

Proxy mode maps configured TCP server listeners to proxy-owned host listeners.

- `OPTION TCP SERVER PORT n` requests a proxy listener on `n`.
- Accepted connections become proxy connection ids mapped to BASIC connection
  slots.
- Request state, path extraction, read/send/close behavior must feed the
  existing shared service layer so BASIC sees the same `MM.INFO(TCP REQUEST n)`,
  `MM.INFO(TCP PATH n)`, `WEB TCP READ`, `WEB TCP SEND`, and
  `WEB TCP CLOSE` behavior as host-native/hardware.
- `RUN` cleanup must close accepted connections but preserve/reopen the
  configured listener through shared lifecycle policy.

### UDP

Proxy mode maps UDP listeners and sends to proxy-owned sockets.

- `OPTION UDP SERVER PORT n` binds a proxy UDP socket.
- Incoming packets are queued with source address metadata.
- WASM polling drains queued packets into shared `MM.MESSAGE$` /
  `MM.ADDRESS$` state.
- `WEB UDP SEND` sends from the proxy socket.

### TFTP

Prefer reusing the shared TFTP protocol core where possible.

Two viable implementation choices:

1. Proxy exposes UDP primitives only; the WASM runtime runs the shared TFTP
   core over proxied UDP.
2. Proxy implements the TFTP service and forwards file operations to the WASM
   filesystem bridge.

Start with option 1 if the polling/timing model is adequate; it keeps protocol
policy in shared code.

### Telnet

Proxy mode binds the Telnet listener and forwards bytes to/from the WASM
console bridge.

- The proxy owns the TCP listener.
- The WASM runtime remains the console authority.
- Telnet input is delivered through the same key/input path used by the
  existing console, with clear ownership to avoid racing browser keyboard
  input.

### NTP

Proxy mode can support NTP in either of two ways:

- Preferred for conformance: proxy sends/receives UDP NTP packets so the BASIC
  `WEB NTP` path exercises transport behavior.
- Acceptable fallback for deployment: proxy returns host time for a requested
  server only when UDP is unavailable, but this must be a distinct capability
  and not count as full conformance.

### MQTT

Proxy mode should support plain MQTT over TCP for parity with host-native and
hardware.

- Keep current browser MQTT-over-WebSocket path for static mode.
- In proxy mode, either:
  - bridge raw MQTT packets over the proxy channel while shared MQTT wire code
    runs in WASM, or
  - let the proxy own the MQTT TCP socket and expose packet send/receive
    primitives to `hal_net_mqtt_*`.
- The BASIC parser and MQTT wire helpers should remain shared.

## Security Model

The proxy is powerful: it can make arbitrary network connections from the host
running it. It must not be enabled casually on public deployments.

Minimum rules:

- Default bind address is `127.0.0.1`.
- Public bind requires an explicit command-line flag.
- Optional allow-list/deny-list for destination hosts and ports.
- Optional loopback-only mode for conformance.
- No open proxy endpoints that accept arbitrary unauthenticated browser origins.
- Validate `Origin` and `Host` headers for the WebSocket control channel.
- Rate-limit connections and buffer sizes.
- Default maximum response/body sizes must fit the WASM memory budget.

GitHub Pages remains static mode only. If someone wants proxy mode on the
public internet, they must deploy and secure a separate server explicitly.

## Files And Ownership

Likely new files:

- `ports/host_native/wasm_proxy_main.c`
- `ports/host_native/wasm_proxy_protocol.h`
- `ports/host_native/wasm_proxy_protocol.c`
- `ports/host_native/wasm_proxy_net.h`
- `ports/host_native/wasm_proxy_net.c`
- optional `ports/host_native/wasm_proxy_http.h`
- optional `ports/host_native/wasm_proxy_http.c`
- `ports/host_wasm/web/proxy_client.mjs`
- `ports/host_wasm/web/smoke_network_proxy.mjs`
- `porttools/wasm_proxy_network_conformance.py`
- `docs/real-hal/wasm-network-proxy-plan.md`

Likely changed files:

- `ports/host_native/Makefile`: add the `wasm-proxy` target and build rules
  for the standalone C proxy binary.
- `ports/host_wasm/web/serve.py`: keep as static fallback only; do not embed the proxy
  there.
- `ports/host_wasm/web/app.mjs`: detect proxy caps and publish proxy state to the WASM
  bridge.
- `ports/host_wasm/host_wasm_web.c`: select proxy-backed operations when proxy
  capabilities are present.
- `ports/host_wasm/Makefile`: export any new C entry points needed by the JS
  proxy bridge.
- `shared/net` only if a missing abstraction is truly shared across ports.

## Implementation Phases

### Phase 0 - Contract And Mode Detection

- Define proxy capability JSON and WebSocket message envelope.
- Add the native C proxy skeleton:
  - CLI: `ports/host_native/build/wasm_network_proxy [--bind 127.0.0.1] [--port 8000]
    [--web-root ports/host_wasm/web]`.
  - Static serving with COOP/COEP headers.
  - `/__picomite_proxy/caps` returning a fixed initial capability set.
  - `/__picomite_proxy/ws` accepting a versioned no-op WebSocket handshake.
- Add browser-side detection:
  - static mode if `/__picomite_proxy/caps` is absent.
  - proxy mode if caps endpoint and WebSocket handshake succeed.
- Expose `MM.INFO(TCPIP STATUS)` / capability behavior consistently in each
  mode.
- Add a visible developer status in JS debug state, not in the BASIC UI.

Exit gates:

- `make -C ports/host_native wasm-proxy` builds `ports/host_native/build/wasm_network_proxy`.
- `ports/host_native/build/wasm_network_proxy --port 8000 --web-root ports/host_wasm/web` serves the WASM
  app with COOP/COEP headers.
- Existing static WASM smokes still pass.
- A no-op proxy can be detected and reported without changing BASIC semantics.

### Phase 1 - HTTP Proxy For Text Browser Use

- Add proxy HTTP request operation.
- In proxy mode, route `WEB TCP CLIENT REQUEST` HTTP requests through the
  proxy so non-CORS targets such as FrogFind can be fetched.
- Preserve static mode's browser fetch behavior.
- Add a BASIC demo text browser that starts at FrogFind and uses proxy mode
  when available.

Exit gates:

- Same-origin fetch smoke still passes in static mode.
- Proxy smoke fetches a non-CORS HTTP URL and returns response bytes to BASIC.
- Demo works when served by `ports/host_native/build/wasm_network_proxy`.

### Phase 2 - TCP Client Stream

- Add proxy TCP connect/write/read/close operations.
- Route `WEB OPEN TCP STREAM` and `WEB TCP CLIENT STREAM` through proxy mode.
- Preserve explicit unsupported errors in static mode.

Exit gates:

- Browser-driven TCP stream smoke matches host-native TCP stream checks.

### Phase 3 - UDP And NTP

- Add proxy UDP bind/send/receive operations.
- Route UDP server/send and NTP transport through the proxy.
- Confirm shared message/address state and interrupts behave correctly.

Exit gates:

- UDP send/receive smoke passes.
- NTP deterministic responder smoke passes.
- `udp_preserved_after_run` passes in the browser/proxy harness.

### Phase 4 - TCP Server And HTTP Helpers

- Add proxy TCP listener/accept/read/write/close operations.
- Map accepted connections to shared TCP service slots in WASM.
- Route `WEB TRANSMIT CODE/FILE/PAGE/CSS/JS/IMAGE` through the existing shared
  helpers where possible.
- Preserve configured listener after `RUN`.

Exit gates:

- `tcp-server` browser/proxy suite passes, including
  `tcp_server_preserved_after_run`.

### Phase 5 - TFTP

- Decide whether shared TFTP over proxied UDP is stable enough; use that unless
  timing proves impractical.
- Add TFTP conformance coverage in proxy mode.

Exit gates:

- TFTP WRQ/RRQ round trip passes.

### Phase 6 - Telnet Console

- Add proxy Telnet listener and console byte bridge.
- Define input ownership when browser keyboard and Telnet are both active.
- Route Telnet output through existing console output hooks.

Exit gates:

- Telnet console round trip passes from the browser/proxy harness.

### Phase 7 - Plain MQTT Over Proxy

- Add proxy-backed plain MQTT transport.
- Keep browser MQTT-over-WebSocket as static mode behavior.
- Route shared MQTT command parser/wire helpers through the proxy transport
  where possible.

Exit gates:

- Plain MQTT conformance passes through the proxy.
- Existing MQTT-over-WebSocket smoke still passes without proxy.

### Phase 8 - Full Conformance Harness

- Add `porttools/wasm_proxy_network_conformance.py` or extend the existing
  browser smokes to run all suites through the page.
- The harness should start:
  - `ports/host_native/build/wasm_network_proxy`
  - any local peer endpoints needed for tests
  - headless Chromium
- It should drive BASIC through the browser app and collect output through the
  WASM filesystem bridge, mirroring existing smoke style.

Exit gates:

- `python3.11 porttools/wasm_proxy_network_conformance.py all` passes.
- Static smokes still pass without the proxy.
- `make -C ports/host_native wasm-proxy` passes.
- `ports/host_wasm/build.sh` passes.

## Open Questions

- Should the C proxy share helper code with `host_sim_server.c`, or should
  both only share Mongoose and keep separate event handlers?
- Should proxy mode be enabled by URL query (`?proxy=1`), by caps auto-detect,
  or both?
- Should public proxy deployments require an auth token even for same-origin
  pages?
- How much TLS behavior should be exposed through BASIC, given the hardware
  paths currently focus on plain TCP for HTTP-style requests?
- For Telnet, should remote console input be exclusive while a Telnet client is
  attached, or should browser and Telnet input be merged?

## Non-Goals

- Making GitHub Pages bypass browser CORS.
- Running an unauthenticated public open proxy.
- Changing the public BASIC network grammar.
- Replacing real hardware network tests; proxy mode is a browser parity path,
  not a substitute for ESP32/Pico board verification.
