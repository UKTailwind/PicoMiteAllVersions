"""
Cross-platform VT100 terminal client for PicoMiteBT (BLE NUS).

Connects directly to the Pico's BLE NUS characteristic, puts the host
console into raw + virtual-terminal mode, and forwards bytes both ways.
The host terminal (Windows ConHost 10+, any Linux terminal, macOS
Terminal/iTerm) renders the VT100 escape sequences MMBasic emits
natively, so no per-sequence emulation is needed here.

Includes XMODEM-CRC send and receive over the same BLE link, so file
transfers don't need an external terminal program.

Usage:
    python ble_term.py [device-name]

In-terminal commands (Ctrl-] then a letter):
    q, x   quit ble_term
    s      send a file to the Pico via XMODEM (after typing
           "XMODEM R" on the MMBasic prompt)
    r      receive a file from the Pico via XMODEM (after typing
           "XMODEM S" on the MMBasic prompt)
    ?      show command help
"""

import argparse
import asyncio
import os
import platform
import sys
import threading

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # host -> device
TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device -> host (notify)

IS_WINDOWS = platform.system() == "Windows"

ESCAPE_KEY = 0x1D  # Ctrl-]   first key to enter command mode
QUIT_KEYS  = (ord('q'), ord('Q'), ord('x'), ord('X'))
SEND_KEYS  = (ord('s'), ord('S'))
RECV_KEYS  = (ord('r'), ord('R'))
CLEAR_KEYS = (ord('c'), ord('C'))
HELP_KEYS  = (ord('?'), ord('h'), ord('H'))

# UI modes
MODE_TERMINAL    = 0   # normal pass-through
MODE_CMD         = 1   # just saw Ctrl-]; awaiting command key
MODE_PROMPT_SEND = 2   # collecting host filename for XMODEM send
MODE_PROMPT_RECV = 3   # collecting host filename for XMODEM receive
MODE_XMODEM      = 4   # XMODEM transfer in progress

# XMODEM protocol constants
SOH = 0x01
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
CRC_C = 0x43   # 'C'
PAD = 0x1A     # SUB; XMODEM padding for last short packet
XBLOCK = 128


# ---------------------------------------------------------------------------
# Terminal raw-mode setup.
# ---------------------------------------------------------------------------

def setup_terminal():
    """
    Put stdin into raw mode and enable virtual-terminal sequence
    processing on both stdin and stdout. Returns a callable that
    restores the original modes when invoked.
    """
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes

        STD_INPUT_HANDLE = -10
        STD_OUTPUT_HANDLE = -11
        ENABLE_VIRTUAL_TERMINAL_INPUT = 0x0200
        ENABLE_PROCESSED_OUTPUT            = 0x0001
        ENABLE_WRAP_AT_EOL_OUTPUT          = 0x0002
        ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
        DISABLE_NEWLINE_AUTO_RETURN        = 0x0008

        k = ctypes.windll.kernel32
        h_in  = k.GetStdHandle(STD_INPUT_HANDLE)
        h_out = k.GetStdHandle(STD_OUTPUT_HANDLE)

        old_in  = wintypes.DWORD()
        old_out = wintypes.DWORD()
        k.GetConsoleMode(h_in,  ctypes.byref(old_in))
        k.GetConsoleMode(h_out, ctypes.byref(old_out))

        new_in = ENABLE_VIRTUAL_TERMINAL_INPUT
        new_out = (ENABLE_PROCESSED_OUTPUT |
                   ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                   ENABLE_WRAP_AT_EOL_OUTPUT |
                   DISABLE_NEWLINE_AUTO_RETURN)
        k.SetConsoleMode(h_in,  new_in)
        k.SetConsoleMode(h_out, new_out)

        def restore():
            k.SetConsoleMode(h_in,  old_in.value)
            k.SetConsoleMode(h_out, old_out.value)
        return restore

    else:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        tty.setraw(fd)

        def restore():
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        return restore


def write_stdout(data: bytes):
    """Write raw bytes to stdout without any line-ending translation."""
    try:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    except Exception:
        pass


def write_msg(s: str):
    """Write a control message to stderr (off the main data stream)."""
    sys.stderr.write(s)
    sys.stderr.flush()


def translate_keys(data: bytes) -> bytes:
    """
    Map host-terminal key sequences to the forms MMBasic's MMInkey
    parser expects:

      * 0x7F (DEL char that Linux/macOS/modern-Windows-VT terminals
        send for the Backspace key) -> 0x08 (BS).
      * ESC [ H  -> ESC [ 1 ~   (Home, xterm form -> VT220 form)
      * ESC O H  -> ESC [ 1 ~   (Home, application-keypad form)
      * ESC [ F  -> ESC [ 4 ~   (End,  xterm form -> VT220 form)
      * ESC O F  -> ESC [ 4 ~   (End,  application-keypad form)

    Arrow keys, Delete, Insert, PgUp/PgDn, and F-keys already match
    forms MMBasic accepts, so they pass through untouched.
    """
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b == 0x7F:
            out.append(0x08)
            i += 1
        elif b == 0x1B and i + 2 < n and data[i + 1] in (ord('['), ord('O')):
            third = data[i + 2]
            if third == ord('H'):
                out.extend(b"\x1b[1~")
                i += 3
            elif third == ord('F'):
                out.extend(b"\x1b[4~")
                i += 3
            else:
                out.append(b)
                i += 1
        else:
            out.append(b)
            i += 1
    return bytes(out)


# ---------------------------------------------------------------------------
# XMODEM-CRC helpers
# ---------------------------------------------------------------------------

def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# BLE NUS terminal
# ---------------------------------------------------------------------------

class Terminal:
    def __init__(self, device_name):
        self.device_name = device_name
        self.client = None
        self.loop = None
        self.input_queue = None   # async queue: stdin bytes -> BLE
        self.xmodem_q = None      # async queue: BLE bytes -> xmodem task
        self.stop_event = None
        self.quit_flag = threading.Event()
        self._exit_xmodem()
        self.filename_buf = bytearray()
        self.session_no = 0
        self.notify_count = 0

    # ----- stdin reader thread -----

    def stdin_reader(self):
        while not self.quit_flag.is_set():
            try:
                data = os.read(0, 64)
            except OSError:
                break
            if not data:
                break
            self._process_stdin(data)

    def _process_stdin(self, data: bytes):
        """Mode-aware processing of bytes from the host keyboard."""
        forward = bytearray()
        for b in data:
            if self.mode == MODE_TERMINAL:
                if b == ESCAPE_KEY:
                    self.mode = MODE_CMD
                    write_msg(
                        "\r\n[ble_term] command? "
                        "(q=quit, s=send, r=recv, c=clear, ?=help)\r\n")
                else:
                    forward.append(b)

            elif self.mode == MODE_CMD:
                if b in QUIT_KEYS:
                    self._exit_xmodem()
                    self.quit_flag.set()
                    if self.loop is not None:
                        self.loop.call_soon_threadsafe(self.stop_event.set)
                    return
                if b in SEND_KEYS:
                    self.mode = MODE_PROMPT_SEND
                    self.filename_buf.clear()
                    write_msg("\r\n[xmodem-send] host file to send: ")
                elif b in RECV_KEYS:
                    self.mode = MODE_PROMPT_RECV
                    self.filename_buf.clear()
                    write_msg("\r\n[xmodem-recv] host file to save into: ")
                elif b in CLEAR_KEYS:
                    self._exit_xmodem()
                    # Clear entire screen + scrollback, then home the
                    # cursor. \033[3J clears scrollback (xterm/Win10+);
                    # \033[2J clears the visible viewport; \033[H homes
                    # the cursor.
                    write_stdout(b"\x1b[3J\x1b[2J\x1b[H")
                elif b in HELP_KEYS:
                    self._exit_xmodem()
                    write_msg(
                        "\r\n[ble_term] commands:\r\n"
                        "  q, x   quit ble_term\r\n"
                        "  s      send a file (after typing 'XMODEM R name' on Pico)\r\n"
                        "  r      receive a file (after typing 'XMODEM S name' on Pico)\r\n"
                        "  c      clear screen\r\n"
                        "  ?, h   this help\r\n")
                else:
                    self._exit_xmodem()
                    write_msg("\r\n[ble_term] (cancelled)\r\n")

            elif self.mode in (MODE_PROMPT_SEND, MODE_PROMPT_RECV):
                if b in (0x0D, 0x0A):
                    # Filename complete — start transfer
                    name = self.filename_buf.decode("utf-8", errors="replace").strip()
                    self.filename_buf.clear()
                    if not name:
                        self._exit_xmodem()
                        write_msg("\r\n[ble_term] (empty filename, cancelled)\r\n")
                    else:
                        is_send = (self.mode == MODE_PROMPT_SEND)
                        self.mode = MODE_XMODEM
                        coro = (self.xmodem_send(name) if is_send
                                else self.xmodem_recv(name))
                        if self.loop is not None:
                            asyncio.run_coroutine_threadsafe(coro, self.loop)
                elif b in (0x08, 0x7F):  # BS or DEL
                    if self.filename_buf:
                        self.filename_buf.pop()
                        write_msg("\b \b")
                elif b == 0x1B:  # ESC = cancel prompt
                    self._exit_xmodem()
                    self.filename_buf.clear()
                    write_msg("\r\n[ble_term] (cancelled)\r\n")
                elif 0x20 <= b < 0x7F:
                    self.filename_buf.append(b)
                    write_msg(chr(b))

            elif self.mode == MODE_XMODEM:
                # Discard keystrokes during transfer (anything typed
                # would break the protocol). ESC could cancel — punt
                # for now; user can always Ctrl-C the python process.
                pass

        if forward and self.loop is not None:
            asyncio.run_coroutine_threadsafe(
                self.input_queue.put(translate_keys(bytes(forward))),
                self.loop)

    # ----- BLE notification callback -----

    def on_notify(self, _handle, data: bytearray):
        self.notify_count += 1
        if self.mode == MODE_XMODEM:
            # Route protocol bytes to the xmodem task.
            if self.loop is not None and self.xmodem_q is not None:
                asyncio.run_coroutine_threadsafe(
                    self.xmodem_q.put(bytes(data)), self.loop)
        else:
            write_stdout(bytes(data))

    # ----- BLE supervisor + writer -----

    async def ble_session(self):
        self.session_no += 1
        sno = self.session_no
        prev_notify = self.notify_count
        write_msg(
            f"\r[ble_term] session #{sno} scanning for '{self.device_name}'...\r\n")
        dev = await BleakScanner.find_device_by_name(
            self.device_name, timeout=30)
        if dev is None:
            write_msg(f"[ble_term] #{sno} not found, retry\r\n")
            return
        write_msg(f"[ble_term] #{sno} connecting to {dev.address}\r\n")
        try:
            async with BleakClient(dev) as client:
                self.client = client
                try:
                    await client.stop_notify(TX)
                except Exception:
                    pass
                await client.start_notify(TX, self.on_notify)
                write_msg(
                    f"[ble_term] #{sno} connected, notify subscribed\r\n")
                while client.is_connected and not self.stop_event.is_set():
                    await asyncio.sleep(0.5)
        except BleakError as e:
            write_msg(f"[ble_term] #{sno} BLE error: {e}\r\n")
        except Exception as e:
            write_msg(f"[ble_term] #{sno} unexpected: {e}\r\n")
        finally:
            self.client = None
            new_notify = self.notify_count - prev_notify
            write_msg(
                f"[ble_term] #{sno} disconnected "
                f"(received {new_notify} notifies this session)\r\n")

    async def supervisor(self):
        while not self.stop_event.is_set():
            await self.ble_session()
            if self.stop_event.is_set():
                return
            await asyncio.sleep(1)

    async def writer_task(self):
        while not self.stop_event.is_set():
            try:
                data = await asyncio.wait_for(
                    self.input_queue.get(), timeout=0.5)
            except asyncio.TimeoutError:
                continue
            await self._send_bytes(data)

    async def _send_bytes(self, data: bytes):
        """MTU-chunked write with retry. Used by both writer_task
        (keyboard input) and the xmodem tasks (protocol bytes)."""
        # Wait for a live client (up to ~30 s) — bytes typed during a
        # reconnect should still go out once the link is back.
        for _ in range(60):
            client = self.client
            if client is not None and client.is_connected:
                break
            if self.stop_event.is_set():
                return
            await asyncio.sleep(0.5)
        else:
            return
        try:
            mtu = client.mtu_size or 23
        except Exception:
            mtu = 23
        max_chunk = max(20, mtu - 3)
        for off in range(0, len(data), max_chunk):
            chunk = data[off:off + max_chunk]
            for attempt in range(10):
                if client is None or not client.is_connected:
                    break
                try:
                    await client.write_gatt_char(RX, chunk, response=True)
                    break
                except Exception:
                    if attempt < 9:
                        await asyncio.sleep(0.1)

    # ----- XMODEM transfers -----

    async def _drain_xmodem_q(self):
        """Discard any queued bytes before starting a transfer."""
        try:
            while True:
                self.xmodem_q.get_nowait()
        except asyncio.QueueEmpty:
            pass

    def _exit_xmodem(self):
        """Switch back to terminal mode and flush any post-transfer
        bytes (typically the MMBasic prompt: ESC[?25h then '>') that
        arrived while we were still routing notifications into the
        xmodem queue. Without this the ESC byte gets stranded in the
        queue and the literal '[?25h>' shows on the terminal.

        Safe to call from any context, including __init__ before
        xmodem_q is allocated."""
        target_mode = MODE_TERMINAL
        self.mode = target_mode
        if self.xmodem_q is None:
            return
        try:
            while True:
                leftover = self.xmodem_q.get_nowait()
                write_stdout(leftover)
        except asyncio.QueueEmpty:
            pass

    async def _read_byte(self, timeout: float, accum: bytearray):
        """Read one byte from the xmodem queue. `accum` carries
        leftover bytes from previous chunks between calls."""
        while not accum:
            try:
                chunk = await asyncio.wait_for(
                    self.xmodem_q.get(), timeout)
            except asyncio.TimeoutError:
                return None
            accum.extend(chunk)
        b = accum[0]
        del accum[0]
        return b

    async def xmodem_send(self, filename: str):
        try:
            with open(filename, "rb") as f:
                data = f.read()
        except OSError as e:
            write_msg(f"\r\n[xmodem] cannot open '{filename}': {e}\r\n")
            self._exit_xmodem()
            return

        size = len(data)
        write_msg(f"\r\n[xmodem] sending '{filename}' ({size} bytes)\r\n")
        await self._drain_xmodem_q()
        accum = bytearray()

        # Wait for receiver to initiate (C for CRC mode, NAK for
        # checksum). 60 second window.
        use_crc = False
        got_init = False
        for _ in range(60):
            b = await self._read_byte(1.0, accum)
            if b is None:
                continue
            if b == CRC_C:
                use_crc = True
                got_init = True
                break
            if b == NAK:
                use_crc = False
                got_init = True
                break
        if not got_init:
            write_msg("[xmodem] timeout waiting for receiver\r\n")
            self._exit_xmodem()
            return

        mode_str = "CRC" if use_crc else "checksum"
        write_msg(f"[xmodem] receiver ready ({mode_str} mode)\r\n")

        pkt_num = 1
        offset = 0
        while offset < size:
            payload = data[offset:offset + XBLOCK]
            if len(payload) < XBLOCK:
                payload = payload + bytes([PAD]) * (XBLOCK - len(payload))

            packet = bytearray()
            packet.append(SOH)
            packet.append(pkt_num & 0xFF)
            packet.append((~pkt_num) & 0xFF)
            packet.extend(payload)
            if use_crc:
                c = crc16_ccitt(bytes(payload))
                packet.append((c >> 8) & 0xFF)
                packet.append(c & 0xFF)
            else:
                cs = sum(payload) & 0xFF
                packet.append(cs)

            ok = False
            for attempt in range(10):
                await self._send_bytes(bytes(packet))
                reply = await self._read_byte(3.0, accum)
                if reply == ACK:
                    ok = True
                    break
                if reply == CAN:
                    second = await self._read_byte(1.0, accum)
                    if second == CAN:
                        write_msg(
                            "\r\n[xmodem] cancelled by remote\r\n")
                        self._exit_xmodem()
                        return
                # NAK or other: retry
            if not ok:
                write_msg(
                    "\r\n[xmodem] too many retries, aborting\r\n")
                self._exit_xmodem()
                return

            offset += XBLOCK
            pkt_num = (pkt_num + 1) & 0xFF
            write_msg(f"\r[xmodem] sent {offset}/{size} bytes  ")

        # End of file: send EOT, expect ACK.
        for attempt in range(10):
            await self._send_bytes(bytes([EOT]))
            reply = await self._read_byte(3.0, accum)
            if reply == ACK:
                break

        write_msg(f"\r\n[xmodem] send complete, {size} bytes\r\n")
        self._exit_xmodem()

    async def xmodem_recv(self, filename: str):
        try:
            f = open(filename, "wb")
        except OSError as e:
            write_msg(f"\r\n[xmodem] cannot open '{filename}': {e}\r\n")
            self._exit_xmodem()
            return

        write_msg(f"\r\n[xmodem] receiving into '{filename}', CRC mode\r\n")
        await self._drain_xmodem_q()
        accum = bytearray()

        expected = 1
        total = 0
        # Initiate by sending 'C' periodically until we see SOH or EOT.
        init_sent = 0
        while init_sent < 10:
            await self._send_bytes(bytes([CRC_C]))
            init_sent += 1
            b = await self._read_byte(3.0, accum)
            if b is not None and b in (SOH, EOT):
                accum.insert(0, b)
                break
        else:
            write_msg("[xmodem] no response from sender\r\n")
            f.close()
            self._exit_xmodem()
            return

        while True:
            b = await self._read_byte(10.0, accum)
            if b is None:
                write_msg("\r\n[xmodem] timeout, aborting\r\n")
                await self._send_bytes(bytes([CAN, CAN]))
                f.close()
                self._exit_xmodem()
                return
            if b == EOT:
                await self._send_bytes(bytes([ACK]))
                break
            if b == CAN:
                second = await self._read_byte(1.0, accum)
                if second == CAN:
                    write_msg("\r\n[xmodem] cancelled by sender\r\n")
                    f.close()
                    self._exit_xmodem()
                    return
                continue
            if b != SOH:
                # Discard unexpected byte
                continue

            seq  = await self._read_byte(1.0, accum)
            iseq = await self._read_byte(1.0, accum)
            buf = bytearray()
            for _ in range(XBLOCK):
                bb = await self._read_byte(1.0, accum)
                if bb is None:
                    break
                buf.append(bb)
            ch = await self._read_byte(1.0, accum)
            cl = await self._read_byte(1.0, accum)

            valid = (seq is not None and iseq is not None
                     and len(buf) == XBLOCK
                     and ch is not None and cl is not None
                     and seq == ((~iseq) & 0xFF))
            if valid:
                received_crc = (ch << 8) | cl
                calc_crc = crc16_ccitt(bytes(buf))
                if calc_crc != received_crc:
                    valid = False

            if not valid:
                await self._send_bytes(bytes([NAK]))
                continue

            if seq == expected:
                f.write(bytes(buf))
                total += XBLOCK
                expected = (expected + 1) & 0xFF
                write_msg(f"\r[xmodem] received {total} bytes  ")
            # Duplicate (already-received) packet: ACK without storing.
            await self._send_bytes(bytes([ACK]))

        f.close()
        write_msg(f"\r\n[xmodem] receive complete, {total} bytes "
                  f"(may include trailing 0x1A padding)\r\n")
        self._exit_xmodem()

    # ----- entry point -----

    async def main(self):
        self.loop = asyncio.get_running_loop()
        self.input_queue = asyncio.Queue()
        self.xmodem_q = asyncio.Queue()
        self.stop_event = asyncio.Event()
        threading.Thread(target=self.stdin_reader, daemon=True).start()
        await asyncio.gather(self.supervisor(), self.writer_task())


def main():
    p = argparse.ArgumentParser(
        description="BLE NUS VT100 terminal for PicoMiteBT")
    p.add_argument("device", nargs="?", default="Pico-RP2350",
                   help="BLE device name (default: Pico-RP2350)")
    args = p.parse_args()

    write_msg(
        "\r\n"
        "===========================================================\r\n"
        "  ble_term  -  BLE NUS terminal for PicoMiteBT\r\n"
        f"  target device: {args.device}\r\n"
        "  Ctrl-C is forwarded to MMBasic (BASIC break key).\r\n"
        "  Press Ctrl-] then:  q=quit  s=XMODEM-send  r=XMODEM-recv\r\n"
        "                      c=clear screen  ?=help\r\n"
        "===========================================================\r\n"
        "\r\n")

    restore = setup_terminal()
    term = Terminal(args.device)
    try:
        asyncio.run(term.main())
    except KeyboardInterrupt:
        pass
    finally:
        restore()
        write_msg("\r\n[ble_term] bye\r\n")


if __name__ == "__main__":
    main()
