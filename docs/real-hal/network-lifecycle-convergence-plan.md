# Network Lifecycle Convergence Plan

**Goal:** make all network-capable ports differ only in their `hal_net_*`
backend implementation. BASIC-visible network semantics, option behavior,
service lifecycle, `RUN` cleanup, and conformance expectations must live in
shared code.

This plan exists because the command/parser and transport-HAL work converged
much of the surface, but the service lifecycle policy still diverged. The
PicoCalc `WEBRP2350` smoke exposed the gap: Pico opened a configured TCP
server at boot, then `RUN "A:netconf.bas"` called `cleanserver()` and closed
the listener; ESP32 did not behave the same way. That is a policy difference,
not a hardware difference.

The implementation ideal is shared code first. When Pico, ESP32, host-native,
and WASM need the same BASIC-visible behavior, that behavior belongs in
`shared/net`, with ports supplying only narrow backend hooks or capability
bits. Duplicating the same lifecycle decisions in four port files is not a
valid end state.

## Scope

In scope:

- Pico WEB-family ports (`WEB`, `WEBRP2350`, `VGAWIFIRP2350`,
  `DVIWIFIRP2350`).
- ESP32-S3 Metro.
- Host native.
- Host WASM.
- BASIC network options:
  - `OPTION WIFI`
  - `OPTION TCP SERVER PORT`
  - `OPTION UDP SERVER PORT`
  - `OPTION TELNET CONSOLE`
  - `OPTION TFTP`
  - `OPTION WEB MESSAGES`
- Runtime lifecycle:
  - boot/reconnect
  - option apply
  - `RUN`
  - `NEW`
  - `CLEAR`
  - `ClearRuntime`
  - error/interrupt cleanup
- Configured network services:
  - TCP server
  - UDP server
  - TFTP
  - Telnet console
  - MQTT session cleanup
  - TCP client/stream cleanup

Out of scope:

- Adding TLS.
- Adding new BASIC commands.
- Changing the public BASIC grammar except where current ports already differ
  and must be made compatible.
- Making unsupported WASM raw-socket features work in browsers. Unsupported
  features should fail through shared capability checks.

## Required Semantics

All supported ports must share these rules.

1. **Network option setters do not have port-specific semantic behavior.**
   If a port must reboot for a low-level reason, that must be represented as a
   shared lifecycle result and handled by the common caller/tooling, not hidden
   in a port-local command implementation.

2. **Configured services are durable settings.**
   If `Option.TCP_PORT`, `Option.UDP_PORT`, Telnet, or TFTP is enabled, the
   service should be active whenever the network is connected and the port
   supports that capability.

3. **`RUN` clears active sessions, not configured services.**
   `RUN "program.bas"` may close accepted TCP connections, TCP clients, MQTT
   sessions, pending UDP packets, and request slots. It must not leave a
   configured TCP/UDP/TFTP/Telnet listener closed.

4. **`NEW` and hard runtime reset are explicit.**
   Shared code should define whether each cleanup point preserves configured
   listeners or tears them down. Ports must not decide this independently.

5. **Boot/reconnect opens configured services through one shared path.**
   After WiFi/Ethernet reaches an IP-ready state, the shared lifecycle module
   opens configured TCP/UDP/TFTP/Telnet services. Ports only provide backend
   primitives.

6. **Unsupported features fail through capability checks.**
   Host WASM raw TCP server, UDP, TFTP, and Telnet should return consistent
   "not supported" behavior from shared policy, based on HAL capabilities.

7. **Conformance harnesses model shared semantics.**
   Harness behavior should not encode "ESP32 does X, Pico does Y" except for
   unsupported capability skips.

## Target Architecture

Add a shared lifecycle layer:

- `shared/net/mm_net_lifecycle.h`
- `shared/net/mm_net_lifecycle.c`

The lifecycle layer owns:

- applying network options
- saving option values
- opening configured services after connect
- closing active sessions
- preserving/reopening configured listeners after runtime cleanup
- printing consistent option/service status
- exposing capability-aware unsupported-feature errors

The shared layer should be the only place that decides lifecycle policy.
Port-local wrappers are acceptable during migration, but they should delegate
to shared lifecycle functions and should shrink away as phases complete.

Ports own only:

- `hal_net_*` backend implementation
- default hostname/platform-specific credential defaults
- any truly hardware-specific connect/scan/status behavior
- storage details only where the existing Option struct cannot already carry
  the value

### Proposed Shared API

Draft API; exact names may change during implementation.

```c
typedef enum {
    MM_NET_LIFECYCLE_OK = 0,
    MM_NET_LIFECYCLE_UNSUPPORTED,
    MM_NET_LIFECYCLE_REBOOT_REQUIRED,
    MM_NET_LIFECYCLE_ERROR,
} mm_net_lifecycle_result_t;

void mm_net_lifecycle_on_network_ready(void);
void mm_net_lifecycle_on_network_down(void);

void mm_net_lifecycle_close_active_sessions(void);
void mm_net_lifecycle_preserve_configured_services(void);
void mm_net_lifecycle_runtime_reset(bool full_reset);

int mm_net_lifecycle_option_setter(unsigned char *cmdline);
int mm_net_lifecycle_mminfo(unsigned char *ep, int64_t *out_iret,
                            unsigned char *out_sret, int *out_targ);
void mm_net_lifecycle_print_options(void);
```

The shared layer should call existing shared service modules where possible:

- `mm_net_service.c`
- `mm_net_options.c`
- `mm_net_wifi_cmd.c`
- `mm_net_web_cmd.c`
- `mm_net_tcp_client_cmd.c`
- `mm_net_tcp_server_cmd.c`
- `mm_net_udp_cmd.c`
- `mm_net_tftp.c`
- `mm_net_mqtt_hal_cmd.c`

## Current Divergences To Remove

### Pico WEB-Family

- `MMsetwifi.c::port_web_option_setter()` saves some network options and
  soft-resets immediately.
- `MMsetwifi.c::WebConnect()` owns configured-service startup.
- `MMtcpserver.c::cleanserver()` historically stopped the configured TCP
  listener during `RUN` cleanup.
- `MMtcpserver.c`, `MMudp.c`, `MMtelnet.c`, and `MMtftp.c` each own parts of
  lifecycle policy in addition to backend glue.

### ESP32-S3

- `esp32_wifi.c::esp32_wifi_option_setter()` applies several network options
  in-place instead of rebooting.
- `esp32_wifi.c::WebConnect()` owns configured-service startup.
- `ports/esp32_s3_metro/main/esp32_peripheral_stubs.c::cleanserver()` is a
  stub, so `RUN` cleanup semantics differ from Pico.
- ESP32 wrapper files own some policy decisions that should move to shared
  lifecycle code.

### Host Native

- Host native has a real POSIX backend and conformance coverage, but
  `host_web.c` still owns lifecycle/policy for options and cleanup.
- Host native should become the fastest shared-policy regression target, not
  a parallel implementation of network semantics.

### Host WASM

- WASM has browser-supported fetch and MQTT-over-WebSocket paths plus
  explicit unsupported raw socket features.
- Unsupported raw TCP server/stream, UDP, TFTP, and Telnet behavior should be
  selected by shared capability checks, not hand-coded as a separate policy
  surface.

## Implementation Phases

### Phase 1 - Lifecycle Inventory And Contract

1. Inventory every network lifecycle entry point:
   - `cleanserver`
   - `close_tcpclient`
   - `closeMQTT`
   - `WebConnect`
   - `ProcessWeb`
   - `port_web_clear_runtime_state`
   - port network option setters
   - boot network connect hooks
   - host/WASM unsupported-feature hooks
2. Create a table mapping each hook to:
   - current owner
   - desired shared owner
   - whether it closes active sessions
   - whether it preserves configured services
   - whether it may require reboot
3. Document the final lifecycle contract in `docs/real-hal/contracts.md` or a
   short companion doc linked from there.

Acceptance:

- A grep-able inventory exists in this plan or a companion doc.
- The intended behavior for `RUN`, `NEW`, option changes, and boot connect is
  explicit for every service.

#### Phase 1 Inventory - May 11, 2026

Lifecycle entry-point inventory:

| Hook | Current owner(s) | Current behavior | Desired shared owner | Active-session cleanup | Preserves configured services | Reboot result |
| --- | --- | --- | --- | --- | --- | --- |
| `cleanserver()` | Pico: `MMtcpserver.c`; ESP32: `esp32_peripheral_stubs.c`; host-native: `host_web.c`; WASM: `host_wasm_web.c`; non-network: `MMweb_stubs.c` | Pico stops/reopens TCP and closes Telnet/TFTP; ESP32 is no-op; host-native stops TCP listener; WASM is no-op | `mm_net_lifecycle_close_active_sessions()` plus service-preservation policy | Yes: accepted TCP slots/request state | Yes for `RUN`; configured TCP/UDP/TFTP/Telnet listeners must remain active when enabled and connected | No |
| `close_tcpclient()` | Pico: `MMTCPclient.c`; ESP32: `esp32_tcp_client.c` through stub wrapper; host-native: `host_web.c`; WASM: `host_wasm_web.c` | Closes TCP client/stream state; wrappers differ only by backend storage | `mm_net_lifecycle_close_active_sessions()` calling port/backend close primitive | Yes: client and stream | Not applicable | No |
| `closeMQTT()` | Pico: `MMMqtt.c`; ESP32: `esp32_mqtt.c`; host-native: `host_web.c`; WASM: `host_wasm_web.c`; non-network: `MMweb_stubs.c` | Closes current MQTT client/session | `mm_net_lifecycle_close_active_sessions()` calling shared MQTT HAL command state close | Yes: MQTT session | Not applicable | No |
| `WebConnect()` | Pico: `MMsetwifi.c`; ESP32: `esp32_wifi.c`; WASM: unsupported stub; non-network: `MMweb_stubs.c` | Pico/ESP32 connect WiFi and open configured TCP/UDP/TFTP/Telnet services inline | `mm_net_lifecycle_on_network_ready()` after backend reports IP-ready | No | Opens all configured services through one path | No |
| `ProcessWeb(int mode)` | Pico: `MMtelnet.c`; ESP32: `esp32_wifi.c`; host-native: `host_web.c`; WASM: `host_wasm_web.c`; non-network: `MMweb_stubs.c` | Polls backend events and service modules; Pico also pumps CYW43 and heartbeat | Shared service poll entry plus port backend poll/heartbeat hook | No, except failed connections may be closed as part of normal polling | Yes | No |
| `port_web_clear_runtime_state()` | Pico: `MMtcpserver.c`; ESP32: `esp32_default_hooks.c`; host-native: `host_web.c`; WASM: `host_wasm_web.c`; non-network: `MMweb_stubs.c` | Pico clears TCP request flags; ESP32 no-op; host-native clears interrupts, MQTT, TFTP session, Telnet conn, message buffers; WASM resets `optionsuppressstatus` only | `mm_net_lifecycle_runtime_reset(full_reset)` | Yes: pending requests, interrupts, clients, MQTT, per-request/session state | Yes for normal `RUN`; full reset may close configured services only through documented policy | No |
| Network option setters | Pico: `MMsetwifi.c::port_web_option_setter()`; ESP32: `esp32_wifi.c::esp32_wifi_option_setter()`; host-native: `host_web.c`; WASM: `host_wasm_web.c` | Pico saves and soft-resets most network options; ESP32/host-native apply many options in place; WASM errors all listed network options | `mm_net_lifecycle_option_setter()` | Only when disabling/rebinding a service or changing credentials | Enabled services are reopened or kept active when connected and supported | `MM_NET_LIFECYCLE_REBOOT_REQUIRED` only if backend truly requires reset |
| Boot network connect hook | Pico: `port_repl_wifi_arch_init_and_connect()` in `MMsetwifi.c`; ESP32 intentionally lazy; host-native has lazy `host_web_ensure_net()`; WASM no-op/unsupported | Port-specific init timing and service startup | Shared lifecycle entry called after port/backend init decides network can be made ready | No | Opens configured services when network reaches ready state | No |
| Unsupported-feature hooks | WASM: `host_wasm_web.c`; stubs: `MMweb_stubs.c`; backend caps: `hal_net_capabilities()` | WASM hand-codes unsupported errors for raw TCP/UDP/TFTP/Telnet/options; stubs no-op or ignore | Shared lifecycle/command capability checks against `hal_net_capabilities()` | No | Unsupported services must not create partial state | No |

Service-level policy:

| Service | Durable option/state | Boot/reconnect | Option change | `RUN` / normal runtime clear | `NEW` / hard runtime reset |
| --- | --- | --- | --- | --- | --- |
| TCP server | `Option.TCP_PORT`, `Option.ServerResponceTime` | Open listener when port is nonzero, network is ready, and `HAL_NET_CAP_TCP_SERVER` is present | Rebind to new nonzero port in place; close listener when set to `0`; save option | Close accepted slots and clear request/path state; keep or reopen listener | Same as `RUN` unless the final shared contract explicitly requests configured-service teardown |
| UDP server | `Option.UDP_PORT`, `Option.UDPServerResponceTime` | Bind listener when port is nonzero, network is ready, and UDP server is supported | Rebind to new nonzero port in place; close socket when set to `0`; save option | Clear pending message/interrupt state; keep bound listener | Same as `RUN` unless full reset policy later says otherwise |
| TFTP | `Option.disabletftp` (`0` means enabled) | Open UDP/69 TFTP service when enabled, network is ready, and UDP send/server capability exists | Open when enabled; close listener/session when disabled; save option | Abort active transfer/session; keep listener enabled | Same as `RUN` unless full reset policy later says otherwise |
| Telnet console | `Option.Telnet` (`0`, `1`, or `-1` for only) | Open TCP/23 listener when enabled, network is ready, and TCP server capability exists | Open when enabled; close listener and active console connection when disabled; save option | Close active Telnet console connection if needed; keep listener enabled | Same as `RUN` unless full reset policy later says otherwise |
| MQTT | No durable broker session option in this slice | No automatic reconnect | Command-owned connect/close | Close current MQTT session and clear topic/message completion state | Close current MQTT session |
| TCP client/stream | Command-owned active state | No automatic reconnect | Command-owned open/close | Close active client/stream and clear stream buffer pointers | Close active client/stream |
| WEB messages | `optionsuppressstatus` | Formatting/status-print setting only | Toggle in place | Reset to default only where existing core reset semantics require it; do not affect listeners | Reset with runtime policy |

The final lifecycle contract is documented from this inventory in
`docs/real-hal/contracts.md` under "Network lifecycle".

### Phase 2 - Shared Lifecycle Skeleton

1. Add `shared/net/mm_net_lifecycle.c/.h`.
2. Move shared option parsing calls into the lifecycle skeleton.
3. Add capability checks for each service:
   - WiFi connect/scan
   - TCP client
   - TCP server
   - UDP
   - NTP
   - TFTP
   - Telnet
   - MQTT
4. Keep existing port implementations behind wrappers at first.

Acceptance:

- All existing builds still compile.
- Host native conformance still passes.
- WASM smoke still passes.
- No behavior change is intended in this phase beyond routing.

Status - May 11, 2026:

- Added `shared/net/mm_net_lifecycle.c/.h`.
- The shared lifecycle skeleton now owns network option parsing/routing for
  `OPTION WIFI`, TCP server port, UDP server port, Telnet, TFTP, and WEB
  messages. Ports provide narrow hooks for credential application and
  service open/close adapters.
- Capability checks for WiFi connect/scan, TCP client/server/stream, UDP,
  NTP, TFTP, Telnet, and MQTT are centralized in
  `mm_net_lifecycle_service_supported()`.
- Pico, ESP32, host-native, and WASM option setters now enter through
  `mm_net_lifecycle_option_setter()` and return shared lifecycle results.
  `mm_net_lifecycle_handle_option_result()` owns the common OK/not-handled/
  unsupported/error/reboot result handling so each port does not duplicate the
  same switch.
- Pico and ESP32 configured-service startup after WiFi connect now goes
  through `mm_net_lifecycle_on_network_ready()` instead of duplicating the
  TCP/UDP/TFTP/Telnet open sequence in each WiFi file.
- Host-native `WEB CONNECT` also routes configured-service startup through
  `mm_net_lifecycle_on_network_ready()`. Host TFTP/Telnet adapters keep the
  existing privileged-port override behavior, but the decision to open enabled
  services is shared.
- Shared runtime cleanup now lives in `mm_net_lifecycle_runtime_reset()`.
  Pico, host-native, and WASM call it with backend hooks for pending TCP
  request cleanup, TCP client close, MQTT close, and any port-specific
  TFTP/Telnet session close. The common helper clears the shared interrupt,
  message, MQTT completion, and `OPTION WEB MESSAGES` runtime state.
- Pico TCP server startup no longer owns the Telnet side effect. Telnet is
  opened by the same configured-service lifecycle path as TCP, UDP, and TFTP.
- WASM raw-socket options now route through the same lifecycle capability
  check while preserving the browser build's existing unsupported error path.
- Verified locally: `make -C ports/host_native`,
  `make -C ports/host_native net-hal-test`,
  `python3.11 porttools/host_network_conformance.py`,
  `python3.11 -m py_compile porttools/network_conformance.py
  porttools/host_basic_network_conformance.py`,
  `python3.11 porttools/host_basic_network_conformance.py --no-build`, and
  `cmake --build build_web2350_picocalc -j 8`.
- Also verified locally after sourcing toolchains through repo helpers:
  `ports/host_wasm/build.sh` and `./buildesp32.sh`.
- Browser WASM network smokes pass against the rebuilt artifacts:
  `node host/web/smoke_network_unsupported.mjs` and
  `node host/web/smoke_network_mqtt_ws.mjs`.
- Toolchain note: do not treat direct `emcc`/`idf.py` lookup failures as
  missing tools. Use `ports/host_wasm/build.sh` for WASM; it sources
  `$HOME/emsdk/emsdk_env.sh`. Use `./buildesp32.sh` for ESP32; it sources
  `${IDF_PATH:-$HOME/esp/esp-idf}/export.sh`.

The implemented code is still intentionally adapter-shaped: ports provide
small open/close/apply hooks, while shared code decides option semantics,
configured-service startup, unsupported-capability behavior, option-result
handling, and runtime cleanup. Further work should keep shrinking port
wrappers instead of adding parallel lifecycle decisions.

### Phase 3 - Option Setter Convergence

1. Replace Pico `port_web_option_setter()` policy with shared lifecycle
   option handling.
2. Replace ESP32 `esp32_wifi_option_setter()` policy with the same shared
   handler.
3. Replace host native network option policy with the same shared handler.
4. Route WASM option behavior through the same shared handler and capability
   checks.
5. Decide and enforce one behavior for each option:
   - preferred: apply in-place where possible
   - if a reboot is truly required, return a shared
     `MM_NET_LIFECYCLE_REBOOT_REQUIRED` result and let the common caller
     perform/report it consistently.

Acceptance:

- `OPTION TCP SERVER PORT <n>` has the same visible behavior on Pico, ESP32,
  and host native.
- `OPTION UDP SERVER PORT <n>` has the same visible behavior on Pico, ESP32,
  and host native.
- `OPTION WIFI ...` behavior is documented and consistent for Pico/ESP32, with
  host/WASM capability handling explicit.
- Serial conformance tools no longer need port-specific assumptions for option
  setters.

Status - May 11, 2026:

- `OPTION WIFI`, `OPTION TCP SERVER PORT`, `OPTION UDP SERVER PORT`,
  `OPTION TELNET CONSOLE`, `OPTION TFTP`, and `OPTION WEB MESSAGES` are routed
  through `mm_net_lifecycle_option_setter()` on Pico, ESP32, host-native, and
  WASM.
- Ports no longer carry separate option-result switches. They call
  `mm_net_lifecycle_handle_option_result()` with only message/reboot callback
  differences.
- Capability gating for unsupported WASM raw-socket services is shared through
  `hal_net_capabilities()` and `mm_net_lifecycle_service_supported()`.

### Phase 4 - Runtime Cleanup Convergence

1. Replace direct divergent calls in `cleanserver()` and
   `port_web_clear_runtime_state()` with shared lifecycle calls.
2. Define two cleanup levels:
   - active-session cleanup: close accepted connections, TCP clients, MQTT
     sessions, request slots, pending UDP payloads
   - configured-service cleanup: close listeners and disable services only
     when the option is disabled or network goes down
3. Update `RUN`/`NEW`/`ClearRuntime` call sites to call the right shared
   cleanup level.
4. Remove port-specific listener preservation/reopen logic once shared code
   owns it.

Acceptance:

- `RUN "A:netconf.bas"` does not close configured TCP listeners on Pico,
  ESP32, or host native.
- Active request slots are still cleared on `RUN`.
- TCP clients/streams do not leak across `RUN`.
- MQTT sessions are closed consistently across runtime cleanup.

Status - May 11, 2026:

- `mm_net_lifecycle_runtime_reset()` centralizes normal runtime cleanup for
  TCP request flags, shared network interrupts/messages, TCP clients, MQTT
  sessions, and optional TFTP/Telnet session hooks.
- Pico, ESP32, host-native, and WASM route `port_web_clear_runtime_state()`
  through the shared cleanup helper. The helper clears active/session state
  without tearing down configured listeners.
- ESP32 `cleanserver()` now delegates to the same shared runtime cleanup path
  instead of being a no-op. ESP32 supplies narrow adapters for TCP request
  cleanup, TCP client close, MQTT close, active TFTP transfer close, and active
  Telnet connection close.
- ESP32 TFTP and Telnet now expose active-session close helpers separately
  from listener teardown, so runtime cleanup can preserve configured services.
- Pico, host-native, ESP32, and WASM `cleanserver()` now delegate to the
  shared runtime cleanup path instead of owning separate listener teardown
  policy. The remaining no-op `cleanserver()` / `port_web_clear_runtime_state()`
  definitions are only in the non-network stub file.
- `mm_net_tcp_service_clear_requests()` now closes accepted TCP server slots
  through the shared TCP service helper, so active server sessions are cleaned
  without closing the configured listener.

### Phase 5 - Configured Service Manager

1. Implement one shared function to open all configured services after network
   connect:
   - TCP server
   - UDP server
   - TFTP
   - Telnet
2. Implement one shared function to close all configured services when the
   network goes down.
3. Keep per-port open/close functions as HAL/backend adapters only.
4. Make status/option printing use shared state where possible.

Acceptance:

- Pico, ESP32, and host native start the same configured services after
  connect.
- Disabled or unsupported services fail consistently.
- WASM unsupported services report through the shared capability path.

Status - May 11, 2026:

- `mm_net_lifecycle_on_network_ready()` opens configured TCP, UDP, TFTP, and
  Telnet services from one shared sequence.
- Pico and ESP32 call the shared ready hook after WiFi reaches IP-ready state.
- Host-native `WEB CONNECT` calls the same ready hook after ensuring the POSIX
  network backend is initialized.
- `mm_net_lifecycle_on_network_down()` defines the matching shared close order;
  ports supply backend close primitives.

### Phase 7 Status - May 11, 2026

- Port-local `cleanserver()` policy has been reduced to delegation in the real
  network ports. Pico and host-native no longer stop/reopen or tear down
  configured listeners from `cleanserver()`; ESP32 and WASM no longer leave it
  as a separate no-op path.
- Port files still provide backend adapters, but option semantics, result
  handling, configured-service startup, runtime reset, active TCP server slot
  cleanup, and shared interrupt/message cleanup are now owned by shared code.
- `OPTION LIST` WiFi formatting is shared through
  `mm_net_print_wifi_option()`. Pico and ESP32 no longer duplicate password
  masking/static-IP formatting for `OPTION WIFI`; they pass stored option
  fields to the shared formatter.
- BASIC `MM.INFO(...)` network query result handling is shared through
  `mm_net_mminfo()`. Pico, ESP32, host-native, and WASM now provide only
  narrow callbacks or values for TCP path/request state and backend-specific
  IP/status values instead of repeating the same parsed-query switch.
- WiFi connect lifecycle is shared through `mm_net_lifecycle_wifi_connect()`.
  Pico and ESP32 now share credential application to the HAL, connection
  status printing, IP reporting, `WIFIconnected` updates, and configured
  service startup after connect.
- `OPTION WIFI` credential storage is shared through
  `mm_net_lifecycle_store_wifi_credentials()`. Pico and ESP32 only provide
  default hostnames and static-IP validation; shared code parses BASIC args,
  copies/clears stored option fields, and saves options.
- `WEB SCAN` command allocation, scan dispatch, array-result handling, console
  printing, and scan error behavior are shared through
  `mm_net_wifi_scan_command()`.
- Runtime service polling is shared through `mm_net_lifecycle_poll()`. Pico,
  ESP32, host-native, and WASM now pass backend poll hooks into one shared
  lifecycle poll order instead of each port owning a separate `ProcessWeb()`
  service sequence. Pico keeps only the CYW43 poll/heartbeat wrapper logic
  around the shared service poll.
- Remaining cleanup work should focus on shrinking adapter boilerplate and
  moving any duplicated status/print behavior into shared code where the
  backend no longer needs to differ.

#### Current Status - May 11, 2026 23:38 EDT

Shared lifecycle implementation is now the active path for the main
BASIC-visible network policy:

- `mm_net_lifecycle_on_network_ready()` owns configured TCP/UDP/TFTP/Telnet
  startup after connect for Pico, ESP32, and host-native.
- `mm_net_lifecycle_option_setter()` owns shared option semantics and in-place
  reopen/close behavior for `OPTION TCP SERVER PORT`, `OPTION UDP SERVER PORT`,
  `OPTION TELNET CONSOLE`, `OPTION TFTP`, `OPTION WEB MESSAGES`, and
  capability-checked `OPTION WIFI`.
- `mm_net_lifecycle_runtime_reset()` owns normal runtime cleanup for active
  TCP request slots, TCP clients/streams, MQTT sessions, TFTP/Telnet sessions,
  shared interrupt state, shared message state, and `optionsuppressstatus`.
- `mm_net_lifecycle_wifi_connect()` and
  `mm_net_lifecycle_store_wifi_credentials()` share Pico/ESP32 WiFi connect
  and credential-storage behavior. Ports now provide only default hostname and
  static-IP validation hooks.
- `mm_net_lifecycle_poll()` owns service poll order and the network-ready
  gate. Pico, ESP32, host-native, and WASM pass narrow backend poll callbacks
  rather than duplicating `ProcessWeb()` lifecycle policy.
- `mm_net_print_wifi_option()`, `mm_net_print_options()`, and
  `mm_net_print_service_options()` own shared `OPTION LIST` output for network
  options.
- `mm_net_mminfo()` owns shared network `MM.INFO(...)` query dispatch; ports
  provide only backend-specific value hooks.
- `mm_net_wifi_scan_command()` owns shared `WEB SCAN` parsing, allocation,
  HAL dispatch, output, and error handling.

Verified gates from this status update:

- Host native build: passed with existing host `MIPS16 optimize`/linker
  warnings only.
- Host native direct network conformance:
  `python3.11 porttools/host_network_conformance.py` passed. This includes the
  host net HAL test plus the BASIC network suite.
- Host BASIC network conformance:
  `python3.11 porttools/host_basic_network_conformance.py --no-build` passed.
  Covered TCP client/request, TCP stream, TCP server, UDP send/receive, NTP,
  MQTT, TFTP, Telnet console, transmit helpers, and option listing.
- Host WASM build: passed with existing Emscripten/WASM warnings only.
- WASM browser smokes passed:
  `node host/web/smoke_network_unsupported.mjs` and
  `node host/web/smoke_network_mqtt_ws.mjs`.
- ESP32-S3 build: passed through `./buildesp32.sh`; only the existing
  `External.h:GetPinStatus` qualifier warning appeared.
- Pico WEBRP2350/PicoCalc build: passed through
  `cmake --build build_web2350_picocalc -j 8`.
- `git diff --check`: passed.

Open work after this point:

- Continue shrinking adapter boilerplate where it is purely pass-through.
- Add or run hardware serial conformance on ESP32-S3 and PicoCalc WEBRP2350
  after flashing hardware.
- Keep any remaining port-local network code limited to backend primitives,
  capability declarations, hardware init, hostname defaults, validation hooks,
  and device-specific polling such as Pico CYW43 heartbeat/poll support.

### Phase 6 - Harness And Tooling Cleanup

1. Update `porttools/network_conformance.py` so option changes follow the
   shared lifecycle contract.
2. Keep retry loops only for normal network timing races, not to paper over
   semantic divergence.
3. Add checks that prove service preservation across `RUN`:
   - set TCP server port
   - run a BASIC server program
   - verify host can connect
   - interrupt
   - run again
   - verify host can still connect
4. Add equivalent UDP preservation tests where supported.
5. Keep host-native conformance as the fast policy gate.

Acceptance:

- Host native full conformance passes.
- ESP32 full conformance passes on hardware.
- Pico WEBRP2350 full or targeted conformance passes on hardware.
- WASM smoke still verifies fetch/MQTT-supported behavior and raw-socket
  unsupported behavior.

#### Phase 6 Status - May 11, 2026

- Host-native BASIC conformance now models the shared lifecycle contract for
  configured services across repeated `RUN` calls. The server slice configures
  temporary TCP and UDP ports, runs the BASIC server, verifies TCP/UDP traffic,
  reruns the same server under the same options, and verifies TCP and UDP are
  still reachable.
- Serial `network_conformance.py` now has matching checks for hardware-capable
  ports: the TCP server suite interrupts and reruns the BASIC server before
  checking the configured listener again; the UDP suite runs a short BASIC
  program and checks configured UDP receive after `RUN`.
- UDP retry behavior is limited to delivery timing around a running BASIC
  program. The expected BASIC-visible message is unchanged, so retries do not
  mask semantic divergence.
- Verified locally:
  `python3.11 -m py_compile porttools/network_conformance.py
  porttools/host_basic_network_conformance.py
  porttools/host_network_conformance.py`,
  `python3.11 porttools/host_basic_network_conformance.py --no-build`, and
  `python3.11 porttools/host_network_conformance.py` all passed.
- Hardware serial gates are pending, not passed in this worker: ESP32-S3 full
  `network_conformance.py all` and Pico WEBRP2350 targeted/full serial
  conformance still require flashed hardware and serial access.

### Phase 7 - Remove Duplicated Policy

1. Delete or shrink port-local lifecycle code that has moved to shared code.
2. Port files should no longer decide:
   - whether `RUN` preserves a listener
   - whether option setters reopen services
   - how configured services are opened after connect
   - unsupported-feature wording for shared commands
3. Keep only backend primitives and hardware-specific defaults.

Acceptance:

- Grep shows no duplicated lifecycle policy in Pico/ESP32/host/WASM port files.
- Port-local code is limited to HAL/backend primitives and defaults.
- `docs/real-hal/network-core-plan.md` can point to this plan as the lifecycle
  completion slice.

#### Phase 7 Worker Status - May 11, 2026

- Host-native no longer uses `cleanserver()` as the lifecycle TCP close hook.
  `cleanserver()` remains the shared runtime-reset/preserve-listener path,
  while `host_lifecycle_close_tcp()` is now the backend listener stop used by
  shared option disable/rebind policy.
- Pico and ESP32 TCP/UDP open adapters now accept the port chosen by
  `mm_net_lifecycle_on_network_ready()` /
  `mm_net_lifecycle_option_setter()` instead of rereading `Option.TCP_PORT` or
  `Option.UDP_PORT` locally. Port code now provides backend bind primitives;
  shared lifecycle code decides which configured service/port opens.
- Pico, ESP32, and host-native removed duplicated default option-result error
  strings. Shared `mm_net_lifecycle_handle_option_result()` now owns the
  default unsupported/apply-failure wording; Pico keeps only its reboot
  callback because that behavior is hardware-specific.
- Grep evidence after this worker:
  `close_tcp_server = cleanserver` is gone; duplicated
  `"Network feature not supported"` / `"Failed to apply network option"`
  option-result strings are gone from Pico, ESP32, and host-native. The only
  remaining custom option-result handler is WASM's browser-specific
  `"WEB networking not supported in browser build"` path, which is intentionally
  retained for the browser smoke contract while the capability decision still
  comes from shared lifecycle code.
- Verified in this worker: `git diff --check`, `make -C ports/host_native`,
  `cmake --build build_web2350_picocalc -j 8`, `./buildesp32.sh`,
  `python3.11 porttools/host_basic_network_conformance.py --no-build`,
  `python3.11 porttools/host_network_conformance.py`, and a focused
  host-native probe proving `OPTION TCP SERVER PORT 0` closes the configured
  listener all passed. Hardware serial conformance is still pending and was not
  claimed here.
- Remaining intentional adapters are backend primitives and hardware defaults:
  lifecycle hook tables, Pico CYW43 poll/heartbeat, Pico/ESP32 default
  hostname/static-IP validation, ESP32 lazy WiFi boot timing, host-native test
  port overrides for TFTP/Telnet, and WASM browser fetch/MQTT command surface.

#### Parent Evaluation - May 11, 2026 23:57 EDT

Phase 6 and Phase 7 were executed serially through worker agents and reviewed
from the parent context. Both worker results were accepted; no respawn was
needed.

Additional parent verification after accepting Phase 7:

- `git diff --check`: passed.
- `make -C ports/host_native`: passed.
- `cmake --build build_web2350_picocalc -j 8`: passed.
- `ports/host_wasm/build.sh`: passed.
- `./buildesp32.sh`: passed.
- `python3.11 -m py_compile porttools/network_conformance.py
  porttools/host_basic_network_conformance.py
  porttools/host_network_conformance.py`: passed.
- `python3.11 porttools/host_basic_network_conformance.py --no-build`:
  passed, including `host_basic_tcp_server_preserved_after_run` and
  `host_basic_udp_preserved_after_run`.
- `python3.11 porttools/host_network_conformance.py`: passed, including the
  host net HAL conformance binary and BASIC network conformance.
- `node host/web/smoke_network_unsupported.mjs`: passed.
- `node host/web/smoke_network_mqtt_ws.mjs`: passed.
- Focused host-native probe for `OPTION TCP SERVER PORT 0`: passed; the
  listener is released after disabling the option.

Local completion state:

- Shared lifecycle owns option semantics, configured-service startup, runtime
  cleanup, service poll ordering, WiFi credential parsing/storage, WiFi
  connect flow, `OPTION LIST` formatting, network `MM.INFO(...)` dispatch,
  and `WEB SCAN` behavior where supported.
- Host-native and WASM gates pass locally.
- ESP32-S3 and Pico WEBRP2350 compile locally.
- ESP32-S3 and Pico WEBRP2350 serial conformance remain pending because they
  require flashed hardware and serial access; they are not claimed as passed.

#### PicoCalc Hardware Verification - May 12, 2026 01:02 EDT

PicoCalc WEBRP2350 hardware was connected on `/dev/cu.usbmodem101` and flashed
with the current `build_web2350_picocalc/PicoMite.uf2` using:

- `UPDATE FIRMWARE` from MMBasic to enter BOOTSEL.
- `picotool load -v -x build_web2350_picocalc/PicoMite.uf2`.

The initial hardware conformance run before flashing found the old firmware
still rebooted on `OPTION WIFI` / `OPTION TCP SERVER PORT` and timed out in
the TCP server suite. After flashing the current image, the board reported
`MM.INFO(IP ADDRESS)=192.168.4.56`, `MM.INFO(TCPIP STATUS)=3`, and
`MM.INFO(WIFI STATUS)=1`.

PicoCalc WEBRP2350 full serial network conformance then passed:

```sh
python3.11 porttools/network_conformance.py all \
  --port /dev/cu.usbmodem101 \
  --device-host 192.168.4.56 \
  --long-timeout 60 \
  --suite-retries 1
```

Passed suites:

- `tcp-client`: HTTP request and TCP stream.
- `tcp-server`: request handling, transmit helpers, and
  `tcp_server_preserved_after_run`.
- `udp`: send/receive and `udp_preserved_after_run`.
- `tftp`: write/read round trip.
- `telnet`: console round trip.
- `ntp`: deterministic local NTP responder.
- `mqtt`: connect, subscribe, receive, publish, unsubscribe.

Remaining hardware gate after this run:

- ESP32-S3 serial conformance still requires flashed hardware and serial
  access; it is not claimed as passed.

## Verification Matrix

| Target | Required Gates |
| --- | --- |
| Host native | build, `net-hal-test`, host network conformance, BASIC network conformance |
| Host WASM | build, fetch unsupported/raw-socket smoke, MQTT WebSocket smoke |
| ESP32-S3 | build, targeted serial suites, full serial `all` after batches |
| Pico WEB | `WEB`, `WEBRP2350`, `VGAWIFIRP2350`, `DVIWIFIRP2350` builds |
| PicoCalc WEBRP2350 | USB console smoke, WiFi scan/connect, targeted network suites, full serial `all` when stable |

## Completion Criteria

This work is done when:

- Network option semantics are shared across Pico, ESP32, host native, and
  WASM where supported.
- Unsupported WASM/raw-socket behavior comes from shared capability checks.
- `RUN`/`NEW`/`ClearRuntime` lifecycle behavior is shared and documented.
- Configured listeners survive `RUN` consistently.
- Active sessions are cleaned consistently.
- Port-local files no longer contain lifecycle policy except temporary adapter
  glue with TODOs pointing to this plan.
- Conformance harnesses do not encode port-specific lifecycle behavior.

## Notes From PicoCalc Bring-Up

Observed on `WEBRP2350` PicoCalc hardware:

- USB CDC required DTR asserted for the serial helper to receive prompt bytes.
- `OPTION TCP SERVER PORT 18181` intentionally caused a USB disconnect because
  the Pico option setter soft-reset the board.
- After boot, the configured TCP server printed as started on
  `192.168.4.56:18181`.
- `RUN "A:netconf.bas"` closed the listener through Pico's `cleanserver()`,
  causing host fetches to fail with `Connection refused`.
- Reopening/preserving the configured listener after `cleanserver()` made the
  manual TCP server program pass.

These are lifecycle-policy findings. They should be fixed in shared code, not
kept as Pico-specific behavior.
