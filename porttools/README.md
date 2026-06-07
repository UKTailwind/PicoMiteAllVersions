# Port Tools

Small host-side tools for device-port bringup smoke tests. These are intended
to be boring, repeatable checks that drive the real MMBasic interpreter over
serial. They do not replace the host test suite or the ESP-IDF build; they are
for proving that flashed hardware still responds and that hardware-backed
features work end-to-end.

Run commands from the repo root unless noted otherwise.

## Requirements

- Python with `pyserial` installed. On this machine `python3.11` has been used.
- A flashed board exposing the MMBasic prompt over serial.
- For ESP32 network tests, the Metro must already be on WiFi or `WEB CONNECT`
  must be able to use saved `OPTION WIFI` credentials.

Both tools accept `--port`. If it is omitted, `basic_serial.py` checks
`BASIC_PORT`, then common device paths such as `/dev/cu.usbmodem101`.

## `basic_serial.py`

`basic_serial.py` is the general-purpose serial interpreter runner. It opens
the port with DTR and RTS deasserted before `open()` to avoid accidental
ESP32 USB-Serial/JTAG reset behavior, syncs to the `>` prompt with Ctrl-C and
Enter, sends BASIC commands, and fails if the transcript contains a BASIC
error or a prompt timeout.

Quick prompt check after flashing:

```sh
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --boot-wait 1 \
  --cmd 'PRINT "ESP32_PROMPT_OK"' \
  --expect ESP32_PROMPT_OK
```

Run several commands in one session:

```sh
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --boot-wait 2 \
  --cmd 'PRINT MM.INFO$(ID)' \
  --cmd 'PRINT MM.INFO(CPUSPEED)' \
  --cmd 'FILES'
```

Run a line-oriented command script:

```sh
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --script /tmp/esp32-smoke.bas \
  --expect '>'
```

Script files are command transcripts, not saved BASIC programs: each non-blank
line is sent as one immediate-mode command. Lines beginning with `#` are
ignored.

Useful options:

- `--boot-wait N`: capture boot text for `N` seconds before syncing.
- `--no-sync`: skip Ctrl-C/Enter sync when another tool already left the board
  at a prompt.
- `--timeout N`: normal command timeout.
- `--long-timeout N`: timeout for commands expected to block longer. The runner
  automatically uses this for `WEB CONNECT` and `PAUSE`.
- `--expect REGEX`: require a regex match in the ANSI-stripped transcript. May
  be repeated.
- `--quiet`: suppress the live transcript and only return success/failure.

Pico flash-entry checklist:

```sh
# First choice when BASIC is responsive: enter BOOTSEL from the prompt.
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --timeout 5 \
  --long-timeout 10 \
  --cmd 'UPDATE FIRMWARE' || true
sleep 3
picotool load -v -x build_web2350_picocalc/PicoMite.uf2
```

Use `UPDATE FIRMWARE` for the PicoCalc flash loop whenever the BASIC prompt is
reachable. It deliberately drops USB while entering BOOTSEL, so the command
runner may exit non-zero; wait a few seconds, then run `picotool load`.
Avoid `CPU RESTART` for this workflow because it only reboots the application
and can leave macOS serial open/sync stuck without putting the board in BOOTSEL.
If the firmware is wedged and no BASIC prompt is reachable, use BOOTSEL/manual
reset or SWD.

RP2350 PSRAM march tests:

```sh
# Verify the firmware-visible PSRAM size.
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --cmd 'PRINT "PSRAM_SIZE=" + STR$(MM.INFO(PSRAM SIZE))' \
  --expect 'PSRAM_SIZE='

# Exercise the cached XIP PSRAM window. An optional number tests that many MB.
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --timeout 120 \
  --cmd 'RAM TEST 1' \
  --expect 'RAM TEST OK'

# Exercise the no-cache alias directly.
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --timeout 120 \
  --cmd 'RAM TEST NOCACHE 1' \
  --expect 'RAM TEST OK'
```

`RAM TEST` is destructive to PSRAM contents. Run it from the prompt during
hardware bringup, not while a BASIC program is depending on RAM slots or PSRAM
heap state. Use `RAM TEST` with no size argument for the configured usable
PSRAM range, or `RAM TEST ALL` to include the 2 MB reserved area above
`MM.INFO(PSRAM SIZE)`. Full-range tests can be quiet for tens of seconds while
scanning the final phase, so raise `--timeout` rather than only
`--long-timeout`.

ESP32-S3 PSRAM smoke:

```sh
python3.11 porttools/psram_smoke.py --target esp32 \
  --port /dev/cu.usbmodem2101 \
  --long-timeout 120 \
  --very-long-timeout 180
```

The ESP32 target publishes the reserved PSRAM slab through the shared
`MM.INFO(PSRAM SIZE)` / `RAM` command surface. `RAM TEST NOCACHE` remains an
expected ESP32 error because the S3 has no cache-bypass PSRAM alias.
`RAM FILE LOAD` is not implemented on ESP32; the harness checks that the error
is explicit while still exercising `RAM SAVE` / `RAM LOAD` / `RAM RUN`.

Known-good checks from the ESP32 bringup:

```sh
python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --boot-wait 1 \
  --cmd 'PRINT "PORTTOOLS_OK"' \
  --expect PORTTOOLS_OK

python3.11 porttools/basic_serial.py \
  --port /dev/cu.usbmodem101 \
  --boot-wait 1 \
  --cmd 'PRINT "FINAL_FLASH_OK"' \
  --expect FINAL_FLASH_OK \
  --quiet
```

## `esp32_fs_vm_smoke.py`

`esp32_fs_vm_smoke.py` is the ESP32-S3 Metro hardware smoke suite for the
prompt, `MM.INFO`, A: LittleFS, BASIC file I/O, program load/save/run paths,
the bytecode VM, GPIO/ADC pins, and opt-in persistent/visual/network checks.
It writes short BASIC programs to A:, runs them, checks success markers, and
removes generated files unless `--keep-files` is used.

Default run:

```sh
python3.11 porttools/esp32_fs_vm_smoke.py \
  --port /dev/cu.usbmodem101
```

Run selected suites:

```sh
python3.11 porttools/esp32_fs_vm_smoke.py fs vm gpio \
  --port /dev/cu.usbmodem101
```

Default `all` expands to `info fs program vm gpio`. It deliberately excludes
flash-slot persistence, the GP46 NeoPixel visual check, and WiFi/network
conformance.

Implemented checks:

- `info`: prompt sync plus `MM.INFO$(ID)`, `MM.INFO(CPUSPEED)`,
  `MM.INFO(HEAP)`, `MM.INFO(STACK)`, `MM.INFO(FREE SPACE)`, and clean B:/SD
  detection. B: not configured or no card is reported as `SKIP`, not failure.
- `fs`: A: create/delete files and directories, `CHDIR`, `MKDIR`, `RMDIR`,
  `KILL`, `RENAME`, `COPY`, wildcard `DIR$`, filenames with spaces,
  overwrite behavior, file existence and size checks, missing-file and
  duplicate-directory errors, plus `OPEN`, `PRINT #`, `INPUT$`, `LINE INPUT`,
  `LOC`, `LOF`, `EOF`, `SEEK`, append, and overwrite file I/O.
- `program`: `LOAD`, `SAVE`, `RUN`, `FRUN`, autorun `LOAD ..., R`,
  empty-program `SAVE` refusal, and prompt recovery after the expected error.
- `vm`: `FRUN` of a bridged mixed scalar/array `DIM` regression, arithmetic,
  strings, arrays, `SUB`/`FUNCTION`, `SELECT CASE`, `DATA`/`READ`/`RESTORE`,
  VM-side file I/O, and a Sieve of Eratosthenes benchmark that verifies 168
  primes up to 1000.
- `gpio`: safe Metro checks on GP13 DOUT/DIN and GP1 ARAW, then verifies
  current `SETPIN ..., PWM` and `SERVO` unsupported errors remain explicit.
- `flash`: opt-in flash-slot persistence using `FLASH ERASE`, `FLASH SAVE`,
  RTS reset/resync, `FLASH LOAD`, `RUN`, `FLASH RUN`, and slot cleanup.
  `VAR SAVE`/`VAR RESTORE` is additionally gated by `--var-save` because it
  leaves a persistent saved variable.
- `ws2812`: GP46 onboard NeoPixel red/green/blue/off command sequence. The
  suite reports `SKIP` unless `--ws2812-visual` is passed.
- `network`: reports `SKIP` unless `--run-network` is passed, then chains to
  `network_conformance.py` through this repo's `porttools/` path. It defaults
  to the full `all` conformance suite. Use `--network-suite`,
  `--connect-command`, `--network-host`, `--device-host`,
  `--network-suite-retries`, and `--network-suite-timeout` as needed.

Run ESP32 network conformance through the smoke runner:

```sh
python3.11 porttools/esp32_fs_vm_smoke.py network \
  --run-network \
  --port /dev/cu.usbmodem101 \
  --connect-command 'WEB CONNECT'
```

If automatic address detection picks the wrong interface, pass the Mac-side
address reachable from the ESP32 and/or the ESP32 address reachable from the
Mac:

```sh
python3.11 porttools/esp32_fs_vm_smoke.py network \
  --run-network \
  --port /dev/cu.usbmodem101 \
  --network-host 192.168.1.23 \
  --device-host 192.168.1.57 \
  --network-suite-timeout 240
```

To debug a narrower network surface before running the full suite:

```sh
python3.11 porttools/esp32_fs_vm_smoke.py network \
  --run-network \
  --network-suite tcp-client \
  --port /dev/cu.usbmodem101
```

Useful options:

- `--boot-wait N`, `--timeout N`, `--long-timeout N`: serial timing controls.
- `--drive A:` and `--prefix NAME`: target drive and temporary path prefix.
- `--flash-slot N`: flash program slot used by `flash`. Defaults to slot 3.
- `--keep-files`: leave generated BASIC files on A: for manual inspection.
- `--reset-app`: pulse RTS before the initial prompt sync using
  `basic_serial.py`'s reset behavior.
- `--ws2812-visual`: opt into the GP46 visual LED sequence.
- `--var-save`: opt into persistent `VAR SAVE`/`VAR RESTORE` inside `flash`.
- `--run-network`: opt into the WiFi-dependent network conformance handoff.
- `--network-suite NAME`: choose the network conformance suite passed through
  to `network_conformance.py`; defaults to `all`.

## `esp32_tcp_smoke.py`

`esp32_tcp_smoke.py` exercises the ESP32 BASIC TCP client surface against
temporary TCP endpoints running on the Mac. It starts:

- an HTTP responder on `--http-port` for `WEB TCP CLIENT REQUEST`;
- a line stream responder on `--stream-port` for `WEB TCP CLIENT STREAM`.

Then it drives the board through serial using `basic_serial.BasicSerial`.

Default run:

```sh
python3.11 porttools/esp32_tcp_smoke.py \
  --port /dev/cu.usbmodem101
```

If the Mac address selected from the route table is wrong, pass the address
reachable from the ESP32 explicitly:

```sh
python3.11 porttools/esp32_tcp_smoke.py \
  --port /dev/cu.usbmodem101 \
  --host 192.168.4.23
```

If the board does not have saved WiFi credentials, provide a connect command:

```sh
python3.11 porttools/esp32_tcp_smoke.py \
  --port /dev/cu.usbmodem101 \
  --host 192.168.4.23 \
  --connect-command 'WEB CONNECT "ssid","password"'
```

The script verifies:

- Mac-side server received `GET /tcp-smoke HTTP/1.0`.
- BASIC received an `ESP32_CLIENT_OK` HTTP response.
- Mac-side stream server received `INLINE\n`.
- BASIC received `ACK INLINE` and the final stream line `STREAM3`.

At the end it prints a `--- checks ---` block. Any `FAIL` means the smoke test
returns non-zero and prints the Mac-side server log.

Useful options:

- `--bind ADDR`: local bind address for the Mac-side servers. Defaults to all
  interfaces.
- `--host ADDR`: Mac/IP address that the ESP32 should connect to.
- `--gateway ADDR`: route target used to infer the Mac source address when
  `--host` is omitted. Defaults to `192.168.4.1`.
- `--http-port N`, `--stream-port N`: override local listener ports.
- `--connect-command CMD`: BASIC command used before TCP client checks.
- `--boot-wait`, `--timeout`, `--long-timeout`: serial timing controls.

If this test hangs at `WEB CONNECT`, first confirm the board can reach the
prompt with `basic_serial.py`, then run `WEB CONNECT` manually or pass the exact
connect command. If the TCP checks fail but WiFi connected, check the Mac
firewall and confirm the `--host` address is on the same network as the ESP32.

## `esp32_sd_smoke.py`

`esp32_sd_smoke.py` exercises the Adafruit Metro ESP32-S3 onboard microSD slot
as MMBasic drive `B:`. It lists the card, optionally verifies an expected file
and first-line text, then creates, reads, renames, and deletes a temporary file.

```sh
python3.11 porttools/esp32_sd_smoke.py \
  --port /dev/cu.usbmodem101 \
  --expect-file readme.txt \
  --expect-text hello
```

## `pico_fs_vm_smoke.py`

`pico_fs_vm_smoke.py` is the Pico-family hardware smoke suite for the onboard
filesystem, BASIC file I/O, program load/save paths, flash slots, display,
timers, `RUN`, `FRUN`, the bytecode VM, WEB status, and integer math. It writes
short BASIC programs to the target drive, runs them on the board, checks their
success markers, and removes the generated programs unless `--keep-files` is
used.

Default run against the Pico internal filesystem:

```sh
python3.11 porttools/pico_fs_vm_smoke.py \
  --port /dev/cu.usbmodem101
```

Run only selected suites:

```sh
python3.11 porttools/pico_fs_vm_smoke.py fs vm sieve \
  --port /dev/cu.usbmodem101
```

Implemented checks:

- `device`: prompt sync, `MM.INFO$(ID)`, `MM.INFO$(CPUSPEED)`,
  `MM.INFO(FREE SPACE)`, PicoCalc keyboard/battery/charging info, optional
  `--expect-psram`, and a non-paginated `DIR$` probe.
- `fs`: creates a temporary directory on `A:`, changes into it, writes and
  reads BASIC files, checks `INPUT$`, `LINE INPUT`, `LOC`, `LOF`, `EOF`,
  `SEEK`, `MM.INFO(EXISTS FILE)`, `MM.INFO(EXISTS DIR)`,
  `MM.INFO(FILESIZE)`, `DIR$`, wildcard directory iteration, `COPY`,
  `RENAME`, filenames with spaces, repeated create/delete, `KILL`, and
  `RMDIR`.
- `errors`: intentional filesystem errors for missing files, duplicate
  directories, non-empty directory removal, and rename-over-existing behavior.
- `large`: writes and verifies a multi-sector text file.
- `program`: `LOAD`, `SAVE`, plain `RUN`, and autorun `LOAD ..., R`.
- `chain`: `CHAIN` with `MM.CMDLINE$`. This is explicit rather than part of
  default `all` because it has exposed USB prompt recovery hangs on current
  PicoCalc firmware.
- `flash`: destructive test of one flash program slot using `FLASH ERASE`,
  `FLASH SAVE`, `FLASH LIST`, `FLASH LOAD`, and `FLASH RUN`.
- `autosave`: drives `AUTOSAVE N` over serial and runs the captured program.
- `vm`: runs generated programs with `FRUN`, checks the bridged mixed
  scalar/array `DIM` regression, integer loops and basic string handling,
  arrays passed to `SUB`/`FUNCTION`, `SELECT CASE`, float math,
  `DATA`/`READ`/`RESTORE`, then verifies VM-side file write/read/delete.
- `sieve`: runs a bytecode VM Sieve of Eratosthenes with a `!FAST` inner loop
  and verifies there are 168 primes up to 1000.
- `timing`: `TIMER`, `PAUSE`, and `SETTICK`.
- `display`: scalar graphics, `PIXEL()` readback, and framebuffer copy/merge.
- `web`: quick `MM.INFO(WIFI STATUS)` and `MM.INFO$(IP ADDRESS)` check. Use
  `--connect-command` when the board needs an explicit `WEB CONNECT`.
- `network`: hands off to `network_conformance.py all`. This is not part of
  default `all` because it starts host-side TCP/UDP/TFTP/Telnet/NTP/MQTT
  services and takes much longer.

Useful options:

- `--drive A:`: drive to test. `A:` is the default and is the Pico internal
  filesystem.
- `--prefix NAME`: temporary file and directory prefix. Defaults to `pfs`.
- `--flash-slot N`: flash slot to erase/save/load/run. Defaults to slot 3.
- `--keep-files`: leave the generated BASIC programs on the drive for manual
  reruns.
- `--expect-psram`: fail the device smoke if `OPTION LIST` does not show
  `OPTION PSRAM`.
- `--connect-command CMD`: run a specific WEB connect command before the `web`
  suite and pass it through to the `network` suite.
- `--reset-app`: send Ctrl-C before prompt sync.

## `network_conformance.py`

`network_conformance.py` is the cross-port WEB surface runner used by the
network-core refactor plan. It still drives hardware over serial, but the
checks are named for the BASIC behavior rather than for ESP32.

Run all currently implemented suites:

```sh
python3.11 porttools/network_conformance.py all \
  --port /dev/cu.usbmodem101
```

Run individual suites:

```sh
python3.11 porttools/network_conformance.py tcp-client --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py tcp-server --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py udp --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py tftp --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py telnet --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py ntp --port /dev/cu.usbmodem101
python3.11 porttools/network_conformance.py mqtt --port /dev/cu.usbmodem101
```

Implemented checks:

- `tcp-client`: `WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`,
  `WEB OPEN TCP STREAM`, `WEB TCP CLIENT STREAM`, and `WEB CLOSE TCP CLIENT`
  against temporary Mac-side TCP servers.
- `tcp-server`: configures a temporary `OPTION TCP SERVER PORT`, loads a small
  BASIC polling server, fetches from the Mac, and verifies `MM.INFO(TCP REQUEST
  n)`, `WEB TCP READ`, `MM.INFO(TCP PATH n)`, long-string `WEB TCP SEND`, and
  `WEB TCP CLOSE`. It then reruns the BASIC server under the same configured
  port and verifies the listener still accepts connections after `RUN`.
- `udp`: configures a temporary `OPTION UDP SERVER PORT`, verifies `WEB UDP
  SEND` to a Mac-side UDP listener, sends a datagram back to the device, and
  checks `MM.MESSAGE$` / `MM.ADDRESS$`. It also runs a short BASIC program and
  verifies the configured UDP listener still receives after `RUN`.
- `tftp`: enables `OPTION TFTP ON`, writes a file to the device with TFTP WRQ,
  reads it back with RRQ, and restores the previous TFTP enabled/disabled
  state inferred from `OPTION LIST`.
- `telnet`: enables `OPTION TELNET CONSOLE ON`, connects to the device's
  Telnet port, sends a BASIC expression over the socket, verifies the evaluated
  result is mirrored back, and restores the previous non-ONLY Telnet setting.
- `ntp`: starts a local fake NTP responder on UDP port 123, runs `WEB NTP`
  against it, and verifies the 48-byte request, firmware status output,
  `DATE$`, and `TIME$`.
- `mqtt`: starts a tiny local MQTT 3.1.1 broker, verifies
  `WEB MQTT CONNECT`, `SUBSCRIBE`, incoming `MM.TOPIC$` / `MM.MESSAGE$`,
  `PUBLISH`, `UNSUBSCRIBE`, and `CLOSE` over plain TCP.

The TCP and UDP server suites read the existing saved ports first and restore
them at the end. Use `--host` when automatic Mac address selection picks the
wrong interface, and `--device-host` when `MM.INFO(IP ADDRESS)` is not the
address the Mac should connect to.

Harness details encoded from ESP32 hardware bringup:

- Generated BASIC programs are written to A: with quote-preserving
  `PRINT #1,...CHR$(34)...` expressions rather than numbered immediate-mode
  lines. The ESP32 prompt executes numbered lines immediately; it does not use
  them as an editor upload protocol.
- Marker parsing uses the last marker occurrence so echoed commands do not
  masquerade as command output.
- TCP client stream checks pause after closing the request client and after
  opening the stream client, then guard zero-length long-string reads so a
  timing miss reports `STREAM_EMPTY` instead of crashing the harness.
- Suites that run long-lived BASIC programs reopen and resync the serial port
  before restoring saved options. Use `--suite-retries N` to rerun a failing
  suite when validating hardware timing.
- Prompt-sync failures report the captured byte count and a short clean
  transcript tail, so a silent USB serial path is distinguishable from an
  unexpected prompt or boot message.
- Each serial suite attempt has a hard wall-clock watchdog. Override it with
  `--suite-timeout SECONDS` when debugging a slow board; the default is derived
  from `--long-timeout` with a 180-second floor.
- The runner preserves the running app between suites by default and uses
  Ctrl-C/Enter sync plus bounded serial reopen/resync. Pass
  `--reset-before-suite` only when you explicitly want an RTS reset before each
  suite. `basic_serial.py` exposes the same reset pulse as `--reset-app`.
- The NTP suite uses a deterministic local responder instead of public NTP.
  Firmware currently sends to UDP port 123, so `--ntp-port` is only useful with
  external port redirection or ports that make the NTP port configurable.

## Host Network HAL Conformance

The native host port now links `ports/host_native/hal_net_posix.c`, not the
no-network stub. Run the host HAL conformance target whenever the network HAL
contract or shared WEB core changes:

```sh
make -C ports/host_native net-hal-test
```

This builds and runs `ports/host_native/hal_net_posix_test.c`. It verifies the
host `hal_net.h` backend through loopback TCP client, TCP server
accept/send/close, UDP bind/receive, UDP send, capability bits, and loopback IP
reporting. It also checks the shared HTTP path and NTP packet helpers that the
shared WEB command layer will call, including a HAL-backed NTP exchange against
a local UDP responder. The same check is available through:

```sh
python3.11 porttools/host_network_conformance.py
```

The serial `network_conformance.py` suites remain the BASIC WEB surface gate
for device builds; the host BASIC WEB suites should be enabled against the same
behavior as the shared command core lands.

`porttools/host_basic_network_conformance.py` also exercises the host-native
BASIC WEB surface in-process: TCP client/server, UDP, fake NTP, plain MQTT,
shared transmit helpers, `OPTION LIST`, and host TFTP. The harness sets
`MMBASIC_HOST_TFTP_PORT` so the host TFTP service binds an unprivileged UDP
port while preserving the BASIC default of UDP/69 outside tests. Its server
slice repeatedly runs the same BASIC server under configured TCP and UDP ports
to prove that `RUN` preserves configured listeners while clearing only active
request/message state.

## Syntax Check

After editing these tools:

```sh
python3.11 -m py_compile \
  porttools/basic_serial.py \
  porttools/esp32_fs_vm_smoke.py \
  porttools/esp32_tcp_smoke.py \
  porttools/network_conformance.py \
  porttools/host_network_conformance.py \
  porttools/host_basic_network_conformance.py
```
