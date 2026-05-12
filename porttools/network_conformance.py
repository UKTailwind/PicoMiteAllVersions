#!/usr/bin/env python3
"""Network conformance smoke tests for BASIC WEB-capable ports.

The runner drives a real MMBasic prompt over serial and uses host-side sockets
for the peer endpoints. It intentionally tests the BASIC-visible surface rather
than a specific backend implementation.
"""

from __future__ import annotations

import argparse
import calendar
import queue
import re
import signal
import socket
import threading
import time
import traceback
from dataclasses import dataclass
from typing import Callable

from basic_serial import BasicSerial, default_port, strip_ansi


class SuiteTimeoutError(TimeoutError):
    pass


def _suite_alarm(_signum, _frame) -> None:
    raise SuiteTimeoutError("suite watchdog expired")


@dataclass
class CheckResult:
    name: str
    ok: bool
    detail: str = ""


@dataclass
class ServerLog:
    http_first_line: str = ""
    http_bytes: int = 0
    stream_request: bytes = b""


@dataclass
class MqttLog:
    connected: bool = False
    client_id: str = ""
    published_topic: str = ""
    published_payload: bytes = b""
    subscribed_topic: str = ""
    unsubscribed_topic: str = ""
    disconnected: bool = False


def local_ip_for(target: str = "192.168.4.1") -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((target, 9))
        return sock.getsockname()[0]
    finally:
        sock.close()


def clean_text(text: str) -> str:
    return strip_ansi(text.encode("latin1", "replace")).decode("latin1", "replace")


def marker_value(transcript: str, marker: str) -> str | None:
    matches = re.findall(rf"{re.escape(marker)}([^\r\n]+)", clean_text(transcript))
    if not matches:
        return None
    return matches[-1].strip()


def command_marker(
    basic: BasicSerial,
    command: str,
    marker: str,
    *,
    timeout: float,
) -> str:
    text = basic.command(command, timeout=timeout).text
    value = marker_value(text, marker)
    if value is None:
        raise RuntimeError(f"marker {marker!r} not found after {command!r}\n{clean_text(text)}")
    return value


def sync_boot_waits(base: float) -> list[float]:
    waits: list[float] = []
    for value in (base, max(base, 5.0), max(base, 10.0)):
        if not waits or waits[-1] != value:
            waits.append(value)
    return waits


def reopen_and_sync(basic: BasicSerial, *, timeout: float, boot_wait: float, reset_app: bool = False) -> None:
    basic.close()
    time.sleep(0.5)
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    attempt_timeout = min(20.0, timeout)
    boot_waits = sync_boot_waits(boot_wait)
    attempt = 0
    while time.monotonic() < deadline:
        try:
            basic.open()
            if reset_app:
                basic.reset_app()
            wait = boot_waits[min(attempt, len(boot_waits) - 1)]
            basic.sync(timeout=attempt_timeout, boot_wait=wait)
            return
        except Exception as exc:
            last_error = exc
            basic.close()
            time.sleep(0.5)
            attempt += 1
    if last_error:
        raise last_error
    raise TimeoutError("could not reopen serial port")


def sync_with_reopen_retry(basic: BasicSerial, args: argparse.Namespace) -> None:
    try:
        if args.reset_before_suite:
            basic.reset_app()
        basic.sync(timeout=min(20.0, args.long_timeout), boot_wait=args.boot_wait)
    except Exception:
        errors: list[str] = []
        reset_attempts = [args.reset_before_suite]
        if not args.reset_before_suite:
            reset_attempts.append(True)
        for reset_app in reset_attempts:
            try:
                reopen_and_sync(
                    basic,
                    timeout=args.long_timeout,
                    boot_wait=args.boot_wait,
                    reset_app=reset_app,
                )
                return
            except Exception as exc:
                errors.append(f"reset={reset_app}: {exc}")
        raise TimeoutError("could not sync to BASIC prompt; " + "; ".join(errors))


def maybe_connect(basic: BasicSerial, args: argparse.Namespace) -> None:
    if args.connect_command:
        timeout = args.long_timeout if args.connect_command.upper().startswith("WEB CONNECT") else args.timeout
        basic.command(args.connect_command, timeout=timeout)


def print_checks(checks: list[CheckResult]) -> int:
    print("--- checks ---")
    for check in checks:
        suffix = f" ({check.detail})" if check.detail else ""
        print(f"{check.name}: {'ok' if check.ok else 'FAIL'}{suffix}")
    return 0 if all(check.ok for check in checks) else 1


class TcpSmokeServers:
    def __init__(self, host: str, http_port: int, stream_port: int):
        self.host = host
        self.http_port = http_port
        self.stream_port = stream_port
        self.log = ServerLog()
        self.ready: queue.Queue[str] = queue.Queue()
        self.stop = threading.Event()
        self.threads: list[threading.Thread] = []

    def __enter__(self) -> "TcpSmokeServers":
        self.threads = [
            threading.Thread(target=self._http_server, daemon=True),
            threading.Thread(target=self._stream_server, daemon=True),
        ]
        for thread in self.threads:
            thread.start()
        seen = set()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and seen != {"http", "stream"}:
            try:
                seen.add(self.ready.get(timeout=0.25))
            except queue.Empty:
                pass
        if seen != {"http", "stream"}:
            raise RuntimeError(f"servers did not start: {seen}")
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.stop.set()
        for port in (self.http_port, self.stream_port):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            except OSError:
                pass
        for thread in self.threads:
            thread.join(timeout=1)

    def _http_server(self) -> None:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((self.host, self.http_port))
        srv.listen(5)
        srv.settimeout(0.25)
        self.ready.put("http")
        with srv:
            while not self.stop.is_set():
                try:
                    conn, addr = srv.accept()
                except socket.timeout:
                    continue
                with conn:
                    conn.settimeout(10)
                    data = b""
                    try:
                        while b"\r\n\r\n" not in data and len(data) < 4096:
                            chunk = conn.recv(1024)
                            if not chunk:
                                break
                            data += chunk
                    except socket.timeout:
                        pass
                    first = data.splitlines()[0].decode("latin1", "replace") if data.splitlines() else ""
                    if data:
                        self.log.http_first_line = first
                        self.log.http_bytes = len(data)
                    body = (
                        f"NETCONF_TCP_CLIENT_OK\nFROM={addr[0]}\n"
                        f"REQUEST={first}\nBYTES={len(data)}\n"
                    ).encode("latin1")
                    response = (
                        b"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                        + str(len(body)).encode("ascii")
                        + b"\r\nConnection: close\r\n\r\n"
                        + body
                    )
                    conn.sendall(response)

    def _stream_server(self) -> None:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((self.host, self.stream_port))
        srv.listen(5)
        srv.settimeout(0.25)
        self.ready.put("stream")
        with srv:
            while not self.stop.is_set():
                try:
                    conn, _addr = srv.accept()
                except socket.timeout:
                    continue
                with conn:
                    conn.settimeout(0.25)
                    data = b""
                    deadline = time.monotonic() + 10.0
                    while not self.stop.is_set() and time.monotonic() < deadline:
                        try:
                            chunk = conn.recv(1024)
                        except socket.timeout:
                            continue
                        if not chunk:
                            break
                        data += chunk
                        if b"\n" in data or len(data) >= 1024:
                            break
                    if data:
                        self.log.stream_request = data
                    if data:
                        conn.sendall(b"ACK " + data)
                        for i in range(4):
                            conn.sendall(f"STREAM{i}\n".encode("ascii"))
                            time.sleep(0.05)
                    else:
                        conn.sendall(b"NO_COMMAND\n")


class UdpListener:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.ready: queue.Queue[None] = queue.Queue()
        self.received: queue.Queue[tuple[bytes, tuple[str, int]]] = queue.Queue()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self) -> "UdpListener":
        self.thread.start()
        self.ready.get(timeout=5)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.stop.set()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.sendto(b"", ("127.0.0.1", self.port))
            except OSError:
                pass
        self.thread.join(timeout=1)

    def _serve(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((self.host, self.port))
            sock.settimeout(0.25)
            self.ready.put(None)
            while not self.stop.is_set():
                try:
                    data, addr = sock.recvfrom(4096)
                except socket.timeout:
                    continue
                if data:
                    self.received.put((data, addr))

    def recv(self, timeout: float) -> tuple[bytes, tuple[str, int]] | None:
        try:
            return self.received.get(timeout=timeout)
        except queue.Empty:
            return None


class NtpResponder:
    def __init__(self, host: str, port: int, unix_epoch: int):
        self.host = host
        self.port = port
        self.unix_epoch = unix_epoch
        self.ready: queue.Queue[Exception | None] = queue.Queue()
        self.received: queue.Queue[tuple[bytes, tuple[str, int]]] = queue.Queue()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self) -> "NtpResponder":
        self.thread.start()
        state = self.ready.get(timeout=5)
        if state is not None:
            raise state
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.stop.set()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.sendto(b"", ("127.0.0.1", self.port))
            except OSError:
                pass
        self.thread.join(timeout=1)

    def _serve(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock.bind((self.host, self.port))
            except Exception as exc:
                self.ready.put(exc)
                return
            sock.settimeout(0.25)
            self.ready.put(None)
            while not self.stop.is_set():
                try:
                    data, addr = sock.recvfrom(512)
                except socket.timeout:
                    continue
                if not data:
                    continue
                self.received.put((data, addr))
                response = bytearray(48)
                response[0] = 0x24  # LI=0, version=4, mode=4/server.
                response[1] = 1     # Stratum 1; nonzero is required by ESP32.
                seconds_since_1900 = self.unix_epoch + 2208988800
                stamp = seconds_since_1900.to_bytes(4, "big")
                response[32:36] = stamp
                response[40:44] = stamp
                sock.sendto(response, addr)

    def recv(self, timeout: float) -> tuple[bytes, tuple[str, int]] | None:
        try:
            return self.received.get(timeout=timeout)
        except queue.Empty:
            return None


def mqtt_encode_remaining_length(length: int) -> bytes:
    out = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length:
            byte |= 0x80
        out.append(byte)
        if not length:
            return bytes(out)


def mqtt_read_packet(conn: socket.socket) -> tuple[int, bytes] | None:
    first = conn.recv(1)
    if not first:
        return None
    multiplier = 1
    remaining = 0
    for _ in range(4):
        raw = conn.recv(1)
        if not raw:
            return None
        byte = raw[0]
        remaining += (byte & 0x7F) * multiplier
        if not byte & 0x80:
            break
        multiplier *= 128
    else:
        raise RuntimeError("invalid MQTT remaining length")

    payload = bytearray()
    while len(payload) < remaining:
        chunk = conn.recv(remaining - len(payload))
        if not chunk:
            return None
        payload.extend(chunk)
    return first[0], bytes(payload)


def mqtt_utf8(data: bytes, pos: int) -> tuple[str, int]:
    if pos + 2 > len(data):
        raise RuntimeError("truncated MQTT string")
    length = (data[pos] << 8) | data[pos + 1]
    pos += 2
    if pos + length > len(data):
        raise RuntimeError("truncated MQTT string")
    return data[pos:pos + length].decode("utf-8", "replace"), pos + length


def mqtt_packet(packet_type_flags: int, body: bytes) -> bytes:
    return bytes([packet_type_flags]) + mqtt_encode_remaining_length(len(body)) + body


class MqttTestBroker:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.log = MqttLog()
        self.ready: queue.Queue[Exception | None] = queue.Queue()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self) -> "MqttTestBroker":
        self.thread.start()
        state = self.ready.get(timeout=5)
        if state is not None:
            raise state
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.stop.set()
        try:
            socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
        except OSError:
            pass
        self.thread.join(timeout=1)

    def _serve(self) -> None:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind((self.host, self.port))
            srv.listen(1)
        except Exception as exc:
            self.ready.put(exc)
            srv.close()
            return
        srv.settimeout(0.25)
        self.ready.put(None)
        with srv:
            while not self.stop.is_set():
                try:
                    conn, _addr = srv.accept()
                except socket.timeout:
                    continue
                with conn:
                    conn.settimeout(10)
                    self._handle_client(conn)
                if self.log.connected:
                    return

    def _handle_client(self, conn: socket.socket) -> None:
        while not self.stop.is_set():
            packet = mqtt_read_packet(conn)
            if packet is None:
                return
            header, body = packet
            packet_type = header >> 4
            flags = header & 0x0F
            if packet_type == 1:
                self._handle_connect(conn, body)
            elif packet_type == 3:
                self._handle_publish(conn, flags, body)
            elif packet_type == 8:
                self._handle_subscribe(conn, body)
            elif packet_type == 10:
                self._handle_unsubscribe(conn, body)
            elif packet_type == 14:
                self.log.disconnected = True
                return

    def _handle_connect(self, conn: socket.socket, body: bytes) -> None:
        proto_name, pos = mqtt_utf8(body, 0)
        if proto_name not in ("MQTT", "MQIsdp") or pos + 4 > len(body):
            return
        pos += 4  # protocol level, flags, keepalive
        client_id, _pos = mqtt_utf8(body, pos)
        self.log.connected = True
        self.log.client_id = client_id
        conn.sendall(b"\x20\x02\x00\x00")

    def _handle_publish(self, conn: socket.socket, flags: int, body: bytes) -> None:
        topic, pos = mqtt_utf8(body, 0)
        qos = (flags >> 1) & 0x03
        packet_id = b""
        if qos:
            packet_id = body[pos:pos + 2]
            pos += 2
        self.log.published_topic = topic
        self.log.published_payload = body[pos:]
        if qos == 1 and len(packet_id) == 2:
            conn.sendall(b"\x40\x02" + packet_id)

    def _handle_subscribe(self, conn: socket.socket, body: bytes) -> None:
        if len(body) < 5:
            return
        packet_id = body[:2]
        topic, _pos = mqtt_utf8(body, 2)
        self.log.subscribed_topic = topic
        conn.sendall(b"\x90\x03" + packet_id + b"\x00")
        payload = b"NETCONF_MQTT_IN"
        topic_bytes = topic.encode("utf-8")
        publish_body = len(topic_bytes).to_bytes(2, "big") + topic_bytes + payload
        conn.sendall(mqtt_packet(0x30, publish_body))

    def _handle_unsubscribe(self, conn: socket.socket, body: bytes) -> None:
        if len(body) < 4:
            return
        packet_id = body[:2]
        topic, _pos = mqtt_utf8(body, 2)
        self.log.unsubscribed_topic = topic
        conn.sendall(b"\xB0\x02" + packet_id)


def run_tcp_client(args: argparse.Namespace) -> int:
    mac_ip = args.host or local_ip_for(args.gateway)
    commands = [
        "OPTION BASE 0",
        "DIM INTEGER A%(4096/8)",
        "CR$=CHR$(13)+CHR$(10)",
        f'WEB OPEN TCP CLIENT "{mac_ip}",{args.http_port}',
        f'WEB TCP CLIENT REQUEST "GET /tcp-smoke HTTP/1.0"+CR$+"Host: {mac_ip}"+CR$+CR$,A%(),7000',
        'PRINT "REQ_LEN=";LLEN(A%())',
        'PRINT LGETSTR$(A%(),1,240)',
        "WEB CLOSE TCP CLIENT",
        "PAUSE 500",
        "DIM INTEGER S%(256/8)",
        "DIM INTEGER R%,W%",
        "R%=0:W%=0",
        f'WEB OPEN TCP STREAM "{mac_ip}",{args.stream_port}',
        "PAUSE 100",
        'WEB TCP CLIENT STREAM "INLINE"+CHR$(10),S%(),R%,W%',
        "PAUSE 2000",
        'PRINT "STREAM_PTR=";R%;",";W%',
        "S%(0)=W%",
        'IF W%>0 THEN PRINT LGETSTR$(S%(),1,W%) ELSE PRINT "STREAM_EMPTY"',
        "WEB CLOSE TCP CLIENT",
    ]

    transcript_parts: list[str] = []
    with TcpSmokeServers(args.bind, args.http_port, args.stream_port) as servers:
        with BasicSerial(args.port, args.baud) as basic:
            sync_with_reopen_retry(basic, args)
            maybe_connect(basic, args)
            basic.command("NEW", timeout=args.timeout)
            for command in commands:
                print(f">>> {command}", flush=True)
                timeout = args.long_timeout if command.startswith(("WEB TCP CLIENT REQUEST", "PAUSE ")) else args.timeout
                result = basic.command(command, timeout=timeout)
                print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
                transcript_parts.append(result.text)

    clean = clean_text("".join(transcript_parts))
    checks = [
        CheckResult("tcp_client_http_response", "NETCONF_TCP_CLIENT_OK" in clean and "REQUEST=GET /tcp-smoke HTTP/1.0" in clean),
        CheckResult(
            "tcp_client_http_peer",
            servers.log.http_first_line == "GET /tcp-smoke HTTP/1.0" and servers.log.http_bytes > 0,
            f"first_line={servers.log.http_first_line!r} bytes={servers.log.http_bytes}",
        ),
        CheckResult("tcp_client_stream_peer", servers.log.stream_request == b"INLINE\n", repr(servers.log.stream_request)),
        CheckResult("tcp_client_stream_response", "ACK INLINE" in clean and "STREAM3" in clean),
    ]
    return print_checks(checks)


TCP_SERVER_PROGRAM = [
    "OPTION BASE 0",
    "X%=123",
    "DIM INTEGER B%(2048/8),O%(1024/8)",
    "CR$=CHR$(13)+CHR$(10)",
    'PRINT "NETCONF_TCP_SERVER_READY"',
    "DO",
    "FOR C=1 TO MM.INFO(MAX CONNECTIONS)",
    "IF MM.INFO(TCP REQUEST C) THEN GOSUB SendIt",
    "NEXT C",
    "PAUSE 20",
    "LOOP",
    "SendIt:",
    "WEB TCP READ C,B%()",
    "P$=MM.INFO(TCP PATH C)",
    'IF P$="/netconf/page" THEN WEB TRANSMIT PAGE C,"A:netpage.htm": RETURN',
    'IF P$="/netconf/file" THEN WEB TRANSMIT FILE C,"A:netfile.txt","text/plain": RETURN',
    'IF P$="/netconf/css" THEN WEB TRANSMIT CSS C,"A:netstyle.css": RETURN',
    'IF P$="/netconf/js" THEN WEB TRANSMIT JS C,"A:netscript.js": RETURN',
    'IF P$="/netconf/image" THEN WEB TRANSMIT IMAGE C,"A:netpic.png": RETURN',
    'IF P$="/netconf/code" THEN WEB TRANSMIT CODE C,404: RETURN',
    "LONGSTRING CLEAR O%()",
    'LONGSTRING APPEND O%(),"HTTP/1.0 200 OK"+CR$',
    'LONGSTRING APPEND O%(),"Content-Type: text/plain"+CR$',
    'LONGSTRING APPEND O%(),"Connection: close"+CR$+CR$',
    'LONGSTRING APPEND O%(),"NETCONF_TCP_SERVER_OK"+CHR$(10)',
    'LONGSTRING APPEND O%(),"PATH="+P$+CHR$(10)',
    'LONGSTRING APPEND O%(),"LEN="+STR$(LLEN(B%()))+CHR$(10)',
    "WEB TCP SEND C,O%()",
    "WEB TCP CLOSE C",
    "RETURN",
]

TCP_SERVER_FILES = {
    "A:netpage.htm": ["NETCONF_TRANSMIT_PAGE {X%}"],
    "A:netfile.txt": ["NETCONF_TRANSMIT_FILE"],
    "A:netstyle.css": ["body{color:#123456}"],
    "A:netscript.js": ['console.log("NETCONF_TRANSMIT_JS");'],
    "A:netpic.png": ["NETCONF_TRANSMIT_IMAGE"],
}

UDP_PRESERVE_PROGRAM = [
    'PRINT "NETCONF_UDP_RUN_READY"',
    "DO",
    'IF MM.MESSAGE$="NETCONF_UDP_AFTER_RUN" THEN PRINT "NETCONF_UDP_RUN_MSG=";MM.MESSAGE$: PRINT "NETCONF_UDP_RUN_ADDR=";MM.ADDRESS$: END',
    "PAUSE 20",
    "LOOP",
]


def basic_quote(text: str) -> str:
    return '"' + text.replace('"', '""') + '"'


def basic_string_expr(text: str) -> str:
    parts = text.split('"')
    expr_parts: list[str] = []
    for idx, part in enumerate(parts):
        if part:
            expr_parts.append(basic_quote(part))
        if idx != len(parts) - 1:
            expr_parts.append("CHR$(34)")
    return "+".join(expr_parts) if expr_parts else '""'


def write_program_file(basic: BasicSerial, path: str, lines: list[str], timeout: float) -> None:
    create = f"OPEN {basic_quote(path)} FOR OUTPUT AS #1"
    print(f">>> {create}", flush=True)
    result = basic.command(create, timeout=timeout)
    print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
    try:
        for line in lines:
            command = f"PRINT #1,{basic_string_expr(line)}"
            print(f">>> {command}", flush=True)
            result = basic.command(command, timeout=timeout)
            print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
    finally:
        print(">>> CLOSE #1", flush=True)
        result = basic.command("CLOSE #1", timeout=timeout)
        print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)


def wait_for_substring(basic: BasicSerial, needle: str, timeout: float) -> str:
    assert basic.serial is not None
    deadline = time.monotonic() + timeout
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = basic.serial.read(4096)
        if chunk:
            raw.extend(chunk)
            text = clean_text(raw.decode("latin1", "replace"))
            if needle in text:
                return text
        else:
            time.sleep(0.01)
    raise TimeoutError(f"timed out waiting for {needle!r}\n{clean_text(raw.decode('latin1', 'replace'))}")


def interrupt_to_prompt(basic: BasicSerial, timeout: float) -> None:
    assert basic.serial is not None
    basic.serial.write(b"\x03\r")
    basic.serial.flush()
    raw = basic.wait_for_prompt(timeout)
    if not basic._has_prompt(raw):
        raise TimeoutError(
            "timeout waiting for prompt after interrupt\n" +
            clean_text(raw.decode("latin1", "replace"))
        )


def fetch_http(host: str, port: int, path: str, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while True:
        try:
            with socket.create_connection((host, port), timeout=timeout) as sock:
                sock.settimeout(timeout)
                req = f"GET {path} HTTP/1.0\r\nHost: {host}\r\nConnection: close\r\n\r\n"
                sock.sendall(req.encode("latin1"))
                chunks: list[bytes] = []
                while True:
                    try:
                        chunk = sock.recv(4096)
                    except socket.timeout:
                        break
                    if not chunk:
                        break
                    chunks.append(chunk)
                return b"".join(chunks)
        except OSError as exc:
            last_error = exc
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


def tftp_write_read(host: str, port: int, filename: str, payload: bytes,
                    timeout: float) -> tuple[bool, bytes, str]:
    def packet(opcode: int, *parts: bytes) -> bytes:
        return opcode.to_bytes(2, "big") + b"".join(parts)

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            sock.sendto(packet(2, filename.encode("ascii"), b"\0octet\0"),
                        (host, port))
            data, addr = sock.recvfrom(1024)
            if data != b"\x00\x04\x00\x00":
                return False, b"", f"bad WRQ ack {data!r}"
            sock.sendto(packet(3, b"\x00\x01", payload), addr)
            data, _ = sock.recvfrom(1024)
            if data != b"\x00\x04\x00\x01":
                return False, b"", f"bad DATA ack {data!r}"

            sock.sendto(packet(1, filename.encode("ascii"), b"\0octet\0"),
                        (host, port))
            data, addr = sock.recvfrom(1024)
            if len(data) < 4 or data[:4] != b"\x00\x03\x00\x01":
                return False, b"", f"bad RRQ data {data!r}"
            received = data[4:]
            sock.sendto(packet(4, b"\x00\x01"), addr)
            return True, received, ""
    except OSError as exc:
        return False, b"", f"{type(exc).__name__}: {exc}"


def http_headers(response: bytes) -> dict[str, str]:
    head = response.split(b"\r\n\r\n", 1)[0].decode("latin1", "replace")
    headers: dict[str, str] = {}
    for line in head.splitlines()[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip().lower()
    return headers


def http_status_line(response: bytes) -> str:
    return response.splitlines()[0].decode("latin1", "replace") if response.splitlines() else ""


def run_with_restored_option(
    basic: BasicSerial,
    info_command: str,
    marker: str,
    set_command: Callable[[int], str],
    new_port: int,
    body: Callable[[], int],
    args: argparse.Namespace,
) -> int:
    old_port = int(command_marker(basic, info_command, marker, timeout=args.timeout))
    try:
        if old_port != new_port:
            set_option_with_reboot_tolerance(basic, set_command, new_port, args)
        return body()
    finally:
        if old_port != new_port:
            restore_option_or_raise(basic, set_command, old_port, args)


def set_option_with_reboot_tolerance(
    basic: BasicSerial,
    set_command: Callable[[int], str],
    port: int,
    args: argparse.Namespace,
) -> None:
    try:
        basic.command(set_command(port), timeout=args.long_timeout)
        return
    except Exception:
        reopen_and_sync(
            basic,
            timeout=args.long_timeout,
            boot_wait=max(args.boot_wait, 5.0),
            reset_app=False,
        )


def command_with_reboot_tolerance(
    basic: BasicSerial,
    command: str,
    args: argparse.Namespace,
) -> None:
    try:
        basic.command(command, timeout=args.long_timeout)
        return
    except Exception:
        reopen_and_sync(
            basic,
            timeout=args.long_timeout,
            boot_wait=max(args.boot_wait, 5.0),
            reset_app=False,
        )


def restore_option_or_raise(
    basic: BasicSerial,
    set_command: Callable[[int], str],
    old_port: int,
    args: argparse.Namespace,
) -> None:
    errors: list[str] = []
    try:
        set_option_with_reboot_tolerance(basic, set_command, old_port, args)
        return
    except Exception as exc:
        errors.append(f"current-session: {exc}")

    for reset_app in (False, True):
        try:
            reopen_and_sync(
                basic,
                timeout=args.long_timeout,
                boot_wait=args.boot_wait,
                reset_app=reset_app,
            )
            return
        except Exception as exc:
            errors.append(f"reset={reset_app}: {exc}")
            basic.close()
            time.sleep(1.0)
    raise RuntimeError(
        f"failed to restore port option to {old_port}; " + "; ".join(errors)
    )


def run_tcp_server(args: argparse.Namespace) -> int:
    checks: list[CheckResult] = []
    with BasicSerial(args.port, args.baud) as basic:
        sync_with_reopen_retry(basic, args)
        maybe_connect(basic, args)
        device_host = args.device_host or command_marker(
            basic,
            'PRINT "NETCONF_IP=";MM.INFO(IP ADDRESS)',
            "NETCONF_IP=",
            timeout=args.timeout,
        )

        def body() -> int:
            for path, lines in TCP_SERVER_FILES.items():
                write_program_file(basic, path, lines, args.timeout)
            write_program_file(basic, "A:netconf.bas", TCP_SERVER_PROGRAM, args.timeout)
            assert basic.serial is not None
            running_program = False
            try:
                run_command = 'RUN "A:netconf.bas"'
                print(f">>> {run_command}", flush=True)
                basic.serial.write((run_command + "\r").encode("latin1"))
                basic.serial.flush()
                running_program = True
                wait_for_substring(basic, "NETCONF_TCP_SERVER_READY", args.timeout)
                response = fetch_http(device_host, args.tcp_server_port, "/netconf/path?x=1", args.long_timeout)
                text = response.decode("latin1", "replace")
                print(text, end="" if text.endswith("\n") else "\n", flush=True)
                page = fetch_http(device_host, args.tcp_server_port, "/netconf/page", args.long_timeout)
                file_resp = fetch_http(device_host, args.tcp_server_port, "/netconf/file", args.long_timeout)
                css = fetch_http(device_host, args.tcp_server_port, "/netconf/css", args.long_timeout)
                js = fetch_http(device_host, args.tcp_server_port, "/netconf/js", args.long_timeout)
                image = fetch_http(device_host, args.tcp_server_port, "/netconf/image", args.long_timeout)
                code = fetch_http(device_host, args.tcp_server_port, "/netconf/code", args.long_timeout)
                css_headers = http_headers(css)
                js_headers = http_headers(js)
                image_headers = http_headers(image)
                code_status = http_status_line(code)
                checks.extend(
                    [
                        CheckResult("tcp_server_response", "NETCONF_TCP_SERVER_OK" in text),
                        CheckResult("tcp_server_path", "PATH=/netconf/path?x=1" in text),
                        CheckResult("tcp_server_read_len", re.search(r"LEN=\s*[1-9][0-9]*", text) is not None),
                        CheckResult("transmit_page", b"NETCONF_TRANSMIT_PAGE 123" in page),
                        CheckResult("transmit_file", b"NETCONF_TRANSMIT_FILE" in file_resp),
                        CheckResult("transmit_css_mime", css_headers.get("content-type", "").startswith("text/css"), repr(css_headers)),
                        CheckResult("transmit_js_mime", "javascript" in js_headers.get("content-type", ""), repr(js_headers)),
                        CheckResult("transmit_image_mime", image_headers.get("content-type", "").startswith("image/png"), repr(image_headers)),
                        CheckResult("transmit_code_status", "404" in code_status, repr(code_status)),
                    ]
                )
                interrupt_to_prompt(basic, args.long_timeout)
                running_program = False

                print(f">>> {run_command}", flush=True)
                basic.serial.write((run_command + "\r").encode("latin1"))
                basic.serial.flush()
                running_program = True
                wait_for_substring(basic, "NETCONF_TCP_SERVER_READY", args.timeout)
                after_run = fetch_http(device_host, args.tcp_server_port, "/netconf/after-run", args.long_timeout)
                after_run_text = after_run.decode("latin1", "replace")
                print(after_run_text, end="" if after_run_text.endswith("\n") else "\n", flush=True)
                checks.append(
                    CheckResult(
                        "tcp_server_preserved_after_run",
                        "PATH=/netconf/after-run" in after_run_text,
                    )
                )
                return print_checks(checks)
            finally:
                if running_program:
                    try:
                        interrupt_to_prompt(basic, args.long_timeout)
                    except Exception:
                        pass

        return run_with_restored_option(
            basic,
            'PRINT "NETCONF_OLD_TCP=";MM.INFO(TCP PORT)',
            "NETCONF_OLD_TCP=",
            lambda port: f"OPTION TCP SERVER PORT {port}",
            args.tcp_server_port,
            body,
            args,
        )


def run_udp(args: argparse.Namespace) -> int:
    mac_ip = args.host or local_ip_for(args.gateway)
    checks: list[CheckResult] = []
    with BasicSerial(args.port, args.baud) as basic:
        sync_with_reopen_retry(basic, args)
        maybe_connect(basic, args)
        device_host = args.device_host or command_marker(
            basic,
            'PRINT "NETCONF_IP=";MM.INFO(IP ADDRESS)',
            "NETCONF_IP=",
            timeout=args.timeout,
        )

        def body() -> int:
            with UdpListener(args.bind, args.udp_host_port) as listener:
                basic.command(f'WEB UDP SEND "{mac_ip}",{args.udp_host_port},"NETCONF_UDP_OUT"', timeout=args.timeout)
                packet = listener.recv(args.timeout)
            checks.append(CheckResult("udp_send", packet is not None and packet[0] == b"NETCONF_UDP_OUT" if packet else False))

            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(b"NETCONF_UDP_IN", (device_host, args.udp_server_port))
            basic.command("PAUSE 500", timeout=args.long_timeout)
            msg = command_marker(basic, 'PRINT "NETCONF_UDP_MSG=";MM.MESSAGE$', "NETCONF_UDP_MSG=", timeout=args.timeout)
            addr = command_marker(basic, 'PRINT "NETCONF_UDP_ADDR=";MM.ADDRESS$', "NETCONF_UDP_ADDR=", timeout=args.timeout)
            checks.append(CheckResult("udp_receive_message", msg == "NETCONF_UDP_IN", repr(msg)))
            checks.append(CheckResult("udp_receive_address", bool(addr), repr(addr)))

            write_program_file(basic, "A:netudp.bas", UDP_PRESERVE_PROGRAM, args.timeout)
            assert basic.serial is not None
            run_command = 'RUN "A:netudp.bas"'
            print(f">>> {run_command}", flush=True)
            basic.serial.write((run_command + "\r").encode("latin1"))
            basic.serial.flush()
            wait_for_substring(basic, "NETCONF_UDP_RUN_READY", args.timeout)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(b"NETCONF_UDP_AFTER_RUN", (device_host, args.udp_server_port))
            run_output = wait_for_substring(basic, "NETCONF_UDP_RUN_ADDR=", args.long_timeout)
            prompt_raw = basic.wait_for_prompt(args.timeout)
            if not basic._has_prompt(prompt_raw):
                raise TimeoutError(
                    "timeout waiting for prompt after UDP RUN preservation check\n" +
                    clean_text(prompt_raw.decode("latin1", "replace"))
                )
            checks.append(
                CheckResult(
                    "udp_preserved_after_run",
                    "NETCONF_UDP_RUN_MSG=NETCONF_UDP_AFTER_RUN" in run_output,
                )
            )
            return print_checks(checks)

        return run_with_restored_option(
            basic,
            'PRINT "NETCONF_OLD_UDP=";MM.INFO(UDP PORT)',
            "NETCONF_OLD_UDP=",
            lambda port: f"OPTION UDP SERVER PORT {port}",
            args.udp_server_port,
            body,
            args,
        )


def run_tftp(args: argparse.Namespace) -> int:
    checks: list[CheckResult] = []
    with BasicSerial(args.port, args.baud) as basic:
        sync_with_reopen_retry(basic, args)
        maybe_connect(basic, args)
        device_host = args.device_host or command_marker(
            basic,
            'PRINT "NETCONF_IP=";MM.INFO(IP ADDRESS)',
            "NETCONF_IP=",
            timeout=args.timeout,
        )
        option_list = basic.command("OPTION LIST", timeout=args.timeout).clean_text
        old_disabled = "OPTION TFTP OFF" in option_list
        try:
            command_with_reboot_tolerance(basic, "OPTION TFTP ON", args)
            ok, received, detail = tftp_write_read(
                device_host, args.tftp_port, "net_tftp.txt",
                b"NETCONF_TFTP_OK", args.long_timeout,
            )
            checks.append(
                CheckResult("tftp_roundtrip",
                            ok and received == b"NETCONF_TFTP_OK",
                            detail or repr(received))
            )
            return print_checks(checks)
        finally:
            restore = "OPTION TFTP OFF" if old_disabled else "OPTION TFTP ON"
            try:
                command_with_reboot_tolerance(basic, restore, args)
            except Exception:
                pass


def telnet_read_until(sock: socket.socket, needle: bytes, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        remaining = max(0.05, deadline - time.monotonic())
        sock.settimeout(min(0.5, remaining))
        try:
            chunk = sock.recv(512)
        except socket.timeout:
            continue
        if not chunk:
            break
        data.extend(chunk)
        if needle in data:
            return bytes(data)
    return bytes(data)


def run_telnet(args: argparse.Namespace) -> int:
    checks: list[CheckResult] = []
    with BasicSerial(args.port, args.baud) as basic:
        sync_with_reopen_retry(basic, args)
        maybe_connect(basic, args)
        device_host = args.device_host or command_marker(
            basic,
            'PRINT "NETCONF_IP=";MM.INFO(IP ADDRESS)',
            "NETCONF_IP=",
            timeout=args.timeout,
        )
        option_list = basic.command("OPTION LIST", timeout=args.timeout).clean_text
        old_mode = "OFF"
        if "OPTION TELNET CONSOLE ONLY" in option_list:
            old_mode = "ONLY"
        elif "OPTION TELNET CONSOLE ON" in option_list:
            old_mode = "ON"

        try:
            command_with_reboot_tolerance(basic, "OPTION TELNET CONSOLE ON", args)
            with socket.create_connection((device_host, args.telnet_port),
                                          timeout=args.long_timeout) as sock:
                sock.settimeout(0.2)
                try:
                    sock.recv(128)
                except socket.timeout:
                    pass
                sock.sendall(b"A=7+8:PRINT A\r")
                data = telnet_read_until(sock, b" 15", args.long_timeout)
            checks.append(
                CheckResult(
                    "telnet_console_roundtrip",
                    b" 15" in data,
                    repr(data),
                )
            )
            return print_checks(checks)
        finally:
            if old_mode != "ONLY":
                try:
                    command_with_reboot_tolerance(
                        basic,
                        f"OPTION TELNET CONSOLE {old_mode}",
                        args,
                    )
                except Exception:
                    pass


def parse_basic_time(value: str) -> tuple[int, int, int] | None:
    match = re.fullmatch(r"(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?", value)
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def run_ntp(args: argparse.Namespace) -> int:
    mac_ip = args.host or local_ip_for(args.gateway)
    ntp_epoch = calendar.timegm((2026, 1, 2, 3, 4, 5, 0, 0, 0))
    transcript_parts: list[str] = []
    with NtpResponder(args.bind, args.ntp_port, ntp_epoch) as responder:
        with BasicSerial(args.port, args.baud) as basic:
            sync_with_reopen_retry(basic, args)
            maybe_connect(basic, args)
            commands = [
                f'WEB NTP 0,"{mac_ip}",{args.ntp_timeout_ms}',
                'PRINT "NETCONF_NTP_DATE=";DATE$',
                'PRINT "NETCONF_NTP_TIME=";TIME$',
            ]
            for command in commands:
                print(f">>> {command}", flush=True)
                timeout = args.long_timeout if command.startswith("WEB NTP") else args.timeout
                result = basic.command(command, timeout=timeout)
                print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
                transcript_parts.append(result.text)
        packet = responder.recv(args.timeout)

    clean = clean_text("".join(transcript_parts))
    date_value = marker_value(clean, "NETCONF_NTP_DATE=")
    time_value = marker_value(clean, "NETCONF_NTP_TIME=")
    parsed_time = parse_basic_time(time_value or "")
    checks = [
        CheckResult("ntp_request", packet is not None and len(packet[0]) == 48 and packet[0][0] == 0x1B if packet else False),
        CheckResult("ntp_status", "got ntp response: 02/01/2026 03:04:05" in clean),
        CheckResult("ntp_date", date_value == "02-01-2026", repr(date_value)),
        CheckResult(
            "ntp_time",
            parsed_time is not None and parsed_time[0] == 3 and parsed_time[1] == 4 and 5 <= parsed_time[2] <= 30,
            repr(time_value),
        ),
    ]
    return print_checks(checks)


def run_mqtt(args: argparse.Namespace) -> int:
    mac_ip = args.host or local_ip_for(args.gateway)
    transcript_parts: list[str] = []
    with MqttTestBroker(args.bind, args.mqtt_port) as broker:
        with BasicSerial(args.port, args.baud) as basic:
            sync_with_reopen_retry(basic, args)
            maybe_connect(basic, args)
            commands = [
                f'WEB MQTT CONNECT "{mac_ip}",{args.mqtt_port},"",""',
                'WEB MQTT SUBSCRIBE "netconf/in",0',
                "PAUSE 500",
                'PRINT "NETCONF_MQTT_TOPIC=";MM.TOPIC$',
                'PRINT "NETCONF_MQTT_MSG=";MM.MESSAGE$',
                'WEB MQTT PUBLISH "netconf/out","NETCONF_MQTT_OUT",0,0',
                'WEB MQTT UNSUBSCRIBE "netconf/in"',
                "WEB MQTT CLOSE",
            ]
            for command in commands:
                print(f">>> {command}", flush=True)
                timeout = args.long_timeout if command.startswith(("WEB MQTT CONNECT", "PAUSE ")) else args.timeout
                result = basic.command(command, timeout=timeout)
                print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
                transcript_parts.append(result.text)

    clean = clean_text("".join(transcript_parts))
    topic_value = marker_value(clean, "NETCONF_MQTT_TOPIC=")
    message_value = marker_value(clean, "NETCONF_MQTT_MSG=")
    checks = [
        CheckResult("mqtt_connect", broker.log.connected and bool(broker.log.client_id), broker.log.client_id),
        CheckResult("mqtt_subscribe", broker.log.subscribed_topic == "netconf/in", repr(broker.log.subscribed_topic)),
        CheckResult("mqtt_receive_topic", topic_value == "netconf/in", repr(topic_value)),
        CheckResult("mqtt_receive_message", message_value == "NETCONF_MQTT_IN", repr(message_value)),
        CheckResult(
            "mqtt_publish",
            broker.log.published_topic == "netconf/out" and broker.log.published_payload == b"NETCONF_MQTT_OUT",
            f"topic={broker.log.published_topic!r} payload={broker.log.published_payload!r}",
        ),
        CheckResult("mqtt_unsubscribe", broker.log.unsubscribed_topic == "netconf/in", repr(broker.log.unsubscribed_topic)),
    ]
    return print_checks(checks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "suite",
        choices=("all", "tcp-client", "tcp-server", "udp", "tftp", "telnet", "ntp", "mqtt"),
        help="conformance suite to run",
    )
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--host", help="host/Mac IP reachable from the device; defaults to route toward gateway")
    parser.add_argument("--device-host", help="device IP/hostname; defaults to MM.INFO(IP ADDRESS)")
    parser.add_argument("--gateway", default="192.168.4.1")
    parser.add_argument("--http-port", type=int, default=18180)
    parser.add_argument("--stream-port", type=int, default=18183)
    parser.add_argument("--tcp-server-port", type=int, default=18181)
    parser.add_argument("--udp-host-port", type=int, default=18184)
    parser.add_argument("--udp-server-port", type=int, default=18185)
    parser.add_argument("--tftp-port", type=int, default=69)
    parser.add_argument("--telnet-port", type=int, default=23)
    parser.add_argument("--ntp-port", type=int, default=123,
                        help="local fake NTP responder port; WEB NTP uses port 123 unless externally redirected")
    parser.add_argument("--ntp-timeout-ms", type=int, default=7000)
    parser.add_argument("--mqtt-port", type=int, default=18186)
    parser.add_argument("--connect-command", default="WEB CONNECT")
    parser.add_argument("--boot-wait", type=float, default=2.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=35.0)
    parser.add_argument("--suite-retries", type=int, default=1,
                        help="retry a failing suite this many times before reporting failure")
    parser.add_argument("--suite-timeout", type=float, default=0.0,
                        help="hard wall-clock timeout per suite attempt; default derives from --long-timeout")
    parser.add_argument("--reset-before-suite", dest="reset_before_suite",
                        action="store_true", default=False,
                        help="pulse RTS before each suite sync")
    parser.add_argument("--no-reset-before-suite", dest="reset_before_suite",
                        action="store_false",
                        help=argparse.SUPPRESS)
    args = parser.parse_args()

    suites: list[Callable[[argparse.Namespace], int]]
    if args.suite == "all":
        suites = [run_tcp_client, run_tcp_server, run_udp, run_tftp, run_telnet, run_ntp, run_mqtt]
    elif args.suite == "tcp-client":
        suites = [run_tcp_client]
    elif args.suite == "tcp-server":
        suites = [run_tcp_server]
    elif args.suite == "ntp":
        suites = [run_ntp]
    elif args.suite == "mqtt":
        suites = [run_mqtt]
    elif args.suite == "tftp":
        suites = [run_tftp]
    elif args.suite == "telnet":
        suites = [run_telnet]
    else:
        suites = [run_udp]

    status = 0
    suite_timeout = args.suite_timeout if args.suite_timeout > 0 else max(180.0, args.long_timeout * 4)
    for suite in suites:
        suite_name = suite.__name__.removeprefix("run_").replace("_", "-")
        suite_status = 1
        for attempt in range(1, args.suite_retries + 1):
            label = suite_name if args.suite_retries == 1 else f"{suite_name} attempt {attempt}/{args.suite_retries}"
            print(f"=== {label} ===", flush=True)
            try:
                old_handler = signal.signal(signal.SIGALRM, _suite_alarm)
                signal.setitimer(signal.ITIMER_REAL, suite_timeout)
                suite_status = suite(args)
                signal.setitimer(signal.ITIMER_REAL, 0)
                signal.signal(signal.SIGALRM, old_handler)
            except SuiteTimeoutError as exc:
                signal.setitimer(signal.ITIMER_REAL, 0)
                signal.signal(signal.SIGALRM, old_handler)
                print(f"{suite_name}: FAIL ({exc}; {suite_timeout:.1f}s)", flush=True)
                suite_status = 1
            except Exception:
                signal.setitimer(signal.ITIMER_REAL, 0)
                signal.signal(signal.SIGALRM, old_handler)
                traceback.print_exc()
                suite_status = 1
            if suite_status == 0:
                break
        status |= suite_status
    return status


if __name__ == "__main__":
    raise SystemExit(main())
