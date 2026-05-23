#!/usr/bin/env python3
"""
ports/pc386/tests/run_smokes.py — XMODEM-upload + RUN each smoke under
ports/pc386/demos/smokes/, aggregate OK_/FAIL_ markers, report a summary.

Usage:
    python3 run_smokes.py                    # run all smokes
    python3 run_smokes.py core math          # just those two
    PC386_TTY=/dev/cu.usbXXX python3 run_smokes.py

Stdlib only. Reuses send_xmodem from xmodem_upload.py via subprocess so
we don't duplicate the protocol code.
"""

import argparse
import os
import re
import select
import subprocess
import sys
import termios
import time

REPO       = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SMOKES_DIR = os.path.join(REPO, "ports", "pc386", "demos", "smokes")
UPLOAD_SH  = os.path.join(os.path.dirname(__file__), "xmodem_upload.py")
TTY        = os.environ.get("PC386_TTY", "/dev/cu.usbserial-0001")
BAUD       = int(os.environ.get("PC386_BAUD", "38400"))
DEFAULT_RUN_TIMEOUT = 20.0  # per-smoke RUN budget; bump if a smoke is genuinely slow


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


def drain(fd: int, t: float = 0.3) -> None:
    end = time.monotonic() + t
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            os.read(fd, 4096)


def run_basic(fd: int, remote_path: str, timeout: float) -> bytes:
    """Send `RUN "<remote_path>"` and capture output until SMOKE_DONE or
    timeout. Returns the raw captured bytes."""
    drain(fd, 0.2)
    os.write(fd, b"\r")
    time.sleep(0.4)
    os.write(fd, f'RUN "{remote_path}"\r'.encode("ascii"))
    buf = b""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            c = os.read(fd, 4096)
            if c: buf += c
        if b"SMOKE_DONE" in buf: break
    return buf


def upload(local_bas: str, remote_name: str) -> bool:
    """Spawn xmodem_upload.py for the upload (reuses its protocol code).
    Returns True on success."""
    r = subprocess.run(
        ["python3", UPLOAD_SH, local_bas, remote_name],
        env={**os.environ, "PC386_TTY": TTY, "PC386_BAUD": str(BAUD)},
        capture_output=True, text=True, timeout=60,
    )
    sys.stderr.write(r.stderr)
    return r.returncode == 0


def parse_results(text: str) -> tuple[list[str], list[str]]:
    oks   = re.findall(r"(?m)^OK_(\w+)", text)
    fails = re.findall(r"(?m)^FAIL_\S+", text)
    return oks, fails


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("smokes", nargs="*",
                        help="Smoke names (without .bas). Default: all under demos/smokes/")
    parser.add_argument("--timeout", type=float, default=DEFAULT_RUN_TIMEOUT,
                        help="Per-smoke RUN timeout in seconds (default: 90)")
    parser.add_argument("--verbose", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Dump each smoke's full output (default: on). "
                             "Use --no-verbose only when you really want "
                             "summary-only — without the device's output, "
                             "failures are undiagnosable.")
    args = parser.parse_args(argv[1:])

    if not os.path.isdir(SMOKES_DIR):
        print(f"smokes dir not found: {SMOKES_DIR}", file=sys.stderr); return 2

    if args.smokes:
        names = args.smokes
    else:
        names = sorted(
            os.path.splitext(f)[0]
            for f in os.listdir(SMOKES_DIR)
            if f.endswith(".bas")
        )

    if not names:
        print("no smokes found.", file=sys.stderr); return 2

    if not os.path.exists(TTY):
        print(f"tty not found: {TTY}", file=sys.stderr); return 3

    total_ok = 0
    total_fail = 0
    summary_rows = []

    for name in names:
        local = os.path.join(SMOKES_DIR, name + ".bas")
        if not os.path.exists(local):
            print(f"[skip] missing: {local}", file=sys.stderr)
            continue
        remote = name.upper() + ".BAS"
        remote_path = "C:/" + remote
        print(f"\n========== {name} ==========")
        print(f"[upload] {local} -> {remote_path}")
        if not upload(local, remote_path):
            print(f"[FAIL] upload of {name} failed", file=sys.stderr)
            summary_rows.append((name, 0, ["upload_failed"]))
            total_fail += 1
            continue
        # Run.
        fd = open_port(TTY, BAUD)
        try:
            buf = run_basic(fd, remote_path, args.timeout)
        finally:
            os.close(fd)
        text = buf.decode("utf-8", "replace")
        if args.verbose:
            sys.stdout.write(text)
            sys.stdout.write("\n")
            sys.stdout.flush()
        oks, fails = parse_results(text)
        total_ok += len(oks)
        total_fail += len(fails)
        summary_rows.append((name, len(oks), fails))
        print(f"[done] OK={len(oks)} FAIL={len(fails)} {fails if fails else ''}")

    print("\n" + "=" * 50)
    print("SUMMARY")
    print("=" * 50)
    name_w = max((len(n) for n, _, _ in summary_rows), default=8)
    for name, ok_count, fails in summary_rows:
        status = "PASS" if not fails else "FAIL"
        print(f"  {status}  {name:<{name_w}}  OK={ok_count:3d}  {fails if fails else ''}")
    print(f"\nTotal: OK={total_ok}  FAIL={total_fail}")
    return 0 if total_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
