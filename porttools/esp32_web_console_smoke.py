#!/usr/bin/env python3
"""Smoke-test the ESP32 reserved web-console page and WebSocket endpoint."""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import os
import socket
import struct
import sys
from pathlib import Path
from typing import Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from basic_serial import BasicSerial, default_port  # noqa: E402


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def check_http(host: str, port: int, path: str, needle: bytes, timeout: float) -> None:
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read()
    finally:
        conn.close()
    if resp.status != 200:
        raise RuntimeError(f"GET {path}: HTTP {resp.status}")
    if needle not in body:
        raise RuntimeError(f"GET {path}: expected marker not found")


def recv_exact(sock: socket.socket, n: int) -> bytes:
    chunks = []
    remaining = n
    while remaining:
        data = sock.recv(remaining)
        if not data:
            raise RuntimeError("socket closed")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def encode_client_frame(opcode: int, payload: bytes = b"") -> bytes:
    mask = os.urandom(4)
    header = bytearray([0x80 | opcode])
    length = len(payload)
    if length <= 125:
        header.append(0x80 | length)
    elif length <= 65535:
        header.extend([0x80 | 126, (length >> 8) & 0xFF, length & 0xFF])
    else:
        raise ValueError("payload too large")
    masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
    return bytes(header) + mask + masked


def read_server_frame(sock: socket.socket) -> Tuple[int, bytes]:
    first = recv_exact(sock, 2)
    fin = first[0] & 0x80
    opcode = first[0] & 0x0F
    masked = first[1] & 0x80
    length = first[1] & 0x7F
    if not fin:
        raise RuntimeError("fragmented server frame")
    if masked:
        raise RuntimeError("masked server frame")
    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]
        if length > 1024 * 1024:
            raise RuntimeError(f"server frame too large: {length}")
    return opcode, recv_exact(sock, length)


def parse_display_frame(payload: bytes) -> tuple[str, int, int, bytes]:
    if len(payload) < 8:
        raise RuntimeError("short display frame")
    magic = payload[:4].decode("ascii", "replace")
    w, h = struct.unpack_from("<HH", payload, 4)
    return magic, w, h, payload[8:]


def rgba_at(rgba: bytes, width: int, x: int, y: int) -> tuple[int, int, int, int]:
    off = (y * width + x) * 4
    return tuple(rgba[off : off + 4])  # type: ignore[return-value]


def websocket_smoke(host: str, port: int, timeout: float, expect_display: bool) -> None:
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    expected_accept = base64.b64encode(
        hashlib.sha1((key + WS_GUID).encode("ascii")).digest()
    ).decode("ascii")

    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    try:
        request = (
            "GET /__web_console/ws HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        )
        sock.sendall(request.encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            response += sock.recv(4096)
            if not response:
                raise RuntimeError("no upgrade response")
        header = response.decode("iso-8859-1")
        if "101 Switching Protocols" not in header:
            raise RuntimeError(f"upgrade failed: {header.splitlines()[0]}")
        if f"Sec-WebSocket-Accept: {expected_accept}" not in header:
            raise RuntimeError("bad Sec-WebSocket-Accept")

        sock.sendall(encode_client_frame(0x1, b'{"op":"status"}'))
        sock.sendall(encode_client_frame(0x9, b"ping"))

        saw_hello = False
        saw_status = False
        saw_frmb = False
        saw_cmds = False
        saw_pong = False
        final_pixel_ok = not expect_display
        for _ in range(10):
            opcode, payload = read_server_frame(sock)
            if opcode == 0x1:
                text = payload.decode("utf-8", "replace")
                saw_hello = saw_hello or '"op":"hello"' in text
                saw_status = saw_status or '"op":"status"' in text
            elif opcode == 0x2:
                magic, w, h, body = parse_display_frame(payload)
                if magic == "FRMB":
                    saw_frmb = True
                    if w not in (320,) or h not in (240, 320):
                        raise RuntimeError(f"unexpected FRMB geometry {w}x{h}")
                    if len(body) != w * h * 4:
                        raise RuntimeError("FRMB payload size mismatch")
                    if expect_display:
                        final_pixel_ok = rgba_at(body, w, 10, 10)[:3] == (255, 0, 0)
                elif magic == "CMDS":
                    saw_cmds = True
            elif opcode == 0xA:
                saw_pong = payload == b"ping"
            if saw_hello and saw_status and saw_frmb and saw_pong and final_pixel_ok:
                break
        if not (saw_hello and saw_status and saw_frmb and saw_pong and final_pixel_ok):
            raise RuntimeError(
                "missing frames: "
                f"hello={saw_hello} status={saw_status} "
                f"frmb={saw_frmb} cmds={saw_cmds} "
                f"pong={saw_pong} final_pixel={final_pixel_ok}"
            )
        sock.sendall(encode_client_frame(0x8, b"\x03\xe8"))
    finally:
        sock.close()


def run_display_sequence(serial_port: str, baud: int, timeout: float) -> None:
    commands = [
        "CLS RGB(BLACK)",
        "COLOUR RGB(GREEN), RGB(BLACK)",
        'PRINT "WEB DISPLAY PHASE4"',
        "PIXEL 10, 10, RGB(RED)",
        "LINE 0, 20, 90, 20, 1, RGB(BLUE)",
        "BOX 100, 30, 40, 30, 1, RGB(YELLOW), RGB(CYAN)",
        'TEXT 12, 90, "PHASE4", , 1, 1, RGB(WHITE), RGB(BLACK)',
        'PRINT "ESP32_WEB_DISPLAY_SERIAL_OK"',
    ]
    with BasicSerial(serial_port, baud) as basic:
        basic.sync(timeout=timeout)
        output = ""
        for cmd in commands:
            output += basic.command(cmd, timeout=timeout).clean_text
        if "ESP32_WEB_DISPLAY_SERIAL_OK" not in output:
            raise RuntimeError("serial display sequence did not recover prompt")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="ESP32 IP address")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--serial-port", default=default_port(), help="serial device path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--display-sequence",
        action="store_true",
        help="draw a small BASIC display sequence over serial before checking FRMB pixels",
    )
    args = parser.parse_args()

    if args.display_sequence:
        run_display_sequence(args.serial_port, args.baud, args.timeout)
    check_http(args.host, args.port, "/__web_console/", b"MMBasic Web Console", args.timeout)
    check_http(args.host, args.port, "/__web_console/app.js", b"/__web_console/ws", args.timeout)
    websocket_smoke(args.host, args.port, args.timeout, args.display_sequence)
    print("esp32_web_console_smoke: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"esp32_web_console_smoke: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
