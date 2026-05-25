"""
BLE NUS <-> TCP bridge for PicoMiteBT.

Speaks just enough Telnet (IAC option negotiation) to convince
Tera Term in default "Service: Telnet" mode to operate in
char-at-a-time mode with server echo - matching the WEB build's
MMtelnet.c behaviour.

Usage:
    python ble_bridge.py [device-name] [--port N] [--host H]

Then in Tera Term: File -> New connection -> TCP/IP, host 127.0.0.1,
service Telnet, port 5555 (or whatever --port specifies). Each keypress
is sent immediately; Ctrl-C, Escape, F-keys, etc. pass through.

The bridge keeps the TCP listener open across BLE drops, so when the
Pico reboots (e.g. after OPTION CPU_SPEED) the bridge transparently
reconnects underneath.
"""

import argparse
import asyncio
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # host -> device
TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device -> host (notify)

# Telnet negotiation sent to the TCP client on connect. Same sequence
# as PicoMite WEB build's MMtelnet.c so Tera Term ends up in
# char-at-a-time / server-echo mode.
#   IAC WILL SGA    : we will suppress go-ahead
#   IAC DO   SGA    : please do too
#   IAC WILL ECHO   : we will handle echo (so don't local-echo)
#   IAC DO   NAWS   : would like window size
#   IAC DONT NAWS   : never mind (matches MMtelnet)
TELNET_INIT = bytes([255, 251, 3,
                     255, 253, 3,
                     255, 251, 1,
                     255, 253, 34,
                     255, 254, 34])

IAC  = 0xFF
WILL = 0xFB
WONT = 0xFC
DO   = 0xFD
DONT = 0xFE
SB   = 0xFA
SE   = 0xF0


def strip_telnet(data: bytes, state: dict) -> bytes:
    """
    Filter inbound TCP bytes to undo Telnet NVT encoding before
    forwarding to the BLE NUS RX characteristic. Two transformations:

      * Strip Telnet IAC sequences (option negotiation, subnegotiation,
        and IAC IAC -> single 0xFF data byte).
      * Un-stuff CR NUL pairs back to a single CR. The Telnet sender
        must follow every CR data byte with NUL; the receiver must
        strip the NUL. Essential for XMODEM, where 8-bit data with
        embedded CRs would otherwise be inflated by NULs and break
        packet validation.

    `state` is a per-connection dict carrying the parse FSM across
    read() calls. Same protocol handling MMtelnet.c uses on the WEB
    build, mirrored here for the BLE NUS bridge.

    IAC states:
      0: normal data
      1: just saw IAC, awaiting command
      2: saw IAC + WILL/WONT/DO/DONT, awaiting option byte
      3: inside subnegotiation, awaiting IAC SE
      4: inside subnegotiation, just saw IAC
    """
    out = bytearray()
    s = state.get("s", 0)
    after_cr = state.get("cr", False)
    for b in data:
        if s == 0:
            if b == IAC:
                s = 1
                after_cr = False
            elif after_cr and b == 0x00:
                # CR NUL stuffing -> drop the NUL; CR already emitted.
                after_cr = False
            else:
                out.append(b)
                after_cr = (b == 0x0D)
        elif s == 1:
            if b == IAC:
                # IAC IAC = literal 0xFF data byte
                out.append(IAC)
                after_cr = False
                s = 0
            elif b in (WILL, WONT, DO, DONT):
                s = 2
            elif b == SB:
                s = 3
            else:
                # 2-byte command (NOP, AYT, etc.) - just consume
                s = 0
        elif s == 2:
            # option byte after WILL/WONT/DO/DONT - consume
            s = 0
        elif s == 3:
            if b == IAC:
                s = 4
        elif s == 4:
            if b == SE:
                s = 0
            else:
                s = 3
    state["s"] = s
    state["cr"] = after_cr
    return bytes(out)


class Bridge:
    def __init__(self, device_name):
        self.device_name = device_name
        self.client = None
        self.writer = None
        self.tcp_lock = asyncio.Lock()

    def on_notify(self, _handle, data: bytearray):
        if self.writer is not None and not self.writer.is_closing():
            try:
                payload = bytes(data)
                # Telnet NVT encoding for outbound bytes:
                #   * 0xFF (IAC) -> 0xFF 0xFF (doubled, so it isn't
                #     mistaken for an IAC command).
                #   * 0x0D (CR)  -> 0x0D 0x00 (NUL stuffing so the
                #     Telnet client treats it as data CR, not a line
                #     terminator). Essential for XMODEM where 8-bit
                #     data may contain CRs.
                # Order matters: do FF doubling FIRST so the NULs we
                # add after CRs aren't themselves consumed by IAC.
                if b"\xff" in payload:
                    payload = payload.replace(b"\xff", b"\xff\xff")
                if b"\x0d" in payload:
                    payload = payload.replace(b"\x0d", b"\x0d\x00")
                self.writer.write(payload)
            except Exception:
                pass

    async def ble_session(self):
        print(f"[BLE] scanning for '{self.device_name}'...")
        dev = await BleakScanner.find_device_by_name(
            self.device_name, timeout=30)
        if dev is None:
            print(f"[BLE] not found, will retry")
            return
        print(f"[BLE] found {dev.address}, connecting...")
        try:
            async with BleakClient(dev) as client:
                self.client = client
                print("[BLE] connected, subscribing to TX notifications")
                await client.start_notify(TX, self.on_notify)
                while client.is_connected:
                    await asyncio.sleep(0.5)
                print("[BLE] disconnected")
        except BleakError as e:
            print(f"[BLE] error: {e}")
        except Exception as e:
            print(f"[BLE] unexpected: {e}")
        finally:
            self.client = None

    async def ble_supervisor(self):
        while True:
            await self.ble_session()
            await asyncio.sleep(1)

    async def handle_tcp(self, reader, writer):
        peer = writer.get_extra_info("peername")
        async with self.tcp_lock:
            print(f"[TCP] client connected from {peer}")
            self.writer = writer
            # Send Telnet init so Tera Term enters char-at-a-time mode.
            try:
                writer.write(TELNET_INIT)
                await writer.drain()
            except Exception:
                pass
            iac_state = {"s": 0}
            try:
                while True:
                    data = await reader.read(256)
                    if not data:
                        break
                    clean = strip_telnet(data, iac_state)
                    if clean and self.client is not None \
                            and self.client.is_connected:
                        await self.send_to_ble(clean)
            except ConnectionResetError:
                pass
            finally:
                self.writer = None
                writer.close()
                try:
                    await writer.wait_closed()
                except Exception:
                    pass
                print(f"[TCP] client {peer} disconnected")

    async def send_to_ble(self, data: bytes):
        """Send to NUS RX in MTU-safe chunks.

        Windows BLE returns E_INVALIDARG (-2147024809 / "The parameter
        is incorrect") if the payload exceeds the current MTU - 3, and
        also throttles fire-and-forget (response=False) writes — too
        many queued at once raises the same error. Two defences:

        1. Always cap chunks to current MTU - 3 (or 20 bytes for the
           default BLE MTU of 23, which is what the link uses for the
           first second or so after reconnect until MTU exchange).
        2. Use response=True to apply backpressure — slower (each
           write waits for the peer ACK) but eliminates the "queue
           full" rejections on bulk transfers like AutoSave paste-ins.
        """
        # Cache the client locally; the supervisor task can null
        # self.client at any point between our awaits when the BLE
        # link drops.
        client = self.client
        if client is None or not client.is_connected:
            return
        try:
            mtu = client.mtu_size or 23
        except Exception:
            mtu = 23
        max_chunk = max(20, mtu - 3)
        for off in range(0, len(data), max_chunk):
            chunk = data[off:off + max_chunk]
            # Up to 10 retries with 100 ms backoff = 1 s total. Covers
            # transient MTU-exchange windows, Windows queue stalls, and
            # ATT_ERROR_INSUFFICIENT_RESOURCES from the Pico when its
            # RX ring is full (low-CPU AutoSave scenario).
            for attempt in range(10):
                # Re-check on each retry — the link may have dropped
                # while we were waiting between retries.
                if client is None or not client.is_connected:
                    return
                try:
                    await client.write_gatt_char(
                        RX, chunk, response=True)
                    break
                except Exception as e:
                    if attempt == 9:
                        print(f"[TCP] write failed: {e}")
                    else:
                        await asyncio.sleep(0.1)


async def main():
    p = argparse.ArgumentParser()
    p.add_argument("device", nargs="?", default="Pico-RP2350",
                   help="BLE device name (default: Pico-RP2350)")
    p.add_argument("--port", type=int, default=5555,
                   help="Local TCP port (default: 5555)")
    p.add_argument("--host", default="127.0.0.1",
                   help="Local bind host (default: 127.0.0.1)")
    args = p.parse_args()

    bridge = Bridge(args.device)
    server = await asyncio.start_server(
        bridge.handle_tcp, args.host, args.port)
    print(f"[TCP] listening on telnet://{args.host}:{args.port}")
    print(f"[BLE] target device: '{args.device}'")
    async with server:
        await asyncio.gather(server.serve_forever(), bridge.ble_supervisor())


try:
    asyncio.run(main())
except KeyboardInterrupt:
    print("\nbye")
