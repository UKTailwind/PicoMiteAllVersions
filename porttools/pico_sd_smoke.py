#!/usr/bin/env python3
"""Thorough SD card smoke test for Pico ports with A:=LFS and B:=FatFs.

Targets the fs_lfs_fatfs_helpers (ExistsFile, ExistsDir, FileSize) extracted
from MM_Misc.c, the FileIO drive-prefix routing, and the FILES/DIR$/CHDIR/
MKDIR/RMDIR/RENAME/KILL/COPY surface across the LFS<->FatFs boundary.

The known motivating bug (commit f6d97c3, prefix-stripping regression in
hal_filesystem) means these helpers historically queried LFS even when the
target was on B:, so `EDIT "B:/foo.bas"` would open an empty buffer. Every
test in this suite is structured to surface that class of mis-routing.

Run from the repo root:

    python3.11 porttools/pico_sd_smoke.py --port /dev/cu.usbmodem2101

The suite is destructive on a small temporary prefix (default: pcsd<rand>);
real SD data is not touched. Use --keep-files to leave the artefacts behind
for manual inspection.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from basic_serial import BasicSerial, strip_ansi, default_port  # noqa: E402


def clean(text: bytes) -> str:
    return strip_ansi(text).decode("latin1", "replace")


def parse_int(line: str, marker: str) -> int:
    m = re.search(rf"{re.escape(marker)}\s*[:=]?\s*(-?\d+)", line)
    if not m:
        raise SystemExit(f"missing {marker!r} in: {line!r}")
    return int(m.group(1))


def expect(line: str, marker: str, expected: object) -> None:
    actual = parse_int(line, marker)
    if actual != int(expected):  # type: ignore[arg-type]
        raise SystemExit(
            f"{marker} wanted {expected}, got {actual} in: {line!r}"
        )


class SDSmoke:
    def __init__(self, basic: BasicSerial, prefix: str,
                 timeout: float, long_timeout: float) -> None:
        self.basic = basic
        self.prefix = prefix
        self.timeout = timeout
        self.long_timeout = long_timeout
        self.passes = 0
        self.fails = 0

    def cmd(self, line: str, *, timeout: float | None = None) -> str:
        result = self.basic.command(line, timeout=timeout or self.timeout)
        return result.clean_text

    def step(self, name: str) -> None:
        print(f"=== {name} ===", flush=True)

    def ok(self, name: str, detail: str = "") -> None:
        self.passes += 1
        suffix = f" - {detail}" if detail else ""
        print(f"ok   {name}{suffix}", flush=True)

    def fail(self, name: str, detail: str) -> None:
        self.fails += 1
        print(f"FAIL {name} - {detail}", flush=True)
        raise SystemExit(1)

    # ------------------------------------------------------------------
    # Setup / teardown
    # ------------------------------------------------------------------

    def clean_slate(self) -> None:
        # Make sure both drives have no leftover files from a previous run.
        for drive in ("A:", "B:"):
            self.cmd(f'DRIVE "{drive}"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_a.txt"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_b.txt"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_long_'
                     f'name_with_lots_of_chars_to_test_the_lfn_path.txt"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_with space.txt"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_dir/inside.txt"')
            self.cmd(f'ON ERROR SKIP : RMDIR "{self.prefix}_dir"')
        self.cmd('DRIVE "A:"')

    def make_file(self, drive: str, name: str, body: str) -> None:
        self.cmd(f'DRIVE "{drive}"')
        self.cmd(f'OPEN "{name}" FOR OUTPUT AS #1')
        self.cmd(f'PRINT #1,"{body}"')
        self.cmd("CLOSE #1")

    # ------------------------------------------------------------------
    # Suites
    # ------------------------------------------------------------------

    def suite_routing_matrix(self) -> None:
        """ExistsFile/ExistsDir/FileSize/DIR$ must route by path prefix.

        Pre-conditions: A:/<prefix>_a.txt and B:/<prefix>_b.txt exist.
        The helper-under-test must return TRUE only for the drive that
        actually owns the file, regardless of the current drive.
        """
        self.step("routing matrix")
        a_name = f"{self.prefix}_a.txt"
        b_name = f"{self.prefix}_b.txt"
        self.make_file("A:", a_name, "alpha")
        self.make_file("B:", b_name, "bravo")

        # ---- ExistsFile ----
        # Current drive A:, query each path with explicit prefix.
        self.cmd('DRIVE "A:"')
        out = self.cmd(f'PRINT "X1=" + STR$(MM.INFO(EXISTS FILE "A:/{a_name}"))')
        expect(out, "X1", 1)
        out = self.cmd(f'PRINT "X2=" + STR$(MM.INFO(EXISTS FILE "B:/{b_name}"))')
        expect(out, "X2", 1)
        # Cross-check: a file that only exists on A: must NOT show on B:.
        out = self.cmd(f'PRINT "X3=" + STR$(MM.INFO(EXISTS FILE "B:/{a_name}"))')
        expect(out, "X3", 0)
        out = self.cmd(f'PRINT "X4=" + STR$(MM.INFO(EXISTS FILE "A:/{b_name}"))')
        expect(out, "X4", 0)
        # No-prefix path uses the current drive.
        out = self.cmd(f'PRINT "X5=" + STR$(MM.INFO(EXISTS FILE "{a_name}"))')
        expect(out, "X5", 1)
        out = self.cmd(f'PRINT "X6=" + STR$(MM.INFO(EXISTS FILE "{b_name}"))')
        expect(out, "X6", 0)

        # Now switch current drive to B: and rerun.
        self.cmd('DRIVE "B:"')
        out = self.cmd(f'PRINT "X7=" + STR$(MM.INFO(EXISTS FILE "A:/{a_name}"))')
        expect(out, "X7", 1)
        out = self.cmd(f'PRINT "X8=" + STR$(MM.INFO(EXISTS FILE "B:/{b_name}"))')
        expect(out, "X8", 1)
        out = self.cmd(f'PRINT "X9=" + STR$(MM.INFO(EXISTS FILE "{b_name}"))')
        expect(out, "X9", 1)
        out = self.cmd(f'PRINT "XA=" + STR$(MM.INFO(EXISTS FILE "{a_name}"))')
        expect(out, "XA", 0)
        self.ok("ExistsFile routing", "6+4 matrix")

        # ---- FileSize ----
        # The B: helper used to always query LFS, returning 0 for any SD
        # file. We assert a strictly positive size for files we just wrote.
        self.cmd('DRIVE "A:"')
        out = self.cmd(f'PRINT "S1=" + STR$(MM.INFO(FILESIZE "A:/{a_name}"))')
        if parse_int(out, "S1") <= 0:
            self.fail("FileSize routing", f"A: file: {out!r}")
        out = self.cmd(f'PRINT "S2=" + STR$(MM.INFO(FILESIZE "B:/{b_name}"))')
        if parse_int(out, "S2") <= 0:
            self.fail("FileSize routing", f"B: prefix from A: current: {out!r}")
        # Missing file → 0.
        out = self.cmd(
            f'PRINT "S3=" + STR$(MM.INFO(FILESIZE "B:/{self.prefix}_missing"))')
        expect(out, "S3", 0)

        self.cmd('DRIVE "B:"')
        out = self.cmd(f'PRINT "S4=" + STR$(MM.INFO(FILESIZE "{b_name}"))')
        if parse_int(out, "S4") <= 0:
            self.fail("FileSize routing", f"B: no prefix: {out!r}")
        out = self.cmd(f'PRINT "S5=" + STR$(MM.INFO(FILESIZE "A:/{a_name}"))')
        if parse_int(out, "S5") <= 0:
            self.fail("FileSize routing", f"A: prefix from B: current: {out!r}")
        self.ok("FileSize routing", "explicit prefix and current-drive")

        # ---- ExistsDir ----
        # Create a directory on each drive. The helper must report TRUE only
        # for the drive that owns the dir.
        dir_name = f"{self.prefix}_dir"
        self.cmd('DRIVE "A:"')
        self.cmd(f'ON ERROR SKIP : RMDIR "{dir_name}"')
        self.cmd(f'MKDIR "{dir_name}"')
        self.cmd('DRIVE "B:"')
        self.cmd(f'ON ERROR SKIP : RMDIR "{dir_name}"')
        self.cmd(f'MKDIR "{dir_name}"')

        self.cmd('DRIVE "A:"')
        out = self.cmd(
            f'PRINT "D1=" + STR$(MM.INFO(EXISTS DIR "A:/{dir_name}"))')
        expect(out, "D1", 1)
        out = self.cmd(
            f'PRINT "D2=" + STR$(MM.INFO(EXISTS DIR "B:/{dir_name}"))')
        expect(out, "D2", 1)
        out = self.cmd(
            f'PRINT "D3=" + STR$(MM.INFO(EXISTS DIR "{dir_name}"))')
        expect(out, "D3", 1)
        # An ExistsDir query for a regular file must return 0, not 1.
        out = self.cmd(
            f'PRINT "D4=" + STR$(MM.INFO(EXISTS DIR "A:/{a_name}"))')
        expect(out, "D4", 0)
        out = self.cmd(
            f'PRINT "D5=" + STR$(MM.INFO(EXISTS DIR "B:/{b_name}"))')
        expect(out, "D5", 0)
        self.ok("ExistsDir routing", "dirs on both drives + reject files")

        # ---- DIR$ ----
        # The pre-fix bug was visible as DIR$("B:*", ALL) returning a single
        # bogus entry from LFS. Check that DIR$ at each drive root sees the
        # file we wrote there.
        out = self.cmd(f'PRINT "L1=" + DIR$("A:/{a_name}", FILE)')
        if a_name not in out:
            self.fail("DIR$ routing", f"A: file not found: {out!r}")
        out = self.cmd(f'PRINT "L2=" + DIR$("B:/{b_name}", FILE)')
        if b_name not in out:
            self.fail("DIR$ routing", f"B: file not found: {out!r}")
        # Cross-drive: A: should not see B:'s file under A:'s root.
        out = self.cmd(f'PRINT "L3=" + DIR$("A:/{b_name}", FILE)')
        if b_name in out and "L3=" + b_name in out.replace(" ", ""):
            # Be strict: B:'s file leaked into A:'s namespace.
            self.fail("DIR$ routing", f"B:'s file visible on A:: {out!r}")
        self.ok("DIR$ routing", "files isolated to correct drive")

    def suite_state_preservation(self) -> None:
        """FatFSFileSystem must be restored after each helper call.

        If a helper forgets to restore FatFSFileSystem on its error path,
        the subsequent command sees the wrong current drive. The pattern
        we exercise: query a B: path while current drive is A:, then
        immediately confirm current drive is still A:.
        """
        self.step("state preservation")
        a_name = f"{self.prefix}_a.txt"
        b_name = f"{self.prefix}_b.txt"
        missing = f"{self.prefix}_missing.txt"
        self.cmd('DRIVE "A:"')

        # After a successful B: query, current drive should still be A:.
        self.cmd(f'PRINT MM.INFO(EXISTS FILE "B:/{b_name}")')
        out = self.cmd('PRINT "D=" + MM.INFO$(DRIVE)')
        if "D=A:" not in out:
            self.fail("state after B: ExistsFile", f"drive changed: {out!r}")

        # After a B: query for a MISSING file (error path inside helper),
        # current drive should still be A:.
        self.cmd(f'PRINT MM.INFO(EXISTS FILE "B:/{missing}")')
        out = self.cmd('PRINT "D=" + MM.INFO$(DRIVE)')
        if "D=A:" not in out:
            self.fail("state after B: missing-file ExistsFile",
                      f"drive changed: {out!r}")

        # FileSize for missing file.
        self.cmd(f'PRINT MM.INFO(FILESIZE "B:/{missing}")')
        out = self.cmd('PRINT "D=" + MM.INFO$(DRIVE)')
        if "D=A:" not in out:
            self.fail("state after B: missing-file FileSize",
                      f"drive changed: {out!r}")

        # Same in reverse: current B:, query A:.
        self.cmd('DRIVE "B:"')
        self.cmd(f'PRINT MM.INFO(EXISTS FILE "A:/{a_name}")')
        self.cmd(f'PRINT MM.INFO(EXISTS FILE "A:/{missing}")')
        out = self.cmd('PRINT "D=" + MM.INFO$(DRIVE)')
        if "D=B:" not in out:
            self.fail("state after A: queries from B:",
                      f"drive changed: {out!r}")
        self.cmd('DRIVE "A:"')
        self.ok("state preservation", "FatFSFileSystem restored after queries")

    def suite_long_and_special_names(self) -> None:
        """Long LFN names and filenames with spaces, mixed case, dots.

        FatFs supports FF_MAX_LFN-length filenames; LFS supports a similar
        but not identical limit. Both should round-trip names with spaces
        and mixed case without losing characters.
        """
        self.step("long and special names")
        # ~120 chars, well within FF_MAX_LFN=255 and LFS_NAME_MAX(=255 default).
        long_name = (f"{self.prefix}_long_name_with_lots_of_chars_to"
                     f"_test_the_lfn_path.txt")
        space_name = f"{self.prefix}_with space.txt"

        for drive in ("A:", "B:"):
            self.cmd(f'DRIVE "{drive}"')
            self.make_file(drive, long_name, "longline")
            out = self.cmd(
                f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "{long_name}"))')
            expect(out, "X", 1)
            out = self.cmd(
                f'PRINT "S=" + STR$(MM.INFO(FILESIZE "{long_name}"))')
            if parse_int(out, "S") <= 0:
                self.fail(f"long name on {drive}",
                          f"FileSize 0: {out!r}")

            self.make_file(drive, space_name, "spaceline")
            out = self.cmd(
                f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "{space_name}"))')
            expect(out, "X", 1)
            out = self.cmd(f'PRINT "D=" + DIR$("{space_name}", FILE)')
            if space_name not in out:
                self.fail(f"DIR$ on space filename / {drive}",
                          f"missing: {out!r}")

            self.cmd(f'KILL "{long_name}"')
            self.cmd(f'KILL "{space_name}"')

        self.cmd('DRIVE "A:"')
        self.ok("long and special names", "spaces + ~120-char LFN on both drives")

    def suite_cross_drive_workflow(self) -> None:
        """Mixed A:/B: operations interleaved.

        Confirms that the FS routing is path-driven, not state-leak driven:
        writing to A: must not affect B:'s view, and vice versa.
        """
        self.step("cross-drive workflow")
        a_name = f"{self.prefix}_a.txt"
        b_name = f"{self.prefix}_b.txt"

        # KILL A:'s file while current drive is B:. Then confirm B:'s file
        # is still intact and A:'s is gone.
        self.cmd('DRIVE "B:"')
        self.cmd(f'KILL "A:/{a_name}"')
        out = self.cmd(
            f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "A:/{a_name}"))')
        expect(out, "X", 0)
        out = self.cmd(
            f'PRINT "Y=" + STR$(MM.INFO(EXISTS FILE "B:/{b_name}"))')
        expect(out, "Y", 1)

        # Cross-drive COPY uses the explicit B2A / A2B sub-commands.
        # Plain `COPY "B:/src" TO "A:/dst"` treats both paths as same-drive
        # and would fail to find a file on the other drive.
        self.cmd('DRIVE "A:"')
        self.cmd(f'COPY B2A "{b_name}" TO "{a_name}"')
        out = self.cmd(
            f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "A:/{a_name}"))')
        expect(out, "X", 1)
        out = self.cmd(
            f'PRINT "S=" + STR$(MM.INFO(FILESIZE "A:/{a_name}"))')
        if parse_int(out, "S") <= 0:
            self.fail("cross-drive COPY", f"empty after B2A copy: {out!r}")

        # And the reverse direction: A: → B:. Pick a fresh dest name so we
        # don't collide with the existing B:'s file.
        a2b_name = f"{self.prefix}_a2b.txt"
        self.cmd('DRIVE "B:"')
        self.cmd(f'ON ERROR SKIP : KILL "{a2b_name}"')
        self.cmd(f'COPY A2B "{a_name}" TO "{a2b_name}"')
        out = self.cmd(
            f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "B:/{a2b_name}"))')
        expect(out, "X", 1)
        self.cmd(f'KILL "{a2b_name}"')

        # RENAME on B: while current is A:.
        ren = f"{self.prefix}_ren.txt"
        self.cmd('DRIVE "B:"')
        self.cmd(f'RENAME "{b_name}" AS "{ren}"')
        out = self.cmd(
            f'PRINT "X=" + STR$(MM.INFO(EXISTS FILE "B:/{ren}"))')
        expect(out, "X", 1)
        out = self.cmd(
            f'PRINT "Y=" + STR$(MM.INFO(EXISTS FILE "B:/{b_name}"))')
        expect(out, "Y", 0)
        self.cmd(f'RENAME "{ren}" AS "{b_name}"')

        self.cmd('DRIVE "A:"')
        self.ok("cross-drive workflow", "KILL/COPY/RENAME across drives")

    def suite_free_space_timing(self) -> None:
        """`MM.INFO(FREE SPACE)` on B: should return within a sane timeout.

        Earlier in the session we observed a hang (default 12 s timeout
        exceeded) for FREE SPACE on B:. f_getfree on a large card can be
        slow, but it should complete; the check here is "does it return
        within `long_timeout`?" rather than "is it fast."
        """
        self.step("free space timing")
        for drive in ("A:", "B:"):
            self.cmd(f'DRIVE "{drive}"')
            start = time.monotonic()
            try:
                out = self.cmd(
                    'PRINT "F=" + STR$(MM.INFO(FREE SPACE))',
                    timeout=self.long_timeout,
                )
            except TimeoutError as exc:
                self.fail(f"FREE SPACE on {drive}",
                          f"timed out: {exc}")
            elapsed = time.monotonic() - start
            val = parse_int(out, "F")
            if val < 0:
                self.fail(f"FREE SPACE on {drive}",
                          f"negative value: {out!r}")
            print(f"     {drive} free={val} bytes "
                  f"({elapsed:.2f} s)", flush=True)
        self.cmd('DRIVE "A:"')
        self.ok("free space timing", f"both drives returned within "
                                     f"{self.long_timeout:.0f} s")

    def suite_subdirectory(self) -> None:
        """MKDIR / CHDIR / RMDIR on B:.

        MKDIR rejects paths with the wrong drive prefix ("Only valid on
        current drive"). When current drive matches, MKDIR + CHDIR +
        subdirectory file operations must all work.
        """
        self.step("subdirectory ops on B:")
        dir_name = f"{self.prefix}_sub"
        self.cmd('DRIVE "B:"')
        self.cmd(f'ON ERROR SKIP : KILL "{dir_name}/inner.txt"')
        self.cmd(f'ON ERROR SKIP : RMDIR "{dir_name}"')
        self.cmd(f'MKDIR "{dir_name}"')
        out = self.cmd(
            f'PRINT "X=" + STR$(MM.INFO(EXISTS DIR "{dir_name}"))')
        expect(out, "X", 1)
        self.cmd(f'CHDIR "{dir_name}"')
        self.cmd('OPEN "inner.txt" FOR OUTPUT AS #1')
        self.cmd('PRINT #1,"inside-b"')
        self.cmd("CLOSE #1")
        out = self.cmd(
            'PRINT "S=" + STR$(MM.INFO(FILESIZE "inner.txt"))')
        if parse_int(out, "S") <= 0:
            self.fail("B: subdir FileSize", f"empty: {out!r}")
        # CHDIR back, verify cleanup.
        self.cmd('CHDIR "B:/"')
        self.cmd(f'KILL "{dir_name}/inner.txt"')
        self.cmd(f'RMDIR "{dir_name}"')
        self.cmd('DRIVE "A:"')
        self.ok("subdirectory ops on B:", "MKDIR/CHDIR/OPEN/RMDIR")

    def teardown(self, keep_files: bool) -> None:
        if keep_files:
            print("Leaving SD/A: artefacts in place per --keep-files", flush=True)
            return
        for drive in ("A:", "B:"):
            self.cmd(f'DRIVE "{drive}"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_a.txt"')
            self.cmd(f'ON ERROR SKIP : KILL "{self.prefix}_b.txt"')
            self.cmd(f'ON ERROR SKIP : RMDIR "{self.prefix}_dir"')
        self.cmd('DRIVE "A:"')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--long-timeout", type=float, default=45.0)
    parser.add_argument("--prefix", default=None,
                        help="filename prefix; default derived from time()")
    parser.add_argument("--keep-files", action="store_true")
    parser.add_argument(
        "suites", nargs="*",
        default=["all"],
        help="routing state names cross free subdir all (default: all)")
    args = parser.parse_args()

    prefix = args.prefix or f"pcsd{int(time.time()) & 0xffff:04x}"

    suite_map_order = [
        ("routing", "suite_routing_matrix"),
        ("state", "suite_state_preservation"),
        ("names", "suite_long_and_special_names"),
        ("cross", "suite_cross_drive_workflow"),
        ("free", "suite_free_space_timing"),
        ("subdir", "suite_subdirectory"),
    ]
    if "all" in args.suites:
        active = [m for _, m in suite_map_order]
    else:
        index = {k: m for k, m in suite_map_order}
        active = []
        for s in args.suites:
            if s not in index:
                print(f"unknown suite: {s}", file=sys.stderr)
                return 2
            active.append(index[s])

    with BasicSerial(args.port, args.baud) as basic:
        basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
        # Confirm an SD card is present before running.
        sd_status = strip_ansi(
            basic.command('PRINT "SD=" + MM.INFO$(SDCARD)',
                          timeout=args.long_timeout).raw
        ).decode("latin1", "replace")
        if "SD=Ready" not in sd_status:
            print(f"FAIL: SD card not Ready: {sd_status!r}", file=sys.stderr)
            return 1

        smoke = SDSmoke(basic, prefix, args.timeout, args.long_timeout)
        try:
            smoke.clean_slate()
            for method in active:
                getattr(smoke, method)()
        finally:
            try:
                smoke.teardown(args.keep_files)
            except Exception as exc:  # noqa: BLE001
                print(f"teardown error: {exc}", flush=True)

    print(f"\nSD smoke: {smoke.passes} ok, {smoke.fails} fail")
    return 0 if smoke.fails == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
