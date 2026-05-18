#!/usr/bin/env python3
"""Drive mmbasic_test --repl through LOAD/NEW sequences.

Regression gate for the cmd_new program-erase path on the host_native
build (the same code path the host_wasm browser build inherits).

Background: cmd_new calls hal_flash_erase(realflashpointer, MAX_PROG_SIZE)
after FlashWriteInit(PROGRAM_FLASH) sets realflashpointer = PROGSTART.
On host the legacy flash_range_erase / _program shims silently drop
any offset >= 2 * MAX_PROG_SIZE, so the program region (which the
runtime_program.c LOAD path writes to at offset 0 in flash_prog_buf)
is never cleared by NEW. Symptom: LOAD x.bas ; NEW ; LIST still shows
x.bas. This script reproduces that and a few neighbouring cases.

Run:
  ports/host_native already built ?
  python3 porttools/host_new_smoke.py
  (add --no-build to skip the `make` step)
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import textwrap
from dataclasses import dataclass
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


def drive_repl(binary: Path, sd_root: Path, script: str, timeout: float) -> str:
    """Pipe `script` into mmbasic_test --repl and return stdout text.

    Piped (non-TTY) stdin keeps the REPL in cooked line-buffered mode,
    so we can simply send LF-terminated commands. EOF on stdin makes
    the REPL exit cleanly.
    """
    proc = subprocess.run(
        [str(binary), "--repl", "--sd-root", str(sd_root)],
        input=script,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return proc.stdout


def list_block_after(out: str, after_line: str) -> str:
    """Slice out the REPL response between the prompt that issued
    `after_line` and the next prompt."""
    needle = f"> {after_line}"
    i = out.find(needle)
    if i < 0:
        return ""
    tail = out[i + len(needle):]
    j = tail.find("\n>")
    return (tail if j < 0 else tail[:j]).strip()


def check_load_new_clears(binary: Path, sd_root: Path) -> list[Check]:
    """LOAD x.bas → LIST shows it → NEW → LIST is empty."""
    (sd_root / "marker.bas").write_text(textwrap.dedent("""\
        10 PRINT "STILL-HERE"
        20 PRINT "AND-HERE-TOO"
    """))
    script = textwrap.dedent("""\
        LOAD "B:/marker.bas"
        LIST
        NEW
        LIST
    """)
    out = drive_repl(binary, sd_root, script, timeout=10.0)
    before = list_block_after(out, 'LOAD "B:/marker.bas"\n> LIST')
    # The block above stretches across LOAD+LIST since LOAD prints nothing;
    # easier to just locate LIST regions independently.
    lists = []
    idx = 0
    while True:
        k = out.find("> LIST", idx)
        if k < 0:
            break
        tail = out[k + len("> LIST"):]
        j = tail.find("\n>")
        lists.append((tail if j < 0 else tail[:j]).strip())
        idx = k + 1
    if len(lists) < 2:
        return [Check("load_new_two_list_blocks", False, repr(out[-400:]))]
    pre_list, post_list = lists[0], lists[1]
    return [
        Check("load_then_list_shows_program",
              'STILL-HERE' in pre_list, repr(pre_list)),
        Check("new_clears_program",
              'STILL-HERE' not in post_list and 'AND-HERE-TOO' not in post_list,
              repr(post_list)),
    ]


def check_load_new_load_swap(binary: Path, sd_root: Path) -> list[Check]:
    """LOAD a → NEW → LOAD b → LIST should show only b."""
    (sd_root / "a.bas").write_text('10 PRINT "FROM-A"\n')
    (sd_root / "b.bas").write_text('10 PRINT "FROM-B"\n')
    script = textwrap.dedent("""\
        LOAD "B:/a.bas"
        NEW
        LOAD "B:/b.bas"
        LIST
    """)
    out = drive_repl(binary, sd_root, script, timeout=10.0)
    k = out.find("> LIST")
    if k < 0:
        return [Check("swap_list_present", False, repr(out[-400:]))]
    tail = out[k + len("> LIST"):]
    j = tail.find("\n>")
    listed = (tail if j < 0 else tail[:j]).strip()
    return [
        Check("swap_shows_b", 'FROM-B' in listed, repr(listed)),
        Check("swap_doesnt_show_a", 'FROM-A' not in listed, repr(listed)),
    ]


def check_new_idempotent(binary: Path, sd_root: Path) -> list[Check]:
    """NEW; NEW twice shouldn't error."""
    (sd_root / "any.bas").write_text('10 PRINT "X"\n')
    script = textwrap.dedent("""\
        LOAD "B:/any.bas"
        NEW
        NEW
        LIST
    """)
    out = drive_repl(binary, sd_root, script, timeout=10.0)
    return [
        Check("double_new_no_error", "Error" not in out, repr(out[-300:])),
    ]


def check_new_then_run_empty(binary: Path, sd_root: Path) -> list[Check]:
    """LOAD a polluting program, NEW, then RUN — the loaded program
    must not execute. Probe is a file side-effect (POLLUTED line
    appended to log.txt) rather than stdout, so the gate doesn't
    depend on RUN's empty-program behaviour (which may either error
    or silently return depending on the variant)."""
    polluter = sd_root / "polluter.bas"
    polluter.write_text(textwrap.dedent("""\
        10 OPEN "B:/log.txt" FOR APPEND AS #1
        20 PRINT #1, "POLLUTED"
        30 CLOSE #1
    """))
    log = sd_root / "log.txt"
    if log.exists():
        log.unlink()
    script = textwrap.dedent("""\
        LOAD "B:/polluter.bas"
        NEW
        RUN
    """)
    drive_repl(binary, sd_root, script, timeout=10.0)
    log_text = log.read_text() if log.exists() else ""
    return [
        Check("polluter_did_not_run_after_new",
              "POLLUTED" not in log_text, repr(log_text)),
    ]


def print_checks(checks: list[Check]) -> int:
    failed = 0
    print("--- checks ---")
    for check in checks:
        suffix = f" ({check.detail})" if check.detail else ""
        print(f"{check.name}: {'ok' if check.ok else 'FAIL'}{suffix}")
        if not check.ok:
            failed += 1
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_root())
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--make", default="make")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args(argv)

    root = args.repo.resolve()
    binary = (args.binary or (root / "ports" / "host_native" / "build" / "mmbasic_test")).resolve()
    if not args.no_build:
        rc = subprocess.call(
            [args.make, "-C", str(root / "ports" / "host_native")], cwd=root)
        if rc != 0:
            return rc
    if not binary.exists():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    checks: list[Check] = []
    with tempfile.TemporaryDirectory(prefix="mmbasic-new-smoke-") as td:
        sd_root = Path(td)
        checks += check_load_new_clears(binary, sd_root)
        checks += check_load_new_load_swap(binary, sd_root)
        checks += check_new_idempotent(binary, sd_root)
        checks += check_new_then_run_empty(binary, sd_root)

    return print_checks(checks)


if __name__ == "__main__":
    raise SystemExit(main())
