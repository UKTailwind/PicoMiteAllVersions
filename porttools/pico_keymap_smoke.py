#!/usr/bin/env python3
"""Hardware smoke for MMInkey's escape-sequence decoder.

Drives ANSI / VT100 escape sequences over USB-serial and verifies each
one decodes to the expected MMBasic key code (per Hardware_Includes.h).
The check happens via a BASIC INKEY$ poll that prints the decoded code —
INKEY$ uses MMInkey directly, so MMgetline's F-key macro substitution
doesn't interfere.

This is the gate for the audit's Finding 2 (MMInkey + escape decoder
consolidation). Each case is tagged with the port families that currently
decode it correctly, derived from a diff against the pre-extraction
`PicoMite.c` (commit 12769ef^) — the canonical ground truth:

  - legacy   = supported by legacy PicoMite.c / pico_console.c
  - xterm    = host added xterm-style ESC[H / ESC[F (not in legacy)
  - esp32    = esp32 added SHIFT_TAB + modifier-parameter skip (beyond legacy)

After Finding 2 lands, the union of all three feature sets should pass
on every port. Before then, runs against Pico will fail the xterm and
esp32 cases; runs against ESP32 will fail only the xterm cases; runs
against the host will fail the SHIFT-F* and alt-form F1-F4 cases.

Run from the repo root:
    python3.11 porttools/pico_keymap_smoke.py --port /dev/cu.usbmodem101
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, default_port, strip_ansi  # noqa: E402


# Hardware_Includes.h key codes.
UP, DOWN, LEFT, RIGHT = 0x80, 0x81, 0x82, 0x83
INSERT, HOME, END = 0x84, 0x86, 0x87
PUP, PDOWN = 0x88, 0x89
DEL = 0x7F
ALT = 0x8B
F1, F2, F3, F4, F5 = 0x91, 0x92, 0x93, 0x94, 0x95
F6, F7, F8, F9, F10 = 0x96, 0x97, 0x98, 0x99, 0x9A
F11, F12 = 0x9B, 0x9C
# Shift-F* in legacy = F* + 0x20.
SHIFT_F3, SHIFT_F4, SHIFT_F5, SHIFT_F6 = F3 + 0x20, F4 + 0x20, F5 + 0x20, F6 + 0x20
SHIFT_F7, SHIFT_F8, SHIFT_F9, SHIFT_F10 = F7 + 0x20, F8 + 0x20, F9 + 0x20, F10 + 0x20
SHIFT_TAB = 0x9F
ESC = 0x1B


@dataclass
class Case:
    name: str
    bytes_to_send: bytes
    expected_code: int
    # Tags indicating which feature set this case belongs to. After the
    # union consolidation lands every case is expected to pass on every
    # port; pre-consolidation, cases not in a port's feature set may FAIL
    # legitimately and that's flagged as "EXPECTED_FAIL" instead of "FAIL".
    feature_set: str = "legacy"
    notes: str = ""


CASES: list[Case] = [
    # --- Arrow keys (ESC [ A/B/C/D) — legacy ------------------------------
    Case("arrow_up",      b"\x1b[A", UP),
    Case("arrow_down",    b"\x1b[B", DOWN),
    Case("arrow_right",   b"\x1b[C", RIGHT),
    Case("arrow_left",    b"\x1b[D", LEFT),

    # --- ESC [ H / F → HOME/END — xterm-style, host added ---------------
    Case("home_esc_H",    b"\x1b[H",  HOME, feature_set="xterm"),
    Case("end_esc_F",     b"\x1b[F",  END,  feature_set="xterm"),

    # --- Navigation via ESC [ n ~ — legacy --------------------------------
    Case("home_csi",      b"\x1b[1~", HOME),
    Case("insert_csi",    b"\x1b[2~", INSERT),
    Case("delete_csi",    b"\x1b[3~", DEL),
    Case("end_csi",       b"\x1b[4~", END),
    Case("pageup_csi",    b"\x1b[5~", PUP),
    Case("pagedown_csi",  b"\x1b[6~", PDOWN),

    # --- F1-F5 via ESC O P/Q/R/S/T — legacy (T→F5 is the one host lost) -
    Case("f1_eso",        b"\x1bOP", F1),
    Case("f2_eso",        b"\x1bOQ", F2),
    Case("f3_eso",        b"\x1bOR", F3),
    Case("f4_eso",        b"\x1bOS", F4),
    Case("f5_eso_T",      b"\x1bOT", F5,
         notes="legacy maps ESC O T → F5 (host runtime_console.c lost this)"),

    # --- F1-F4 alt-form via ESC [ 1n~ — legacy (host lost) ---------------
    Case("f1_csi_alt",    b"\x1b[11~", F1, notes="legacy alt-form (host lost)"),
    Case("f2_csi_alt",    b"\x1b[12~", F2, notes="legacy alt-form (host lost)"),
    Case("f3_csi_alt",    b"\x1b[13~", F3, notes="legacy alt-form (host lost)"),
    Case("f4_csi_alt",    b"\x1b[14~", F4, notes="legacy alt-form (host lost)"),

    # --- F5-F12 via ESC [ n ~ — legacy ------------------------------------
    Case("f5_csi",        b"\x1b[15~", F5),
    Case("f6_csi",        b"\x1b[17~", F6),
    Case("f7_csi",        b"\x1b[18~", F7),
    Case("f8_csi",        b"\x1b[19~", F8),
    Case("f9_csi",        b"\x1b[20~", F9),
    Case("f10_csi",       b"\x1b[21~", F10),
    Case("f11_csi",       b"\x1b[23~", F11),
    Case("f12_csi",       b"\x1b[24~", F12),

    # --- Shift-F3 via ESC O 2 R — legacy quirk ----------------------------
    Case("shift_f3_eso_2R", b"\x1bO2R", SHIFT_F3,
         notes="legacy-only path; host_native lost it"),

    # --- Shift-F* via ESC [ n ~ — legacy ----------------------------------
    Case("shift_f3_csi",  b"\x1b[25~", SHIFT_F3),
    Case("shift_f4_csi",  b"\x1b[26~", SHIFT_F4),
    Case("shift_f5_csi",  b"\x1b[28~", SHIFT_F5),
    Case("shift_f6_csi",  b"\x1b[29~", SHIFT_F6),
    Case("shift_f7_csi",  b"\x1b[31~", SHIFT_F7),
    Case("shift_f8_csi",  b"\x1b[32~", SHIFT_F8),
    Case("shift_f9_csi",  b"\x1b[33~", SHIFT_F9),
    Case("shift_f10_csi", b"\x1b[34~", SHIFT_F10),

    # --- Shift-Tab — ESP32 extra ------------------------------------------
    Case("shift_tab",     b"\x1b[Z",   SHIFT_TAB, feature_set="esp32"),

    # --- Modifier-parameter skip (xterm/ESP32) — strip modifier, return
    # base key. esp32 added this; we want every port to handle it after
    # consolidation since modern terminals send Shift-arrow as
    # `ESC [ 1 ; 2 A` and similar.
    Case("mod_home",      b"\x1b[1;2H", HOME, feature_set="esp32",
         notes="xterm modifier form: Shift-Home"),
    Case("mod_pageup",    b"\x1b[5;2~", PUP,  feature_set="esp32",
         notes="xterm modifier form: Shift-PageUp"),
    Case("mod_up",        b"\x1b[1;5A", UP,   feature_set="esp32",
         notes="xterm modifier form: Ctrl-Up"),

    # --- Plain ESC (no following byte within the timeout) -----------------
    Case("plain_esc",     b"\x1b", ESC),
]


# Which feature sets are believed-supported by each port today. The
# consolidation goal is to make every port support {legacy, xterm, esp32}.
PORT_FEATURE_SETS: dict[str, set[str]] = {
    "pico":  {"legacy"},
    "esp32": {"legacy", "esp32"},
    "host":  {"xterm"},  # host lost much of legacy, gained xterm
    "any":   {"legacy", "xterm", "esp32"},  # use after consolidation
}


def drive_inkey_capture(basic: BasicSerial, send_bytes: bytes) -> tuple[bool, str, int | None]:
    """Send INKEY$ poll, then push `send_bytes`. Return the decoded code."""
    assert basic.serial is not None
    basic.read_for(0.05)
    program = b'DO:k$=INKEY$:LOOP UNTIL k$<>"":?"K=";ASC(k$)\r'
    basic.serial.write(program)
    basic.serial.flush()
    # ESP32 USB Serial/JTAG is noticeably slower at line parsing than the
    # Pico CDC; the 150 ms gap was sometimes too tight, causing the
    # escape sequence to arrive while the editor was still parsing the
    # command line. 300 ms is comfortable across all ports.
    time.sleep(0.3)
    basic.serial.write(send_bytes)
    basic.serial.flush()
    end = time.monotonic() + 3.0
    buf = bytearray()
    code: int | None = None
    while time.monotonic() < end:
        chunk = basic.serial.read(4096)
        if chunk:
            buf.extend(chunk)
            text = strip_ansi(bytes(buf)).decode("latin1", "replace")
            m = re.search(r"K=\s*(\d+)", text)
            if m:
                code = int(m.group(1))
                break
        else:
            time.sleep(0.01)
    if code is None:
        visible = strip_ansi(bytes(buf)).decode("latin1", "replace")
        return False, f"no K= match in transcript={visible[-200:]!r}", None
    return True, f"code={code}", code


def run_cases(basic: BasicSerial, cases: list[Case], expected_features: set[str]) -> dict[str, int]:
    counts = {"pass": 0, "fail": 0, "expected_fail": 0}
    for c in cases:
        try:
            ok_capture, detail, code = drive_inkey_capture(basic, c.bytes_to_send)
        except Exception as exc:
            counts["fail"] += 1
            print(f"  EXC   {c.name:18s} ({c.feature_set:<6s})  exception={exc!r}")
            continue

        expected = c.feature_set in expected_features
        passed = ok_capture and (code == c.expected_code)
        if passed:
            counts["pass"] += 1
            mark = "OK   "
        elif not expected:
            counts["expected_fail"] += 1
            mark = "XFAIL"
        else:
            counts["fail"] += 1
            mark = "FAIL "
        suffix = ""
        if c.notes and not passed:
            suffix = f"  -- {c.notes}"
        actual_code = code if code is not None else "no-response"
        print(f"  {mark} {c.name:18s} ({c.feature_set:<6s})  got={actual_code!r:<14s} want={hex(c.expected_code)}{suffix}")
        # Re-sync between cases so a stale buffer doesn't poison the next.
        try:
            basic.sync(timeout=3.0)
        except Exception:
            pass
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--features", choices=list(PORT_FEATURE_SETS.keys()), default="any",
                        help="which feature sets this port is expected to support "
                             "(default: any — flags every shortfall as a real FAIL)")
    parser.add_argument("--filter", help="only run cases whose name matches this regex")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    cases = CASES
    if args.filter:
        rx = re.compile(args.filter)
        cases = [c for c in cases if rx.search(c.name)]
    if args.list:
        for c in cases:
            print(f"{c.name:24s} {c.feature_set:8s} -> {hex(c.expected_code)}  {c.notes}")
        return 0

    expected = PORT_FEATURE_SETS[args.features]
    print(f"Running {len(cases)} keymap cases, expected feature sets: {sorted(expected)}")
    with BasicSerial(args.port) as basic:
        basic.sync(timeout=8.0, boot_wait=args.boot_wait)
        counts = run_cases(basic, cases, expected)

    total = counts["pass"] + counts["fail"] + counts["expected_fail"]
    print(f"\n{counts['pass']}/{total} pass  "
          f"({counts['expected_fail']} expected-fail, {counts['fail']} unexpected-fail)")
    return 0 if counts["fail"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
