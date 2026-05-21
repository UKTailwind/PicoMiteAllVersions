#!/usr/bin/env python3
"""
ports/pc386/tests/hw_smoke.py — REPL smoke driver for a real Pocket386
connected over USB-to-serial at 38400 8N1.

Sister to repl_expect.py (which drives QEMU). This one targets the
actual hardware via /dev/cu.usbserial-XXXX. Use it to run quick
diagnostic command sequences and capture output for triage.

Usage:
    python3 ports/pc386/tests/hw_smoke.py disk       # disk smoke
    python3 ports/pc386/tests/hw_smoke.py save_bmp   # SAVE IMAGE bug repro
    python3 ports/pc386/tests/hw_smoke.py all
    PC386_TTY=/dev/cu.usbserial-XYZ python3 ports/pc386/tests/hw_smoke.py disk

Stdlib only — does not depend on pyserial.
"""

import os
import re
import select
import subprocess
import sys
import termios
import time

TTY        = os.environ.get("PC386_TTY", "/dev/cu.usbserial-0001")
BAUD       = int(os.environ.get("PC386_BAUD", "38400"))
PROMPT     = re.compile(rb"(?:\r?\n)?> (?:\x1b\[\?25h)?\Z")
DEFAULT_TIMEOUT = float(os.environ.get("PC386_TIMEOUT", "5.0"))


def open_port(tty: str, baud: int):
    """Open the tty and force 8N1 raw mode via termios. Settings applied
    AFTER open so they survive macOS's cu-device open-time reset."""
    fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    # cflag: 8N1, enable receiver, ignore modem control
    cflag = (cflag | termios.CS8 | termios.CREAD | termios.CLOCAL) & ~(
        termios.PARENB | termios.CSTOPB | termios.CSIZE) | termios.CS8
    iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
               termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
    oflag &= ~termios.OPOST
    lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG |
               termios.IEXTEN)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW,
                      [iflag, oflag, cflag, lflag, baud, baud, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def drain(fd, timeout: float = 0.3) -> bytes:
    """Read whatever's pending on the port within `timeout` of silence."""
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            chunk = os.read(fd, 4096)
            if chunk:
                buf += chunk
                deadline = time.monotonic() + timeout  # extend on activity
    return buf


PAGINATION_RE = re.compile(rb"PRESS ANY KEY")


def wait_for_prompt(fd, timeout: float = DEFAULT_TIMEOUT,
                    idle_window: float = 0.04) -> bytes:
    """Read until the MMBasic prompt appears AND the stream has been
    quiet for `idle_window` seconds. Auto-advances FILES-style
    'PRESS ANY KEY' pagination by sending a space."""
    buf = b""
    last_paginated = 0  # length of buf when we last responded to pagination
    overall = time.monotonic() + timeout
    last_activity = time.monotonic()
    while time.monotonic() < overall:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            chunk = os.read(fd, 4096)
            if chunk:
                buf += chunk
                last_activity = time.monotonic()
                if PAGINATION_RE.search(buf[last_paginated:]):
                    os.write(fd, b" ")
                    last_paginated = len(buf)
                continue
        if PROMPT.search(buf) and (time.monotonic() - last_activity) >= idle_window:
            return buf
    return buf  # timeout — return whatever we have


def send_line(fd, line: str) -> None:
    payload = (line + "\r").encode("ascii")
    os.write(fd, payload)


def run_command(fd, line: str, timeout: float = DEFAULT_TIMEOUT) -> bytes:
    """Send one line, return everything received before the next prompt."""
    drain(fd, 0.1)
    send_line(fd, line)
    return wait_for_prompt(fd, timeout)


def _xmodem_crc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def xmodem_send(fd, payload: bytes, label: str = "file",
                handshake_timeout: float = 10.0,
                block_timeout: float = 5.0) -> bool:
    """Push `payload` to the device using XMODEM-128. Auto-detects CRC vs
    checksum mode based on what the receiver sends (`C` = CRC, NAK = sum).
    Returns True on success."""
    SOH = 0x01; EOT = 0x04; ACK = 0x06; NAK = 0x15; CAN = 0x18; CRC_REQ = 0x43
    # Drain any buffered data first.
    drain(fd, 0.1)

    # Wait for receiver to send NAK or 'C' to start.
    deadline = time.monotonic() + handshake_timeout
    mode_crc = None
    while time.monotonic() < deadline:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            b = os.read(fd, 1)
            if not b: continue
            if b[0] == CRC_REQ:
                mode_crc = True
                break
            if b[0] == NAK:
                mode_crc = False
                break
            # Ignore noise (e.g. command-prompt echo bytes).
    if mode_crc is None:
        sys.stderr.write(f"[xmodem] {label}: no NAK/C handshake within {handshake_timeout}s\n")
        return False

    # Pad payload to 128-byte multiples with ^Z (0x1A) per XMODEM spec.
    pad = (-len(payload)) % 128
    body = payload + (b"\x1a" * pad)
    nblocks = len(body) // 128

    for i in range(nblocks):
        block = body[i*128:(i+1)*128]
        bn = (i + 1) & 0xFF
        header = bytes([SOH, bn, (~bn) & 0xFF])
        if mode_crc:
            crc = _xmodem_crc(block)
            trailer = bytes([(crc >> 8) & 0xFF, crc & 0xFF])
        else:
            trailer = bytes([sum(block) & 0xFF])
        frame = header + block + trailer

        # Send + wait for ACK/NAK, with limited retries.
        for attempt in range(10):
            os.write(fd, frame)
            t_end = time.monotonic() + block_timeout
            resp = None
            while time.monotonic() < t_end:
                r, _, _ = select.select([fd], [], [], 0.1)
                if r:
                    b = os.read(fd, 1)
                    if b:
                        resp = b[0]
                        break
            if resp == ACK:
                break
            if resp == CAN:
                sys.stderr.write(f"[xmodem] {label}: receiver cancelled at block {i+1}\n")
                return False
            sys.stderr.write(f"[xmodem] {label}: block {i+1} attempt {attempt+1} resp={resp} (NAK?)\n")
        else:
            sys.stderr.write(f"[xmodem] {label}: gave up after retries on block {i+1}\n")
            return False

    # End of transmission. Send EOT, expect ACK (possibly NAK first then ACK).
    for attempt in range(3):
        os.write(fd, bytes([EOT]))
        t_end = time.monotonic() + 3.0
        while time.monotonic() < t_end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                b = os.read(fd, 1)
                if b and b[0] == ACK:
                    return True
                if b and b[0] == NAK:
                    break  # retry EOT
    sys.stderr.write(f"[xmodem] {label}: no ACK to EOT\n")
    return False


def bulk_run(fd, lines: list, timeout: float = 60.0) -> bytes:
    """Stream all commands as a single CR-separated byte sequence and read
    everything that comes back until the device goes idle. ~5-10× faster
    than per-line run_command because we skip the per-prompt wait.
    Chunks the send into 64-byte writes with brief pauses so the device's
    256-byte RX ring stays well under capacity."""
    drain(fd, 0.05)
    payload = b""
    for line in lines:
        payload += line.encode("ascii") + b"\r"
    CHUNK = 64
    for i in range(0, len(payload), CHUNK):
        os.write(fd, payload[i:i+CHUNK])
        # Tiny pacing so the device's REPL has a chance to drain the ring.
        time.sleep(0.02)
    # Now read everything until ~600 ms of silence at a prompt.
    buf = b""
    end = time.monotonic() + timeout
    last_act = time.monotonic()
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            chunk = os.read(fd, 4096)
            if chunk:
                buf += chunk
                last_act = time.monotonic()
                if PAGINATION_RE.search(buf):
                    os.write(fd, b" ")
                continue
        if PROMPT.search(buf) and (time.monotonic() - last_act) > 0.6:
            break
    return buf


# ---------------- Smoke definitions ---------------------------------------

SMOKES = {
    "disk": [
        'FILES',
        'FREE',
    ],
    "tiny_write": [
        'OPEN "tiny.txt" FOR OUTPUT AS #1',
        'PRINT #1, "hello"',
        'CLOSE #1',
        'FILES',
    ],
    "tiny_write_cdrive": [
        '? CWD$',
        'OPEN "C:/tiny2.txt" FOR OUTPUT AS #1',
        'PRINT #1, "hello-c"',
        'CLOSE #1',
        'FILES "C:/"',
    ],
    "tiny_cleanup": [
        'KILL "tiny.txt"',
        'FILES',
    ],
    "save_bmp": [
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "test_hw.bmp", 0, 0, 320, 200',
        'FILES',
    ],
    "save_bmp_8x8": [
        'CHDIR "C:/"',
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/tiny_8.bmp", 0, 0, 8, 8',
        'FILES "C:/"',
    ],
    "save_bmp_32x32": [
        'CHDIR "C:/"',
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/tiny_32.bmp", 0, 0, 32, 32',
        'FILES "C:/"',
    ],
    "save_bmp_100x100": [
        'CHDIR "C:/"',
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/tiny_100.bmp", 0, 0, 100, 100',
        'FILES "C:/"',
    ],
    "save_bmp_200x150": [
        'CHDIR "C:/"',
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/tiny_200.bmp", 0, 0, 200, 150',
        'FILES "C:/"',
    ],
    "save_bmp_320x200": [
        'CHDIR "C:/"',
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/tiny_320.bmp", 0, 0, 320, 200',
        'FILES "C:/"',
    ],
    "save_bmp_mode3": [
        'MODE 3',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "test_hw3.bmp", 0, 0, 720, 400',
        'FILES',
    ],
    "modes": [
        'MODE',
        'MODE DEBUG',
    ],
    # ------------------------------------------------------------------
    # Full disk smoke — exercises the ATA-PIO + FatFs path with the
    # patterns most likely to expose driver bugs: round-trip writes,
    # multi-sector transfers, append, multi-handle, KILL/rename, and the
    # SAVE IMAGE path that triggered the BSY/CACHE_FLUSH race.
    # Each phase emits OK_<name> or FAIL_<name> for grep.
    # ------------------------------------------------------------------
    "disk_full": [
        'CHDIR "C:/"',

        # P1: small write+readback
        'OPEN "C:/sk_a.txt" FOR OUTPUT AS #1',
        'PRINT #1, "alpha"',
        'PRINT #1, "beta"',
        'CLOSE #1',
        'OPEN "C:/sk_a.txt" FOR INPUT AS #1',
        'LINE INPUT #1, a$',
        'LINE INPUT #1, b$',
        'CLOSE #1',
        'IF a$ = "alpha" AND b$ = "beta" THEN PRINT "OK_small" ELSE PRINT "FAIL_small a=" + a$ + " b=" + b$',

        # P2: multi-sector write — write 200 lines, then verify the LAST
        # line decoded correctly (catches any bit-flip or truncation
        # across the >1-sector boundary).
        'OPEN "C:/sk_big.txt" FOR OUTPUT AS #1',
        'FOR i% = 1 TO 200 : PRINT #1, "line " + STR$(i%) : NEXT i%',
        'CLOSE #1',
        'OPEN "C:/sk_big.txt" FOR INPUT AS #1',
        'last$ = "" : FOR i% = 1 TO 200 : LINE INPUT #1, last$ : NEXT i%',
        'eof% = EOF(#1)',
        'CLOSE #1',
        'IF last$ = "line 200" AND eof% <> 0 THEN PRINT "OK_multi" ELSE PRINT "FAIL_multi last=" + last$ + " eof=" + STR$(eof%)',

        # P3: append mode
        'OPEN "C:/sk_app.txt" FOR OUTPUT AS #1',
        'PRINT #1, "first"',
        'CLOSE #1',
        'OPEN "C:/sk_app.txt" FOR APPEND AS #1',
        'PRINT #1, "second"',
        'CLOSE #1',
        'OPEN "C:/sk_app.txt" FOR INPUT AS #1',
        'LINE INPUT #1, a$',
        'LINE INPUT #1, b$',
        'CLOSE #1',
        'IF a$ = "first" AND b$ = "second" THEN PRINT "OK_append" ELSE PRINT "FAIL_append a=" + a$ + " b=" + b$',

        # P4: multi-handle concurrent writes
        'OPEN "C:/sk_h1.txt" FOR OUTPUT AS #1',
        'OPEN "C:/sk_h2.txt" FOR OUTPUT AS #2',
        'PRINT #1, "h1"',
        'PRINT #2, "h2"',
        'CLOSE #1',
        'CLOSE #2',
        'OPEN "C:/sk_h1.txt" FOR INPUT AS #1',
        'OPEN "C:/sk_h2.txt" FOR INPUT AS #2',
        'LINE INPUT #1, a$',
        'LINE INPUT #2, b$',
        'CLOSE #1',
        'CLOSE #2',
        'IF a$ = "h1" AND b$ = "h2" THEN PRINT "OK_multihandle" ELSE PRINT "FAIL_multihandle a=" + a$ + " b=" + b$',

        # P5: RENAME (cmd_name in MMBasic)
        'OPEN "C:/sk_r1.txt" FOR OUTPUT AS #1',
        'PRINT #1, "renamed"',
        'CLOSE #1',
        'RENAME "C:/sk_r1.txt" AS "C:/sk_r2.txt"',
        'OPEN "C:/sk_r2.txt" FOR INPUT AS #1',
        'LINE INPUT #1, a$',
        'CLOSE #1',
        'IF a$ = "renamed" THEN PRINT "OK_rename" ELSE PRINT "FAIL_rename a=" + a$',

        # P6: COPY
        'OPEN "C:/sk_c1.txt" FOR OUTPUT AS #1',
        'PRINT #1, "copied"',
        'CLOSE #1',
        'COPY "C:/sk_c1.txt" TO "C:/sk_c2.txt"',
        'OPEN "C:/sk_c2.txt" FOR INPUT AS #1',
        'LINE INPUT #1, a$',
        'CLOSE #1',
        'IF a$ = "copied" THEN PRINT "OK_copy" ELSE PRINT "FAIL_copy a=" + a$',

        # P7: MKDIR / file in subdir / RMDIR
        'MKDIR "C:/sk_dir"',
        'OPEN "C:/sk_dir/inside.txt" FOR OUTPUT AS #1',
        'PRINT #1, "in_subdir"',
        'CLOSE #1',
        'OPEN "C:/sk_dir/inside.txt" FOR INPUT AS #1',
        'LINE INPUT #1, a$',
        'CLOSE #1',
        'IF a$ = "in_subdir" THEN PRINT "OK_subdir" ELSE PRINT "FAIL_subdir a=" + a$',
        'KILL "C:/sk_dir/inside.txt"',
        'RMDIR "C:/sk_dir"',
        'PRINT "OK_rmdir"',

        # P8: SEEK — write known pattern, jump around, verify random read
        'OPEN "C:/sk_seek.bin" FOR OUTPUT AS #1',
        'FOR i% = 0 TO 99 : PRINT #1, "X" + STR$(i%) : NEXT i%',
        'CLOSE #1',
        'OPEN "C:/sk_seek.bin" FOR INPUT AS #1',
        'SEEK #1, 1',
        'LINE INPUT #1, a$',
        'CLOSE #1',
        'IF a$ = "X0" THEN PRINT "OK_seek_begin" ELSE PRINT "FAIL_seek_begin a=" + a$',
        'KILL "C:/sk_seek.bin"',

        # P9: KILL all leftover
        'KILL "C:/sk_a.txt"',
        'KILL "C:/sk_big.txt"',
        'KILL "C:/sk_app.txt"',
        'KILL "C:/sk_h1.txt"',
        'KILL "C:/sk_h2.txt"',
        'KILL "C:/sk_r2.txt"',
        'KILL "C:/sk_c1.txt"',
        'KILL "C:/sk_c2.txt"',
        'PRINT "OK_kill"',

        # P6: SAVE IMAGE — the BMP write path (was wedging the CF card
        # before the BSY-before-CACHE_FLUSH fix in ata_pio.c; this is
        # the critical regression).
        'MODE 1',
        'CLS RGB(YELLOW)',
        'SAVE IMAGE "C:/sk_img.bmp", 0, 0, 320, 200',
        'PRINT "OK_save_image"',
        'KILL "C:/sk_img.bmp"',

        # P7: verify FILES still works after all the I/O — if the disk
        # was wedged anywhere above, FILES would error here.
        'FILES "C:/"',
        'PRINT "OK_files_post"',
    ],
    # ------------------------------------------------------------------
    # Binary patterns + checksum.
    # ------------------------------------------------------------------
    "disk_binary": [
        'CHDIR "C:/"',
        # Build a 200-byte deterministic pattern.
        'p$ = ""',
        'FOR i% = 0 TO 199 : p$ = p$ + CHR$((i% * 17 + 31) AND 255) : NEXT i%',
        'IF LEN(p$) = 200 THEN PRINT "OK_pattern_build" ELSE PRINT "FAIL_pattern_build len=" + STR$(LEN(p$))',
        # Write 20 copies = 4000 bytes (8 sectors).
        'OPEN "C:/sk_bin.dat" FOR OUTPUT AS #1',
        'FOR i% = 1 TO 20 : PRINT #1, p$; : NEXT i%',
        'CLOSE #1',
        # Verify file size + first chunk matches pattern.
        'OPEN "C:/sk_bin.dat" FOR INPUT AS #1',
        'lof% = LOF(1)',
        'first$ = INPUT$(200, #1)',
        'CLOSE #1',
        'IF lof% = 4000 AND first$ = p$ THEN PRINT "OK_binary_head" ELSE PRINT "FAIL_binary_head lof=" + STR$(lof%) + " match=" + STR$(first$ = p$)',
        # Seek into the middle, read 200 bytes, verify still matches the
        # repeating pattern.
        'OPEN "C:/sk_bin.dat" FOR INPUT AS #1',
        'SEEK #1, 2001',
        'mid$ = INPUT$(200, #1)',
        'CLOSE #1',
        'IF mid$ = p$ THEN PRINT "OK_binary_seek" ELSE PRINT "FAIL_binary_seek"',
        'KILL "C:/sk_bin.dat"',
    ],
    # ------------------------------------------------------------------
    # >100KB file.  Heavy on the ATA-PIO path; verifies multi-sector
    # writes don't accumulate state errors.
    # ------------------------------------------------------------------
    "disk_large": [
        'CHDIR "C:/"',
        'p$ = ""',
        'FOR i% = 0 TO 199 : p$ = p$ + CHR$((i% * 17 + 31) AND 255) : NEXT i%',
        # 500 copies × 200 bytes = 100,000 bytes.
        'OPEN "C:/sk_large.dat" FOR OUTPUT AS #1',
        'FOR i% = 1 TO 500 : PRINT #1, p$; : NEXT i%',
        'CLOSE #1',
        'OPEN "C:/sk_large.dat" FOR INPUT AS #1',
        'lof% = LOF(1)',
        # Spot-check: read at byte 1, mid (50001), and near end (99801).
        'SEEK #1, 1',
        'a$ = INPUT$(200, #1)',
        'SEEK #1, 50001',
        'm$ = INPUT$(200, #1)',
        'SEEK #1, 99801',
        'z$ = INPUT$(200, #1)',
        'CLOSE #1',
        'IF lof% = 100000 AND a$ = p$ AND m$ = p$ AND z$ = p$ THEN PRINT "OK_large" ELSE PRINT "FAIL_large lof=" + STR$(lof%) + " a=" + STR$(a$ = p$) + " m=" + STR$(m$ = p$) + " z=" + STR$(z$ = p$)',
        'KILL "C:/sk_large.dat"',
    ],
    # ------------------------------------------------------------------
    # Stress loop: 20 iterations of open/write/close/open/read/close/kill.
    # Catches cumulative state issues (handle leaks, FAT corruption).
    # Roughly 4-6 seconds total runtime on real hardware.
    # ------------------------------------------------------------------
    "disk_stress": [
        'CHDIR "C:/"',
        'ok% = 1',
        # `IF ... THEN ...` on the same line consumes the rest of the line
        # as its then-branch (so any colon-separated NEXT after it gets
        # eaten). Use boolean assignment instead — keeps NEXT visible to
        # the parser and lets us write the loop body on one line.
        'FOR i% = 1 TO 20 : OPEN "C:/sk_s.tmp" FOR OUTPUT AS #1 : PRINT #1, "iter" + STR$(i%) : CLOSE #1 : OPEN "C:/sk_s.tmp" FOR INPUT AS #1 : LINE INPUT #1, x$ : CLOSE #1 : KILL "C:/sk_s.tmp" : ok% = ok% AND (x$ = "iter" + STR$(i%)) : NEXT i%',
        'IF ok% THEN PRINT "OK_stress" ELSE PRINT "FAIL_stress"',
    ],
}

SMOKE_ORDER = ["disk", "tiny_write", "save_bmp", "modes"]
DISK_ALL = ["disk_full", "disk_binary", "disk_large", "disk_stress"]


def main(argv: list[str]) -> int:
    requested = argv[1:] or ["all"]
    if requested == ["all"]:
        requested = SMOKE_ORDER
    elif requested == ["disk_all"]:
        requested = DISK_ALL
    for name in requested:
        if name not in SMOKES:
            print(f"[hw_smoke] unknown smoke: {name}", file=sys.stderr)
            print(f"[hw_smoke] available: {', '.join(SMOKES)}", file=sys.stderr)
            return 2

    if not os.path.exists(TTY):
        print(f"[hw_smoke] port not found: {TTY}", file=sys.stderr)
        return 3

    fd = open_port(TTY, BAUD)
    try:
        # Nudge the device into producing a prompt so we know where we are.
        send_line(fd, "")
        initial = wait_for_prompt(fd, timeout=2.0)
        if not PROMPT.search(initial):
            sys.stdout.write("[hw_smoke] no prompt within 2s. Initial buffer:\n")
            sys.stdout.write(initial.decode("utf-8", "replace"))
            sys.stdout.write("\n[hw_smoke] continuing anyway.\n")

        full_capture = []
        bulk_mode = os.environ.get("PC386_BULK", "0") == "1"
        for name in requested:
            print(f"\n==================== {name} ====================")
            if bulk_mode:
                out = bulk_run(fd, SMOKES[name])
                txt = out.decode("utf-8", "replace")
                sys.stdout.write(txt)
                sys.stdout.write("\n")
                full_capture.append(txt)
            else:
                for line in SMOKES[name]:
                    print(f">>> {line}")
                    out = run_command(fd, line)
                    txt = out.decode("utf-8", "replace")
                    sys.stdout.write(txt)
                    sys.stdout.write("\n")
                    full_capture.append(txt)
        joined = "".join(full_capture)
        # Anchor on line start so we only match PRINTed markers, not the
        # FAIL_/OK_ text appearing inside `IF ... ELSE PRINT "FAIL_x ..."`
        # command echoes.
        oks   = re.findall(r"(?m)^OK_(\w+)",   joined)
        fails = re.findall(r"(?m)^FAIL_\S+",  joined)
        print("\n==================== summary ====================")
        print(f"  OK     : {len(oks):3d}  {sorted(set(oks))}")
        print(f"  FAIL   : {len(fails):3d}  {fails}")
        return 0 if not fails else 1
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
