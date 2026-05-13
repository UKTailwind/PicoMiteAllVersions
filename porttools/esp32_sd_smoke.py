#!/usr/bin/env python3
"""Smoke-test the ESP32 Metro B: microSD path over the BASIC serial prompt."""

from __future__ import annotations

import argparse
import os
import re
import time

from basic_serial import BasicSerial, strip_ansi, default_port


def clean(text: bytes) -> str:
    return strip_ansi(text).decode("latin1", "replace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--long-timeout", type=float, default=12.0)
    parser.add_argument("--reset-app", action="store_true")
    parser.add_argument("--expect-file", help="filename that must appear in FILES output")
    parser.add_argument("--expect-text", help="text expected when reading --expect-file")
    args = parser.parse_args()

    stem = f"sd{int(time.time()) & 0xffff:04x}"
    tmp1 = f"{stem}.tmp"
    tmp2 = f"{stem}.ren"

    transcript: list[str] = []
    with BasicSerial(args.port, args.baud) as basic:
        if args.reset_app:
            basic.reset_app()
        transcript.append(clean(basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)))

        def cmd(line: str, timeout: float | None = None) -> str:
            result = basic.command(line, timeout=timeout or args.timeout)
            text = result.clean_text
            transcript.append(text)
            return text

        listing = cmd('FILES "B:"', timeout=args.long_timeout)
        if args.expect_file and not re.search(rf"\b{re.escape(args.expect_file)}\b", listing, re.I):
            raise SystemExit(f"missing expected SD file in listing: {args.expect_file}")

        if args.expect_file and args.expect_text is not None:
            cmd('DRIVE "B:"')
            cmd(f'OPEN "{args.expect_file}" FOR INPUT AS #1')
            cmd("LINE INPUT #1,A$")
            text = cmd('PRINT "READ:" + A$')
            cmd("CLOSE #1")
            if f"READ:{args.expect_text}" not in text:
                raise SystemExit(f"unexpected file text; wanted {args.expect_text!r}")

        cmd('DRIVE "B:"')
        cmd(f'OPEN "{tmp1}" FOR OUTPUT AS #1')
        cmd('PRINT #1,"SD_SMOKE_OK"')
        cmd("CLOSE #1")
        cmd(f'OPEN "{tmp1}" FOR INPUT AS #1')
        cmd("LINE INPUT #1,A$")
        readback = cmd('PRINT "READBACK:" + A$')
        cmd("CLOSE #1")
        if "READBACK:SD_SMOKE_OK" not in readback:
            raise SystemExit("SD write/readback failed")
        cmd(f'RENAME "{tmp1}" AS "{tmp2}"')
        cmd(f'KILL "{tmp2}"')

    print("ESP32 SD smoke: PASS")
    if args.expect_file:
        print(f"Expected file present: {args.expect_file}")
    if args.expect_text is not None:
        print(f"Expected text read: {args.expect_text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
