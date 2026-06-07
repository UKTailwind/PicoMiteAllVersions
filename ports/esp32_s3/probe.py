#!/usr/bin/env python3
"""
ports/esp32_s3/probe.py - drive the ESP32-S3 over USB Serial/JTAG
without picocom. Uses a single pyserial Serial object for both reads
and writes to avoid the macOS dual-fd reset issue.
"""

import argparse, sys, time, re, glob, serial


def find_port():
    matches = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not matches:
        sys.exit("probe: no /dev/cu.usbmodem* found")
    return matches[0]


def open_port(path):
    s = serial.Serial(path, 115200, timeout=0.1,
                      dsrdtr=False, rtscts=False)
    # Make sure neither DTR nor RTS pulses high after open: keep IO0
    # released (app boot) and EN released (no reset).
    s.dtr = False
    s.rts = False
    return s


def reset_app(s):
    """RTS pulse only — chip resets, IO0 stays high → app boot."""
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    time.sleep(0.05)


def read_for(s, secs, log_raw):
    end = time.time() + secs
    out = bytearray()
    while time.time() < end:
        n = s.in_waiting
        chunk = s.read(max(n, 1))
        if chunk:
            out.extend(chunk)
            log_raw.write(chunk)
            log_raw.flush()
        else:
            time.sleep(0.02)
    return bytes(out)


ANSI = re.compile(rb"\x1b\[[0-9;?]*[ -/]*[@-~]")
def strip_ansi(b):
    return ANSI.sub(b"", b)


def banner(t):
    print()
    print("=" * 72)
    print(t)
    print("=" * 72)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default=None)
    ap.add_argument("--boot-secs", type=float, default=4.0)
    ap.add_argument("--cmd-secs",  type=float, default=2.0)
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--raw", default="/tmp/esp32_probe.bin")
    args = ap.parse_args()

    port = args.port or find_port()
    print(f"probe: port={port}")

    s = open_port(port)
    reset_app(s)
    print("probe: chip reset (RTS pulse, IO0 high)")

    with open(args.raw, "wb") as log_raw:
        banner(f"BOOT CAPTURE ({args.boot_secs}s)")
        boot = read_for(s, args.boot_secs, log_raw)
        print(f"  read {len(boot)} bytes")
        sys.stdout.write(strip_ansi(boot).decode("utf-8", errors="replace"))
        sys.stdout.flush()

        for cmd in args.cmd:
            line = (cmd + "\r").encode("utf-8")
            banner(f"SEND: {cmd!r} ({len(line)} bytes)")
            n = s.write(line)
            s.flush()
            print(f"  wrote {n}/{len(line)} bytes")
            resp = read_for(s, args.cmd_secs, log_raw)
            print(f"  read  {len(resp)} bytes")
            sys.stdout.write(strip_ansi(resp).decode("utf-8", errors="replace"))
            sys.stdout.flush()

    s.close()
    print()
    print(f"probe: raw to {args.raw}")


if __name__ == "__main__":
    main()
