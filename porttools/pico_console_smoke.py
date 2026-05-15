#!/usr/bin/env python3
"""Pico hardware smoke for the console output path.

Locks down PRINT/CLS/COLOUR/TAB/CHR$ behaviour over USB-serial before the
audit's Finding 1 + 2 consolidations touch the four duplicate MMputchar /
MMPrintString / SSPrintString / MMInkey implementations. The function
bodies in each port currently differ in subtle ways (flush ordering,
trailing-flush presence). This smoke is the gate: if it stays green after
the consolidation, the per-port differences either didn't matter or the
shared version replicated them correctly.

Runs as immediate-mode BASIC over the serial console. Each test issues a
command, waits for the prompt to return, and asserts that a sentinel
substring appears in the response transcript.

Run from the repo root:
    python3.11 porttools/pico_console_smoke.py --port /dev/cu.usbmodem2101
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Callable

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, default_port, strip_ansi  # noqa: E402


@dataclass
class Case:
    name: str
    commands: list[str]
    # Each predicate gets the ANSI-stripped command transcript (excluding
    # the leading command echo). Predicates can be either:
    #   - a regex string (must match)
    #   - a callable (transcript) -> (bool, detail)
    predicates: list[object] = field(default_factory=list)


def strip_echo(transcript: str, command: str) -> str:
    """The Pico echoes the typed command before running it. Drop the first
    line if it matches the command, so assertions look at output only."""
    lines = transcript.splitlines()
    if lines and lines[0].strip() == command.strip():
        return "\n".join(lines[1:])
    # The prompt-tail '\r\n> ' sometimes leaves a blank line before output;
    # the echo may also be preceded by an ANSI cursor-show. Handle both.
    out = []
    seen_echo = False
    for line in lines:
        if not seen_echo and command.strip() in line:
            seen_echo = True
            continue
        out.append(line)
    return "\n".join(out)


def regex_check(pattern: str) -> Callable[[str], tuple[bool, str]]:
    rx = re.compile(pattern, re.DOTALL)
    def check(text: str) -> tuple[bool, str]:
        m = rx.search(text)
        return (bool(m), f"pattern={pattern!r} text={text[:200]!r}")
    return check


def no_error_check(text: str) -> tuple[bool, str]:
    bad = "Error :" in text or "Error:" in text
    return (not bad, f"saw_error={bad} text={text[:200]!r}")


def run_case(basic: BasicSerial, case: Case) -> tuple[bool, str]:
    full_transcript = ""
    for cmd in case.commands:
        result = basic.command(cmd, timeout=8.0, check_error=False)
        full_transcript += result.clean_text
    clean = strip_ansi(full_transcript.encode("latin1", "replace")).decode("latin1", "replace")
    # Drop the leading "command echo" of the LAST command so predicates
    # match the actual output, not the echoed source.
    if case.commands:
        clean = strip_echo(clean, case.commands[-1])
    details = []
    ok = True
    for pred in case.predicates:
        if isinstance(pred, str):
            check = regex_check(pred)
        elif callable(pred):
            check = pred
        else:
            raise TypeError(f"bad predicate {pred!r}")
        passed, detail = check(clean)
        if not passed:
            ok = False
        details.append(("OK" if passed else "FAIL") + ":" + detail)
    return ok, " | ".join(details) if details else f"text={clean[:160]!r}"


def build_cases() -> list[Case]:
    cases: list[Case] = []

    def add(name, commands, *predicates):
        if isinstance(commands, str):
            commands = [commands]
        cases.append(Case(name=name, commands=commands, predicates=list(predicates)))

    # --- PRINT basics ------------------------------------------------------
    add("print_string", 'PRINT "BASIC_PRINT_OK"',
        r"BASIC_PRINT_OK", no_error_check)
    add("print_number", 'PRINT 42',
        r"\b42\b", no_error_check)
    add("print_negative", 'PRINT -17',
        r"-17", no_error_check)
    add("print_float", 'PRINT 1.5',
        r"1\.5", no_error_check)
    add("print_concat_semicolon", 'PRINT "a";"b";"c"',
        r"abc", no_error_check)
    add("print_concat_plus", 'PRINT "x" + "y" + "z"',
        r"xyz", no_error_check)
    # Comma separator emits a literal TAB (CHR$(9)) between fields, not
    # padding spaces — the receiving terminal does the column alignment.
    add("print_comma_separator", 'PRINT "a","b"',
        r"a[\t ]+b", no_error_check)
    # Numbers prefixed with a space when positive.
    add("print_number_list", 'PRINT 1;2;3',
        r" 1 2 3", no_error_check)
    # Trailing semicolon suppresses newline within a single statement —
    # use a multi-statement line so the prompt doesn't intervene.
    add("print_no_newline", 'PRINT "FOO";:PRINT "BAR"',
        r"FOOBAR", no_error_check)

    # --- CHR$ embeddings ---------------------------------------------------
    add("chr_ascii_letters", 'PRINT CHR$(65)+CHR$(66)+CHR$(67)',
        r"ABC", no_error_check)
    add("chr_tab_in_middle", 'PRINT "X" + CHR$(9) + "Y"',
        r"X\tY|X +Y", no_error_check)
    # String() function — repeated character.
    add("string_repeat_short", 'PRINT STRING$(8,"=")',
        r"={8}", no_error_check)

    # --- Long output -------------------------------------------------------
    # The flush-on-trailing-byte case: 200 chars in one PRINT. If any port's
    # MMPrintString fails to flush at the end the tail can be lost on
    # buffered transports. The sentinel "TAIL" must appear at the end.
    add("print_long_200", 'PRINT STRING$(196,"x") + "TAIL"',
        r"x{50,}TAIL", no_error_check)
    # STRING$ is capped at MAXSTRLEN (255). Build a longer payload via
    # a FOR loop with `;` suppression so it lands as one continuous PRINT
    # stream — that exercises MMputchar's flush more aggressively than
    # one 200-char STRING$ ever can.
    add("print_long_loop_400",
        'FOR i=1 TO 400:PRINT "y";:NEXT:PRINT "DONE"',
        r"y{50,}DONE", no_error_check)
    # Long string with embedded newlines via CHR$(13) — exercises
    # MMputchar's MMCharPos reset path.
    add("print_with_cr", 'PRINT "L1" + CHR$(13) + "L2"',
        r"L1\s*L2", no_error_check)

    # --- TAB() column tracking --------------------------------------------
    # TAB(20) → 19 leading spaces, then COL20. Width: depends on
    # Option.Tab/columns. We check the marker appears.
    add("tab_function", 'PRINT TAB(10);"AT10"',
        r"AT10", no_error_check)

    # --- CLS roundtrip ----------------------------------------------------
    # CLS clears, then a subsequent PRINT must still work.
    add("cls_then_print",
        ['CLS', 'PRINT "AFTER_CLS"'],
        r"AFTER_CLS", no_error_check)

    # --- COLOUR roundtrip --------------------------------------------------
    # COLOUR sets foreground/background; verify a subsequent PRINT still
    # shows the string. On host/Pico framebuffer ports the colour change is
    # invisible over serial, but the command must not error.
    add("colour_set_and_print",
        ['COLOUR RGB(BLUE), RGB(BLACK)',
         'PRINT "COLOURED"',
         'COLOUR RGB(WHITE), RGB(BLACK)'],
        r"COLOURED", no_error_check)

    # --- Multi-statement lines ---------------------------------------------
    # Use fresh names so we don't collide with anything left over from
    # earlier interactive sessions on the same REPL.
    add("multi_statement", 'ALPHA=7:BETA=11:PRINT ALPHA+BETA',
        r"\b18\b", no_error_check)

    # --- FOR/NEXT counting (drives many PRINTs in one execution) -----------
    add("for_print_loop",
        'FOR i=1 TO 10:PRINT i;:NEXT:PRINT " end"',
        r" 1 2 3 4 5 6 7 8 9 10 end", no_error_check)

    # --- Stress: 26 lines of PRINT in one program -------------------------
    # If MMPrintString drops the trailing flush, the last few lines may not
    # appear. We send "alphabet" as 26 lines and check the last is present.
    add("alphabet_lines",
        'FOR i=65 TO 90:PRINT CHR$(i);:NEXT:PRINT " Z_TAIL"',
        r"ABCDEFGHIJKLMNOPQRSTUVWXYZ Z_TAIL", no_error_check)

    # --- OPTION HEIGHT/WIDTH should not affect single-line outputs --------
    add("info_id",
        'PRINT MM.INFO$(DEVICE)',
        r"\S+", no_error_check)

    # --- INPUT regression (sanity that we didn't break MMgetline) ---------
    # Drive INPUT a$ via the same trick the input-smoke uses: push the
    # value through immediate-mode by setting it directly. The MMgetline
    # path is exhaustively exercised by pico_input_smoke.py; here we just
    # confirm the variable round-trip works.
    add("input_var_roundtrip",
        ['a$ = "ROUNDTRIP_OK"', 'PRINT a$'],
        r"ROUNDTRIP_OK", no_error_check)

    return cases


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--filter", help="only run cases whose name matches this regex")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--verbose", action="store_true")
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
    failures: list[tuple[str, str]] = []
    with BasicSerial(args.port) as basic:
        basic.sync(timeout=8.0, boot_wait=args.boot_wait)
        for c in cases:
            try:
                ok, detail = run_case(basic, c)
            except Exception as exc:
                ok, detail = False, f"exception={exc!r}"
            status = "OK  " if ok else "FAIL"
            print(f"  {status}  {c.name:24s}  {detail if (not ok or args.verbose) else ''}")
            if ok:
                passed += 1
            else:
                failures.append((c.name, detail))

    total = passed + len(failures)
    print(f"\n{passed}/{total} passed")
    if failures:
        print("\nFailures:")
        for name, detail in failures:
            print(f"  - {name}: {detail}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
