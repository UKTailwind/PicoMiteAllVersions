#!/usr/bin/env python3
"""Telnet transport for MMBasic smoke tests.

Mirrors the interface of `BasicSerial` so the existing smoke scripts can
swap transports with a CLI flag instead of being rewritten. Drives the
same MMBasic prompt the USB-CDC console drives, just over TCP — which is
exactly what makes it a useful regression gate after the audit's
Finding 2 + normaliser consolidation: telnet input should now arrive at
MMInkey via the same shared escape decoder + byte-normaliser path as
USB-CDC bytes.

Both Pico (WebMite variants) and ESP32-S3 Metro support telnet:

  OPTION TELNET CONSOLE ON     ' enable; ONLY suppresses USB-CDC
  PRINT MM.INFO$(IP ADDRESS)   ' find the IP

Usage:
    from basic_telnet import BasicTelnet
    with BasicTelnet("192.168.1.42", port=23) as basic:
        basic.sync(timeout=8.0)
        basic.command('PRINT "ok"')
"""

from __future__ import annotations

import re
import socket
import time
from typing import Any

ANSI_RE = re.compile(rb"\x1b\[[0-9;:]*[A-Za-z]")


def _strip_ansi(data: bytes) -> bytes:
    return ANSI_RE.sub(b"", data)


# Telnet IAC sequences we eat on incoming side so the BASIC prompt parser
# never sees them. RFC 854: IAC = 0xFF; IAC IAC = literal 0xFF data byte;
# IAC <verb> <option> (verbs 251-254) = 3-byte negotiation; IAC <single>
# (250-255 minus the above) = 2-byte command. SB...SE is consumed too.
IAC = 0xFF
SB, SE = 250, 240


def _strip_iac(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b == 0x00:
            # RFC 854 NVT: bare CR is transmitted as CR NUL; the NUL is
            # the "ignore me, the CR was literal" marker. Standalone NUL
            # bytes are also legal as no-ops. Either way, drop them so
            # smoke regexes don't trip on them.
            i += 1
            continue
        if b != IAC:
            out.append(b)
            i += 1
            continue
        # IAC seen.
        if i + 1 >= n:
            # IAC at buffer edge — drop the lone IAC; the next read will
            # bring its continuation. Smoke tests aren't worried about
            # split-segment IAC sequences.
            break
        nxt = data[i + 1]
        if nxt == IAC:
            out.append(IAC)  # literal 0xFF
            i += 2
            continue
        if nxt == SB:
            # Skip subnegotiation until IAC SE.
            j = i + 2
            while j < n - 1:
                if data[j] == IAC and data[j + 1] == SE:
                    j += 2
                    break
                j += 1
            i = j
            continue
        if 251 <= nxt <= 254:
            # WILL / WONT / DO / DONT — skip IAC + verb + option.
            i += 3
            continue
        # Standalone IAC command (NOP, GA, etc.).
        i += 2
    return bytes(out)


class _SocketAsSerial:
    """A pyserial-compatible read/write surface over a TCP socket. The
    smokes poke at `basic.serial.read(N)`, `.write(b)`, `.flush()`, and
    `.reset_input_buffer()`; this class implements just enough of that
    surface to keep them working."""

    def __init__(self, sock: socket.socket, read_timeout: float):
        self._sock = sock
        self.timeout = read_timeout
        self._sock.settimeout(read_timeout)
        self._pending = bytearray()  # bytes left over after IAC stripping

    def read(self, n: int) -> bytes:
        out = bytearray()
        while len(out) < n:
            if self._pending:
                take = min(n - len(out), len(self._pending))
                out.extend(self._pending[:take])
                del self._pending[:take]
                continue
            try:
                chunk = self._sock.recv(4096)
            except (socket.timeout, BlockingIOError, TimeoutError):
                break
            if not chunk:
                break
            stripped = _strip_iac(chunk)
            if not stripped:
                # Whole chunk was telnet protocol bytes — keep looking.
                continue
            take = min(n - len(out), len(stripped))
            out.extend(stripped[:take])
            if take < len(stripped):
                self._pending.extend(stripped[take:])
        return bytes(out)

    def write(self, data: bytes) -> int:
        # Escape any literal 0xFF in payload to IAC IAC per RFC 854.
        if IAC.to_bytes(1, "little") in data:
            data = data.replace(bytes([IAC]), bytes([IAC, IAC]))
        self._sock.sendall(data)
        return len(data)

    def flush(self) -> None:
        # TCP has no userspace buffer here; sendall already pushed it.
        return None

    def reset_input_buffer(self) -> None:
        # Drain anything queued on the socket.
        self._sock.settimeout(0.0)
        try:
            while True:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
        except (socket.timeout, BlockingIOError, TimeoutError):
            pass
        finally:
            self._sock.settimeout(self.timeout)
        self._pending.clear()


class CommandResult:
    """Mirrors basic_serial.CommandResult so the smokes don't care which
    transport they got."""

    def __init__(self, command: str, raw: bytes):
        self.command = command
        self.raw = raw

    @property
    def text(self) -> str:
        return self.raw.decode("latin1", "replace")

    @property
    def clean_text(self) -> str:
        return _strip_ansi(self.raw).decode("latin1", "replace")


class BasicTelnet:
    def __init__(self, host: str, port: int = 23, read_timeout: float = 0.05):
        self.host = host
        self.port = port
        self.read_timeout = read_timeout
        self._sock: socket.socket | None = None
        self.serial: _SocketAsSerial | None = None

    def __enter__(self) -> "BasicTelnet":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def open(self) -> None:
        sock = socket.create_connection((self.host, self.port), timeout=5.0)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock = sock
        self.serial = _SocketAsSerial(sock, self.read_timeout)
        # Let the device's telnet daemon push its initial IAC option
        # block before we send anything; otherwise our Ctrl-C / Enter
        # gets interleaved with the negotiation.
        time.sleep(0.3)
        self.serial.reset_input_buffer()

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._sock.close()
            self._sock = None
            self.serial = None

    # The following methods mirror BasicSerial exactly, with the same
    # behaviour. We can't easily inherit because BasicSerial owns pyserial
    # plumbing, but the algorithms below operate purely against the
    # `.serial.read/.write/.flush` surface we share with it.

    def read_for(self, seconds: float) -> bytes:
        assert self.serial is not None
        end = time.monotonic() + seconds
        out = bytearray()
        while time.monotonic() < end:
            chunk = self.serial.read(4096)
            if chunk:
                out.extend(chunk)
        return bytes(out)

    @staticmethod
    def _has_prompt(raw: bytes) -> bool:
        clean = _strip_ansi(raw).replace(b"\r", b"\n")
        tail = clean[-300:].rstrip()
        return tail.endswith(b">")

    def wait_for_prompt(self, timeout: float) -> bytes:
        assert self.serial is not None
        end = time.monotonic() + timeout
        out = bytearray()
        while time.monotonic() < end:
            chunk = self.serial.read(4096)
            if chunk:
                out.extend(chunk)
                if self._has_prompt(bytes(out)):
                    return bytes(out)
            else:
                time.sleep(0.005)
        return bytes(out)

    def sync(self, timeout: float = 8.0, boot_wait: float = 0.0) -> bytes:
        assert self.serial is not None
        out = bytearray()
        if boot_wait:
            out.extend(self.read_for(boot_wait))
        self.serial.write(b"\x03\r")
        self.serial.flush()
        out.extend(self.wait_for_prompt(timeout))
        if not self._has_prompt(bytes(out)):
            self.serial.write(b"\r")
            self.serial.flush()
            out.extend(self.wait_for_prompt(timeout))
        if not self._has_prompt(bytes(out)):
            clean_tail = _strip_ansi(bytes(out))[-240:].decode("latin1", "replace")
            raise TimeoutError(
                f"could not sync to BASIC prompt; captured {len(out)} bytes; "
                f"tail={clean_tail!r}"
            )
        return bytes(out)

    def command(self, command: str, timeout: float = 10.0, check_error: bool = True) -> CommandResult:
        assert self.serial is not None
        self.serial.write((command + "\r").encode("latin1"))
        self.serial.flush()
        raw = self.wait_for_prompt(timeout)
        result = CommandResult(command, raw)
        if check_error and ("Error :" in result.clean_text or "Error:" in result.clean_text):
            raise RuntimeError(f"BASIC error after {command!r}\n{result.clean_text}")
        if not self._has_prompt(raw):
            raise TimeoutError(f"timeout waiting for prompt after {command!r}\n{result.clean_text}")
        return result


# Re-export for symmetry with basic_serial.strip_ansi
strip_ansi = _strip_ansi


def open_transport(spec: str) -> Any:
    """Open either a serial or telnet transport from a spec string.

    `spec` is either:
      - "host[:port]"        → BasicTelnet
      - "/dev/cu.usb..."     → BasicSerial (delegates to basic_serial)
      - "telnet:host[:port]" → BasicTelnet (explicit)

    This is the recommended factory for smokes that want a `--target`
    flag accepting either transport.
    """
    import sys as _sys
    _sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from basic_serial import BasicSerial  # noqa: E402

    if spec.startswith("telnet:"):
        rest = spec[len("telnet:"):]
        host, _, port_s = rest.partition(":")
        port = int(port_s) if port_s else 23
        return BasicTelnet(host, port)
    if spec.startswith("/dev/"):
        return BasicSerial(spec)
    # Default heuristic: anything with a dot or colon and no slash is a
    # network target.
    if "/" not in spec and ("." in spec or ":" in spec):
        host, _, port_s = spec.partition(":")
        port = int(port_s) if port_s else 23
        return BasicTelnet(host, port)
    return BasicSerial(spec)
