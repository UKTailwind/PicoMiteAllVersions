#!/usr/bin/env python3
"""ESP32 Web/TCP smoke test using the MMBasic interpreter over serial."""

from __future__ import annotations

import argparse
import os
import queue
import re
import socket
import threading
import time
from dataclasses import dataclass

from basic_serial import BasicSerial, strip_ansi


@dataclass
class ServerLog:
    http_first_line: str = ""
    http_bytes: int = 0
    stream_request: bytes = b""


def local_ip_for(target: str = "192.168.4.1") -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((target, 9))
        return sock.getsockname()[0]
    finally:
        sock.close()


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
                        f"ESP32_CLIENT_OK\nFROM={addr[0]}\n"
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
                    conn.settimeout(10)
                    data = b""
                    try:
                        while b"\n" not in data and len(data) < 1024:
                            chunk = conn.recv(1024)
                            if not chunk:
                                break
                            data += chunk
                    except socket.timeout:
                        pass
                    if data:
                        self.log.stream_request = data
                    if data:
                        conn.sendall(b"ACK " + data)
                        for i in range(4):
                            conn.sendall(f"STREAM{i}\n".encode("ascii"))
                            time.sleep(0.05)
                    else:
                        conn.sendall(b"NO_COMMAND\n")


def run_smoke(args: argparse.Namespace) -> tuple[str, ServerLog]:
    mac_ip = args.host or local_ip_for(args.gateway)
    commands = [
        "NEW",
        "OPTION BASE 0",
        args.connect_command,
        "DIM INTEGER A%(4096/8)",
        "CR$=CHR$(13)+CHR$(10)",
        f'WEB OPEN TCP CLIENT "{mac_ip}",{args.http_port}',
        f'WEB TCP CLIENT REQUEST "GET /tcp-smoke HTTP/1.0"+CR$+"Host: {mac_ip}"+CR$+CR$,A%(),7000',
        'PRINT "REQ_LEN=";LLEN(A%())',
        'PRINT LGETSTR$(A%(),1,220)',
        "WEB CLOSE TCP CLIENT",
        "DIM INTEGER S%(256/8)",
        "DIM INTEGER R%,W%",
        "R%=0:W%=0",
        f'WEB OPEN TCP STREAM "{mac_ip}",{args.stream_port}',
        'WEB TCP CLIENT STREAM "INLINE"+CHR$(10),S%(),R%,W%',
        "PAUSE 1000",
        'PRINT "STREAM_PTR=";R%;",";W%',
        "S%(0)=W%",
        "PRINT LGETSTR$(S%(),1,W%)",
        "WEB CLOSE TCP CLIENT",
    ]

    transcript_parts: list[str] = []
    with TcpSmokeServers(args.bind, args.http_port, args.stream_port) as servers:
        with BasicSerial(args.port, args.baud) as basic:
            transcript_parts.append(basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait).decode("latin1", "replace"))
            for command in commands:
                print(f">>> {command}", flush=True)
                timeout = args.long_timeout if command == args.connect_command else args.timeout
                if command.startswith("WEB TCP CLIENT REQUEST") or command.startswith("PAUSE "):
                    timeout = args.long_timeout
                result = basic.command(command, timeout=timeout)
                print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
                transcript_parts.append(result.text)
        return "".join(transcript_parts), servers.log


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("BASIC_PORT", "/dev/cu.usbmodem101"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--host", help="Mac/IP address reachable from ESP32; defaults to route toward gateway")
    parser.add_argument("--gateway", default="192.168.4.1")
    parser.add_argument("--http-port", type=int, default=18180)
    parser.add_argument("--stream-port", type=int, default=18183)
    parser.add_argument("--connect-command", default="WEB CONNECT")
    parser.add_argument("--boot-wait", type=float, default=2.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=35.0)
    args = parser.parse_args()

    transcript, log = run_smoke(args)
    clean = strip_ansi(transcript.encode("latin1", "replace")).decode("latin1", "replace")
    checks = {
        "http_response": "ESP32_CLIENT_OK" in clean and "REQUEST=GET /tcp-smoke HTTP/1.0" in clean,
        "http_server_saw_request": log.http_first_line == "GET /tcp-smoke HTTP/1.0" and log.http_bytes > 0,
        "stream_request": log.stream_request == b"INLINE\n",
        "stream_response": "ACK INLINE" in clean and "STREAM3" in clean,
    }
    print("--- checks ---")
    for name, ok in checks.items():
        print(f"{name}: {'ok' if ok else 'FAIL'}")
    if not all(checks.values()):
        print("--- server log ---")
        print(f"http_first_line={log.http_first_line!r} http_bytes={log.http_bytes}")
        print(f"stream_request={log.stream_request!r}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
