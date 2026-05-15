#!/usr/bin/env python3
"""Device-side smoke for DATE$ / TIME$ / EPOCH / DATETIME$ / DAY$.

Locks down the BASIC wall-clock conversion path before the audit's
hal_calendar refactor consolidates the per-port timegm/gmtime shim
wrappers behind a single HAL contract. Every BASIC datetime function
ultimately calls `timegm` or `gmtime` (or both) under the hood:

    EPOCH("dd-mm-yyyy hh:mm:ss")  →  timegm
    DATETIME$(epoch_int)          →  gmtime
    DAY$("dd-mm-yyyy")            →  timegm → gmtime (tm_wday)

The fixed-input checks here use known oracles taken from the host
test suite (host/tests/t194_datetime_funs.bas) so any algorithmic
drift between the bare hand-rolled body (Pico's GPS.c) and a libc
backing (host/wasm/ESP32) shows up as a single failing check.

Runs as immediate-mode BASIC over the serial console. Works against
both the PicoCalc (USB-CDC) and the ESP32-S3 Metro (USB Serial/JTAG)
because the BASIC surface is identical; the underlying timegm/gmtime
impls are what the smoke is gating against.

Run from the repo root:
    python3.11 porttools/device_datetime_smoke.py --port /dev/cu.usbmodem101
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, default_port, strip_ansi  # noqa: E402


@dataclass
class Check:
    name: str
    command: str
    expected: str


# Oracles cross-checked against host/tests/t194_datetime_funs.bas plus
# standard Unix epoch reference values.
CHECKS: tuple[Check, ...] = (
    # EPOCH: dd-mm-yyyy hh:mm:ss → seconds since 1970-01-01 UTC
    Check("epoch_y2024_jan1",        'PRINT EPOCH("01-01-2024 00:00:00")',                              "1704067200"),
    Check("epoch_y2024_jan1_offset", 'PRINT EPOCH("01-01-2024 12:34:56") - EPOCH("01-01-2024 00:00:00")', "45296"),
    Check("epoch_y2000_mar1",        'PRINT EPOCH("01-03-2000 00:00:00")',                              "951868800"),

    # DATETIME$: int epoch → dd-mm-yyyy hh:mm:ss
    Check("datetime_y2024_jan1",     'PRINT DATETIME$(1704067200)',                                     "01-01-2024 00:00:00"),
    Check("datetime_y2000_mar1",     'PRINT DATETIME$(951868800)',                                      "01-03-2000 00:00:00"),

    # DAY$: dd-mm-yyyy → weekday name (verifies tm_wday from gmtime)
    Check("day_y2024_jan1_mon",      'PRINT DAY$("01-01-2024")',                                        "Monday"),
    Check("day_y2024_jun15_sat",     'PRINT DAY$("15-06-2024")',                                        "Saturday"),
    Check("day_y2000_feb29_tue",     'PRINT DAY$("29-02-2000")',                                        "Tuesday"),

    # Round-trip: same input bounced through EPOCH then DATETIME$
    Check("roundtrip_y1980_jul04",   'PRINT DATETIME$(EPOCH("04-07-1980 18:30:15"))',                   "04-07-1980 18:30:15"),
    Check("roundtrip_y2099_dec31",   'PRINT DATETIME$(EPOCH("31-12-2099 23:59:59"))',                   "31-12-2099 23:59:59"),
)


def strip_echo(text: str, command: str) -> str:
    """Drop the command-echo line from a transcript so assertions match
    actual output. Mirrors pico_console_smoke.strip_echo behaviour."""
    lines = text.splitlines()
    out = []
    seen_echo = False
    for line in lines:
        if not seen_echo and command.strip() in line:
            seen_echo = True
            continue
        out.append(line)
    return "\n".join(out)


def run_checks(basic: BasicSerial) -> tuple[int, list[tuple[str, str]]]:
    passed = 0
    failures: list[tuple[str, str]] = []
    for c in CHECKS:
        try:
            result = basic.command(c.command, timeout=8.0, check_error=False)
        except Exception as exc:
            failures.append((c.name, f"exception={exc!r}"))
            print(f"  FAIL  {c.name:<28} exception={exc!r}")
            continue
        clean = strip_ansi(result.clean_text.encode("latin1", "replace")).decode("latin1", "replace")
        clean = strip_echo(clean, c.command).strip()
        # Match: expected appears on a line by itself (or as a trimmed line).
        lines = [ln.strip() for ln in clean.splitlines() if ln.strip()]
        match = any(ln == c.expected for ln in lines)
        if match:
            passed += 1
            print(f"  OK    {c.name:<28} = {c.expected!r}")
        else:
            failures.append((c.name, f"expected={c.expected!r} lines={lines!r}"))
            print(f"  FAIL  {c.name:<28} expected={c.expected!r} lines={lines!r}")
    return passed, failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port(), help="serial device path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-wait", type=float, default=1.0)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    with BasicSerial(args.port, baud=args.baud) as basic:
        basic.sync(timeout=8.0, boot_wait=args.boot_wait)
        passed, failures = run_checks(basic)
    total = passed + len(failures)
    print(f"\n{passed}/{total} passed")
    if failures:
        print("\nFailures:")
        for name, detail in failures:
            print(f"  - {name}: {detail}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
