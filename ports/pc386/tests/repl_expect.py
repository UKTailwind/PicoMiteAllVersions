#!/usr/bin/env python3
"""
ports/pc386/tests/repl_expect.py — interactive REPL test harness.

Boots mmbasic.elf under QEMU with COM1 piped to a pty, waits for the
prompt, sends a line, captures output until the next prompt, compares
to expected, repeats.

Each test is a list of (input_line, expected_output_substr) pairs.
Empty `expected_output_substr` means "any output is fine, just expect
the prompt back."

Run:
  python3 ports/pc386/tests/repl_expect.py            # all tests
  python3 ports/pc386/tests/repl_expect.py basic      # just "basic"
"""

import os
import re
import select
import subprocess
import sys
import time

PORT_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL    = os.path.join(PORT_DIR, "build", "mmbasic.elf")
A_IMG     = os.path.join(PORT_DIR, "test_disks", "a.img")
C_IMG     = os.path.join(PORT_DIR, "test_disks", "c.img")

QEMU = [
    "qemu-system-i386",
    "-kernel", KERNEL,
    "-m", "16M",
    "-display", "none",
    "-serial", "stdio",
    "-no-reboot", "-no-shutdown",
    "-d", "guest_errors",
    "-drive", f"file={A_IMG},format=raw,if=ide,index=0",
    "-drive", f"file={C_IMG},format=raw,if=ide,index=1",
]

ANSI_RE   = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")
# The MMBasic prompt is "> " followed (in interactive REPL mode) by the
# show-cursor escape \x1b[?25h. We anchor on the END of the buffer so we
# don't match stale prompt bytes left over from earlier commands.
PROMPT_TAIL_RE = re.compile(rb"(?:\r?\n)?> (?:\x1b\[\?25h)?\Z")


def strip_ansi(b: bytes) -> bytes:
    return ANSI_RE.sub(b"", b)


def read_until_prompt(proc, timeout: float, label: str) -> bytes:
    """Read from proc.stdout until the buffer ENDS with a prompt sequence."""
    buf = b""
    deadline = time.monotonic() + timeout
    last_match_at = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        rfds, _, _ = select.select([proc.stdout], [], [], min(remaining, 0.05))
        if proc.stdout in rfds:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
            last_match_at = None  # new bytes — keep reading
        else:
            # No new bytes for 50ms. If buffer ends with a prompt, accept.
            if PROMPT_TAIL_RE.search(buf):
                return buf
        if proc.poll() is not None:
            break
    raise TimeoutError(
        f"{label}: timeout after {timeout}s waiting for prompt.\n"
        f"--- captured {len(buf)} bytes ---\n"
        f"{strip_ansi(buf)[-2000:].decode('utf-8', errors='replace')}\n"
        "--------------------------------"
    )


def run_test(name: str, steps: list[tuple[str, str]]) -> bool:
    """Boot the kernel, run each (input, expected_substr) step, report."""
    print(f"\n=== {name} ===")
    proc = subprocess.Popen(
        QEMU,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )
    # non-blocking stdout
    os.set_blocking(proc.stdout.fileno(), False)

    try:
        # Wait for the first prompt (after banner).
        boot_out = read_until_prompt(proc, timeout=10, label="boot")

        for idx, (cmd, expected) in enumerate(steps, start=1):
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
            try:
                got = read_until_prompt(proc, timeout=15, label=f"step {idx} `{cmd}`")
            except TimeoutError as e:
                print(f"  STEP {idx} `{cmd}`: TIMEOUT")
                print(f"  {e}")
                return False
            stripped = strip_ansi(got).decode("utf-8", errors="replace")
            ok = (expected == "") or (expected in stripped)
            mark = "OK  " if ok else "FAIL"
            preview = stripped.replace("\r\n", " | ").strip()[:200]
            print(f"  [{mark}] `{cmd}` → {preview}")
            if not ok:
                print(f"          expected substring: {expected!r}")
                print(f"          --- full output (no ANSI) ---")
                for line in stripped.splitlines():
                    print(f"          {line}")
                print(f"          ------------------------------")
                return False
        return True
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        # QEMU's disk-image file lock takes a moment to release after the
        # process is gone — without this short settle, the next test's
        # qemu-system-i386 trips "Failed to get write lock" mid-boot.
        time.sleep(0.5)


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

TESTS = {
    "arith":  [
        ("PRINT 1+1",          " 2"),
        ("PRINT 3*4+5",        " 17"),
        ("PRINT 2^10",         " 1024"),
    ],

    "math":   [
        ("PRINT SIN(0)",       " 0"),
        ("PRINT COS(0)",       " 1"),
        ("PRINT SQR(2)",       "1.414"),
        ("PRINT INT(3.7)",     " 3"),
    ],

    "vars":   [
        ("LET X = 42",         ""),
        ("PRINT X",            " 42"),
        ("LET Y = X * 2",      ""),
        ("PRINT Y",            " 84"),
        ("LET S$ = \"hello\"", ""),
        ("PRINT S$",           "hello"),
    ],

    "loops":  [
        ("FOR I=1 TO 3 : PRINT I, I*I : NEXT I", "3"),
    ],

    "strings": [
        ("PRINT LEN(\"hello\")",                 " 5"),
        ("PRINT UCASE$(\"abc\")",                "ABC"),
        ("PRINT LEFT$(\"PicoMite\", 4)",         "Pico"),
    ],

    "files":  [
        # The drive switch + FILES on B: (the FatFs first volume) should list
        # at least HELLO.BAS.
        ("B:",                  ""),
        ("FILES",               "HELLO.BAS"),
    ],

    "files_a_alias": [
        # A: must work too — port_drivecheck_remap routes A: → FATFSFILE so
        # FILES sees the same volume.
        ("A:",                  ""),
        ("FILES",               "HELLO.BAS"),
    ],

"load_run": [
        ("LOAD \"B:\\HELLO.BAS\"", ""),
        ("LIST",                   "Print"),
        ("RUN",                    "Hello"),
    ],

    "fizzbuzz_run": [
        # Multi-command session: switch drive, list, load, run a more
        # interesting program, then verify FizzBuzz output.
        ("B:",                       ""),
        ("LOAD \"FIZZBUZZ.BAS\"",    ""),
        ("RUN",                      "FizzBuzz"),
    ],

    "session_short": [
        ("PRINT 6*7",                " 42"),
        ("B:",                       ""),
        ("FILES",                    "HELLO.BAS"),
    ],

    "errors_unsupported": [
        # Stage 3 doesn't have audio, graphics, or GPIO. Each entry
        # should produce a clear error and bounce back to the prompt
        # — NOT halt the kernel.
        ("PLAY TONE 440, 440",       "PLAY TONE not available until stage 6"),
        ("FRAMEBUFFER CREATE",       "FRAMEBUFFER not available until stage 5"),
        ("SETPIN 1, DOUT",           "SETPIN not available until stage 6.5"),
        # Verify the prompt comes back and arithmetic still works after
        # any of those errors.
        ("PRINT 99 + 1",             " 100"),
    ],

    "session": [
        # Simulate a real interactive session: do some math, set a var,
        # switch drives, list, load, run, define an inline program.
        ("PRINT 6*7",                " 42"),
        ("LET N = 10",               ""),
        ("PRINT N * N",              " 100"),
        ("B:",                       ""),
        ("FILES",                    "HELLO.BAS"),
        ("LOAD \"HELLO.BAS\"",       ""),
        ("RUN",                      "Hello"),
        # After RUN, the program is still in ProgMemory; LIST should still work.
        ("LIST",                     "Print"),
        ("PRINT \"after run\"",      "after run"),
    ],
}


def main() -> int:
    if not os.path.exists(KERNEL):
        print(f"error: {KERNEL} not found. Run `make -C ports/pc386` first.",
              file=sys.stderr)
        return 1

    selected = sys.argv[1:] or list(TESTS.keys())
    unknown = [s for s in selected if s not in TESTS]
    if unknown:
        print(f"error: unknown tests: {unknown}", file=sys.stderr)
        print(f"available: {list(TESTS.keys())}", file=sys.stderr)
        return 1

    failed = []
    for name in selected:
        ok = run_test(name, TESTS[name])
        if not ok:
            failed.append(name)

    print()
    if failed:
        print(f"FAILED: {failed}")
        return 1
    print(f"PASSED ({len(selected)}/{len(selected)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
