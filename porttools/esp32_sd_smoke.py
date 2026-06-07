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

        def cmd(line: str, timeout: float | None = None, *, paginate: bool = False) -> str:
            runner = basic.command_with_pagination if paginate else basic.command
            result = runner(line, timeout=timeout or args.timeout)
            text = result.clean_text
            transcript.append(text)
            return text

        listing = cmd('FILES "B:"', timeout=args.long_timeout, paginate=True)
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

        # Regression guard: ExistsFile / FileSize must route to the FatFS
        # backend when the current drive is B: (or the path is prefixed
        # B:...). Previously these helpers always queried LFS, so EDIT
        # "foo.bas" on an SD-resident file opened an empty buffer.
        size_line = cmd(f'PRINT "SIZE:" + STR$(MM.INFO(FILESIZE "{tmp1}"))')
        m = re.search(r"SIZE:\s*(-?\d+)", size_line)
        if not m:
            raise SystemExit(f"FILESIZE output not found in: {size_line!r}")
        if int(m.group(1)) <= 0:
            raise SystemExit(
                f"MM.INFO(FILESIZE) returned non-positive size for SD file "
                f"{tmp1!r}: {size_line!r}"
            )
        exists_line = cmd(f'PRINT "EXISTS:" + STR$(MM.INFO(EXISTS FILE "{tmp1}"))')
        if not re.search(r"EXISTS:\s*1\b", exists_line):
            raise SystemExit(
                f"MM.INFO(EXISTS FILE) failed to report SD file present: "
                f"{exists_line!r}"
            )
        # Same check via an explicit B: prefix from the A: drive — exercises
        # the prefix-routing path, not just the current-drive fallback.
        cmd('DRIVE "A:"')
        size_pfx = cmd(f'PRINT "BSIZE:" + STR$(MM.INFO(FILESIZE "B:/{tmp1}"))')
        m2 = re.search(r"BSIZE:\s*(-?\d+)", size_pfx)
        if not m2 or int(m2.group(1)) <= 0:
            raise SystemExit(
                f"MM.INFO(FILESIZE) with B: prefix failed: {size_pfx!r}"
            )
        cmd('DRIVE "B:"')

        cmd(f'RENAME "{tmp1}" AS "{tmp2}"')
        cmd(f'KILL "{tmp2}"')
        cmd('DRIVE "A:"')

    print("ESP32 SD smoke: PASS")
    if args.expect_file:
        print(f"Expected file present: {args.expect_file}")
    if args.expect_text is not None:
        print(f"Expected text read: {args.expect_text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
