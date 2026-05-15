#!/usr/bin/env python3
"""Pico hardware smoke for the FILES command (cmd_files in FileIO.c).

Locks down current behaviour as a regression baseline before any
cmd_files refactor lands (e.g. the EEVBlog multi-pass selection-sort
prototype). Three concerns:

  1. correctness of the file set (every fixture entry appears exactly
     once)
  2. sort order across all four sort modes (NAME, SIZE, TYPE, TIME)
     plus the default
  3. speed on a 1000-file directory (regression budget for a future
     refactor)

Fixture filenames are kept short enough that the PicoCalc 40-column
LCD doesn't wrap the FILES output mid-name. The collision pair uses
two names that share their first 8 bytes verbatim — a future prototype
that sorts on an 8-byte prefix should be tolerant of either order for
this pair only; the smoke flags those positions as a known accept-
either swap.

Run from the repo root:
    python3.11 porttools/pico_files_smoke.py --port /dev/cu.usbmodem101
    python3.11 porttools/pico_files_smoke.py --port /dev/cu.usbmodem101 --bulk-count 0   # skip bulk
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, CommandResult, default_port, strip_ansi  # noqa: E402


FIXTURE_DIR = "/files_smoke_tmp"
BULK_DIR    = "/bulk_smoke_tmp"
DEFAULT_DRIVE = "A:"
# 128 matches the legacy HAL_PORT_FILES_MAX cap on the host build. The
# EEVBlog multi-pass selection sort is O(n²) on readdir, so 128 entries
# is a realistic "many files" stress without crossing into the multi-
# minute regime that 1000 entries hits. Override via --bulk-count for
# bigger stress runs (1000 still works, just slow).
BULK_DEFAULT_COUNT = 128

# Fixture chosen so the LCD doesn't wrap any output line at 40 cols,
# and so each sort mode produces a deterministic expected order on
# the *current* implementation. Sizes chosen to be all-distinct
# so SIZE sort has no ties to break. Extensions chosen so TYPE
# sort has a stable order with ties only within the .bas group.
#
# Collision pair: same1.bas / same2.bas share their first 8 bytes
# ("same1.ba" vs "same2.ba" — different at byte 4 actually, hmm).
# Need true 8-byte collision. Use "samename" prefix: "samename.A"
# and "samename.B" — both 10 chars, first 8 bytes "samename" identical.
COLLISION_A = "smname.AA"   # 9 chars; first 8 = "smname.A"
COLLISION_B = "smname.AB"   # 9 chars; first 8 = "smname.A" -- COLLIDES
FIXTURE_FILES = [
    # (filename, content). PRINT #1,"x"; → file size == len(content).
    # Sizes kept short to dodge MMBasic command-line truncation (~256
    # chars per immediate-mode statement).
    ("aaa.bas",   "AA"),                    #  2 bytes
    ("bbb.bas",   "BBBB"),                  #  4
    ("ccc.bas",   "CCCCCC"),                #  6
    ("zzz.bas",   "ZZZZZZZZ"),              #  8
    ("eee.txt",   "abcdefghij"),            # 10
    ("fff.dat",   "0123456789" * 5),        # 50
    (COLLISION_A, "alpha"),                 #  5  — collides at first 8 bytes with B
    (COLLISION_B, "betas"),                 #  5
    ("big.bas",   "x" * 100),               # 100  — largest
]
FIXTURE_DIRS = ["sub1", "sub2"]

# LFS auto-lists "." and ".." in every directory; FILES treats them
# as <DIR> entries, so they show up in the entries list.
LFS_AUTO_DIRS = {".", ".."}


@dataclass
class FilesLine:
    is_dir: bool
    size: int                 # 0 for directories
    name: str
    # File-only timestamp components (packed for monotonic compare).
    # 0 for directories (whose line has no timestamp).
    time_packed: int = 0      # (yyyy<<26)|(mm<<22)|(dd<<17)|(hh<<11)|(mn<<5)


def _unwrap_lines(text: str, width: int = 40) -> list[str]:
    """Coalesce serial output that wrapped at column `width`.

    The cmd_files printer fires its own \r\n after every entry, but
    inside a single entry it wraps via ListNewLine when MMCharPos >=
    Option.Width. We rejoin physical lines that look like they wrapped:
    if a line is exactly `width` chars wide and is followed by a
    continuation that starts at column 1 with a non-prefix character,
    treat the next line as a continuation.

    Heuristic: if a line doesn't start with one of the recognised
    line shapes (HH:MM... or whitespace+<DIR>+space+name or summary),
    glue it to the previous physical line.
    """
    raw_lines = text.replace("\r", "\n").split("\n")
    # Compile recognisers
    dir_re  = re.compile(r"^\s*<DIR>\s+\S")
    file_re = re.compile(r"^\s*\d{2}:\d{2}\s+\d{2}-\d{2}-\d{4}\s+\d+\s+\S")
    other_starts = ("PRESS ANY KEY", ">", "")
    out: list[str] = []
    for line in raw_lines:
        if not out:
            out.append(line)
            continue
        # If this line clearly starts a new entry / summary / prompt,
        # keep it standalone. Otherwise it's a wrap continuation.
        if (dir_re.match(line) or file_re.match(line) or
                "PRESS ANY KEY" in line or
                line.strip().startswith(">") or
                not line.strip()):
            out.append(line)
        else:
            # Continuation: append to previous
            out[-1] = out[-1] + line
    return out


def parse_files(text: str) -> tuple[list[FilesLine], str]:
    """Extract entries + summary line from a FILES transcript."""
    entries: list[FilesLine] = []
    summary = ""
    summary_re = re.compile(r"\b(\d+)\s+director(?:y|ies),\s+(\d+)\s+file")
    dir_re  = re.compile(r"^\s*<DIR>\s+(\S.*)$")
    file_re = re.compile(r"^\s*(\d{2}):(\d{2})\s+(\d{2})-(\d{2})-(\d{4})\s+(\d+)\s+(\S.*)$")
    for line in _unwrap_lines(text):
        s = line.rstrip()
        if not s:
            continue
        if summary_re.search(s):
            summary = s.strip()
            continue
        m = dir_re.match(s)
        if m:
            entries.append(FilesLine(is_dir=True, size=0, name=m.group(1).strip()))
            continue
        m = file_re.match(s)
        if m:
            hh, mn, dd, mm, yyyy = (int(m.group(i)) for i in (1, 2, 3, 4, 5))
            packed = (yyyy << 26) | (mm << 22) | (dd << 17) | (hh << 11) | (mn << 5)
            entries.append(FilesLine(is_dir=False, size=int(m.group(6)),
                                     name=m.group(7).strip(),
                                     time_packed=packed))
    return entries, summary


def run_command_with_pagination(basic: BasicSerial, command: str,
                                timeout: float = 600.0) -> CommandResult:
    """Run a BASIC command and auto-respond to 'PRESS ANY KEY' prompts."""
    assert basic.serial is not None
    basic.serial.write((command + "\r").encode("latin1"))
    basic.serial.flush()
    end = time.monotonic() + timeout
    out = bytearray()
    marker = b"PRESS ANY KEY"
    last_pos = -1
    while time.monotonic() < end:
        chunk = basic.serial.read(4096)
        if chunk:
            out.extend(chunk)
            tail_start = max(0, len(out) - 8192)
            tail_clean = strip_ansi(bytes(out[tail_start:]))
            idx = tail_clean.rfind(marker)
            absolute_idx = tail_start + idx if idx >= 0 else -1
            if absolute_idx > last_pos:
                basic.serial.write(b" ")
                basic.serial.flush()
                last_pos = absolute_idx
            if BasicSerial._has_prompt(bytes(out)):
                return CommandResult(command, bytes(out))
        else:
            time.sleep(0.005)
    raise TimeoutError(f"timeout running {command!r}")


def make_small_setup(drive: str) -> list[str]:
    # MMBasic requires the bare drive-letter command ("A:" / "B:") to
    # switch drives. CHDIR/MKDIR with an explicit drive prefix that
    # differs from the current drive errors with "Only valid on current
    # drive" (FileIO.c:1310). Switch drive first, then use bare paths.
    lines = [drive, 'CHDIR "/"']
    # best-effort cleanup of leftovers
    lines.append(f'ON ERROR SKIP : CHDIR "{FIXTURE_DIR}"')
    for d in FIXTURE_DIRS:
        lines.append(f'ON ERROR SKIP : RMDIR "{d}"')
    for name, _ in FIXTURE_FILES:
        lines.append(f'ON ERROR SKIP : KILL "{name}"')
    lines.append('CHDIR "/"')
    lines.append(f'ON ERROR SKIP : RMDIR "{FIXTURE_DIR[1:]}"')
    lines.append(f'MKDIR "{FIXTURE_DIR}"')
    lines.append(f'CHDIR "{FIXTURE_DIR}"')
    for d in FIXTURE_DIRS:
        lines.append(f'MKDIR "{d}"')
    # Two-stage creation, with the RTC jumped forward between halves
    # so the on-disk file timestamps differ across the boundary. FAT
    # date/time stores minutes (and 2-sec increments on seconds), so
    # a 5-minute jump is plenty.
    mid = len(FIXTURE_FILES) // 2
    lines.append('DATE$ = "01-01-2024"')
    lines.append('TIME$ = "10:00:00"')
    for name, content in FIXTURE_FILES[:mid]:
        lines.append(f'OPEN "{name}" FOR OUTPUT AS #1 : PRINT #1,"{content}"; : CLOSE #1')
    lines.append('TIME$ = "10:05:00"')
    for name, content in FIXTURE_FILES[mid:]:
        lines.append(f'OPEN "{name}" FOR OUTPUT AS #1 : PRINT #1,"{content}"; : CLOSE #1')
    return lines


def make_small_teardown(drive: str) -> list[str]:
    lines = [drive, f'ON ERROR SKIP : CHDIR "{FIXTURE_DIR}"']
    for name, _ in FIXTURE_FILES:
        lines.append(f'ON ERROR SKIP : KILL "{name}"')
    for d in FIXTURE_DIRS:
        lines.append(f'ON ERROR SKIP : RMDIR "{d}"')
    lines.append('CHDIR "/"')
    lines.append(f'ON ERROR SKIP : RMDIR "{FIXTURE_DIR[1:]}"')
    return lines


# ---------------------------------------------------------------------------
# Expected-order tables for the small fixture, current implementation
# ---------------------------------------------------------------------------

def expected_files_set() -> set[str]:
    return {name for name, _ in FIXTURE_FILES}


def expected_dirs_set(drive: str) -> set[str]:
    # Only the fixture dirs are required. "." and ".." may or may not
    # appear depending on the filesystem: LFS always lists them in
    # subdir scans; FatFS (B:) doesn't on this port. check_set() filters
    # them out before comparing so either behaviour is accepted.
    return set(FIXTURE_DIRS)


def expected_sizes() -> dict[str, int]:
    return {name: len(content) for name, content in FIXTURE_FILES}


def expected_name_order() -> list[str]:
    """Filenames in case-insensitive alphabetical order."""
    return sorted(expected_files_set(), key=str.lower)


def expected_size_order() -> list[str]:
    """Filenames in size-descending order. Ties broken by current
    implementation's behaviour: insertion-sort with > comparison
    leaves equal-size entries in directory order, which on LFS is
    roughly creation order. We just verify sizes are non-increasing
    and the file SET matches — the relative position of equal-size
    pairs is left as 'any order'."""
    return sorted(expected_files_set(),
                  key=lambda n: -expected_sizes()[n])


def expected_type_order() -> list[str]:
    """Filenames in ascending extension order, ties broken by name."""
    def ext(n: str) -> str:
        idx = n.rfind(".")
        return n[idx:].lower() if idx >= 0 else ""
    return sorted(expected_files_set(), key=lambda n: (ext(n), n.lower()))


# ---------------------------------------------------------------------------
# Order validators
# ---------------------------------------------------------------------------

def check_set(entries: list[FilesLine], drive: str = DEFAULT_DRIVE) -> tuple[bool, str]:
    files = {e.name for e in entries if not e.is_dir}
    dirs  = {e.name for e in entries if e.is_dir} - LFS_AUTO_DIRS
    ef = expected_files_set()
    ed = expected_dirs_set(drive)
    if files != ef:
        return (False, f"file set mismatch: missing={ef - files} extra={files - ef}")
    if dirs != ed:
        return (False, f"dir set mismatch: missing={ed - dirs} extra={dirs - ed}")
    return (True, f"{len(files)} files + {len(dirs)} dirs")


def check_sizes(entries: list[FilesLine]) -> tuple[bool, str]:
    want = expected_sizes()
    for e in entries:
        if e.is_dir:
            continue
        if e.size != want.get(e.name):
            return (False, f"size mismatch for {e.name}: got {e.size}, "
                           f"expected {want.get(e.name)}")
    return (True, "all sizes correct")


def check_dirs_first(entries: list[FilesLine]) -> tuple[bool, str]:
    """For NAME and TYPE sorts the current code prefixes the sort key
    with 'D' for dirs and 'F' for files, so dirs come first."""
    saw_file = False
    for e in entries:
        if not e.is_dir:
            saw_file = True
        elif saw_file:
            return (False, f"directory {e.name!r} appeared after a file")
    return (True, "directories before files")


def check_time_order_desc(entries: list[FilesLine]) -> tuple[bool, str]:
    """TIME sort: newest first. Verify non-increasing packed
    timestamp. With the two-stage fixture setup (PAUSE 70s between
    halves) we should see at least two distinct timestamp groups,
    so this catches an out-of-order swap."""
    files = [e for e in entries if not e.is_dir]
    distinct_times = {e.time_packed for e in files}
    for i in range(1, len(files)):
        if files[i - 1].time_packed < files[i - 1].time_packed:  # never true
            pass
        if files[i - 1].time_packed < files[i].time_packed:
            return (False, f"time sort not descending at index {i}: "
                           f"{files[i-1].name} ({files[i-1].time_packed:x}) older than "
                           f"{files[i].name} ({files[i].time_packed:x})")
    return (True, f"timestamps non-increasing across {len(distinct_times)} "
                  f"distinct value(s)")


def _name_order_with_swap_tolerance(actual: list[str], expected: list[str],
                                    swap_pair: tuple[str, str]) -> tuple[bool, str]:
    """Verify actual matches expected, accepting either order for the
    given swap_pair (used to tolerate 8-byte sort-key prefix collisions
    in a future prototype). For the *current* implementation (which
    uses the full filename) the pair will always come out in canonical
    sorted order, so this tolerance is a no-op here."""
    if len(actual) != len(expected):
        return (False, f"length mismatch: got {len(actual)}, expected {len(expected)}")
    pair = set(swap_pair)
    i = 0
    while i < len(actual):
        if actual[i] == expected[i]:
            i += 1
            continue
        if (i + 1 < len(actual)
                and actual[i] in pair and actual[i + 1] in pair
                and expected[i] in pair and expected[i + 1] in pair):
            i += 2
            continue
        return (False, f"order diverges at index {i}: got {actual[i]!r}, "
                       f"expected {expected[i]!r}\nfull got={actual}\nfull expected={expected}")
    return (True, "order matches (within swap tolerance)")


def check_name_order(entries: list[FilesLine]) -> tuple[bool, str]:
    names = [e.name for e in entries if not e.is_dir]
    return _name_order_with_swap_tolerance(
        names, expected_name_order(), (COLLISION_A, COLLISION_B))


def check_size_order(entries: list[FilesLine]) -> tuple[bool, str]:
    """Size sort: current impl is ASCENDING (smallest first). Ties:
    any order (current impl preserves encounter order). Verify
    monotonic non-decreasing."""
    sizes = [e.size for e in entries if not e.is_dir]
    for i in range(1, len(sizes)):
        if sizes[i - 1] > sizes[i]:
            return (False, f"non-ascending at index {i}: {sizes[i-1]} > {sizes[i]}\n"
                           f"full={sizes}")
    return (True, f"sizes ascending: {sizes}")


def check_type_order(entries: list[FilesLine]) -> tuple[bool, str]:
    """Type sort: extensions ascending. Current impl does NOT tie-break
    on name within an extension group — within-group order is encounter
    order (whatever the FS readdir returns). Verify extensions only."""
    def ext(n: str) -> str:
        idx = n.rfind(".")
        return n[idx:].lower() if idx >= 0 else ""
    names = [e.name for e in entries if not e.is_dir]
    exts = [ext(n) for n in names]
    for i in range(1, len(exts)):
        if exts[i - 1] > exts[i]:
            return (False, f"extensions not ascending at index {i}: "
                           f"{exts[i-1]!r} > {exts[i]!r} (full={list(zip(names, exts))})")
    return (True, f"extensions ascending: {exts}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run_files_test(basic: BasicSerial, label: str, command: str,
                   *validators) -> tuple[bool, str, int]:
    """Run a FILES command, parse, run validators. Returns (ok, detail, ms)."""
    cmd = f'TIMER=0 : {command} : ? "ELAPSED_MS=";TIMER'
    try:
        result = run_command_with_pagination(basic, cmd, timeout=120.0)
    except Exception as exc:
        return (False, f"exception={exc!r}", -1)
    clean = strip_ansi(result.raw).decode("latin1", "replace")
    m = re.search(r"ELAPSED_MS=\s*(\d+)", clean)
    elapsed = int(m.group(1)) if m else -1
    entries, summary = parse_files(clean)
    details = []
    for v in validators:
        ok, detail = v(entries)
        details.append(detail)
        if not ok:
            return (False, " | ".join(details) + f"\nraw={clean[-1000:]!r}", elapsed)
    return (True, " | ".join(details), elapsed)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--drive", default=DEFAULT_DRIVE,
                        help='target drive ("A:" or "B:"; default A:)')
    parser.add_argument("--bulk-count", type=int, default=BULK_DEFAULT_COUNT,
                        help=f"number of files for the speed test "
                             f"(default {BULK_DEFAULT_COUNT}; 0 to skip)")
    parser.add_argument("--keep-fixture", action="store_true",
                        help="don't remove fixtures at the end")
    args = parser.parse_args(argv)
    drive = args.drive.rstrip("/")
    if not drive.endswith(":"):
        drive += ":"

    passed = 0
    failures: list[tuple[str, str]] = []
    with BasicSerial(args.port) as basic:
        basic.sync(timeout=8.0, boot_wait=args.boot_wait)

        # ----------------- small fixture setup -----------------------
        print(f"=== small fixture setup on {drive} ===", flush=True)
        try:
            for line in make_small_setup(drive):
                basic.command(line, check_error=False)
        except Exception as exc:
            print(f"setup FAILED: {exc!r}")
            return 2

        # Bind drive into check_set
        def check_set_bound(entries):
            return check_set(entries, drive)

        print(f"=== small fixture tests on {drive} ===", flush=True)
        small_tests = [
            ("files_default_name_sort", "FILES",            [check_set_bound, check_sizes, check_dirs_first, check_name_order]),
            ("files_explicit_name",     'FILES "*", NAME',  [check_set_bound, check_sizes, check_dirs_first, check_name_order]),
            ("files_size_sort",         'FILES "*", SIZE',  [check_set_bound, check_sizes, check_size_order]),
            ("files_type_sort",         'FILES "*", TYPE',  [check_set_bound, check_sizes, check_dirs_first, check_type_order]),
            ("files_time_sort",         'FILES "*", TIME',  [check_set_bound, check_sizes, check_time_order_desc]),
        ]
        for label, cmd, vs in small_tests:
            ok, detail, ms = run_files_test(basic, label, cmd, *vs)
            status = "OK  " if ok else "FAIL"
            print(f"  {status}  {label:<28}  {ms} ms  |  {detail}")
            if ok:
                passed += 1
            else:
                failures.append((label, detail))

        if not args.keep_fixture:
            try:
                for line in make_small_teardown(drive):
                    basic.command(line, check_error=False)
            except Exception as exc:
                print(f"small cleanup warning: {exc!r}")

        # ----------------- bulk fixture ------------------------------
        if args.bulk_count > 0:
            print(f"\n=== bulk fixture setup ({args.bulk_count} files on {drive}) ===", flush=True)
            # Drive-select must be its own statement (the bare "B:" form
            # is a syntactic shortcut, not a function call); embed it
            # before the MKDIR/CHDIR sequence.
            try:
                basic.command(drive, timeout=5, check_error=False)
                basic.command('CHDIR "/"', timeout=5, check_error=False)
            except Exception:
                pass
            bulk_setup = (
                f'ON ERROR SKIP : RMDIR "{BULK_DIR[1:]}" : '
                f'MKDIR "{BULK_DIR}" : '
                f'CHDIR "{BULK_DIR}" : '
                f'FOR i%=1 TO {args.bulk_count} : '
                f'OPEN "b"+FORMAT$(i%,"%04g")+".bas" FOR OUTPUT AS #1 : '
                f'? #1,i%; : CLOSE #1 : NEXT : ? "BULK_SETUP_DONE"'
            )
            try:
                basic.command(bulk_setup, timeout=300.0)
            except Exception as exc:
                print(f"bulk setup FAILED: {exc!r}")
                failures.append(("bulk_setup", f"exception={exc!r}"))
            else:
                print(f"=== bulk fixture tests on {drive} ===", flush=True)
                # Just verify completion + summary count; sort order
                # validation is moot for 128/1000 mechanically-named files.
                def check_bulk_count(entries):
                    files = {e.name for e in entries if not e.is_dir}
                    if len(files) != args.bulk_count:
                        return (False, f"got {len(files)} files in output, expected {args.bulk_count}")
                    return (True, f"{len(files)} files listed")

                # Switch to drive + chdir into bulk dir, then time FILES.
                basic.command(drive, timeout=5, check_error=False)
                ok, detail, ms = run_files_test(
                    basic, "bulk_files_speed",
                    f'CHDIR "{BULK_DIR}" : FILES',
                    check_bulk_count)
                status = "OK  " if ok else "FAIL"
                rate = f"{ms / max(args.bulk_count,1):.2f} ms/entry" if ms > 0 else "?"
                print(f"  {status}  bulk_files_speed               {ms} ms  ({rate})  |  {detail}")
                if ok:
                    passed += 1
                else:
                    failures.append(("bulk_files_speed", detail))

                if not args.keep_fixture:
                    print("\n=== bulk cleanup ===", flush=True)
                    basic.command(drive, timeout=5, check_error=False)
                    teardown = (
                        f'ON ERROR SKIP : CHDIR "{BULK_DIR}" : '
                        f'FOR i%=1 TO {args.bulk_count} : '
                        f'ON ERROR SKIP : KILL "b"+FORMAT$(i%,"%04g")+".bas" : '
                        f'NEXT : CHDIR "/" : '
                        f'ON ERROR SKIP : RMDIR "{BULK_DIR[1:]}" : '
                        f'? "BULK_TEARDOWN_DONE"'
                    )
                    try:
                        basic.command(teardown, timeout=600.0, check_error=False)
                    except Exception as exc:
                        print(f"bulk cleanup warning: {exc!r}")

    total = passed + len(failures)
    print(f"\n{passed}/{total} passed")
    if failures:
        print("\nFailures:")
        for name, detail in failures:
            print(f"  - {name}: {detail[:400]}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
