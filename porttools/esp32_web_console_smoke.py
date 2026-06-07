#!/usr/bin/env python3
"""Smoke-test the ESP32 reserved web-console page and WebSocket endpoint."""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import os
import select
import socket
import struct
import sys
import time
from pathlib import Path
from typing import Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from basic_serial import BasicSerial, default_port  # noqa: E402


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
DISPLAY_SENTINEL_X = 300
DISPLAY_SENTINEL_Y = 200


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


def rgb332_to_rgb(c: int) -> tuple[int, int, int]:
    r = round(((c >> 5) & 0x07) * 255 / 7)
    g = round(((c >> 2) & 0x07) * 255 / 7)
    b = round((c & 0x03) * 255 / 3)
    return r, g, b


def cmds_pixel_rgb_at(payload: bytes, target_x: int, target_y: int) -> tuple[int, int, int] | None:
    magic, w, h, _body = parse_display_frame(payload)
    if magic != "CMDS" or target_x < 0 or target_y < 0 or target_x >= w or target_y >= h:
        return None
    pixel: tuple[int, int, int] | None = None
    view = memoryview(payload)
    p = 8
    end = len(payload)
    while p < end:
        op = view[p]
        p += 1
        if op == 0x01:
            if p + 4 > end:
                return pixel
            rgb = struct.unpack_from("<I", payload, p)[0]
            p += 4
            pixel = ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)
        elif op == 0x02:
            if p + 12 > end:
                return pixel
            x, y = struct.unpack_from("<hh", payload, p)
            bw, bh = struct.unpack_from("<HH", payload, p + 4)
            rgb = struct.unpack_from("<I", payload, p + 8)[0]
            p += 12
            if x <= target_x < x + bw and y <= target_y < y + bh:
                pixel = ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)
        elif op == 0x03:
            if p + 8 > end:
                return pixel
            x, y = struct.unpack_from("<hh", payload, p)
            rgb = struct.unpack_from("<I", payload, p + 4)[0]
            p += 8
            if x == target_x and y == target_y:
                pixel = ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)
        elif op == 0x04:
            if p + 6 > end:
                return pixel
            p += 6
            pixel = None
        elif op == 0x05:
            if p + 8 > end:
                return pixel
            x, y = struct.unpack_from("<hh", payload, p)
            bw, bh = struct.unpack_from("<HH", payload, p + 4)
            p += 8
            n = bw * bh * 4
            if p + n > end:
                return pixel
            if x <= target_x < x + bw and y <= target_y < y + bh:
                off = p + ((target_y - y) * bw + (target_x - x)) * 4
                pixel = tuple(payload[off : off + 3])  # type: ignore[assignment]
            p += n
        elif op == 0x06:
            if p + 8 > end:
                return pixel
            x, y = struct.unpack_from("<hh", payload, p)
            bw, bh = struct.unpack_from("<HH", payload, p + 4)
            p += 8
            wanted = None
            if x <= target_x < x + bw and y <= target_y < y + bh:
                wanted = (target_y - y) * bw + (target_x - x)
            seen = 0
            total = bw * bh
            while seen < total and p + 3 <= end:
                run = struct.unpack_from("<H", payload, p)[0]
                c = payload[p + 2]
                p += 3
                if wanted is not None and seen <= wanted < seen + run:
                    pixel = rgb332_to_rgb(c)
                seen += run
            if seen != total:
                return pixel
        else:
            return pixel
    return pixel


def cmds_contains_blit(payload: bytes) -> bool:
    magic, _w, _h, _body = parse_display_frame(payload)
    if magic != "CMDS":
        return False
    view = memoryview(payload)
    p = 8
    end = len(payload)
    while p < end:
        op = view[p]
        p += 1
        if op == 0x01:
            p += 4
        elif op == 0x02:
            p += 12
        elif op == 0x03:
            p += 8
        elif op == 0x04:
            p += 6
        elif op == 0x05:
            if p + 8 > end:
                return False
            bw = struct.unpack_from("<H", payload, p + 4)[0]
            bh = struct.unpack_from("<H", payload, p + 6)[0]
            return bw > 0 and bh > 0
        elif op == 0x06:
            if p + 8 > end:
                return False
            bw = struct.unpack_from("<H", payload, p + 4)[0]
            bh = struct.unpack_from("<H", payload, p + 6)[0]
            p += 8
            pixels = bw * bh
            seen = 0
            while seen < pixels and p + 3 <= end:
                run = struct.unpack_from("<H", payload, p)[0]
                if run <= 0:
                    return False
                seen += run
                p += 3
            return bw > 0 and bh > 0 and seen == pixels
        else:
            return False
        if p > end:
            return False
    return False


def send_key(sock: socket.socket, code: int) -> None:
    sock.sendall(encode_client_frame(0x1, f'{{"op":"key","code":{code}}}'.encode("ascii")))


def send_key_text(sock: socket.socket, text: str, delay: float = 0.015) -> None:
    for ch in text:
        send_key(sock, ord(ch))
        time.sleep(delay)


def wait_for_display_update(sock: socket.socket, timeout: float, label: str) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        opcode, payload = read_server_frame(sock)
        if opcode == 0x8:
            raise RuntimeError(f"{label}: server closed websocket")
        if opcode == 0x2:
            magic, _w, _h, _body = parse_display_frame(payload)
            if magic == "FRMB" or (magic == "CMDS" and cmds_contains_blit(payload)):
                return
    raise RuntimeError(f"{label}: did not produce display update output")


def wait_for_pong(sock: socket.socket, payload: bytes, timeout: float, label: str) -> None:
    sock.sendall(encode_client_frame(0x9, payload))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        opcode, frame_payload = read_server_frame(sock)
        if opcode == 0x8:
            raise RuntimeError(f"{label}: server closed websocket")
        if opcode == 0xA and frame_payload == payload:
            return
    raise RuntimeError(f"{label}: websocket did not answer ping")


def drain_display_frames(sock: socket.socket, idle_timeout: float,
                         max_frames: int, label: str) -> int:
    frames = 0
    while frames < max_frames:
        ready, _, _ = select.select([sock], [], [], idle_timeout)
        if not ready:
            break
        opcode, payload = read_server_frame(sock)
        if opcode == 0x8:
            raise RuntimeError(f"{label}: server closed websocket")
        if opcode == 0x2:
            magic, _w, _h, _body = parse_display_frame(payload)
            if magic != "FRMB" and magic != "CMDS":
                raise RuntimeError(f"{label}: invalid display frame {magic!r}")
            frames += 1
    return frames


def open_websocket(host: str, port: int, timeout: float) -> socket.socket:
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
        return sock
    except Exception:
        sock.close()
        raise


def websocket_smoke(host: str, port: int, timeout: float, expect_display: bool) -> None:
    sock = open_websocket(host, port, timeout)
    try:
        sock.sendall(encode_client_frame(0x1, b'{"op":"status"}'))
        sock.sendall(encode_client_frame(0x9, b"ping"))

        saw_hello = False
        saw_status = False
        saw_display = False
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
                    saw_display = True
                    if w not in (320,) or h not in (240, 320):
                        raise RuntimeError(f"unexpected FRMB geometry {w}x{h}")
                    if len(body) != w * h * 4:
                        raise RuntimeError("FRMB payload size mismatch")
                    if expect_display:
                        final_pixel_ok = (
                            rgba_at(body, w, DISPLAY_SENTINEL_X,
                                    DISPLAY_SENTINEL_Y)[:3] == (255, 0, 0)
                        )
                elif magic == "CMDS":
                    saw_cmds = True
                    if cmds_contains_blit(payload):
                        saw_display = True
                    if expect_display:
                        final_pixel_ok = (
                            cmds_pixel_rgb_at(payload, DISPLAY_SENTINEL_X,
                                              DISPLAY_SENTINEL_Y) ==
                            (255, 0, 0)
                        )
            elif opcode == 0xA:
                saw_pong = payload == b"ping"
            display_ok = saw_display or not expect_display
            if saw_hello and saw_status and display_ok and saw_pong and final_pixel_ok:
                break
        display_ok = saw_display or not expect_display
        if not (saw_hello and saw_status and display_ok and saw_pong and final_pixel_ok):
            raise RuntimeError(
                "missing frames: "
                f"hello={saw_hello} status={saw_status} "
                f"display={saw_display} cmds={saw_cmds} "
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
        "LINE 0, 20, 90, 20, 1, RGB(BLUE)",
        "BOX 100, 30, 40, 30, 1, RGB(YELLOW), RGB(CYAN)",
        'TEXT 12, 90, "PHASE4", , 1, 1, RGB(WHITE), RGB(BLACK)',
        'PRINT "ESP32_WEB_DISPLAY_SERIAL_OK"',
        f"PIXEL {DISPLAY_SENTINEL_X}, {DISPLAY_SENTINEL_Y}, RGB(RED)",
    ]
    with BasicSerial(serial_port, baud) as basic:
        basic.sync(timeout=timeout)
        output = ""
        for cmd in commands:
            output += basic.command(cmd, timeout=timeout).clean_text
        if "ESP32_WEB_DISPLAY_SERIAL_OK" not in output:
            raise RuntimeError("serial display sequence did not recover prompt")


def run_keyboard_sequence(host: str, port: int, serial_port: str, baud: int, timeout: float) -> None:
    with BasicSerial(serial_port, baud) as basic:
        basic.sync(timeout=timeout)
        sock = open_websocket(host, port, timeout)
        try:
            saw_display = False
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and not saw_display:
                opcode, payload = read_server_frame(sock)
                if opcode == 0x2:
                    magic = parse_display_frame(payload)[0]
                    if magic == "FRMB" or (magic == "CMDS" and cmds_contains_blit(payload)):
                        saw_display = True
            if not saw_display:
                raise RuntimeError("keyboard sequence did not receive initial display frame")

            send_key_text(sock, "PRINT 2+3\r")
            wait_for_display_update(sock, timeout, "print sequence")
            wait_for_pong(sock, b"after-print", timeout, "print sequence")

            send_key_text(sock, "ZZZ\r")
            wait_for_display_update(sock, timeout, "bad-command sequence")
            wait_for_pong(sock, b"after-bad-command", timeout, "bad-command sequence")

            send_key_text(sock, "FILES\r")
            wait_for_display_update(sock, timeout, "files sequence")
            wait_for_pong(sock, b"after-files", timeout, "files sequence")
            for idx, code in enumerate((ord(" "), ord(" "), ord(" "),
                                        ord(" "), ord("\r"))):
                send_key(sock, code)
                drain_display_frames(sock, 0.05, 8,
                                     f"files-dismiss-{idx} sequence")
                wait_for_pong(sock, f"after-files-dismiss-{idx}".encode("ascii"),
                              timeout, f"files-dismiss-{idx} sequence")

            send_key_text(sock, 'EDIT "site.bas"\r')
            wait_for_display_update(sock, timeout, "editor-open sequence")
            wait_for_pong(sock, b"after-editor-open", timeout, "editor-open sequence")
            send_key(sock, 27)
            wait_for_display_update(sock, timeout, "editor-escape sequence")
            wait_for_pong(sock, b"after-editor-escape", timeout, "editor-escape sequence")
            send_key(sock, ord("Y"))
            wait_for_display_update(sock, timeout, "editor-discard sequence")
            wait_for_pong(sock, b"after-editor-discard", timeout, "editor-discard sequence")
        finally:
            try:
                sock.sendall(encode_client_frame(0x8, b"\x03\xe8"))
            except OSError:
                pass
            sock.close()
            time.sleep(0.2)
        basic.sync(timeout=timeout)


def run_editor_scroll_stress(host: str, port: int, serial_port: str,
                             baud: int, timeout: float) -> None:
    with BasicSerial(serial_port, baud) as basic:
        basic.sync(timeout=timeout)
        basic.command('OPEN "webstress.bas" FOR OUTPUT AS #1', timeout=timeout)
        basic.command(
            'FOR I=1 TO 180: PRINT #1, "PRINT "; I; " 0123456789 ABCDEF": NEXT I',
            timeout=max(timeout, 20.0),
        )
        basic.command("CLOSE #1", timeout=timeout)

        sock = open_websocket(host, port, timeout)
        try:
            drain_display_frames(sock, 0.05, 8, "scroll-stress initial")
            send_key_text(sock, 'EDIT "webstress.bas"\r', delay=0.005)
            wait_for_display_update(sock, timeout, "scroll-stress editor-open")
            wait_for_pong(sock, b"scroll-open", timeout, "scroll-stress editor-open")

            frames = 0
            for i in range(80):
                send_key(sock, 137)
                frames += drain_display_frames(sock, 0.005, 4, "scroll-stress page-down")
                if i % 10 == 9:
                    wait_for_pong(sock, f"scroll-{i}".encode("ascii"), timeout,
                                  "scroll-stress page-down")
            frames += drain_display_frames(sock, 0.2, 64, "scroll-stress drain")
            if frames == 0:
                raise RuntimeError("scroll-stress did not receive display frames")

            send_key(sock, 27)
            wait_for_display_update(sock, timeout, "scroll-stress editor-escape")
            send_key(sock, ord("Y"))
            wait_for_pong(sock, b"scroll-exit", timeout, "scroll-stress editor-exit")
        finally:
            try:
                sock.sendall(encode_client_frame(0x8, b"\x03\xe8"))
            except OSError:
                pass
            sock.close()
            time.sleep(0.2)
        basic.sync(timeout=timeout)


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
    parser.add_argument(
        "--keyboard-sequence",
        action="store_true",
        help="exercise PRINT, bad command, FILES, and EDIT over WebSocket display updates",
    )
    parser.add_argument(
        "--editor-scroll-stress",
        action="store_true",
        help="create a long file, scroll it in EDIT over WebSocket, and validate frame boundaries",
    )
    args = parser.parse_args()

    if args.display_sequence:
        run_display_sequence(args.serial_port, args.baud, args.timeout)
    check_http(args.host, args.port, "/__web_console/", b"MMBasic Web Console", args.timeout)
    check_http(args.host, args.port, "/__web_console/app.js", b"/__web_console/ws", args.timeout)
    validated_websocket = False
    if args.display_sequence:
        websocket_smoke(args.host, args.port, args.timeout, True)
        validated_websocket = True
    if args.keyboard_sequence:
        run_keyboard_sequence(args.host, args.port, args.serial_port, args.baud, args.timeout)
    if args.editor_scroll_stress:
        run_editor_scroll_stress(args.host, args.port, args.serial_port, args.baud, args.timeout)
    if not validated_websocket:
        websocket_smoke(args.host, args.port, args.timeout, False)
    print("esp32_web_console_smoke: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"esp32_web_console_smoke: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
