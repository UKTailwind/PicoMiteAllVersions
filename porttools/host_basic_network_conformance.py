#!/usr/bin/env python3
"""Run host-native BASIC WEB command conformance checks."""

from __future__ import annotations

import argparse
import calendar
import os
import pty
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
import select
from dataclasses import dataclass
from pathlib import Path

from network_conformance import MqttTestBroker


NTP_UNIX_DELTA = 2_208_988_800


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def free_port(kind: int = socket.SOCK_STREAM) -> int:
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class HttpPeer:
    def __init__(self, port: int) -> None:
        self.port = port
        self.first_line = ""
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()

    def __enter__(self) -> "HttpPeer":
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        if not self.ready.wait(2.0):
            raise RuntimeError("HTTP peer did not start")
        return self

    def __exit__(self, *_: object) -> None:
        if self.thread:
            self.thread.join(2.0)

    def _run(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind(("127.0.0.1", self.port))
            srv.listen(1)
            self.ready.set()
            conn, _ = srv.accept()
            with conn:
                conn.settimeout(2.0)
                data = bytearray()
                while b"\r\n\r\n" not in data and len(data) < 4096:
                    chunk = conn.recv(1024)
                    if not chunk:
                        break
                    data.extend(chunk)
                self.first_line = data.splitlines()[0].decode("latin1", "replace") if data else ""
                body = b"HOST_BASIC_TCP_OK\n"
                response = (
                    b"HTTP/1.0 200 OK\r\n"
                    b"Content-Type: text/plain\r\n"
                    b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
                    b"Connection: close\r\n\r\n" + body
                )
                conn.sendall(response)


class StreamPeer:
    def __init__(self, port: int) -> None:
        self.port = port
        self.first_line = ""
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()

    def __enter__(self) -> "StreamPeer":
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        if not self.ready.wait(2.0):
            raise RuntimeError("TCP stream peer did not start")
        return self

    def __exit__(self, *_: object) -> None:
        if self.thread:
            self.thread.join(2.0)

    def _run(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind(("127.0.0.1", self.port))
            srv.listen(1)
            self.ready.set()
            conn, _ = srv.accept()
            with conn:
                conn.settimeout(2.0)
                data = bytearray()
                while b"\n" not in data and len(data) < 1024:
                    chunk = conn.recv(128)
                    if not chunk:
                        break
                    data.extend(chunk)
                self.first_line = data.splitlines()[0].decode("latin1", "replace") if data else ""
                conn.sendall(b"HOST_BASIC_STREAM_OK\n")
                time.sleep(0.2)


class UdpPeer:
    def __init__(self, port: int) -> None:
        self.port = port
        self.packet: bytes | None = None
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()

    def __enter__(self) -> "UdpPeer":
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        if not self.ready.wait(2.0):
            raise RuntimeError("UDP peer did not start")
        return self

    def __exit__(self, *_: object) -> None:
        if self.thread:
            self.thread.join(2.0)

    def _run(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", self.port))
            sock.settimeout(5.0)
            self.ready.set()
            try:
                self.packet, _ = sock.recvfrom(2048)
            except socket.timeout:
                self.packet = None


class NtpPeer:
    def __init__(self, port: int, unix_seconds: int) -> None:
        self.port = port
        self.unix_seconds = unix_seconds
        self.request: bytes | None = None
        self.thread: threading.Thread | None = None
        self.ready = threading.Event()

    def __enter__(self) -> "NtpPeer":
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        if not self.ready.wait(2.0):
            raise RuntimeError("NTP peer did not start")
        return self

    def __exit__(self, *_: object) -> None:
        if self.thread:
            self.thread.join(2.0)

    def _run(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", self.port))
            sock.settimeout(5.0)
            self.ready.set()
            try:
                request, addr = sock.recvfrom(256)
            except socket.timeout:
                self.request = None
                return
            self.request = request
            response = bytearray(48)
            response[0] = 0x24
            response[1] = 1
            ntp_seconds = self.unix_seconds + NTP_UNIX_DELTA
            struct.pack_into("!I", response, 40, ntp_seconds)
            sock.sendto(response, addr)


def basic_program(http_port: int, stream_port: int, udp_port: int, ntp_port: int, mqtt_port: int) -> str:
    return f"""OPTION BASE 0
DIM INTEGER A%(4096/8)
DIM INTEGER S%(1024/8)
R%=0:W%=0
CR$=CHR$(13)+CHR$(10)
WEB CONNECT
PRINT "HOST_BASIC_IP=";MM.INFO(IP ADDRESS)
PRINT "HOST_BASIC_TCPIP=";MM.INFO(TCPIP STATUS)
WEB OPEN TCP CLIENT "127.0.0.1",{http_port},2000
WEB TCP CLIENT REQUEST "GET /host-basic HTTP/1.0"+CR$+"Host: 127.0.0.1"+CR$+CR$,A%(),2000
PRINT "HOST_BASIC_TCP_LEN=";LLEN(A%())
PRINT LGETSTR$(A%(),1,LLEN(A%()))
WEB CLOSE TCP CLIENT
WEB OPEN TCP STREAM "127.0.0.1",{stream_port},2000
WEB TCP CLIENT STREAM "STREAM /host-basic"+CR$,S%(),R%,W%
PAUSE 500
S%(0)=W%
PRINT "HOST_BASIC_STREAM_POS=";R%;",";W%
PRINT "HOST_BASIC_STREAM_DATA=";LGETSTR$(S%(),1,W%)
WEB CLOSE TCP CLIENT
WEB UDP SEND "127.0.0.1",{udp_port},"HOST_BASIC_UDP_OK"
WEB NTP 0,"127.0.0.1:{ntp_port}",2000
PRINT "HOST_BASIC_NTP_DATE=";DATE$
PRINT "HOST_BASIC_NTP_TIME=";TIME$
WEB MQTT CONNECT "127.0.0.1",{mqtt_port},"",""
WEB MQTT SUBSCRIBE "host/basic/in",0
PAUSE 500
PRINT "HOST_BASIC_MQTT_TOPIC=";MM.TOPIC$
PRINT "HOST_BASIC_MQTT_MSG=";MM.MESSAGE$
WEB MQTT PUBLISH "host/basic/out","HOST_BASIC_MQTT_OUT",0,0
WEB MQTT UNSUBSCRIBE "host/basic/in"
WEB MQTT CLOSE
"""


def server_basic_program() -> str:
    return """OPTION BASE 0
X%=123
GOTUDP=0
DIM INTEGER B%(2048/8),O%(1024/8)
CR$=CHR$(13)+CHR$(10)
OPEN "B:hostpage.htm" FOR OUTPUT AS #1
PRINT #1,"HOST_BASIC_PAGE {X%}"
CLOSE #1
OPEN "B:hostfile.txt" FOR OUTPUT AS #1
PRINT #1,"HOST_BASIC_FILE"
CLOSE #1
WEB CONNECT
WEB TCP INTERRUPT WebRequest
PRINT "HOST_BASIC_SERVER_READY"
SERVED=0
DO
IF GOTUDP=0 AND MM.MESSAGE$<>"" THEN PRINT "HOST_BASIC_UDP_RECV=";MM.MESSAGE$:PRINT "HOST_BASIC_UDP_ADDR=";MM.ADDRESS$:GOTUDP=1
PAUSE 10
LOOP
SUB WebRequest
FOR C=1 TO MM.INFO(MAX CONNECTIONS)
IF MM.INFO(TCP REQUEST C) THEN GOSUB SendIt
NEXT C
END SUB
SendIt:
WEB TCP READ C,B%()
P$=MM.INFO(TCP PATH C)
IF P$="/host/page" THEN WEB TRANSMIT PAGE C,"B:hostpage.htm":GOTO Sent
IF P$="/host/file" THEN WEB TRANSMIT FILE C,"B:hostfile.txt","text/plain":GOTO Sent
IF P$="/host/code" THEN WEB TRANSMIT CODE C,404:GOTO Sent
LONGSTRING CLEAR O%()
LONGSTRING APPEND O%(),"HTTP/1.0 200 OK"+CR$
LONGSTRING APPEND O%(),"Content-Type: text/plain"+CR$
LONGSTRING APPEND O%(),"Connection: close"+CR$+CR$
LONGSTRING APPEND O%(),"HOST_BASIC_SERVER_OK"+CHR$(10)
LONGSTRING APPEND O%(),"PATH="+P$+CHR$(10)
LONGSTRING APPEND O%(),"LEN="+STR$(LLEN(B%()))+CHR$(10)
WEB TCP SEND C,O%()
WEB TCP CLOSE C
Sent:
SERVED=SERVED+1
IF SERVED>=1 THEN END
RETURN
"""


def run_basic(binary: Path, program: str, timeout: float) -> subprocess.CompletedProcess[str]:
    with tempfile.NamedTemporaryFile("w", suffix=".bas", delete=False) as handle:
        handle.write(program)
        path = Path(handle.name)
    try:
        return subprocess.run(
            [str(binary), str(path), "--interp"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    finally:
        try:
            path.unlink()
        except OSError:
            pass


def read_until(proc: subprocess.Popen[bytes], needle: bytes, timeout: float) -> bytes:
    assert proc.stdout is not None
    deadline = time.monotonic() + timeout
    data = bytearray()
    fd = proc.stdout.fileno()
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            if proc.poll() is not None:
                chunk = proc.stdout.read()
                if chunk:
                    data.extend(chunk)
                break
            continue
        chunk = os.read(fd, 1)
        if not chunk:
            break
        data.extend(chunk)
        if needle in data:
            return bytes(data)
    raise TimeoutError(f"timed out waiting for {needle!r}\n{data.decode('latin1', 'replace')}")


def drain_available(proc: subprocess.Popen[bytes], settle: float = 0.1) -> bytes:
    assert proc.stdout is not None
    deadline = time.monotonic() + settle
    data = bytearray()
    fd = proc.stdout.fileno()
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.01)
        if not ready:
            continue
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        data.extend(chunk)
        deadline = time.monotonic() + settle
    return bytes(data)


def read_prompt_after_drain(proc: subprocess.Popen[bytes], drained: bytes, timeout: float) -> bytes:
    if b">" in drained:
        return b""
    return read_until(proc, b">", timeout)


def send_repl(proc: subprocess.Popen[bytes], command: str) -> None:
    assert proc.stdin is not None
    proc.stdin.write((command + "\n").encode("latin1"))
    proc.stdin.flush()


def run_repl_program(proc: subprocess.Popen[bytes], program: str, ready: bytes, timeout: float) -> bytes:
    output = bytearray()
    send_repl(proc, program)
    output.extend(read_until(proc, ready, timeout))
    return bytes(output)


def send_udp(payload: bytes, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(payload, ("127.0.0.1", port))


def send_udp_until(proc: subprocess.Popen[bytes], payload: bytes, port: int, needle: bytes, timeout: float) -> bytes:
    assert proc.stdout is not None
    deadline = time.monotonic() + timeout
    data = bytearray()
    fd = proc.stdout.fileno()
    next_send = 0.0
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_send:
            send_udp(payload, port)
            next_send = now + 0.2
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            if proc.poll() is not None:
                chunk = proc.stdout.read()
                if chunk:
                    data.extend(chunk)
                break
            continue
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        data.extend(chunk)
        if needle in data:
            return bytes(data)
    raise TimeoutError(f"timed out waiting for {needle!r}\n{data.decode('latin1', 'replace')}")


def fetch_http(port: int, path: str, timeout: float) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        request = f"GET {path} HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
        sock.sendall(request.encode("latin1"))
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


def tftp_write_read(port: int, filename: str, payload: bytes, timeout: float) -> tuple[bool, bytes, str]:
    def packet(opcode: int, *parts: bytes) -> bytes:
        return struct.pack("!H", opcode) + b"".join(parts)

    def parse_oack(data: bytes) -> dict[str, str]:
        if len(data) < 2 or data[:2] != b"\x00\x06":
            return {}
        parts = data[2:].split(b"\0")
        opts: dict[str, str] = {}
        for i in range(0, len(parts) - 1, 2):
            if not parts[i]:
                break
            opts[parts[i].decode("ascii", "ignore").lower()] = \
                parts[i + 1].decode("ascii", "ignore")
        return opts

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            request = packet(2, filename.encode("ascii"), b"\0octet\0",
                             b"blksize\0", b"508\0")
            sock.sendto(request, ("127.0.0.1", port))
            data, addr = sock.recvfrom(1024)
            block_size = 512
            if data[:2] == b"\x00\x06":
                block_size = int(parse_oack(data).get("blksize", "512"))
            elif data == b"\x00\x04\x00\x00":
                pass
            else:
                return False, b"", f"bad WRQ ack {data!r}"
            block = 1
            for offset in range(0, len(payload), block_size):
                chunk = payload[offset:offset + block_size]
                sock.sendto(packet(3, struct.pack("!H", block), chunk), addr)
                data, _ = sock.recvfrom(1024)
                if data != packet(4, struct.pack("!H", block)):
                    return False, b"", f"bad DATA ack {data!r}"
                block += 1
            if len(payload) % block_size == 0:
                sock.sendto(packet(3, struct.pack("!H", block)), addr)
                data, _ = sock.recvfrom(1024)
                if data != packet(4, struct.pack("!H", block)):
                    return False, b"", f"bad final DATA ack {data!r}"

            request = packet(1, filename.encode("ascii"), b"\0octet\0",
                             b"blksize\0", b"508\0")
            sock.sendto(request, ("127.0.0.1", port))
            received = bytearray()
            data, addr = sock.recvfrom(1024)
            block_size = 512
            expected_block = 1
            if data[:2] == b"\x00\x06":
                block_size = int(parse_oack(data).get("blksize", "512"))
                last_error: OSError | None = None
                for _ in range(4):
                    sock.sendto(packet(4, b"\x00\x00"), addr)
                    try:
                        data, addr = sock.recvfrom(1024)
                        break
                    except socket.timeout as exc:
                        last_error = exc
                else:
                    raise last_error or socket.timeout("timed out waiting for DATA1")
            while True:
                if len(data) < 4 or data[:2] != b"\x00\x03":
                    return False, b"", f"bad RRQ data {data!r}"
                block = struct.unpack("!H", data[2:4])[0]
                if block != expected_block:
                    return False, b"", f"bad RRQ block {block}, expected {expected_block}"
                chunk = data[4:]
                received.extend(chunk)
                sock.sendto(packet(4, data[2:4]), addr)
                if len(chunk) < block_size:
                    break
                expected_block += 1
                data, addr = sock.recvfrom(1024)
            return True, bytes(received), ""
    except OSError as exc:
        return False, b"", f"{type(exc).__name__}: {exc}"


def real_telnet_client_roundtrip(host: str, port: int, timeout: float) -> tuple[bool, bytes, str]:
    telnet = os.environ.get("TELNET") or shutil.which("telnet")
    if not telnet:
        return False, b"", "telnet binary not found"
    proc = subprocess.Popen(
        [telnet, host, str(port)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )
    output = bytearray()

    def read_until(needle: bytes) -> bool:
        assert proc.stdout is not None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if needle in output:
                return True
            ready, _, _ = select.select([proc.stdout.fileno()], [], [], 0.05)
            if ready:
                chunk = os.read(proc.stdout.fileno(), 256)
                if chunk:
                    output.extend(chunk)
                    continue
                break
            if proc.poll() is not None:
                break
        return needle in output

    def send(data: bytes) -> None:
        assert proc.stdin is not None
        proc.stdin.write(data)
        proc.stdin.flush()

    try:
        if not read_until(b"Connected"):
            return False, bytes(output), "telnet did not connect"
        send(b"PRINT 2+3\r\n")
        if not read_until(b" 5"):
            return False, bytes(output), "missing PRINT 2+3 output"
        send(b"a\r\n")
        if not read_until(b"Unknown command"):
            return False, bytes(output), "missing BASIC error"
        if proc.poll() is not None:
            return False, bytes(output), "telnet closed after BASIC error"
        send(b"PRINT 6+7\r\n")
        if not read_until(b" 13"):
            return False, bytes(output), "missing PRINT 6+7 output after error"
        return True, bytes(output), ""
    except OSError as exc:
        return False, bytes(output), f"{type(exc).__name__}: {exc}"
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                proc.kill()


def telnet_console_roundtrip(binary: Path, telnet_port: int, timeout: float) -> tuple[bool, bytes, str]:
    env = os.environ.copy()
    env["MMBASIC_HOST_TELNET_PORT"] = str(telnet_port)
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        [str(binary), "--repl"],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=subprocess.STDOUT,
        bufsize=0,
        env=env,
        close_fds=True,
    )
    os.close(slave_fd)
    output = bytearray()
    def read_pty_until(needle: bytes) -> bytes:
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master_fd], [], [], 0.05)
            if not ready:
                if proc.poll() is not None:
                    break
                continue
            try:
                chunk = os.read(master_fd, 256)
            except OSError:
                break
            if not chunk:
                break
            data.extend(chunk)
            if needle in data:
                return bytes(data)
        raise TimeoutError(f"timed out waiting for {needle!r}\n{data.decode('latin1', 'replace')}")

    try:
        output.extend(read_pty_until(b">"))
        os.write(master_fd, b"OPTION TELNET CONSOLE ON\n")
        output.extend(read_pty_until(b">"))
        ok, data, detail = real_telnet_client_roundtrip("127.0.0.1", telnet_port, timeout)
        if proc.poll() is None:
            proc.terminate()
        return ok, data, detail
    except (OSError, TimeoutError) as exc:
        return False, bytes(output), f"{type(exc).__name__}: {exc}"
    finally:
        if proc.poll() is None:
            proc.kill()
        try:
            os.close(master_fd)
        except OSError:
            pass


def run_server_basic(binary: Path, tcp_port: int, udp_port: int, tftp_port: int, timeout: float) -> tuple[str, dict[str, bytes], tuple[bool, bytes, str]]:
    tempdir_obj = tempfile.TemporaryDirectory()
    tempdir = Path(tempdir_obj.name)
    path = tempdir / "server.bas"
    path.write_text(server_basic_program(), encoding="utf-8")
    env = os.environ.copy()
    env["MMBASIC_HOST_TFTP_PORT"] = str(tftp_port)

    proc = subprocess.Popen(
        [str(binary), "--repl", "--sd-root", str(tempdir)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
        env=env,
    )
    output = bytearray()
    responses: dict[str, bytes] = {}
    tftp_result: tuple[bool, bytes, str] = (False, b"", "not run")
    try:
        output.extend(read_until(proc, b">", timeout))
        send_repl(proc, f"OPTION UDP SERVER PORT {udp_port}")
        output.extend(read_until(proc, b">", timeout))
        send_repl(proc, f"OPTION TCP SERVER PORT {tcp_port}")
        output.extend(read_until(proc, b">", timeout))
        send_repl(proc, "OPTION TFTP ON")
        output.extend(read_until(proc, b">", timeout))
        send_repl(proc, "OPTION WEB MESSAGES OFF")
        output.extend(read_until(proc, b">", timeout))
        send_repl(proc, "OPTION LIST")
        output.extend(read_until(proc, b"OPTION WEB MESSAGES OFF", timeout))
        output.extend(read_until(proc, b">", timeout))

        for index, route in enumerate(("/host/path?x=1", "/host/after-run", "/host/page", "/host/file", "/host/code")):
            output.extend(run_repl_program(proc, 'RUN "server.bas"', b"HOST_BASIC_SERVER_READY", timeout))
            if index == 0:
                output.extend(send_udp_until(proc, b"HOST_BASIC_UDP_IN", udp_port, b"HOST_BASIC_UDP_ADDR=", timeout))
                payload = b"HOST_BASIC_TFTP_OK:" + bytes((i % 251 for i in range(1100)))
                tftp_result = tftp_write_read(tftp_port, "host_tftp.txt", payload, timeout)
                output.extend(drain_available(proc))
            elif route == "/host/after-run":
                output.extend(send_udp_until(proc, b"HOST_BASIC_UDP_AFTER_RUN", udp_port, b"HOST_BASIC_UDP_ADDR=", timeout))
            try:
                responses[route] = fetch_http(tcp_port, route, timeout)
            except OSError as exc:
                responses[route] = f"FETCH_ERROR {type(exc).__name__}: {exc}".encode("latin1")
            drained = drain_available(proc)
            output.extend(drained)
            output.extend(read_prompt_after_drain(proc, drained, timeout))
        if proc.poll() is None:
            proc.terminate()
        if proc.stdout is not None:
            output.extend(proc.stdout.read() or b"")
    finally:
        if proc.poll() is None:
            proc.kill()
        tempdir_obj.cleanup()
    return output.decode("latin1", "replace"), responses, tftp_result


def print_checks(checks: list[Check]) -> int:
    failed = 0
    print("--- checks ---")
    for check in checks:
        suffix = f" ({check.detail})" if check.detail else ""
        print(f"{check.name}: {'ok' if check.ok else 'FAIL'}{suffix}")
        if not check.ok:
            failed += 1
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_root())
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--make", default="make")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args(argv)

    root = args.repo.resolve()
    binary = args.binary or (root / "host" / "mmbasic_test")
    if not args.no_build:
        rc = subprocess.call([args.make, "-C", str(root / "ports" / "host_native")], cwd=root)
        if rc != 0:
            return rc

    http_port = free_port(socket.SOCK_STREAM)
    stream_port = free_port(socket.SOCK_STREAM)
    udp_port = free_port(socket.SOCK_DGRAM)
    ntp_port = free_port(socket.SOCK_DGRAM)
    mqtt_port = free_port(socket.SOCK_STREAM)
    tcp_server_port = free_port(socket.SOCK_STREAM)
    udp_server_port = free_port(socket.SOCK_DGRAM)
    tftp_port = free_port(socket.SOCK_DGRAM)
    telnet_port = free_port(socket.SOCK_STREAM)
    ntp_epoch = calendar.timegm((2026, 1, 2, 3, 4, 5, 0, 0, 0))

    with HttpPeer(http_port) as http, StreamPeer(stream_port) as stream, UdpPeer(udp_port) as udp, NtpPeer(ntp_port, ntp_epoch) as ntp, MqttTestBroker("127.0.0.1", mqtt_port) as mqtt:
        result = run_basic(binary, basic_program(http_port, stream_port, udp_port, ntp_port, mqtt_port), args.timeout)

    server_output, server_responses, tftp_result = run_server_basic(binary, tcp_server_port, udp_server_port, tftp_port, args.timeout)
    telnet_ok, telnet_output, telnet_detail = telnet_console_roundtrip(binary, telnet_port, args.timeout)

    print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    print(server_output, end="" if server_output.endswith("\n") else "\n")
    print(telnet_output.decode("latin1", "replace"), end="" if telnet_output.endswith(b"\n") else "\n")
    raw_response = server_responses.get("/host/path?x=1", b"")
    page_response = server_responses.get("/host/page", b"")
    file_response = server_responses.get("/host/file", b"")
    code_response = server_responses.get("/host/code", b"")
    after_run_response = server_responses.get("/host/after-run", b"")
    expected_tftp_payload = b"HOST_BASIC_TFTP_OK:" + bytes((i % 251 for i in range(1100)))
    tftp_ok, tftp_payload, tftp_detail = tftp_result
    checks = [
        Check("host_basic_exit", result.returncode == 0, f"rc={result.returncode}"),
        Check("host_basic_ip", "HOST_BASIC_IP=127.0.0.1" in result.stdout),
        Check("host_basic_tcpip", "HOST_BASIC_TCPIP= 1" in result.stdout),
        Check("host_basic_tcp_peer", http.first_line == "GET /host-basic HTTP/1.0", repr(http.first_line)),
        Check("host_basic_tcp_response", "HOST_BASIC_TCP_OK" in result.stdout),
        Check("host_basic_stream_peer", stream.first_line == "STREAM /host-basic", repr(stream.first_line)),
        Check("host_basic_stream_response", "HOST_BASIC_STREAM_DATA=HOST_BASIC_STREAM_OK" in result.stdout),
        Check("host_basic_udp_send", udp.packet == b"HOST_BASIC_UDP_OK", repr(udp.packet)),
        Check("host_basic_ntp_request", ntp.request is not None and len(ntp.request) == 48 and ntp.request[0] == 0x1B),
        Check("host_basic_ntp_date", "HOST_BASIC_NTP_DATE=02-01-2026" in result.stdout),
        Check("host_basic_ntp_time", "HOST_BASIC_NTP_TIME=03:04:05" in result.stdout),
        Check("host_basic_mqtt_connect", mqtt.log.connected and bool(mqtt.log.client_id), mqtt.log.client_id),
        Check("host_basic_mqtt_subscribe", mqtt.log.subscribed_topic == "host/basic/in", repr(mqtt.log.subscribed_topic)),
        Check("host_basic_mqtt_receive_topic", "HOST_BASIC_MQTT_TOPIC=host/basic/in" in result.stdout),
        Check("host_basic_mqtt_receive_message", "HOST_BASIC_MQTT_MSG=NETCONF_MQTT_IN" in result.stdout),
        Check(
            "host_basic_mqtt_publish",
            mqtt.log.published_topic == "host/basic/out" and mqtt.log.published_payload == b"HOST_BASIC_MQTT_OUT",
            f"topic={mqtt.log.published_topic!r} payload={mqtt.log.published_payload!r}",
        ),
        Check("host_basic_mqtt_unsubscribe", mqtt.log.unsubscribed_topic == "host/basic/in", repr(mqtt.log.unsubscribed_topic)),
        Check("host_basic_option_list_tcp", f"OPTION TCP SERVER PORT {tcp_server_port}" in server_output),
        Check("host_basic_option_list_udp", f"OPTION UDP SERVER PORT {udp_server_port}" in server_output),
        Check("host_basic_option_list_messages", "OPTION WEB MESSAGES OFF" in server_output),
        Check("host_basic_tcp_server_ready", "HOST_BASIC_SERVER_READY" in server_output),
        Check("host_basic_udp_receive", "HOST_BASIC_UDP_RECV=HOST_BASIC_UDP_IN" in server_output),
        Check("host_basic_udp_address", "HOST_BASIC_UDP_ADDR=" in server_output),
        Check("host_basic_udp_preserved_after_run", "HOST_BASIC_UDP_RECV=HOST_BASIC_UDP_AFTER_RUN" in server_output),
        Check("host_basic_tcp_server_response", b"HOST_BASIC_SERVER_OK" in raw_response),
        Check("host_basic_tcp_server_path", b"PATH=/host/path?x=1" in raw_response),
        Check("host_basic_tcp_server_preserved_after_run", b"PATH=/host/after-run" in after_run_response),
        Check("host_basic_transmit_page", b"HOST_BASIC_PAGE 123" in page_response),
        Check("host_basic_transmit_file", b"HOST_BASIC_FILE" in file_response),
        Check("host_basic_transmit_code", code_response.startswith(b"HTTP/1.1 404"), code_response.splitlines()[0].decode("latin1", "replace") if code_response else ""),
        Check("host_basic_tftp_roundtrip", tftp_ok and tftp_payload == expected_tftp_payload, tftp_detail or repr(tftp_payload)),
        Check("host_basic_telnet_console", telnet_ok, telnet_detail or repr(telnet_output)),
    ]
    return print_checks(checks)


if __name__ == "__main__":
    raise SystemExit(main())
