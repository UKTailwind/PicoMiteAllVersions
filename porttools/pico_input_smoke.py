#!/usr/bin/env python3
"""Pico hardware smoke for MMgetline / INPUT.

Drives the connected PicoMite over USB serial and exercises every branch of
the shared `MMgetline` in `runtime/runtime_getline.c` — the function used by
INPUT, LINE INPUT, the REPL PIN prompt, cmd_files pagination, and several
prompts in Draw.c. The function regressed during spine extraction (commit
12769ef) where four port-local copies drifted; this smoke gates against that.

Each subtest sends an INPUT statement, transmits a controlled byte sequence
(with optional inter-byte delays so we can also verify per-character echo),
then asks BASIC to PRINT the captured variable bracketed by sentinels so we
can read the exact contents out of the noisy console transcript.

Run from the repo root:
    python3.11 porttools/pico_input_smoke.py --port /dev/cu.usbmodem2101
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass

REPO_ROOT_HINT = __file__  # not used at runtime; kept so basic_serial import resolves
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, default_port, strip_ansi  # noqa: E402


# MAXSTRLEN in MMBasic is 255. The "too long" guard fires at nbrchars > MAXSTRLEN.
MMBASIC_MAXSTRLEN = 255

# Hardware_Includes.h key codes — F-keys travel as single high-bit bytes.
F1, F2, F3 = 0x91, 0x92, 0x93
F4, F5, F6 = 0x94, 0x95, 0x96
F7, F8, F9 = 0x97, 0x98, 0x99
F10, F11, F12 = 0x9A, 0x9B, 0x9C


@dataclass
class Case:
    name: str
    keystrokes: bytes
    expected: str
    # If set, do not require equality — just look for this regex in the
    # whole transcript captured during the INPUT statement.
    expect_pattern: str | None = None
    # If set, the line is expected to error out; assert the error string and
    # do not check a captured value.
    expect_error: str | None = None


def drain(basic: BasicSerial, seconds: float) -> bytes:
    return basic.read_for(seconds)


def send_bytes(basic: BasicSerial, data: bytes, per_byte_delay: float = 0.0) -> None:
    assert basic.serial is not None
    if per_byte_delay <= 0.0:
        basic.serial.write(data)
        basic.serial.flush()
        return
    for b in data:
        basic.serial.write(bytes([b]))
        basic.serial.flush()
        time.sleep(per_byte_delay)


def wait_for(basic: BasicSerial, needle: bytes, timeout: float) -> bytes:
    """Read until `needle` shows up in the (ANSI-stripped) tail, or timeout."""
    assert basic.serial is not None
    end = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < end:
        chunk = basic.serial.read(4096)
        if chunk:
            buf.extend(chunk)
            if needle in strip_ansi(bytes(buf)):
                return bytes(buf)
        else:
            time.sleep(0.01)
    return bytes(buf)


# Use single-byte STX/ETX sentinels emitted via CHR$() so the BASIC source we
# send (echoed back by the prompt) cannot itself contain a literal sentinel —
# otherwise the regex would match the echoed command line, not the result.
SENTINEL_LEFT = b"\x02"
SENTINEL_RIGHT = b"\x03"
PRINT_RESULT_CMD = b'PRINT CHR$(2);a$;CHR$(3)\r'

RESULT_RE = re.compile(
    re.escape(SENTINEL_LEFT) + b"(.*?)" + re.escape(SENTINEL_RIGHT),
    re.DOTALL,
)


def run_input_case(basic: BasicSerial, case: Case, per_byte_delay: float = 0.0) -> tuple[bool, str]:
    """Drive one INPUT statement; return (passed, detail_message)."""
    assert basic.serial is not None
    # Clear any pending data so prior cases don't pollute the read window.
    drain(basic, 0.05)

    # Issue the INPUT statement. Use immediate mode: `INPUT a$` prompts with
    # "? " then assigns the line to a$.
    basic.serial.write(b"INPUT a$\r")
    basic.serial.flush()

    # Wait briefly for the "? " prompt before pushing keystrokes so we don't
    # race the prompt printer.
    pre_input = wait_for(basic, b"? ", timeout=2.0)

    # Send the test keystrokes.
    send_bytes(basic, case.keystrokes, per_byte_delay=per_byte_delay)

    # Wait for the BASIC prompt to return (success) OR an Error: line.
    settled = wait_for(basic, b">", timeout=5.0)
    combined = pre_input + settled
    visible = strip_ansi(combined).decode("latin1", "replace")

    if case.expect_error is not None:
        # Recover the prompt and assert the error appeared.
        ok = case.expect_error in visible
        # Re-sync because the error path may have left us mid-line.
        basic.sync(timeout=3.0)
        return ok, visible

    # Pull a$ back via a PRINT with sentinels so we can pluck the value out.
    drain(basic, 0.05)
    basic.serial.write(PRINT_RESULT_CMD)
    basic.serial.flush()
    after = wait_for(basic, SENTINEL_RIGHT, timeout=3.0)
    after_clean = strip_ansi(after)
    match = RESULT_RE.search(after_clean)
    if not match:
        return False, f"no sentinel match\n--- got ---\n{after_clean.decode('latin1', 'replace')}"
    captured = match.group(1).decode("latin1", "replace")

    if case.expect_pattern is not None:
        ok = re.search(case.expect_pattern, captured, re.DOTALL) is not None
        return ok, f"captured={captured!r}"
    ok = captured == case.expected
    return ok, f"captured={captured!r} expected={case.expected!r}"


def build_cases() -> list[Case]:
    long_run = b"X" * (MMBASIC_MAXSTRLEN + 5) + b"\r"
    # Printable ASCII minus comma (INPUT's field separator) and double-quote
    # (INPUT treats a leading quote specially; we sidestep that here so the
    # comparison is purely against MMgetline's output, not INPUT's parser).
    # Leading 'X' prevents INPUT's skipspace() from trimming the literal space
    # at the start of the printable range — we want to assert the space round-
    # trips through MMgetline, not test INPUT's behaviour.
    ascii_safe = b"X" + bytes(c for c in range(32, 127) if c not in (ord(','), ord('"')))
    return [
        Case("basic_cr",      b"hello\r",           "hello"),
        Case("basic_lf",      b"hello\n",           "hello"),
        Case("empty",         b"\r",                ""),
        Case("backspace",     b"abcd\x08\x08xy\r",  "abxy"),
        Case("bs_underflow",  b"\x08\x08hi\r",      "hi"),
        # tab_at_start lives in run_tab_at_start_echo_check() — INPUT's
        # skipspace() on the captured line strips the leading 4 spaces, so
        # we verify tab expansion through the per-character echo instead.
        Case("tab_pad_3",     b"x\tab\r",           "x   ab"),
        Case("tab_pad_2",     b"xy\tab\r",          "xy  ab"),
        Case("tab_pad_1",     b"xyz\tab\r",         "xyz ab"),
        Case("tab_full_4",    b"abcd\tef\r",        "abcd    ef"),
        Case("punctuation",   b'a "quoted" and; semi\r',  # no comma — INPUT splits on it
                                                    'a "quoted" and; semi'),
        Case("ascii_no_sep",  ascii_safe + b"\r",
                              ascii_safe.decode("ascii")),
        # F-key macro substitutions — depend on the console layer passing
        # high-bit bytes straight through MMfgetc. The hard-coded macros
        # (F2..F4, F10..F12) are checked since they don't depend on the
        # user's Option.Fxkey configuration.
        Case("f2_macro",      bytes([F2]),          "RUN"),
        Case("f3_macro",      bytes([F3]),          "LIST"),
        Case("f4_macro",      bytes([F4]),          "EDIT"),
        Case("f10_macro",     bytes([F10]),         "AUTOSAVE"),
        Case("f11_macro",     bytes([F11]),         "XMODEM RECEIVE"),
        Case("f12_macro",     bytes([F12]),         "XMODEM SEND"),
        # Long-line guard — must error, must not corrupt the prompt.
        Case("too_long",      long_run,             "",
             expect_error="Line is too long"),
    ]


def run_tab_at_start_echo_check(basic: BasicSerial) -> tuple[bool, str]:
    """Tab at the start of a line: INPUT's skipspace() trims it from a$, so
    verify tab expansion via the per-character echo MMgetline emits. We send
    a tab and immediately read; the four-space pad must arrive before the
    next byte does."""
    drain(basic, 0.05)
    basic.serial.write(b"INPUT a$\r")
    basic.serial.flush()
    wait_for(basic, b"? ", timeout=2.0)
    basic.serial.write(b"\t")
    basic.serial.flush()
    echo = drain(basic, 0.3)
    basic.serial.write(b"ab\r")
    basic.serial.flush()
    wait_for(basic, b">", timeout=3.0)
    echo_clean = strip_ansi(echo).decode("latin1", "replace")
    ok = echo_clean.count(" ") >= 4
    return ok, f"echo_after_tab={echo_clean!r} (need >=4 spaces)"


def run_echo_check(basic: BasicSerial) -> tuple[bool, str]:
    """Type 'hi' slowly, verify each char is echoed back before Enter."""
    drain(basic, 0.05)
    basic.serial.write(b"INPUT a$\r")
    basic.serial.flush()
    wait_for(basic, b"? ", timeout=2.0)

    # Send 'h' and 'i' with 60 ms gaps so we can observe per-byte echo.
    basic.serial.write(b"h")
    basic.serial.flush()
    early = drain(basic, 0.2)
    basic.serial.write(b"i")
    basic.serial.flush()
    mid = drain(basic, 0.2)
    basic.serial.write(b"\r")
    basic.serial.flush()
    wait_for(basic, b">", timeout=3.0)

    echo_clean = strip_ansi(early + mid).decode("latin1", "replace")
    ok = "h" in echo_clean and "i" in echo_clean
    return ok, f"echo_transcript={echo_clean!r}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--per-byte-delay", type=float, default=0.0,
                        help="seconds to wait between writing each keystroke; useful for echo timing")
    parser.add_argument("--filter", help="only run cases whose name matches this regex")
    parser.add_argument("--list", action="store_true", help="print case list and exit")
    args = parser.parse_args()

    cases = build_cases()
    if args.filter:
        rx = re.compile(args.filter)
        cases = [c for c in cases if rx.search(c.name)]
    if args.list:
        for c in cases:
            print(c.name)
        return 0

    passed = 0
    failed: list[tuple[str, str]] = []

    with BasicSerial(args.port) as basic:
        basic.sync(timeout=8.0, boot_wait=args.boot_wait)

        for case in cases:
            ok, detail = run_input_case(basic, case, per_byte_delay=args.per_byte_delay)
            status = "OK  " if ok else "FAIL"
            print(f"  {status}  {case.name:18s}  {detail}")
            if ok:
                passed += 1
            else:
                failed.append((case.name, detail))

        # Echo timing check (separate because it needs per-byte pacing).
        ok, detail = run_echo_check(basic)
        status = "OK  " if ok else "FAIL"
        print(f"  {status}  {'echo_per_byte':18s}  {detail}")
        if ok:
            passed += 1
        else:
            failed.append(("echo_per_byte", detail))

        # Tab-at-start is observed via echo because INPUT's skipspace()
        # strips leading whitespace from the captured variable.
        ok, detail = run_tab_at_start_echo_check(basic)
        status = "OK  " if ok else "FAIL"
        print(f"  {status}  {'tab_at_start_echo':18s}  {detail}")
        if ok:
            passed += 1
        else:
            failed.append(("tab_at_start_echo", detail))

    total = passed + len(failed)
    print(f"\n{passed}/{total} passed")
    if failed:
        print("\nFailures:")
        for name, detail in failed:
            print(f"  - {name}: {detail}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
