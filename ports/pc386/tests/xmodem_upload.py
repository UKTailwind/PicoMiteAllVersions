#!/usr/bin/env python3
"""
ports/pc386/tests/xmodem_upload.py — push a file to the Pocket386 over
serial using MMBasic's `XMODEM RECEIVE "name"`.

Usage:
    python3 xmodem_upload.py LOCAL_FILE [REMOTE_NAME]
    PC386_TTY=/dev/cu.usbserial-XYZ python3 xmodem_upload.py foo.bas

Defaults: TTY=/dev/cu.usbserial-0001, baud=38400, remote name = basename(local).
Stdlib only.
"""

import os
import select
import sys
import termios
import time

TTY  = os.environ.get("PC386_TTY", "/dev/cu.usbserial-0001")
BAUD = int(os.environ.get("PC386_BAUD", "38400"))

SOH = 0x01; EOT = 0x04; ACK = 0x06; NAK = 0x15; CAN = 0x18; CRC_REQ = 0x43


def open_port(tty: str, baud: int) -> int:
    fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = 0
    a[2] = (a[2] | termios.CS8 | termios.CREAD | termios.CLOCAL) & ~(
        termios.PARENB | termios.CSTOPB | termios.CSIZE) | termios.CS8
    a[3] = 0
    a[4] = a[5] = baud
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def _crc(buf: bytes) -> int:
    crc = 0
    for b in buf:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def _read_byte(fd: int, timeout: float):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            b = os.read(fd, 1)
            if b:
                return b[0]
    return None


def drain(fd: int, t: float = 0.2) -> None:
    end = time.monotonic() + t
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            os.read(fd, 4096)


def send_xmodem(fd: int, payload: bytes, label: str) -> bool:
    drain(fd, 0.2)
    # Wait for receiver to ask for data (NAK = checksum, 'C' = CRC).
    # Verbose: log any non-NAK/C bytes so we can see what the device is
    # actually saying when the handshake doesn't fire.
    mode_crc = None
    deadline = time.monotonic() + 15.0
    seen = bytearray()
    while time.monotonic() < deadline:
        b = _read_byte(fd, 0.5)
        if b is None: continue
        if b == CRC_REQ:
            mode_crc = True; break
        if b == NAK:
            mode_crc = False; break
        seen.append(b)
    if mode_crc is None:
        print(f"[xmodem] no NAK/C handshake. Saw: {bytes(seen)!r}", file=sys.stderr)
        return False
    print(f"[xmodem] handshake: {'CRC' if mode_crc else 'checksum'} mode, "
          f"{len(payload)} bytes / {(len(payload)+127)//128} blocks", file=sys.stderr)

    body = payload + b"\x1a" * ((-len(payload)) % 128)
    nblocks = len(body) // 128
    for i in range(nblocks):
        blk = body[i*128:(i+1)*128]
        bn = (i + 1) & 0xFF
        head = bytes([SOH, bn, (~bn) & 0xFF])
        if mode_crc:
            c = _crc(blk); tail = bytes([(c >> 8) & 0xFF, c & 0xFF])
        else:
            tail = bytes([sum(blk) & 0xFF])
        frame = head + blk + tail
        for attempt in range(10):
            os.write(fd, frame)
            r = _read_byte(fd, 5.0)
            if r == ACK:
                break
            if r == CAN:
                print(f"[xmodem] receiver CAN at block {i+1}", file=sys.stderr); return False
            print(f"[xmodem] block {i+1} attempt {attempt+1} resp={r}", file=sys.stderr)
        else:
            return False
        if (i + 1) % 8 == 0 or i + 1 == nblocks:
            sys.stderr.write(f"\r[xmodem] {i+1}/{nblocks} blocks sent")
            sys.stderr.flush()
    sys.stderr.write("\n")

    # EOT (with up to 3 retries).
    for _ in range(3):
        os.write(fd, bytes([EOT]))
        r = _read_byte(fd, 3.0)
        if r == ACK: return True
    print("[xmodem] no ACK to EOT", file=sys.stderr)
    return False


def main(argv: list) -> int:
    if len(argv) < 2:
        print("usage: xmodem_upload.py LOCAL [REMOTE]", file=sys.stderr); return 2
    local = argv[1]
    remote = argv[2] if len(argv) >= 3 else os.path.basename(local).upper()
    if not os.path.exists(local):
        print(f"no such file: {local}", file=sys.stderr); return 3
    payload = open(local, "rb").read()
    print(f"local : {local} ({len(payload)} bytes)", file=sys.stderr)
    print(f"remote: {remote}", file=sys.stderr)

    fd = open_port(TTY, BAUD)
    try:
        # Drop a CR to nudge a prompt, then issue XMODEM RECEIVE.
        drain(fd, 0.1)
        os.write(fd, b"\r")
        time.sleep(0.3)
        cmd = f'XMODEM RECEIVE "{remote}"\r'.encode("ascii")
        os.write(fd, cmd)
        # Give the device a moment to start the XMODEM receiver before we
        # start watching for its NAK/C.
        time.sleep(0.6)
        ok = send_xmodem(fd, payload, remote)
        if not ok: return 1
        # Drain trailing text (the device prints success and a fresh prompt).
        tail = b""
        end = time.monotonic() + 2.0
        while time.monotonic() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                chunk = os.read(fd, 4096)
                if chunk: tail += chunk
        sys.stdout.write(tail.decode("utf-8", "replace"))
        sys.stdout.write("\n[xmodem] upload ok\n")
        return 0
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
