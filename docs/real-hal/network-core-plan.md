# Network Core Refactor Plan

**Goal:** move the BASIC-visible TCP/UDP/NTP/MQTT/WEB surface into a common
core, with narrow HAL backends for Pico/CYW43, ESP32/ESP-IDF, host-native
POSIX, and host-wasm browser networking.

This plan is based on the current ESP32 implementation and the existing Pico
WEB implementation. The immediate target is behavioral parity for the current
BASIC commands, not a new network API.

## Current Handoff Status - May 11, 2026

Do not restart this refactor from the inventory below. A large part of the
command-surface extraction has already happened.

Completed or materially advanced:

- `hal/hal_net.h` exists and is compile-checked across ports.
- Host-native has a real POSIX `hal_net` backend and BASIC WEB surface for TCP
  client/server, UDP, NTP, plain MQTT, shared HTTP transmit helpers, and TFTP.
- ESP32 has a real `hal_net` backend for WiFi connect/scan/status/IP, TCP
  server, TCP client, TCP stream, UDP server/send, NTP transport, TFTP
  transport, and plain MQTT over ESP-IDF MQTT.
- Pico WEB-family builds already consume many shared command/helper modules:
  top-level `WEB` dispatch, network option parsing, `MM.INFO(...)` selector
  parsing, TCP client parser, TCP server parser, UDP parser, MQTT parser, WiFi
  scan/credential parser, HTTP transmit parser, HTTP status/MIME/header/file
  helpers, HTTP PAGE renderer, shared counted-string message buffers, shared
  interrupt flag owner, and shared TFTP option parsing.
- ESP32, host-native, and Pico WEB-family builds use
  `shared/net/mm_net_mqtt_hal_cmd.c` as the shared HAL-backed MQTT command
  executor. Pico keeps lwIP MQTT as the backend implementation behind
  `hal_net_mqtt_*`.
- ESP32, host-native, and Pico WEB-family builds use
  `shared/net/mm_net_tftp.c` as the TFTP protocol core; backends only supply
  UDP send/receive and file callbacks.
- ESP32 wrapper shrink has landed for the WEB transport adapters:
  `esp32_tftp.c`, `esp32_ntp.c`, `esp32_udp.c`, `esp32_mqtt.c`,
  `esp32_tcp_server.c`, and `esp32_tcp_client.c` now own the port-local
  command/service glue that used to live in `esp32_wifi.c`.
- ESP32 and host-native now implement `OPTION TELNET CONSOLE` service behavior
  over the HAL TCP server API. ESP32 owns the service in `esp32_telnet.c`;
  host-native has a loopback listener with `MMBASIC_HOST_TELNET_PORT` /
  `HOST_TELNET_PORT` override support for non-privileged tests.
- ESP32 and host-native use `shared/net/mm_net_service.c` for HAL-backed TCP
  server slot bookkeeping, pending-request state, TCP path extraction,
  read/send/close helpers, and UDP last-message/address receive handling.
- Pico WEB-family builds now also use `shared/net/mm_net_service.c` plus the
  lwIP raw HAL for HTTP/TCP server slot open/poll/read/send/close/path/request
  handling. `OPTION TELNET CONSOLE` now uses the same lwIP raw HAL generic TCP
  server accept/receive/send primitives.
- The legacy `tcp_free_recv_buffers()` / `tcp_realloc_recv_buffers()` hook
  contract has been removed from core autosave paths and port stubs; Pico WEB
  receive buffers are owned by the shared TCP service slots.
- Pico `wifi_includes.h` is gone. lwIP/CYW43 includes now live in the Pico
  source files and backend that use them directly, shared interrupt globals
  use `shared/net/mm_net_interrupts.h`, and dead `NTP_T`/`TCP_CLIENT_T`
  compatibility shells are gone.
- Pico WEB-family TFTP now uses the shared TFTP core over the lwIP raw HAL UDP
  socket API. The old lwIP `tftp.c` server is no longer linked by Pico
  WEB-family builds and has been removed from the tree.
- Pico WEB-family WiFi connect, scan, status, TCP/IP status, and IP address
  reporting now route through `hal_net_wifi_*` on the lwIP raw HAL. Option
  persistence and user-facing startup messages remain in `shared/net/MMsetwifi.c`.
- Host-native conformance covers the BASIC WEB surface without board hardware,
  including interrupt-driven TCP server behavior, TCP client request/stream,
  UDP send/receive, fake NTP, plain MQTT, shared HTTP transmit helpers, and
  TFTP WRQ/RRQ.
- Host-native Telnet has been smoke-tested on loopback with `nc`: enabling
  `OPTION TELNET CONSOLE ON`, sending `PRINT 7+8` over the Telnet socket, and
  receiving `15` plus the prompt.
- Host-native BASIC network conformance now includes a Telnet console
  round-trip using a pseudo-terminal-driven REPL and an override Telnet port.
- ESP32 serial conformance has verified TCP client/server, UDP, TFTP, Telnet,
  NTP, and MQTT on hardware after the Telnet parity slice. A full `all` run
  passed on `/dev/cu.usbmodem101` after flashing the current ESP32 build, and
  a fresh full `all` run passed again after a physical board reset on May 11,
  2026.
- Host-wasm now has a WASM-specific WEB hook surface (`host_wasm_web.c`) that
  does not link host-native POSIX sockets. Browser-supported HTTP
  `WEB TCP CLIENT REQUEST` runs through Emscripten fetch; raw WEB
  stream/server/UDP/TFTP/Telnet commands and options fail explicitly, and
  `MM.INFO(TCPIP STATUS)` / `MM.INFO(WIFI STATUS)` report
  `HAL_NET_UNSUPPORTED`.
- Host-wasm has a browser smoke for the fetch plus unsupported raw-socket
  surface:
  `host/web/smoke_network_unsupported.mjs` boots the shipping app, runs a BASIC
  probe through the REPL, verifies unsupported/offline `MM.INFO(...)` values,
  verifies same-origin fetch-backed GET and POST `WEB TCP CLIENT REQUEST`
  behavior including allowed headers/request body delivery, and checks explicit
  raw request, stream, `WEB`, and `OPTION TELNET CONSOLE` errors.
- Host-wasm now supports browser MQTT-over-WebSocket for `WEB MQTT CONNECT`,
  `SUBSCRIBE`, `PUBLISH`, `UNSUBSCRIBE`, and `CLOSE` through the shared MQTT
  command parser and MQTT wire helpers. The WebSocket is owned by the
  Emscripten main runtime worker so browser socket callbacks can fire while the
  interpreter pthread is sleeping.
- `host/web/smoke_network_mqtt_ws.mjs` boots the shipping app and a tiny local
  WebSocket MQTT broker, then verifies BASIC can receive a subscribed publish
  and publish a QoS 0 message back to the broker.
- Local Pico WEB-family build gates pass for `WEB`, `WEBRP2350`,
  `VGAWIFIRP2350`, and `DVIWIFIRP2350` using the local Pico SDK and ARM GCC
  toolchain after the shared network and WASM fetch changes.

Important distinction:

- Pico is not untouched. It already shares much of the BASIC command parsing
  and HTTP/helper code.
- Pico's core WEB transports now route through
  `drivers/net_lwip_raw/hal_net_lwip.c` and shared service code: WiFi, UDP,
  NTP, TCP client/stream, MQTT, the BASIC HTTP/TCP server path, and Telnet.
  The next Pico milestone is cleanup and hardware smoke: keep shrinking the
  legacy WEB files, remove dead compatibility glue, and avoid redoing
  parser/helper extraction.

Testing policy from hardware experience:

- Prefer large implementation batches before invoking expensive/flaky serial
  hardware tests. Do not run the full ESP32 serial `all` suite after every
  small edit.
- Cheap gates to run frequently: `git diff --check`, `python3.11 -m
  py_compile porttools/network_conformance.py` after harness edits,
  `make -C ports/host_native ...`, `make -C ports/host_native net-hal-test`,
  `porttools/host_network_conformance.py`, and
  `porttools/host_basic_network_conformance.py --no-build`.
- Medium gates after a meaningful batch: ESP32 build, Pico WEB-family builds.
- Expensive hardware gate after a large batch: ESP32 serial conformance.
  Prefer targeted suites first for changed areas, then one full `all` pass
  with `--suite-timeout` once the targeted suites pass.
- When a serial harness issue is discovered, fix the harness/tooling and
  document it before repeating the same fragile workflow. Current fixes include
  suite-level watchdogs and longer prompt-sync timeouts after reset/reopen.

Next large batches, in priority order:

1. Pico hardware-smoke batch: the WEB transports now route through HAL/shared
   code; run board smoke when hardware is available and fix any hardware-only
   regressions.
2. ESP32 Telnet follow-up batch: serial conformance coverage exists and passed
   on hardware. Remaining work here is cleanup only if future board smoke finds
   Telnet-only console edge cases.
3. WASM browser-native client batch: the raw-socket unsupported surface,
   same-origin HTTP fetch path, and MQTT-over-WebSocket path have landed. Next
   is extra fetch/WebSocket hardening for cross-origin/CORS browser limits,
   without adding a hidden backend requirement to the static demo.

## Current Surface Inventory

User-visible commands and functions:

| Surface | BASIC entry points | Current owners |
|---|---|---|
| WiFi setup | `OPTION WIFI`, `WEB CONNECT`, `WEB SCAN [array%()]`, `OPTION SSID$`, `MM.INFO(WIFI STATUS)`, `MM.INFO(TCPIP STATUS)`, `MM.INFO(IP ADDRESS)` | Pico: `shared/net/MMsetwifi.c` + CYW43. ESP32: `esp32_wifi.c` + ESP-IDF. |
| TCP server | `OPTION TCP SERVER PORT`, `WEB TCP INTERRUPT`, `WEB TCP READ`, `WEB TCP SEND`, `WEB TCP CLOSE`, `MM.INFO(TCP REQUEST n)`, `MM.INFO(TCP PORT)`, `MM.INFO(MAX CONNECTIONS)`, currently ESP32-only `MM.INFO(TCP PATH n)` | Pico: `shared/net/MMtcpserver.c`. ESP32: `esp32_tcp_server.c`. |
| HTTP helpers | `WEB TRANSMIT CODE`, `FILE`, `PAGE`, ESP32-only aliases `CSS`, `JS`/`JAVASCRIPT`, `IMAGE` | Pico: `shared/net/MMtcpserver.c`. ESP32: `esp32_tcp_server.c` plus shared HTTP helpers. |
| TCP client | `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`, `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, `WEB CLOSE TCP CLIENT` | Pico: `shared/net/MMTCPclient.c`. ESP32: `esp32_tcp_client.c`. |
| UDP | `OPTION UDP SERVER PORT`, `WEB UDP INTERRUPT`, `WEB UDP SEND`, `MM.INFO(UDP PORT)`, `MM.MESSAGE$`, `MM.ADDRESS$` | Pico: `shared/net/MMudp.c`. ESP32: `esp32_udp.c`. |
| NTP | `WEB NTP [offset [, server [, timeout]]]` updates `DATE$` / `TIME$` through `TimeOffsetToUptime` | Pico: `shared/net/MMntp.c`. ESP32: `esp32_ntp.c`. |
| MQTT | `WEB MQTT CONNECT`, `PUBLISH`, `SUBSCRIBE`, `UNSUBSCRIBE`, `CLOSE`, `MM.TOPIC$`, `MM.MESSAGE$` | Pico: `shared/net/MMMqtt.c` adapter + lwIP MQTT HAL. ESP32: `esp32_mqtt.c` + ESP-IDF MQTT HAL. |
| Adjacent WEB features | `OPTION WEB MESSAGES`, `OPTION TELNET CONSOLE`, `OPTION TFTP`, TFTP/Telnet server paths | Pico, ESP32, and host-native now implement TFTP/Telnet service paths; host-native uses override ports for non-privileged loopback tests. |

State currently shared through globals:

- Interrupt flags: `TCPreceived`, `TCPreceiveInterrupt`, `UDPreceive`,
  `UDPinterrupt`, `MQTTComplete`, `MQTTInterrupt`.
- Last-message buffers: `messagebuff`, `addressbuff`, `topicbuff`.
- Option fields: `SSID`, `PASSWORD`, `hostname`, `ipaddress`, `mask`,
  `gateway`, `TCP_PORT`, `ServerResponceTime`, `UDP_PORT`,
  `UDPServerResponceTime`, `Telnet`, `disabletftp`.
- TCP server slots: `MaxPcb` on Pico, `ESP32_MAX_PCB` on ESP32, with
  per-slot pending flag, receive buffer, send path, and close state.

## Current File Shape

Pico WEB ports link this source set:

- `shared/net/MMsetwifi.c`: option parsing, `cmd_web`, WiFi scan/connect, print options,
  `MM.INFO(...)` network dispatch.
- `shared/net/MMtcpserver.c`: TCP listener slots, HTTP transmit helpers, TCP interrupt,
  read/send/close.
- `shared/net/MMTCPclient.c`: TCP client connect/request/stream/close.
- `shared/net/MMudp.c`: UDP listener/send and `MM.MESSAGE$` / `MM.ADDRESS$` updates.
- `shared/net/MMntp.c`: NTP request/response over UDP.
- `shared/net/MMMqtt.c` + `shared/net/mqtt.c`: thin MQTT command adapter and lwIP MQTT backend.

ESP32 started with one monolithic port file, but that is no longer the current
shape:

- `ports/esp32_s3_metro/main/hal_net_esp32.c`: current owner for WiFi
  association/scan/status/IP, sockets, TCP server/client/stream transport, UDP
  transport, TFTP UDP transport, and ESP-IDF MQTT protocol operations.
- `ports/esp32_s3_metro/main/esp32_wifi.c`: still owns option persistence,
  user-facing connect/status messages, `cmd_web` adapter callbacks, and service
  startup after WiFi connect. TCP/UDP/TFTP/NTP/MQTT/Telnet transport glue now
  lives in focused port-local adapter files.

Host-native and host-wasm currently differ intentionally:

- Native host has shared network state and HTTP helpers linked, plus a
  POSIX `hal_net` backend for loopback TCP server/client and UDP.
- WASM links a browser-specific no-raw-socket WEB surface. Browser networking
  cannot expose raw TCP or UDP directly; future work can add browser-native
  client operations such as `fetch` and WebSocket-based protocols.

## Design

Split the network stack into three layers:

```
MMBasic command parser and long-string glue
    shared/net/mm_net_basic.c
    shared/net/mm_net_http.c
    shared/net/mm_net_mqtt_cmd.c
    shared/net/mm_net_ntp.c
    shared/net/mm_net_state.c

Network HAL contract
    hal/hal_net.h

Backend implementations
    drivers/net_lwip_raw/hal_net_lwip.c
    ports/esp32_s3_metro/main/hal_net_esp32.c
    ports/host_native/hal_net_posix.c
    ports/host_wasm/hal_net_wasm.c
    drivers/net_stub/hal_net_stub.c
```

The shared core owns BASIC syntax and user-visible state. Backends own packet
I/O, WiFi association, DNS, scheduling, and platform-specific socket/MQTT
objects.

### Shared Core Responsibilities

Move these out of both Pico and ESP32 backends:

- `cmd_web` dispatch for `CONNECT`, `SCAN`, `NTP`, `UDP`, `MQTT`, `OPEN TCP`,
  `TCP CLIENT`, `TCP`, and `TRANSMIT`.
- `OPTION WIFI`, `OPTION TCP SERVER PORT`, `OPTION UDP SERVER PORT`,
  `OPTION WEB MESSAGES` parsing and print-option formatting.
- `MM.INFO(...)` network cases.
- Long-string array validation and copy-in/copy-out for TCP read, TCP client
  request, scan output, and stream buffers.
- TCP server slot bookkeeping at the BASIC level: slot count, pending request
  bit, copied request bytes, path extraction, and interrupt flag.
- HTTP transmit helpers: status response, file response, MIME inference,
  template `PAGE` expansion, and aliases `CSS`, `JS`, `IMAGE`.
- UDP last-message/address buffers and interrupt flag.
- MQTT last-topic/message buffers and interrupt flag.
- NTP packet construction/parsing and date/time update. NTP should use the UDP
  HAL, not a port-specific NTP implementation.
- Runtime teardown hooks: `cleanserver`, `close_tcpclient`, and
  `port_web_clear_runtime_state`.

`MM.INFO(TCP PATH n)` should become shared behavior. It is the parsed URL path
from the first line of the pending HTTP request on TCP server slot `n`. For
`GET /status.htm HTTP/1.1`, it returns `/status.htm`. This avoids forcing BASIC
programs to parse the request line out of the raw `WEB TCP READ` buffer before
choosing which file or response to transmit. ESP32 already implements this; the
common core should add it for Pico and host-native.

`WEB TCP SEND` should match the Pico/legacy stack. The legacy implementation
accepts an integer-array long string and sends `array%(0)` bytes starting at
`array%(1)`, then leaves the connection open. Programs close explicitly with
`WEB TCP CLOSE n`, or use `WEB TRANSMIT ...` helpers when they want the
HTTP-response-and-close path. ESP32 now follows this shared parser contract;
the earlier ESP32 string form that sent and closed was removed from the common
path.

### HAL Responsibilities

Backends provide network facts and byte transport. They must not parse BASIC,
evaluate template expressions, know about integer-array long strings, or call
into the MMBasic evaluator from a network callback.

Host-native parity is tracked as a first-class backend, not as a fallback. The
current POSIX backend supports `HAL_NET_CAP_TCP_SERVER`,
`HAL_NET_CAP_TCP_CLIENT`, `HAL_NET_CAP_TCP_STREAM`, `HAL_NET_CAP_UDP_SERVER`,
`HAL_NET_CAP_UDP_SEND`, and `HAL_NET_CAP_MQTT_PLAIN`; WiFi operations return
`HAL_NET_UNSUPPORTED` instead of fake success because the host backend is wired
network transport, not wireless association. `make -C ports/host_native
net-hal-test` is the host HAL conformance gate and must keep passing as the
shared WEB command layer starts calling through `hal_net.h`. It also covers the
shared HTTP response/file senders and a tiny in-process MQTT broker so WEB
transmit and MQTT regressions are caught without waiting for device flash
cycles.

Shared extraction progress:

- `shared/net/mm_net_http.c`: request-path extraction used by ESP32 TCP server
  path reporting.
- `shared/net/mm_net_ntp.c`: common 48-byte NTP request construction and
  transmit-timestamp parsing, now used by the existing ESP32 and Pico WEB
  `WEB NTP` command bodies.
- `shared/net/mm_net_ntp_hal.c`: HAL-backed UDP NTP exchange helper covered by
  host network conformance; this is the transport core the later shared
  `WEB NTP` command body should call.
- `shared/net/mm_net_mqtt_cmd.c`: common BASIC argument parsing for
  `WEB MQTT CONNECT`, `PUBLISH`, `SUBSCRIBE`, and `UNSUBSCRIBE`, now used by
  Pico WEB-family, host-native, and ESP32.
- `shared/net/mm_net_mqtt_hal_cmd.c`: common HAL-backed MQTT command executor
  for connect/publish/subscribe/unsubscribe/close, now used by host-native,
  ESP32, and Pico WEB-family builds. Pico keeps lwIP MQTT as the backend
  implementation behind `hal_net_mqtt_*`.
- `shared/net/mm_net_tcp_client_cmd.c`: common BASIC argument parsing,
  long-string destination setup, and stream pointer validation for
  `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`, `WEB OPEN TCP STREAM`, and
  `WEB TCP CLIENT STREAM`, now used by Pico WEB-family builds and ESP32 with
  HAL-backed transport.
- `shared/net/mm_net_tcp_server_cmd.c`: common BASIC argument parsing and
  long-string setup for `WEB TCP INTERRUPT`, `WEB TCP READ`, `WEB TCP SEND`,
  and `WEB TCP CLOSE`, now paired with `shared/net/mm_net_service.c` for
  HAL-backed slot state and socket transport on host-native, ESP32, and Pico
  WEB-family builds.
- `shared/net/mm_net_udp_cmd.c`: common BASIC argument parsing for
  `WEB UDP INTERRUPT` and `WEB UDP SEND`, now used with HAL-backed UDP
  transport on host-native, ESP32, and Pico WEB-family builds.
- `shared/net/mm_net_options.c`: common parsing for `OPTION TCP SERVER PORT`,
  `OPTION UDP SERVER PORT`, `OPTION WEB MESSAGES`, and WEB `MM.INFO(...)`
  selectors. Pico WEB-family builds now also expose `MM.INFO(TCP PATH n)` via
  the shared HTTP path extractor.
- `shared/net/mm_net_transmit_cmd.c`: common BASIC argument parsing for
  `WEB TRANSMIT CODE`, `FILE`, `PAGE`, `CSS`, `JS`/`JAVASCRIPT`, and `IMAGE`.
  ESP32 and Pico WEB-family builds use the same parser; Pico WEB-family builds
  now accept the transmit aliases that were previously ESP32-only.
- `shared/net/mm_net_http.c`: now also owns MIME inference, common status
  reason/body strings, HTTP response header formatting, and the callback-based
  in-memory HTTP response sender used by ESP32 and Pico transmit helpers.
- `shared/net/mm_net_http_file.c`: common `WEB TRANSMIT FILE` stat/open/header
  and chunked read sender over `hal_fs` plus a backend send callback. ESP32 and
  Pico WEB-family builds now use the shared file sender.
- `shared/net/mm_net_http_page.c`: common `WEB TRANSMIT PAGE` template
  renderer over `hal_fs`, including `{...}` expression evaluation. ESP32 and
  Pico WEB-family builds now call this shared renderer; their socket send paths
  still keep backend-local framing/chunking while that transport layer is being
  extracted.
- `shared/net/mm_net_wifi_cmd.c`: common long-string integer-array setup for
  `WEB SCAN [array%()]`, plus common credential argument parsing for
  `OPTION WIFI` / `WEB CONNECT ...`; now used by ESP32 and Pico WEB-family
  builds.
- `shared/net/mm_net_web_cmd.c`: common top-level `WEB` command dispatch for
  `CONNECT`, `SCAN`, `MQTT`, TCP client, `TRANSMIT`, TCP server, `NTP`, and
  UDP. Host-native, ESP32, and Pico WEB-family builds now use this dispatcher
  so command ordering and connected-state checks do not drift while backend
  bodies are still being extracted.

Proposed first-pass contract:

```c
typedef enum {
    HAL_NET_OK = 0,
    HAL_NET_ERR = -1,
    HAL_NET_UNSUPPORTED = -2,
    HAL_NET_TIMEOUT = -3,
    HAL_NET_WOULD_BLOCK = -4,
} hal_net_result_t;

typedef uint16_t hal_net_tcp_server_t;
typedef uint16_t hal_net_tcp_conn_t;
typedef uint16_t hal_net_tcp_client_t;
typedef uint16_t hal_net_udp_socket_t;
typedef uint16_t hal_net_mqtt_client_t;

typedef struct {
    uint8_t bytes[16];
    uint8_t family;      /* 4 or 6 */
    uint16_t port;
} hal_net_addr_t;

typedef struct {
    uint32_t raw;
    const char *name;
} hal_net_capability_t;

enum {
    HAL_NET_CAP_WIFI_SCAN       = 1u << 0,
    HAL_NET_CAP_WIFI_CONNECT    = 1u << 1,
    HAL_NET_CAP_TCP_SERVER      = 1u << 2,
    HAL_NET_CAP_TCP_CLIENT      = 1u << 3,
    HAL_NET_CAP_TCP_STREAM      = 1u << 4,
    HAL_NET_CAP_UDP_SERVER      = 1u << 5,
    HAL_NET_CAP_UDP_SEND        = 1u << 6,
    HAL_NET_CAP_MQTT_PLAIN      = 1u << 7,
    HAL_NET_CAP_MQTT_TLS        = 1u << 8,
    HAL_NET_CAP_HTTP_FETCH      = 1u << 9,   /* WASM/browser-native */
    HAL_NET_CAP_MQTT_WEBSOCKET  = 1u << 10,  /* WASM/browser-native */
};

uint32_t hal_net_capabilities(void);
int hal_net_init(void);
void hal_net_poll(void);

int hal_net_wifi_set_credentials(const char *ssid, const char *pass,
                                 const char *host, const char *ip,
                                 const char *mask, const char *gw);
int hal_net_wifi_connect(uint32_t timeout_ms);
int hal_net_wifi_status(void);
int hal_net_tcpip_status(void);
int hal_net_ip_address(char *out, size_t out_len);
int hal_net_wifi_scan(char *out, size_t out_len, size_t *written,
                      int print_to_console);

int hal_net_tcp_server_open(uint16_t port, int backlog,
                            hal_net_tcp_server_t *out);
int hal_net_tcp_server_close(hal_net_tcp_server_t server);
int hal_net_tcp_accept_event(hal_net_tcp_server_t server,
                             hal_net_tcp_conn_t *conn,
                             uint8_t *buf, size_t cap, size_t *len);
int hal_net_tcp_conn_send(hal_net_tcp_conn_t conn, const void *buf, size_t len,
                          uint32_t timeout_ms);
int hal_net_tcp_conn_close(hal_net_tcp_conn_t conn);

int hal_net_tcp_client_open(const char *host, uint16_t port,
                            uint32_t timeout_ms, hal_net_tcp_client_t *out);
int hal_net_tcp_client_send(hal_net_tcp_client_t client, const void *buf,
                            size_t len, uint32_t timeout_ms);
int hal_net_tcp_client_recv(hal_net_tcp_client_t client, void *buf,
                            size_t cap, size_t *len, uint32_t timeout_ms);
int hal_net_tcp_client_close(hal_net_tcp_client_t client);

int hal_net_udp_bind(uint16_t port, hal_net_udp_socket_t *out);
int hal_net_udp_close(hal_net_udp_socket_t sock);
int hal_net_udp_socket_send(hal_net_udp_socket_t sock, const char *host,
                            uint16_t port,
                            const void *buf, size_t len, uint32_t timeout_ms);
int hal_net_udp_send(const char *host, uint16_t port,
                     const void *buf, size_t len, uint32_t timeout_ms);
int hal_net_udp_recv_event(hal_net_udp_socket_t sock, hal_net_addr_t *from,
                           void *buf, size_t cap, size_t *len);

int hal_net_mqtt_connect(const char *host, uint16_t port, const char *user,
                         const char *pass, const char *client_id,
                         uint32_t timeout_ms, hal_net_mqtt_client_t *out);
int hal_net_mqtt_publish(hal_net_mqtt_client_t client, const char *topic,
                         const void *payload, size_t len, int qos, int retain);
int hal_net_mqtt_subscribe(hal_net_mqtt_client_t client, const char *topic,
                           int qos, uint32_t timeout_ms);
int hal_net_mqtt_unsubscribe(hal_net_mqtt_client_t client, const char *topic,
                             uint32_t timeout_ms);
int hal_net_mqtt_recv_event(hal_net_mqtt_client_t client, char *topic,
                            size_t topic_cap, void *payload,
                            size_t payload_cap, size_t *payload_len);
int hal_net_mqtt_close(hal_net_mqtt_client_t client);
```

This shape deliberately supports both polling and event-driven backends.

- Pico/lwIP raw callbacks append to backend queues; `hal_net_poll()` calls
  `cyw43_arch_poll()` and drains those queues.
- ESP32 FreeRTOS tasks append to backend queues; `hal_net_poll()` can be cheap.
- Native host uses nonblocking POSIX sockets and `select`/`poll`.
- WASM uses JS promises/events to append to queues, then `hal_net_poll()` drains
  them when the interpreter yields.

No multicore requirement is introduced. Network progress is driven by the
existing interpreter poll points (`CheckAbort`, `routinechecks`, `PAUSE`,
`MMInkey`, and explicit blocking waits).

### MQTT Strategy

Do not start by writing a new MQTT wire client. Preserve behavior first.

The first MQTT-specific refactor should make MQTT command parsing shared but
keep protocol handling in the backend:

- Pico backend wraps lwIP MQTT.
- ESP32 backend wraps ESP-IDF MQTT.
- Native host can use a small POSIX MQTT backend or a test-only backend.
- WASM can support MQTT over WebSocket through JS, because browsers cannot open
  raw TCP port 1883.

After Pico, ESP32, and host-native pass the same conformance tests, consider a
second pass that moves MQTT 3.1.1 packet encode/decode into shared code on top
of `hal_net_tcp_client_*`. That is attractive for native host and ESP32, but
it still does not solve browser raw TCP. WASM should use MQTT over WebSocket
or report the MQTT transport unsupported.

### WASM Constraints

Browser WASM cannot implement raw TCP connect semantics behind
`WEB OPEN TCP CLIENT "host",port`, nor can it implement raw TCP server or UDP
directly. It can treat `OPEN TCP CLIENT` plus a parseable HTTP
`TCP CLIENT REQUEST` as a browser fetch endpoint. This plan intentionally does
not support a proxy or relay mode. The WASM backend is client-only and
browser-native only.

Host-wasm backend behavior:

- Current landed behavior: simple HTTP `WEB TCP CLIENT REQUEST` calls are
  represented through browser fetch, including browser-allowed request headers
  and request bodies, while raw WEB stream/server/UDP/TFTP/Telnet commands and
  options report unsupported through the WASM-specific hook surface. Basic
  network status `MM.INFO(...)` returns unsupported/empty values instead of
  pretending a loopback stack exists. Browser smoke coverage exists in
  `host/web/smoke_network_unsupported.mjs`.
- Future MQTT over WebSocket: supported if the broker exposes `ws://` or
  `wss://`.
- Raw TCP server, raw TCP stream, raw non-HTTP TCP client traffic, and raw UDP:
  return `HAL_NET_UNSUPPORTED`.
- `WEB TCP CLIENT REQUEST` may be supported only when the request is parseable
  as a simple HTTP request and can be represented by `fetch`. Otherwise it
  errors with a clear unsupported message.

The common core must consult `hal_net_capabilities()` before accepting commands
whose semantics a backend cannot honor.

## Migration Plan

### Phase 0 - Lock The Surface With Tests

Before moving code, extend `porttools/` with a network conformance runner:

- TCP server smoke: load a BASIC web server, fetch from Mac, verify status,
  headers, file response, template `PAGE`, `CSS`, `JS`, `IMAGE`, `TCP READ`,
  `TCP SEND`, `TCP CLOSE`, `MM.INFO(TCP REQUEST)`, and `MM.INFO(TCP PATH)`
  where available.
- TCP client smoke: keep `esp32_tcp_smoke.py` behavior and make it portable
  across ESP32/Pico/native host.
- UDP smoke: Mac UDP listener and sender; verify `WEB UDP SEND`,
  `WEB UDP INTERRUPT`, `MM.MESSAGE$`, `MM.ADDRESS$`.
- NTP smoke: local fake NTP responder to avoid public network dependency.
- MQTT smoke: local plain MQTT broker or a tiny test broker; verify connect,
  publish, subscribe, unsubscribe, close, `MM.TOPIC$`, `MM.MESSAGE$`.
- Capability smoke: host-wasm unsupported raw TCP/UDP paths must fail cleanly;
  browser-native fetch/MQTT-over-WebSocket paths get separate tests.

Exit gate: current ESP32 hardware smokes still pass; host-native conformance
smokes pass without board hardware; Pico WEB builds still build, with Pico
hardware smoke deferred until hardware is available.

Started:

- Added `porttools/network_conformance.py` as the first cross-port runner. It
  currently covers the existing TCP client smoke, TCP server read/path/send/close
  behavior, UDP send/receive behavior, and fake-NTP date/time behavior against
  host-side sockets. It also includes a tiny local MQTT 3.1.1 broker for the
  plain-TCP MQTT surface.
- Documented the runner in `porttools/README.md`.
- Hardened the runner from ESP32 hardware feedback: quote-preserving BASIC file
  upload, echoed-command-safe marker parsing, guarded TCP stream reads,
  client-state settle pauses, bounded serial reopen/resync without default RTS
  resets, and optional suite retries.
- Added the first `hal_net.h` skeleton and no-network stub backend ahead of the
  shared-core extraction, wired into the native host build so the contract stays
  compile-checked while the real backends are split out.
- Host-native now exposes a first BASIC `WEB` command slice backed by the POSIX
  `hal_net`: `WEB CONNECT` as network init, `WEB OPEN TCP CLIENT`, `WEB TCP
  CLIENT REQUEST`, `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, `WEB CLOSE
  TCP CLIENT`, `WEB UDP SEND`, `OPTION UDP SERVER PORT` receive state,
  `MM.MESSAGE$`, `MM.ADDRESS$`, `WEB NTP`, TCP server request/read/send/close,
  `WEB MQTT CONNECT`/`PUBLISH`/`SUBSCRIBE`/`UNSUBSCRIBE`/`CLOSE`, shared
  `WEB TRANSMIT CODE`/`FILE`/`PAGE` and aliases, and WEB-related
  `MM.INFO(...)` selectors for IP/TCPIP/ports/request state/path. WiFi
  scan/connect still reports unsupported at the BASIC layer. Host-native also
  has a POSIX UDP TFTP server for `OPTION TFTP ON` backed by `hal_fs`, with an
  unprivileged `MMBASIC_HOST_TFTP_PORT` override used by conformance tests
  instead of binding UDP/69.
- Host-native BASIC conformance now runs as part of
  `porttools/host_network_conformance.py`; it covers TCP client request, TCP
  client stream, UDP send, UDP receive/message/address, fake NTP, plain MQTT,
  TCP server read/send/close/path, shared transmit helpers, host `OPTION LIST`,
  and a host TFTP WRQ/RRQ roundtrip. The same pass caught and locked down host
  `hal_fs_stat()` parity with drive-prefixed paths under `--sd-root`, so
  PAGE/FILE transmit failures do not recur.
- Added `shared/net/mm_net_state.c` / `.h` for the MMBasic-format
  message/address/topic buffers and moved the native host `port_fun_mm_mqtt_copy`
  hook onto that shared state. ESP32 and Pico WEB-family MQTT/UDP receive paths
  now write through the shared state helpers and link the same buffer owner.
- Added `shared/net/mm_net_interrupts.c` / `.h` as the single strong owner for
  TCP/UDP/MQTT interrupt flags and callback pointers. RAM-tight non-WEB device
  builds link only this small interrupt owner; WEB/ESP32/host builds link the
  counted-string message buffers separately.
- Host-native now resolves BASIC interrupt targets with the same
  `GetIntAddress()` behavior used by device ports and dispatches WEB TCP/UDP/MQTT
  interrupts from the interpreter poll path. The host BASIC conformance server
  uses `WEB TCP INTERRUPT`, not a polling fallback, so native regressions in the
  interrupt path are caught before flashing ESP32/Pico hardware.
- Added `shared/net/mm_net_service.c` / `.h` for HAL-backed TCP server slot
  state and UDP receive state. Host-native and ESP32 now share TCP server
  open/stop/poll/read/send/close/path/request handling and UDP
  message/address capture, while keeping option persistence, WiFi policy, and
  user-facing messages in their port adapters.
- Added the first Pico `drivers/net_lwip_raw/hal_net_lwip.c` slice. It
  implements HAL UDP send, UDP bind, UDP receive event queueing, DNS resolution
  through lwIP, and capability reporting for Pico WEB-family builds. `shared/net/MMudp.c`
  is now a BASIC-facing wrapper over `hal_net_udp_send()` and the shared UDP
  service poller instead of owning lwIP UDP pcbs directly.
- Pico WEB-family `WEB NTP` now uses `shared/net/mm_net_ntp_hal.c` over the
  lwIP HAL UDP backend instead of owning DNS, UDP pcb setup, receive callbacks,
  and packet parsing in `shared/net/MMntp.c`. The shared NTP helper now calls
  `hal_net_poll()` while waiting so the Pico CYW43 polled stack can make
  progress.
- Pico WEB-family TCP client and TCP stream commands now use the lwIP HAL TCP
  client backend. `shared/net/MMTCPclient.c` is now a BASIC-facing adapter over
  `hal_net_tcp_client_open/send/recv/close`, with stream receive progress
  pumped from `ProcessWeb()`. The lwIP backend owns DNS, `tcp_connect`,
  send/backpressure handling, close/abort cleanup, and a dynamic receive queue.
- Pico WEB-family HTTP/TCP server commands now use `mm_net_tcp_service_t` over
  the lwIP HAL TCP server backend. `shared/net/MMtcpserver.c` keeps the BASIC-facing
  command adapter and HTTP transmit integration, while accept/read/send/close
  and request-path state flow through `hal_net_tcp_server_*`,
  `hal_net_tcp_conn_*`, and `shared/net/mm_net_service.c`. Receive buffers are
  allocated at runtime to keep `DVIWIFIRP2350` BSS within region limits.
- Pico WEB-family WiFi connect, scan, link status, TCP/IP status, and IP
  address reporting now use `hal_net_wifi_set_credentials`,
  `hal_net_wifi_connect`, `hal_net_wifi_scan`, `hal_net_wifi_status`,
  `hal_net_tcpip_status`, and `hal_net_ip_address` on the lwIP HAL backend.
  `shared/net/MMsetwifi.c` remains the option/message/startup adapter.
- Pico WEB-family MQTT commands now use the shared HAL-backed MQTT command
  executor over `hal_net_mqtt_*` in the lwIP HAL backend. `shared/net/MMMqtt.c` is now a
  small adapter that drains incoming MQTT events into shared
  `MM.TOPIC$`/`MM.MESSAGE$` state from `ProcessWeb()`, while `shared/net/mqtt.c` remains
  the lwIP protocol implementation.
- Added `host/web/smoke_network_unsupported.mjs` for the host-wasm capability
  smoke. It verifies the browser build's no-raw-socket contract and
  fetch-backed HTTP client path using the actual shipping app and JS filesystem
  hooks, not a native-host surrogate.
- Added `host/web/smoke_network_mqtt_ws.mjs` for the host-wasm MQTT-over-
  WebSocket path. It uses the actual shipping app plus a local WebSocket MQTT
  broker implemented in the smoke harness, covering subscribe/receive and
  publish from BASIC.
- Ran local Pico WEB-family build gates for `WEB`, `WEBRP2350`,
  `VGAWIFIRP2350`, and `DVIWIFIRP2350` after the latest cleanup; all produced
  `.elf` and `.uf2` artifacts successfully.

### Phase 1 - Extract Shared State And BASIC Glue

Create:

- `shared/net/mm_net_state.c`
- `shared/net/mm_net_state.h`
- `shared/net/mm_net_basic.c`

Move only target-neutral pieces first:

- global interrupt state and last-message/topic/address buffers;
- `port_fun_mm_mqtt_copy`;
- `MM.INFO` network dispatch shell;
- option print/set shell;
- common `cmd_web` dispatch shell;
- TCP path extraction;
- long-string array helpers.

Keep Pico and ESP32 backends behind temporary adapter functions. Do not change
wire behavior in this phase.

Exit gate: no behavior change, same hardware smokes pass.

Started:

- Shared WEB option/MM.INFO selector parsing has moved into
  `shared/net/mm_net_options.c`. ESP32 and Pico WEB-family builds use the same
  parser for TCP/UDP server-port options and WEB-related `MM.INFO(...)`
  selectors.

### Phase 2 - Extract HTTP/TCP Server Helpers

Create `shared/net/mm_net_http.c` and move:

- MIME inference;
- HTTP status response generation;
- `WEB TRANSMIT FILE`;
- `WEB TRANSMIT PAGE` template evaluation;
- `WEB TRANSMIT CSS`, `JS`, `IMAGE`;
- `WEB TCP READ`, `SEND`, `CLOSE` command bodies around common slot state.

Backends supply only:

- open listener;
- report accepted request bytes;
- send bytes on a connection;
- close a connection.

This should delete the duplicated HTTP/file/template code in `esp32_wifi.c`
and the older copy in `shared/net/MMtcpserver.c`.

Exit gate: full website demo still works on ESP32; Pico WEB server smoke still
works or is explicitly marked pending until hardware is available.

Started:

- TCP server command parsing and long-string setup moved into
  `shared/net/mm_net_tcp_server_cmd.c`. Pico WEB-family and ESP32 now share
  the BASIC syntax path for interrupt/read/send/close, including the legacy
  long-string-only `WEB TCP SEND` contract; connection slots and send/receive
  mechanics are still backend-local.
- WEB transmit command parsing moved into `shared/net/mm_net_transmit_cmd.c`,
  and target-neutral MIME/status/header helpers are in `mm_net_http.c`. ESP32
  uses the shared parser and header/MIME helpers; Pico WEB-family uses the
  shared parser and MIME helper while keeping the legacy lwIP send path.
- `WEB TRANSMIT PAGE` template rendering moved into
  `shared/net/mm_net_http_page.c` for ESP32 and Pico WEB-family builds. Pico's
  legacy response framing is preserved around the shared rendered buffer.
- Serial network conformance now exercises `WEB TRANSMIT PAGE`, `FILE`, `CODE`,
  `CSS`, `JS`, and `IMAGE` through the TCP server suite.
- Host-native network conformance now also checks shared HTTP path extraction,
  MIME inference, status reason/body mapping, and response header formatting.
- Serial network conformance retries now catch suite exceptions and perform an
  automatic app-reset sync retry, so transient prompt loss does not abort the
  whole run before the suite retry policy can apply.
- `WEB SCAN [array%()]` array parsing moved into
  `shared/net/mm_net_wifi_cmd.c`, removing duplicate capacity math from Pico
  and ESP32 command handlers.
- `OPTION WIFI` / `WEB CONNECT ...` credential parsing moved into
  `shared/net/mm_net_wifi_cmd.c`; backend-local code now only supplies the
  default hostname, validates static IP strings with its native IP parser, and
  applies the resulting options.
- ESP32 `WEB TRANSMIT FILE` now routes through
  `shared/net/mm_net_http_file.c`; Pico WEB-family `WEB TRANSMIT FILE` now uses
  the same shared file sender through a lwIP callback adapter.
- ESP32 status, PAGE, and in-memory HTTP responses now use the common
  `mm_net_http_send_response` callback path.
- Pico WEB-family `WEB TRANSMIT CODE` now also uses the common status response
  sender through the lwIP callback adapter.

### Phase 3 - Introduce `hal_net.h`

Add `hal/hal_net.h` and a stub backend:

- `drivers/net_stub/hal_net_stub.c`: every function returns
  `HAL_NET_UNSUPPORTED`, no BSS-heavy buffers.

Wire non-network ports to the stub and keep the WEB token out of their token
tables unless the port explicitly opts in.

Port adapters:

- `drivers/net_lwip_raw/hal_net_lwip.c`: lifts CYW43/lwIP raw callback code
  out of `shared/net/MMsetwifi.c`, `shared/net/MMtcpserver.c`, `shared/net/MMTCPclient.c`, `shared/net/MMudp.c`,
  `shared/net/MMntp.c`, and `shared/net/MMMqtt.c`.
- `ports/esp32_s3_metro/main/hal_net_esp32.c`: moves sockets, FreeRTOS tasks,
  WiFi, and ESP-IDF MQTT out of `esp32_wifi.c`.
- `ports/host_native/hal_net_posix.c`: POSIX sockets for native conformance
  tests and optionally for real host use.
- `ports/host_wasm/hal_net_wasm.c`: JS bridge and capability-restricted
  browser client support.

Exit gate: common core links against all four backends and stubs.

Started:

- ESP32 now links a real `hal_net.h` socket backend in
  `ports/esp32_s3_metro/main/hal_net_esp32.c`. The current slice covers WiFi
  association/scan/status/IP reporting, TCP server accept/send/close, TCP
  client connect/send/receive/close, TCP stream transport, UDP bind/send/
  receive, TFTP UDP transport, NTP UDP transport, and ESP-IDF MQTT protocol
  operations. `esp32_wifi.c` is now a BASIC-facing adapter and service
  coordinator rather than the transport owner.
- ESP32 `WEB UDP SEND` now uses `hal_net_udp_send()` through that backend, so
  one BASIC UDP command path is already exercising the transport HAL on real
  hardware.
- Pico WEB-family `WEB UDP SEND`, `OPTION UDP SERVER PORT`, UDP interrupt
  state, `MM.MESSAGE$`, and `MM.ADDRESS$` now route through the lwIP HAL UDP
  backend plus `shared/net/mm_net_service.c`. The four Pico WiFi builds
  (`WEB`, `WEBRP2350`, `VGAWIFIRP2350`, `DVIWIFIRP2350`) build with the new
  backend linked.
- Pico WEB-family `WEB NTP` now uses the same HAL-backed NTP exchange as host
  and ESP32, including optional `host:port` parsing for local fake-NTP tests.
- Pico WEB-family `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`,
  `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, and
  `WEB CLOSE TCP CLIENT` now route through `hal_net_tcp_client_*` on
  `drivers/net_lwip_raw/hal_net_lwip.c`.
- Pico WEB-family `OPTION TCP SERVER PORT`, `WEB TCP INTERRUPT`,
  `WEB TCP READ`, `WEB TCP SEND`, `WEB TCP CLOSE`, `WEB TRANSMIT ...`,
  `MM.INFO(TCP REQUEST n)`, and `MM.INFO(TCP PATH n)` now route through
  `shared/net/mm_net_service.c` and `hal_net_tcp_server_*` /
  `hal_net_tcp_conn_*` on `drivers/net_lwip_raw/hal_net_lwip.c` for the HTTP
  server path.
- Pico WEB-family `OPTION TELNET CONSOLE` now opens its listener through
  `hal_net_tcp_server_*`, accepts with `hal_net_tcp_accept_conn()`, receives
  console input with `hal_net_tcp_conn_recv()`, and sends output with
  `hal_net_tcp_conn_send()` on `drivers/net_lwip_raw/hal_net_lwip.c`.
- Pico WEB-family `WEB CONNECT`, `WEB SCAN [array%()]`,
  `MM.INFO(WIFI STATUS)`, `MM.INFO(TCPIP STATUS)`, and
  `MM.INFO(IP ADDRESS)` now route through `hal_net_wifi_*` on
  `drivers/net_lwip_raw/hal_net_lwip.c`.
- Pico WEB-family `WEB MQTT CONNECT`, `PUBLISH`, `SUBSCRIBE`,
  `UNSUBSCRIBE`, and `CLOSE` now route through
  `shared/net/mm_net_mqtt_hal_cmd.c` and `hal_net_mqtt_*` on
  `drivers/net_lwip_raw/hal_net_lwip.c`, with incoming topic/message state
  drained by `pico_mqtt_poll()`.
- ESP32 TCP server accept/send/close now runs through `hal_net_tcp_server_*`
  and `hal_net_tcp_conn_*`, with `ProcessWeb()` drained from interpreter poll
  points. This removes the ESP32-private TCP listener task from the BASIC WEB
  server path and keeps interrupt delivery aligned with host-native.
- ESP32 TCP client open/request/stream/close now runs through
  `hal_net_tcp_client_*`, including shared stream ring-buffer append semantics.
  The older ESP32 private connect/send/recv path is no longer on the BASIC TCP
  client command path.
- ESP32 UDP server receive and TFTP transport now run through
  `hal_net_udp_bind`, `hal_net_udp_recv_event`, and
  `hal_net_udp_socket_send`. `ProcessWeb()` is also pumped from ESP32
  `MMInkey()` and blocking `MMgetchar()` waits so network services keep
  progressing while the interpreter is idle at the prompt.
- ESP32 WiFi association, scan transport, status, and IP address reporting now
  live in `hal_net_esp32.c` behind `hal_net_wifi_*`. The port-local
  `esp32_wifi.c` layer keeps BASIC credential parsing, option persistence,
  user-facing status messages, and post-connect service startup.

### Phase 4 - Share NTP Over UDP

Move NTP request/parse/update logic into `shared/net/mm_net_ntp.c`:

- build 48-byte NTP request;
- send via `hal_net_udp_socket_send` on a temporary UDP socket so the reply
  returns to the same socket;
- wait for matching response via that temporary UDP socket;
- validate mode/stratum/length;
- update `TimeOffsetToUptime` and `day_of_week`.

Delete ESP32's private NTP command body and Pico's `shared/net/MMntp.c` command body once
the shared version passes the fake-NTP conformance test.

Started:

- ESP32 `WEB NTP` now calls the shared `mm_net_ntp_query_unix_seconds()` helper
  over `hal_net_udp_*` instead of carrying a private socket/send/select/recv
  exchange. The command still owns BASIC argument validation and date/time
  application while the NTP packet and UDP exchange live in shared code.

### Phase 5 - Share TCP Client Semantics

Move command parsing and long-string/ring-buffer handling for:

- `WEB OPEN TCP CLIENT`;
- `WEB TCP CLIENT REQUEST`;
- `WEB OPEN TCP STREAM`;
- `WEB TCP CLIENT STREAM`;
- `WEB CLOSE TCP CLIENT`.

Backends only perform connect/send/receive/close. Stream mode uses a shared
ring-buffer writer fed by backend receive events.

WASM behavior:

- Only parseable HTTP request/response through `fetch`; raw TCP client and
  stream mode return unsupported.

Started:

- TCP client command parsing and long-string/stream argument validation moved
  into `shared/net/mm_net_tcp_client_cmd.c`. Pico WEB-family and ESP32 call the
  shared parser helpers; connect/send/receive mechanics remain backend-local.
- TCP client stream ring-buffer append/overflow semantics are now shared by
  host-native and ESP32 through `mm_net_tcp_client_stream_append()`.
- Host-native now implements both request and stream BASIC paths over the POSIX
  `hal_net` TCP client and exercises stream ring-buffer delivery in host BASIC
  conformance.
- Host-wasm now implements the request path for parseable HTTP requests over
  browser fetch. `WEB OPEN TCP CLIENT` records the browser endpoint,
  `WEB TCP CLIENT REQUEST` synthesizes HTTP response bytes into the BASIC
  long-string buffer, browser-allowed headers/request bodies are forwarded, and
  raw TCP stream mode remains explicitly unsupported.

### Phase 6 - Share UDP Server/Send Semantics

Move `WEB UDP INTERRUPT`, `WEB UDP SEND`, `MM.MESSAGE$`, and `MM.ADDRESS$`
handling into shared code. Backends only bind, send, and report receive events.

Pico and ESP32 should then have identical UDP behavior. Host-native can run
the full UDP smoke without board hardware.

Started:

- UDP command parsing for interrupt setup and send arguments moved into
  `shared/net/mm_net_udp_cmd.c`; Pico WEB-family and ESP32 now share the BASIC
  syntax path while keeping their existing send/listener backends.
- Host-native now binds `OPTION UDP SERVER PORT`, polls receive events through
  `hal_net_udp_recv_event()`, updates the shared `MM.MESSAGE$`/`MM.ADDRESS$`
  buffers, and covers that path in host BASIC conformance.

### Phase 7 - Share MQTT Command Semantics

Move MQTT command parsing and interrupt/message/topic state into
`shared/net/mm_net_mqtt_cmd.c`, but keep protocol operations in HAL:

- `hal_net_mqtt_connect`;
- `publish`;
- `subscribe`;
- `unsubscribe`;
- `recv_event`;
- `close`.

This phase removes duplicate BASIC syntax handling while preserving the stable
lwIP and ESP-IDF protocol clients.

Exit gate: local broker smoke passes on ESP32 and host-native; Pico is smoked
on hardware when available; WASM passes MQTT-over-WebSocket smoke if a broker
with WebSocket transport is configured.

Started:

- MQTT command argument parsing has moved into `shared/net/mm_net_mqtt_cmd.c`.
  Pico WEB-family, host-native, and ESP32 all call the shared parsers; only
  backend protocol operations remain port-local.
- Host-native now has a small POSIX MQTT 3.1.1 backend for plain TCP QoS 0/1
  command smoke coverage and exercises it in both HAL conformance and host
  BASIC conformance with a local broker.
- MQTT remaining-length encoding, UTF-8 string framing, and PUBLISH decoding
  have moved into `shared/net/mm_net_mqtt_wire.c`; the host POSIX MQTT backend
  now uses these shared packet helpers.
- `shared/net/mm_net_mqtt_hal_cmd.c` now owns the BASIC command executor for
  HAL-backed MQTT connect/publish/subscribe/unsubscribe/close. Host-native,
  ESP32, and Pico WEB-family builds use this executor.
- ESP32 plain MQTT now runs through `hal_net_mqtt_*` in `hal_net_esp32.c`.
  ESP-IDF MQTT events are queued in the HAL and drained by `ProcessWeb()` into
  shared `MM.TOPIC$` / `MM.MESSAGE$` state. The full ESP32 serial conformance
  batch passed after this MQTT slice, including the local broker suite.
- Pico plain MQTT now runs through `hal_net_mqtt_*` in
  `drivers/net_lwip_raw/hal_net_lwip.c`. lwIP MQTT events are queued in the HAL
  and drained by `pico_mqtt_poll()` into shared `MM.TOPIC$` / `MM.MESSAGE$`
  state.
- After the later WiFi-HAL slice, standalone ESP32 TFTP and standalone ESP32
  MQTT both passed with the fixed harness. A full `all` serial pass progressed
  through TCP client, TCP server, UDP, TFTP, and NTP; the MQTT entry failed
  before issuing MQTT commands because prompt sync after reset/reopen was too
  short. The harness now uses a longer prompt-sync timeout and suite watchdogs.

### Phase 8 - Optional Shared MQTT Wire Client

Only after the command-surface refactor is stable:

- implement MQTT 3.1.1 encode/decode in shared code over
  `hal_net_tcp_client_*`;
- use it for host-native and possibly ESP32;
- leave Pico on lwIP MQTT if memory/code size remains better;
- leave WASM on MQTT-over-WebSocket because raw TCP is unavailable.

This phase is optional. It should not block common BASIC behavior.

### Phase 9 - Telnet Console And TFTP Parity

Bring the adjacent WEB service surface to ESP32 and host-native after the core
TCP/UDP/MQTT behavior is shared:

- `OPTION TELNET CONSOLE OFF|ON|ONLY`.
- Telnet console listener and console routing.
- `OPTION TFTP OFF|ON|ENABLE|DISABLE`.
- TFTP read/write server over the same file HAL used by A:.

Pico originally owned the reference behavior in `shared/net/MMtelnet.c`, `tftp.c`, and
`shared/net/MMtftp.c`. Pico Telnet now runs over the lwIP raw HAL generic TCP
accept/receive/send path, and Pico TFTP now runs through the shared TFTP core
over the lwIP raw HAL UDP socket API. ESP32 should implement the Telnet service
backend with ESP-IDF sockets or the existing ESP32 TCP HAL. Host-native should
implement it with POSIX sockets so the conformance suite can exercise the
behavior without device hardware.

Native host note: the legacy service ports, especially TFTP on UDP port 69,
may require elevated privileges on macOS. Tests should use a port-override hook
inside the host backend or run the server on an unprivileged test port, while
keeping BASIC-visible defaults compatible with the device behavior.

Started:

- Shared `OPTION TELNET CONSOLE` / `OPTION TFTP` parsing and print formatting
  moved into `shared/net/mm_net_options.c`; Pico WEB-family builds and
  host-native use the same syntax handling.
- Host-native implements a real TFTP read/write server over the POSIX
  `hal_net` UDP socket and `hal_fs` file API. The BASIC default remains UDP/69,
  while `MMBASIC_HOST_TFTP_PORT` lets the conformance harness bind an
  unprivileged port without changing the BASIC-visible command surface.
- Host BASIC conformance now writes and reads a file through TFTP while the
  BASIC web server is running, so host service regressions are caught before
  flashing hardware.
- `shared/net/mm_net_tftp.c` now owns the TFTP RRQ/WRQ/DATA/ACK state machine
  behind small file/send callbacks. Host-native and ESP32 both use this common
  protocol core; backends provide only UDP send/receive and `hal_fs` file I/O.
- Pico WEB-family builds now also use `shared/net/mm_net_tftp.c`; `shared/net/MMtftp.c`
  is a small file/send adapter and poll hook, and the old lwIP `tftp.c` server
  is no longer linked or kept as dead source.
- ESP32 now implements `OPTION TFTP ON|OFF|ENABLE|DISABLE` with a UDP/69
  server over ESP-IDF sockets and LittleFS-backed `hal_fs`, and the serial
  conformance runner has a `tftp` suite that performs a write/read roundtrip
  against the board.
- ESP32 TFTP service internals moved from `esp32_wifi.c` into
  `ports/esp32_s3_metro/main/esp32_tftp.c`, keeping `esp32_wifi.c` as the
  option/dispatch caller for TFTP enablement.
- ESP32 `WEB NTP` command handling moved from `esp32_wifi.c` into
  `ports/esp32_s3_metro/main/esp32_ntp.c`, while still using the shared
  HAL-backed NTP transport.
- ESP32 UDP service state and `WEB UDP ...` command handling moved from
  `esp32_wifi.c` into `ports/esp32_s3_metro/main/esp32_udp.c`.
- ESP32 MQTT command/poll state moved from `esp32_wifi.c` into
  `ports/esp32_s3_metro/main/esp32_mqtt.c`, while keeping the shared
  HAL-backed MQTT command executor.
- ESP32 TCP server slot state, `WEB TCP ...` server commands, and
  `WEB TRANSMIT ...` helpers moved from `esp32_wifi.c` into
  `ports/esp32_s3_metro/main/esp32_tcp_server.c`.
- ESP32 TCP client/request/stream state and commands moved from `esp32_wifi.c`
  into `ports/esp32_s3_metro/main/esp32_tcp_client.c`; stale raw-socket helper
  functions in `esp32_wifi.c` were removed.
- Pico Telnet has moved off the legacy Pico TCP listener internals and now
  uses `hal_net_tcp_server_*`, `hal_net_tcp_accept_conn()`,
  `hal_net_tcp_conn_recv()`, and `hal_net_tcp_conn_send()` through the lwIP raw
  HAL. ESP32 and host-native Telnet service parity remains open.
- ESP32 `OPTION LIST` now routes to the existing port print-options hook, so
  network option conformance can inspect persisted service options instead of
  carrying per-port workarounds.

## Backend Notes

### Pico/CYW43/lwIP

Keep the polled architecture. All lwIP progress happens inside
`hal_net_poll()` / `cyw43_arch_poll()`. Raw callbacks must copy bytes into
backend queues and return quickly. They must not call `evaluate`, `error`,
`GetMemory` in fragile contexts, or touch BASIC variables directly.

Memory pressure matters most on rp2040 WEB. Use compile-time caps and avoid
adding permanent BSS where a per-connection allocation already exists.

### ESP32/ESP-IDF

The ESP32 backend can keep FreeRTOS network tasks. The contract boundary is the
queue between those tasks and the shared core:

- tasks may accept/read sockets and enqueue events;
- the interpreter thread drains events through `hal_net_poll()` and command
  calls;
- no network task calls into the parser, evaluator, or MMBasic allocator except
  through explicitly safe fixed-size queues.

The current `esp32_wifi.c` already proves the user-visible surface. The
refactor should mostly move code, not invent behavior.

### Host Native

Implement enough POSIX socket support to run conformance tests without device
hardware:

- TCP server/client;
- UDP bind/send/receive;
- fake WiFi status as connected when network backend is enabled;
- MQTT plain TCP if using a small backend or shared MQTT wire client later.
- Telnet console and TFTP service parity using POSIX sockets.

This becomes the fast regression net before flashing boards.

### Host WASM

Provide browser client support with honest capabilities:

- browser-native HTTP fetch path;
- browser-native MQTT over WebSocket path;
- raw TCP server, raw TCP client/stream, and raw UDP unsupported.

Do not add a hidden server requirement to the default static WASM demo. The
static site should still run without a backend; extra networking features can
light up only when the user talks to browser-supported endpoints.

## Build Shape Target

After the refactor, network-capable ports should link:

```
shared/net/mm_net_state.c
shared/net/mm_net_basic.c
shared/net/mm_net_http.c
shared/net/mm_net_ntp.c
shared/net/mm_net_mqtt_cmd.c
<one hal_net backend>
```

Pico WEB ports should stop listing `shared/net/MMsetwifi.c`, `shared/net/MMtcpserver.c`,
`shared/net/MMTCPclient.c`, `shared/net/MMudp.c`, `shared/net/MMntp.c`, and `shared/net/MMMqtt.c` as BASIC command bodies.
Pieces that remain useful should move under `drivers/net_lwip_raw/`.

ESP32 should shrink `esp32_wifi.c` into either:

- `hal_net_esp32.c` only, or
- `esp32_wifi.c` as a thin compatibility wrapper plus `hal_net_esp32.c`.

Non-network ports should link only `drivers/net_stub/hal_net_stub.c` and keep
the `WEB` command token absent unless there is a reason to expose a clear
"not supported" command.

## Decisions

- `OPTION TELNET CONSOLE` and TFTP should be implemented on ESP32 and
  host-native. They do not need to block the first TCP/UDP/MQTT common-core
  extraction, but they are part of the target network surface.
- `MM.INFO(TCP PATH n)` should become official cross-port behavior.
- `WEB TCP SEND` should match Pico/legacy behavior: integer-array long string,
  no automatic close.
- MQTT TLS/cert handling is out of scope for this refactor.
- WASM is client-only and browser-native. Raw TCP/UDP commands report
  unsupported; no proxy or relay mode.

## Exit Criteria

- One shared BASIC parser for `WEB` network commands.
- One shared HTTP transmit implementation.
- One shared NTP implementation.
- One shared UDP/TCP/MQTT state model and interrupt path.
- ESP32 hardware and host-native both pass the same TCP/UDP/NTP/MQTT
  conformance smokes.
- Pico/CYW43 builds stay green; Pico hardware smoke is deferred until hardware
  is available.
- Host-native runs the conformance suite without hardware and covers Telnet/TFTP
  where feasible.
- Host-wasm supports browser-native client features and clearly reports raw
  TCP/UDP unsupported.
- No lwIP, CYW43, ESP-IDF, POSIX socket, or emscripten includes in shared
  network command files.
