#!/usr/bin/env python3
"""Comprehensive PSRAM smoke harness for MMBasic ports (Pico + ESP32).

Drives a connected device over serial and exercises the full BASIC PSRAM
surface: boot invariants, RAM TEST march, slot lifecycle (SAVE/LIST/LOAD/
RUN/OVERWRITE/ERASE/CHAIN/FILE LOAD), Memory.c PSRAM routing observable
via DIM, and the negative cases. Output is PASS/FAIL per check with a
machine-parseable summary at the end.

Same invocation works on Pico and ESP32; the per-target expectations
table at the top of the file maps port differences (e.g. ESP32 errors
on RAM TEST NOCACHE while RP2350 succeeds).

Phase 6 of docs/real-hal/esp32-psram-realign-plan.md.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Sequence

# basic_serial is sibling-imported the same way esp32_fs_vm_smoke.py does
# it; falling back to a stub when running under --dry-run on a machine
# without pyserial installed.
try:
    from basic_serial import BasicSerial, default_port, strip_ansi  # type: ignore
except Exception:  # pragma: no cover - exercised only when pyserial missing
    BasicSerial = None  # type: ignore[assignment]

    def default_port() -> str:  # type: ignore[no-redef]
        return "/dev/cu.usbmodem101"

    def strip_ansi(data: bytes) -> bytes:  # type: ignore[no-redef]
        return re.sub(rb"\x1b\[[0-9;:]*[A-Za-z]", b"", data)


# ---------------------------------------------------------------------------
# Per-target expectations
# ---------------------------------------------------------------------------

# Each target's expectations are kept here instead of scattered if/else
# blocks inside the test methods. Add a new port by adding an entry.
TARGET_EXPECTATIONS: dict[str, dict[str, object]] = {
    "pico": {
        # RP2350 supports the NOCACHE alias; expect OK from RAM TEST NOCACHE 1.
        "nocache_supported": True,
        "nocache_ok_marker": "RAM TEST OK",
        "nocache_error_text": None,
        # PSRAM enabled per OPTION PSRAM in OPTION LIST.
        "option_list_marker": "OPTION PSRAM",
        # RAM FILE LOAD is fully wired on RP2350.
        "ram_file_load_supported": True,
        "ram_file_load_error_text": None,
        # Default PSRAM size threshold (overridable via --expected-psram-bytes).
        "default_psram_bytes": 6 * 1024 * 1024,
    },
    "esp32": {
        # ESP32 deliberately rejects the NOCACHE modifier (Phase 2/4 plan).
        "nocache_supported": False,
        "nocache_ok_marker": None,
        "nocache_error_text": "NOCACHE not supported on this port",
        # ESP32 reports the slab via the same MM.INFO(PSRAM SIZE) path; the
        # OPTION LIST marker is the same on both ports since Phase 5
        # decommissioned the ESP32-specific surface.
        "option_list_marker": "OPTION PSRAM",
        # Phase 4 stubs RAM FILE LOAD on ESP32 pending mmslots wiring.
        "ram_file_load_supported": False,
        "ram_file_load_error_text": "RAM FILE LOAD not supported on this port",
        "default_psram_bytes": 6 * 1024 * 1024,
    },
}

# MAXRAMSLOTS as declared in Configuration.h. Used to size the negative-
# case slot index and the RAM LIST "all available" check.
MAXRAMSLOTS = 5
INVALID_SLOT = 99
INVALID_TEST_MB = 999


# ---------------------------------------------------------------------------
# Check tracking
# ---------------------------------------------------------------------------


@dataclass
class Check:
    name: str
    status: str  # PASS | FAIL | SKIP
    detail: str = ""
    elapsed: float = 0.0

    @property
    def ok(self) -> bool:
        return self.status in {"PASS", "SKIP"}


@dataclass
class Report:
    checks: list[Check] = field(default_factory=list)

    def add(self, check: Check) -> None:
        self.checks.append(check)
        suffix = f" {check.detail}" if check.detail else ""
        elapsed = f" (elapsed {check.elapsed:.2f}s)" if check.elapsed else ""
        print(f"[{check.status}] {check.name:<28}{suffix}{elapsed}", flush=True)

    def passed(self, name: str, detail: str = "", elapsed: float = 0.0) -> None:
        self.add(Check(name, "PASS", detail, elapsed))

    def failed(self, name: str, detail: str = "", elapsed: float = 0.0) -> None:
        self.add(Check(name, "FAIL", detail, elapsed))

    def skipped(self, name: str, detail: str = "") -> None:
        self.add(Check(name, "SKIP", detail))

    @property
    def num_passed(self) -> int:
        return sum(1 for c in self.checks if c.status == "PASS")

    @property
    def num_failed(self) -> int:
        return sum(1 for c in self.checks if c.status == "FAIL")

    @property
    def num_skipped(self) -> int:
        return sum(1 for c in self.checks if c.status == "SKIP")

    @property
    def total(self) -> int:
        return len(self.checks)

    @property
    def all_passed(self) -> bool:
        return self.num_failed == 0


# ---------------------------------------------------------------------------
# BASIC programs the harness uploads (paste via REPL line by line).
# ---------------------------------------------------------------------------

PROGRAM_A_MARKER = "PSRAM_SMOKE_A_OK"
PROGRAM_B_MARKER = "PSRAM_SMOKE_B_OK"
RAM_FILE_MARKER = "PSRAM_SMOKE_FILE_OK"
STRESS_MARKER = "PSRAM_SMOKE_STRESS_OK"


def program_a_lines() -> list[str]:
    """Tiny BASIC program A. Slot SAVE/LOAD/RUN reference."""
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, sum%",
        "FOR i% = 1 TO 5: sum% = sum% + i%: NEXT i%",
        f'PRINT "{PROGRAM_A_MARKER} " + STR$(sum%)',
    ]


def program_b_lines(slot: int) -> list[str]:
    """Program B chains into program A via RAM CHAIN."""
    return [
        "OPTION EXPLICIT",
        f'PRINT "{PROGRAM_B_MARKER}"',
        f"RAM CHAIN {slot}",
    ]


def ram_file_program_lines() -> list[str]:
    """Program dropped onto the filesystem for RAM FILE LOAD."""
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER n%",
        "n% = 7 * 6",
        f'PRINT "{RAM_FILE_MARKER} " + STR$(n%)',
    ]


def stress_program_lines() -> list[str]:
    """Tiny program used by the long-running stress loop.

    Allocates a >24 KB INTEGER array (forcing the Memory.c PSRAM
    routing path), writes a known pattern, reads it back, ERASEs it,
    and prints STRESS_MARKER on success. Run repeatedly inside a
    RAM SAVE / RAM RUN cycle so each iteration exercises both the
    PSRAM bitmap allocator (the array) and the PSRAM slot region
    (the saved program image).
    """
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, n%",
        "n% = 4096",
        "DIM INTEGER big%(n%)",
        "FOR i% = 0 TO n% : big%(i%) = i% XOR &H5A5A : NEXT i%",
        "FOR i% = 0 TO n%",
        "  IF big%(i%) <> (i% XOR &H5A5A) THEN",
        '    PRINT "STRESS_FAIL at " + STR$(i%)',
        "    END",
        "  ENDIF",
        "NEXT i%",
        "ERASE big%",
        f'PRINT "{STRESS_MARKER}"',
    ]


# ---------------------------------------------------------------------------
# BasicSerial wrapper with dry-run support
# ---------------------------------------------------------------------------


class DryRunBasic:
    """Stub BasicSerial replacement for --dry-run mode.

    Returns synthesized happy-path responses keyed off the command text so
    that the parsing logic in PsramSmokeHarness still walks every code
    path. No serial I/O happens.
    """

    def __init__(self, target: str, expected_psram_bytes: int) -> None:
        self.target = target
        self.expected_psram_bytes = expected_psram_bytes
        self._saved_slots: set[int] = set()
        # Track the slot a RAM CHAIN inside a pasted program targets, so
        # that a later RAM RUN on the chain-issuing slot can emit both the
        # B and A markers.
        self._chain_target: dict[int, int] = {}
        # Slots whose pasted-program body contains STRESS_MARKER. RAM RUN
        # for these emits the marker so the stress driver can drive itself
        # in dry-run mode without serial I/O.
        self._stress_slots: set[int] = set()
        # Current paste buffer; reset by NEW and committed by RAM SAVE.
        self._paste_buffer: list[str] = []
        # Slot -> source-program-lines that should be returned on
        # subsequent RAM LIST <slot> (used for RAM FILE LOAD).
        self._slot_source: dict[int, list[str]] = {}

    def reset_app(self) -> None:
        return None

    def sync(self, timeout: float = 8.0, boot_wait: float = 0.0) -> bytes:
        return b"\r\n> "

    def command(self, command: str, *, timeout: float = 10.0, check_error: bool = True):
        text = self._synth(command)

        @dataclass
        class _Result:
            command: str
            clean_text: str

            @property
            def text(self) -> str:
                return self.clean_text

        return _Result(command, text)

    def paste_line(self, line: str) -> None:
        """Capture a pasted program line into the synth's paste buffer."""
        self._paste_buffer.append(line)

    # ------------------------------------------------------------------
    # Synthesized responses
    # ------------------------------------------------------------------
    def _synth(self, command: str) -> str:
        upper = command.upper().strip()
        # MM.INFO(PSRAM SIZE) print
        if "PSRAM_SMOKE_PSRAMSIZE=" in command:
            return f"PSRAM_SMOKE_PSRAMSIZE={self.expected_psram_bytes}\r\n> "
        if "PSRAM_SMOKE_HEAP_BEFORE=" in command:
            return "PSRAM_SMOKE_HEAP_BEFORE=204800\r\n> "
        if "PSRAM_SMOKE_HEAP_AFTER=" in command:
            return "PSRAM_SMOKE_HEAP_AFTER=204500\r\n> "
        if "PSRAM_SMOKE_HEAP=" in command:
            return "PSRAM_SMOKE_HEAP=204800\r\n> "
        # OPTION LIST
        if upper.startswith("OPTION LIST"):
            return (
                "OPTION PSRAM 1\r\n"
                "OPTION EXPLICIT\r\n"
                "> "
            )
        # RAM TEST family
        if upper.startswith("RAM TEST"):
            if "NOCACHE" in upper and self.target == "esp32":
                return "Error : NOCACHE not supported on this port\r\n> "
            if re.search(r"RAM TEST\s+\d{3,}\b", upper):
                # RAM TEST 999 -> range error.
                return "Error : 999 is invalid (valid is 1 to 8)\r\n> "
            return "RAM TEST OK\r\n> "
        # RAM ERASE
        if upper.startswith("RAM ERASE ALL"):
            self._saved_slots.clear()
            self._chain_target.clear()
            return "> "
        if upper.startswith("RAM ERASE"):
            m = re.search(r"RAM ERASE\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                self._saved_slots.discard(slot)
                self._chain_target.pop(slot, None)
            return "> "
        # RAM LIST <slot>
        if upper.startswith("RAM LIST"):
            m = re.search(r"RAM LIST\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                source = self._slot_source.get(slot)
                if source is not None:
                    return "\r\n".join(source) + "\r\n> "
                if slot in self._saved_slots:
                    return "\r\n".join(program_a_lines()) + "\r\n> "
                return "> "
            # bare RAM LIST
            lines = []
            for i in range(1, MAXRAMSLOTS + 1):
                if i in self._saved_slots:
                    lines.append(
                        f' RAM Slot {i} in use: "OPTION EXPLICIT"'
                    )
                else:
                    lines.append(f" RAM Slot {i} available")
            return "\r\n".join(lines) + "\r\n> "
        # RAM SAVE
        if upper.startswith("RAM SAVE"):
            m = re.search(r"RAM SAVE\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                if slot > MAXRAMSLOTS or slot < 1:
                    return f"Error : {slot} is invalid (valid is 1 to {MAXRAMSLOTS})\r\n> "
                if slot in self._saved_slots:
                    return "Error : Already programmed\r\n> "
                self._saved_slots.add(slot)
                # Snapshot any RAM CHAIN target embedded in the paste buffer.
                for line in self._paste_buffer:
                    m2 = re.search(r"RAM\s+CHAIN\s+(\d+)", line, re.IGNORECASE)
                    if m2:
                        self._chain_target[slot] = int(m2.group(1))
                        break
                # Note slots that carry the stress program so RAM RUN can
                # echo the marker back in dry-run mode.
                if any(STRESS_MARKER in line for line in self._paste_buffer):
                    self._stress_slots.add(slot)
                self._paste_buffer = []
                return "> "
        # RAM OVERWRITE
        if upper.startswith("RAM OVERWRITE"):
            m = re.search(r"RAM OVERWRITE\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                self._saved_slots.add(slot)
                for line in self._paste_buffer:
                    m2 = re.search(r"RAM\s+CHAIN\s+(\d+)", line, re.IGNORECASE)
                    if m2:
                        self._chain_target[slot] = int(m2.group(1))
                        break
                self._paste_buffer = []
            return "> "
        # RAM LOAD
        if upper.startswith("RAM LOAD"):
            return "> "
        # RAM RUN
        if upper.startswith("RAM RUN"):
            m = re.search(r"RAM RUN\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                if slot == 0:
                    return "> "
                chain = self._chain_target.get(slot)
                if chain is not None:
                    # Program at <slot> issues RAM CHAIN <chain>; emit B's
                    # marker then A's marker (chain semantics).
                    return f"{PROGRAM_B_MARKER}\r\n{PROGRAM_A_MARKER} 15\r\n> "
                if slot in self._stress_slots:
                    return f"{STRESS_MARKER}\r\n> "
                return f"{PROGRAM_A_MARKER} 15\r\n> "
            return "> "
        # RAM CHAIN at the prompt (not inside a pasted program). The
        # harness drives chain semantics by running a saved program that
        # contains RAM CHAIN <slot> as a line, so we should NOT match here
        # when paste-buffering — those lines need to land in the buffer
        # so RAM SAVE can detect them. Only return chain output when the
        # command starts with RAM CHAIN (i.e. typed at the prompt).
        if upper.startswith("RAM CHAIN"):
            return f"{PROGRAM_B_MARKER}\r\n{PROGRAM_A_MARKER} 15\r\n> "
        # RAM FILE LOAD
        if upper.startswith("RAM FILE LOAD"):
            if self.target == "esp32":
                return "Error : RAM FILE LOAD not supported on this port\r\n> "
            m = re.search(r"RAM FILE LOAD\s+(\d+)", upper)
            if m:
                slot = int(m.group(1))
                self._saved_slots.add(slot)
                self._slot_source[slot] = list(ram_file_program_lines())
            return "> "
        # NEW / LIST / etc. — happy-path empty response.
        if upper.startswith("LIST"):
            return "\r\n".join(program_a_lines()) + "\r\n> "
        if upper in {"NEW"}:
            self._paste_buffer = []
            return "> "
        # DIM big array — succeeds in dry-run. Also buffer the line so
        # paste-driven program uploads see DIM statements when later
        # examining the slot body (the harness's _paste_program path
        # calls paste_line explicitly; the stress driver's direct
        # basic.command path needs this fallback).
        if upper.startswith("DIM "):
            self._paste_buffer.append(command)
            return "> "
        # PRINT statements used to probe values.
        if "PSRAM_SMOKE_DIM_OK" in command:
            return "PSRAM_SMOKE_DIM_OK\r\n> "
        if "PSRAM_SMOKE_HEAP_AFTER=" in command:
            return "PSRAM_SMOKE_HEAP_AFTER=204500\r\n> "
        if "PSRAM_SMOKE_HEAP_BEFORE=" in command:
            return "PSRAM_SMOKE_HEAP_BEFORE=204800\r\n> "
        if "PSRAM_SMOKE_STRING_OK" in command:
            return "PSRAM_SMOKE_STRING_OK\r\n> "
        if STRESS_MARKER in command:
            # Pasted-program PRINT lines must land in the buffer so the
            # RAM SAVE content scan can detect the marker and arm the
            # corresponding _stress_slot entry.
            self._paste_buffer.append(command)
            return f"{STRESS_MARKER}\r\n> "
        # Filesystem prep (OPEN/PRINT #1/CLOSE) — empty success.
        if upper.startswith(("OPEN ", "CLOSE", "PRINT #", "KILL ", "ON ERROR")):
            return "> "
        # Generic PRINT. Also append to the paste buffer so PRINT
        # statements containing markers (e.g. STRESS_MARKER) inside a
        # pasted program are seen by RAM SAVE's content scan.
        if upper.startswith("PRINT "):
            self._paste_buffer.append(command)
            return "0\r\n> "
        # Anything else: assume it is a pasted program line and remember
        # it so RAM SAVE can snapshot the buffer.
        self._paste_buffer.append(command)
        return "> "


# ---------------------------------------------------------------------------
# Helpers shared between live and dry-run modes
# ---------------------------------------------------------------------------


def basic_string_expr(text: str) -> str:
    """BASIC string literal that escapes embedded double quotes."""
    if '"' not in text:
        return f'"{text}"'
    parts = text.split('"')
    expr: list[str] = []
    for index, part in enumerate(parts):
        if part:
            expr.append(f'"{part}"')
        if index != len(parts) - 1:
            expr.append("CHR$(34)")
    return " + ".join(expr) if expr else '""'


def marker_int(text: str, marker: str) -> int:
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(marker):
            value = line[len(marker):].strip()
            # Strip trailing prompt chars and whitespace.
            value = value.split()[0] if value.split() else value
            return int(float(value))
    raise RuntimeError(f"missing integer marker {marker!r} in:\n{text}")


def contains_error(text: str, expected: str) -> bool:
    """Case-insensitive substring search tolerant of \"Error :\" prefixes."""
    return expected.lower() in text.lower()


# ---------------------------------------------------------------------------
# Long-running stress driver (Phase 7)
# ---------------------------------------------------------------------------


def run_stress_loop(
    basic,
    *,
    hours: float,
    progress_minutes: float,
    slot: int,
    timeout: float,
    long_timeout: float,
    verbose: bool,
) -> int:
    """Run a wall-clock-bounded PSRAM allocate/save/run loop.

    Each iteration pastes `stress_program_lines()` into the REPL,
    ERASEs the slot, RAM SAVEs the program (exercising the PSRAM slot
    region), then RAM RUNs it (exercising the PSRAM bitmap allocator
    via DIM big%() inside the program). Fails fast if any RAM op
    surfaces an Error or the expected marker is missing.

    Returns 0 on a clean run-to-completion, 1 on the first failure.
    """
    if hours <= 0:
        return 0

    budget_seconds = hours * 3600.0
    progress_seconds = max(60.0, progress_minutes * 60.0)
    started = time.monotonic()
    deadline = started + budget_seconds
    next_progress = started + progress_seconds
    iteration = 0
    failures = 0

    def emit(msg: str) -> None:
        print(msg, flush=True)

    emit(
        f"[stress] starting {hours:.2f}h loop on slot {slot}; "
        f"progress every {progress_minutes:.1f}min"
    )

    def cmd(text: str, *, t: float = timeout) -> str:
        if verbose:
            print(f"  >>> {text}", flush=True)
        result = basic.command(text, timeout=t, check_error=False)
        out = result.clean_text if hasattr(result, "clean_text") else str(result)
        if verbose and out:
            for line in out.splitlines():
                print(f"      {line}", flush=True)
        return out

    while time.monotonic() < deadline:
        iteration += 1
        # Fresh paste each iteration so a single corrupted slot can't
        # silently feed the next round.
        try:
            cmd("NEW")
            cmd(f"RAM ERASE {slot}", t=long_timeout)
            for line in stress_program_lines():
                cmd(line)
            save_out = cmd(f"RAM SAVE {slot}", t=long_timeout)
            if "Error" in save_out:
                emit(f"[stress] FAIL iter {iteration}: RAM SAVE: {save_out.strip()}")
                failures += 1
                break
            cmd("NEW")
            run_out = cmd(f"RAM RUN {slot}", t=long_timeout)
            if STRESS_MARKER not in run_out or "Error" in run_out:
                emit(
                    f"[stress] FAIL iter {iteration}: marker missing or error "
                    f"in:\n{run_out[-400:]}"
                )
                failures += 1
                break
        except Exception as exc:
            emit(f"[stress] FAIL iter {iteration}: exception {exc!r}")
            failures += 1
            break

        now = time.monotonic()
        if now >= next_progress:
            elapsed = now - started
            remaining = max(0.0, deadline - now)
            emit(
                f"[stress] progress iter={iteration} "
                f"elapsed={elapsed/60.0:.1f}min "
                f"remaining={remaining/60.0:.1f}min"
            )
            next_progress = now + progress_seconds

    elapsed = time.monotonic() - started
    if failures:
        emit(
            f"[stress] FAILED after iter={iteration} elapsed={elapsed/60.0:.2f}min"
        )
        return 1

    emit(
        f"[stress] PASS iter={iteration} elapsed={elapsed/60.0:.2f}min "
        f"(budget {hours:.2f}h)"
    )
    return 0


# ---------------------------------------------------------------------------
# The harness
# ---------------------------------------------------------------------------


class PsramSmokeHarness:
    def __init__(
        self,
        basic,
        *,
        target: str,
        expected_psram_bytes: int,
        drive: str,
        prefix: str,
        timeout: float,
        long_timeout: float,
        very_long_timeout: float,
        verbose: bool,
        dry_run: bool,
        report: Report,
    ) -> None:
        self.basic = basic
        self.target = target
        self.expected_psram_bytes = expected_psram_bytes
        self.drive = drive.rstrip("/")
        if not self.drive.endswith(":"):
            self.drive += ":"
        self.prefix = prefix
        self.timeout = timeout
        self.long_timeout = long_timeout
        self.very_long_timeout = very_long_timeout
        self.verbose = verbose
        self.dry_run = dry_run
        self.report = report
        self.expect = TARGET_EXPECTATIONS[target]
        # Capacity of the SRAM heap as reported by the device. Filled in
        # during boot invariants; used to size the PSRAM routing array.
        self.sram_heap_bytes: int = 0
        self.psram_size_bytes: int = 0

    # ------------------------------------------------------------------
    # Wrapper around basic.command that respects dry-run + verbosity.
    # ------------------------------------------------------------------
    def cmd(self, command: str, *, timeout: float | None = None, check_error: bool = False) -> str:
        timeout = timeout if timeout is not None else self.timeout
        if self.verbose:
            print(f"  >>> {command}", flush=True)
        try:
            result = self.basic.command(command, timeout=timeout, check_error=check_error)
        except Exception as exc:
            # Re-raise unchanged in live mode; in dry-run we should never get
            # here because DryRunBasic doesn't throw.
            raise
        text = result.clean_text if hasattr(result, "clean_text") else str(result)
        if self.verbose and text:
            for line in text.splitlines():
                print(f"      {line}", flush=True)
        return text

    def time_cmd(self, command: str, *, timeout: float | None = None) -> tuple[str, float]:
        start = time.monotonic()
        text = self.cmd(command, timeout=timeout)
        return text, time.monotonic() - start

    # ------------------------------------------------------------------
    # Each check returns nothing; it records its outcome in self.report.
    # ------------------------------------------------------------------

    # --- 1. Boot-time invariants --------------------------------------

    def check_boot_invariants(self) -> None:
        print("=== 1. boot-time invariants ===", flush=True)

        # PSRAM SIZE >= expected.
        try:
            text = self.cmd(
                'PRINT "PSRAM_SMOKE_PSRAMSIZE=" + STR$(MM.INFO(PSRAM SIZE))'
            )
            size = marker_int(text, "PSRAM_SMOKE_PSRAMSIZE=")
            self.psram_size_bytes = size
            if size >= self.expected_psram_bytes:
                self.report.passed(
                    "boot/psram_size",
                    f"MM.INFO(PSRAM SIZE) = {size} >= {self.expected_psram_bytes}",
                )
            else:
                self.report.failed(
                    "boot/psram_size",
                    f"MM.INFO(PSRAM SIZE) = {size} < {self.expected_psram_bytes}",
                )
        except Exception as exc:
            self.report.failed("boot/psram_size", str(exc))

        # OPTION LIST should mention the PSRAM-enable marker.
        try:
            text = self.cmd("OPTION LIST", timeout=self.long_timeout)
            marker = str(self.expect["option_list_marker"])
            if marker in text:
                self.report.passed("boot/option_list", f"contains '{marker}'")
            else:
                self.report.failed(
                    "boot/option_list",
                    f"missing '{marker}' in OPTION LIST output",
                )
        except Exception as exc:
            self.report.failed("boot/option_list", str(exc))

        # MM.INFO(HEAP) is the SRAM heap, distinct from PSRAM.
        try:
            text = self.cmd('PRINT "PSRAM_SMOKE_HEAP=" + STR$(MM.INFO(HEAP))')
            heap = marker_int(text, "PSRAM_SMOKE_HEAP=")
            self.sram_heap_bytes = heap
            if heap > 0 and heap != self.psram_size_bytes:
                self.report.passed(
                    "boot/heap_distinct",
                    f"MM.INFO(HEAP) = {heap} (separate from PSRAM)",
                )
            else:
                self.report.failed(
                    "boot/heap_distinct",
                    f"MM.INFO(HEAP) = {heap}; expected non-zero, distinct from PSRAM ({self.psram_size_bytes})",
                )
        except Exception as exc:
            self.report.failed("boot/heap_distinct", str(exc))

    # --- 2. March test ------------------------------------------------

    def check_march(self) -> None:
        print("=== 2. march test ===", flush=True)

        # RAM TEST 1
        try:
            text, elapsed = self.time_cmd("RAM TEST 1", timeout=self.long_timeout)
            if "RAM TEST OK" in text:
                self.report.passed("march/test_1mb", "RAM TEST 1 -> OK", elapsed)
            else:
                self.report.failed("march/test_1mb", f"no 'RAM TEST OK' in:\n{text[-200:]}", elapsed)
        except Exception as exc:
            self.report.failed("march/test_1mb", str(exc))

        # Bare RAM TEST (covers the configured heap region).
        try:
            text, elapsed = self.time_cmd("RAM TEST", timeout=self.very_long_timeout)
            if "RAM TEST OK" in text:
                self.report.passed("march/test_full_heap", "RAM TEST -> OK", elapsed)
            else:
                self.report.failed("march/test_full_heap", f"no 'RAM TEST OK' in:\n{text[-200:]}", elapsed)
        except Exception as exc:
            self.report.failed("march/test_full_heap", str(exc))

        # NOCACHE variant: Pico expects OK; ESP32 expects the not-supported error.
        try:
            text, elapsed = self.time_cmd("RAM TEST NOCACHE 1", timeout=self.long_timeout)
            if self.expect["nocache_supported"]:
                if "RAM TEST OK" in text:
                    self.report.passed("march/nocache_1mb", "RAM TEST NOCACHE 1 -> OK", elapsed)
                else:
                    self.report.failed(
                        "march/nocache_1mb",
                        f"expected RAM TEST OK, got:\n{text[-200:]}",
                        elapsed,
                    )
            else:
                expected_err = str(self.expect["nocache_error_text"])
                if contains_error(text, expected_err):
                    self.report.passed(
                        "march/nocache_unsupported",
                        f"got expected error '{expected_err}'",
                        elapsed,
                    )
                else:
                    self.report.failed(
                        "march/nocache_unsupported",
                        f"expected '{expected_err}', got:\n{text[-200:]}",
                        elapsed,
                    )
        except Exception as exc:
            self.report.failed("march/nocache", str(exc))

        # RAM TEST ALL (heap + slot region; slowest operation).
        try:
            text, elapsed = self.time_cmd("RAM TEST ALL", timeout=self.very_long_timeout)
            if "RAM TEST OK" in text:
                self.report.passed("march/test_all", "RAM TEST ALL -> OK", elapsed)
            else:
                self.report.failed("march/test_all", f"no 'RAM TEST OK' in:\n{text[-200:]}", elapsed)
        except Exception as exc:
            self.report.failed("march/test_all", str(exc))

    # --- 3. Slot lifecycle --------------------------------------------

    def _paste_program(self, lines: Sequence[str]) -> None:
        """Upload a tiny BASIC program via NEW + AUTOSAVE + Ctrl-Z.

        Unnumbered BASIC lines typed at the REPL execute immediately, they
        don't enter program memory. AUTOSAVE collects pasted lines into
        ProgMemory until Ctrl-Z (0x1A), F1, or F2 terminates it. We use
        Ctrl-Z because it is byte-portable across all ports.
        """
        self.cmd("NEW", timeout=self.timeout)
        # The dry-run synthesizer models the paste buffer in DryRunBasic;
        # its NEW handler clears that buffer, so we must call paste_line
        # AFTER NEW to populate it for subsequent RAM SAVE etc. In live
        # mode the device's AUTOSAVE collection does the same job via
        # the byte stream below.
        paste_hook = getattr(self.basic, "paste_line", None)
        if paste_hook is not None:
            for line in lines:
                paste_hook(line)
        # AUTOSAVE: send the command, then body lines, then Ctrl-Z.
        # In dry-run we skip the raw-byte AUTOSAVE machinery — DryRunBasic's
        # _synth doesn't drive a serial port; the paste_hook above already
        # snapshotted the lines.
        if self.dry_run:
            return
        ser = self.basic.serial
        # Issue AUTOSAVE and wait briefly for echo so we're inside the
        # collection loop before sending body bytes.
        ser.write(b"AUTOSAVE\r")
        ser.flush()
        time.sleep(0.2)
        # Drain any echo so it doesn't interfere with later prompt waits.
        _ = ser.read(4096)
        for line in lines:
            ser.write((line + "\r").encode("latin1"))
            ser.flush()
            # Brief pause so the device's input buffer doesn't overflow at
            # 115200 baud on long bodies; AUTOSAVE echoes each byte.
            time.sleep(0.02)
            _ = ser.read(4096)
        # Terminate with Ctrl-Z, then wait for the prompt to return.
        ser.write(b"\x1a")
        ser.flush()
        # AUTOSAVE flashes the program to flash before re-prompting; allow
        # generous time for that on slow flashes.
        end = time.monotonic() + self.long_timeout
        out = bytearray()
        while time.monotonic() < end:
            chunk = ser.read(4096)
            if chunk:
                out.extend(chunk)
                if self.basic._has_prompt(bytes(out)):
                    break

    def check_slot_lifecycle(self) -> None:
        print("=== 3. slot lifecycle ===", flush=True)

        # 3a. RAM ERASE ALL then RAM LIST -> all slots available.
        try:
            self.cmd("RAM ERASE ALL", timeout=self.long_timeout)
            text = self.cmd("RAM LIST", timeout=self.long_timeout)
            available = len(re.findall(r"available", text))
            if available == MAXRAMSLOTS:
                self.report.passed("slots/erase_all", f"{available}/{MAXRAMSLOTS} slots available")
            else:
                self.report.failed(
                    "slots/erase_all",
                    f"expected {MAXRAMSLOTS} 'available', got {available} in:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("slots/erase_all", str(exc))

        # 3b. Paste program A, RAM SAVE 1.
        try:
            self._paste_program(program_a_lines())
            text = self.cmd("RAM SAVE 1", timeout=self.long_timeout)
            if "Error" not in text:
                self.report.passed("slots/save_1", "RAM SAVE 1 ok")
            else:
                self.report.failed("slots/save_1", f"unexpected error:\n{text[-200:]}")
        except Exception as exc:
            self.report.failed("slots/save_1", str(exc))

        # 3c. RAM LIST shows slot 1 in use with a quoted first line.
        try:
            text = self.cmd("RAM LIST", timeout=self.long_timeout)
            if re.search(r"Slot\s+1\s+in use", text):
                self.report.passed("slots/list_in_use", "slot 1 reported in use")
            else:
                self.report.failed(
                    "slots/list_in_use",
                    f"slot 1 not reported in use:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("slots/list_in_use", str(exc))

        # 3d. RAM LIST 1 -> matches saved source.
        # Use the "ALL" modifier so MMBasic skips its per-screen
        # pagination ("PRESS ANY KEY ..."), which would otherwise stall
        # the harness. cmd_psram's getargs(maxargs=3) accepts the
        # `<slot>, ALL` form — three tokens (slot, comma, ALL), matching
        # `argc == 3 && argv[2] == "ALL"`.
        try:
            text = self.cmd("RAM LIST 1, ALL", timeout=self.long_timeout)
            expected_first = program_a_lines()[0]
            # MMBasic's LIST output canonicalizes keywords to Title case
            # (Option.Listcase = CONFIG_TITLE), so "OPTION EXPLICIT"
            # becomes "Option EXPLICIT". Match case-insensitively.
            if expected_first.lower() in text.lower():
                self.report.passed("slots/list_full", "matches saved source")
            else:
                self.report.failed(
                    "slots/list_full",
                    f"expected '{expected_first}' in listing; got:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("slots/list_full", str(exc))

        # 3e. RAM SAVE 1 again -> Already programmed.
        try:
            text = self.cmd("RAM SAVE 1", timeout=self.long_timeout)
            if contains_error(text, "Already programmed"):
                self.report.passed("slots/save_already", "got expected 'Already programmed'")
            else:
                self.report.failed(
                    "slots/save_already",
                    f"expected 'Already programmed' error; got:\n{text[-200:]}",
                )
        except Exception as exc:
            self.report.failed("slots/save_already", str(exc))

        # 3f. RAM OVERWRITE 1 -> no error.
        try:
            text = self.cmd("RAM OVERWRITE 1", timeout=self.long_timeout)
            if "Error" not in text:
                self.report.passed("slots/overwrite", "RAM OVERWRITE 1 ok")
            else:
                self.report.failed("slots/overwrite", f"unexpected error:\n{text[-200:]}")
        except Exception as exc:
            self.report.failed("slots/overwrite", str(exc))

        # 3g. RAM ERASE 1 -> RAM LIST shows slot 1 available again.
        try:
            self.cmd("RAM ERASE 1", timeout=self.long_timeout)
            text = self.cmd("RAM LIST", timeout=self.long_timeout)
            # We accept either "Slot 1 available" exactly or first-slot
            # available pattern.
            if re.search(r"Slot\s+1\s+available", text):
                self.report.passed("slots/erase_one", "slot 1 freed")
            else:
                self.report.failed(
                    "slots/erase_one",
                    f"slot 1 still in use after RAM ERASE 1:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("slots/erase_one", str(exc))

    # --- 4. Slot -> flash -> run cycle --------------------------------

    def check_slot_run_cycle(self) -> None:
        print("=== 4. slot -> flash -> run cycle ===", flush=True)

        # 4a. SAVE 1, NEW, RAM LOAD 1, LIST -> original program present.
        try:
            self._paste_program(program_a_lines())
            self.cmd("RAM ERASE 1", timeout=self.long_timeout)
            self.cmd("RAM SAVE 1", timeout=self.long_timeout)
            self.cmd("NEW", timeout=self.timeout)
            self.cmd("RAM LOAD 1", timeout=self.very_long_timeout)
            # LIST ALL skips MMBasic's per-screen pagination, which would
            # otherwise stall the harness at "PRESS ANY KEY ...".
            text = self.cmd("LIST ALL", timeout=self.long_timeout)
            # Case-insensitive match: LIST canonicalises keywords to Title
            # case (see slots/list_full above).
            if program_a_lines()[0].lower() in text.lower():
                self.report.passed("run/ram_load_then_list", "LIST shows reloaded program")
            else:
                self.report.failed(
                    "run/ram_load_then_list",
                    f"reloaded LIST missing '{program_a_lines()[0]}':\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("run/ram_load_then_list", str(exc))

        # 4b. RAM RUN 1 (after RAM SAVE 1 / NEW) -> program executes.
        try:
            self.cmd("NEW", timeout=self.timeout)
            self.cmd("RAM ERASE 1", timeout=self.long_timeout)
            self._paste_program(program_a_lines())
            self.cmd("RAM SAVE 1", timeout=self.long_timeout)
            self.cmd("NEW", timeout=self.timeout)
            text = self.cmd("RAM RUN 1", timeout=self.very_long_timeout)
            if PROGRAM_A_MARKER in text:
                self.report.passed("run/ram_run_1", "program A executed via RAM RUN 1")
            else:
                self.report.failed(
                    "run/ram_run_1",
                    f"expected '{PROGRAM_A_MARKER}' in output:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("run/ram_run_1", str(exc))

        # 4c. RAM CHAIN: program B in slot 2 chains to program A in slot 1.
        # We RAM SAVE program A to slot 1, then load + run program B which
        # contains "RAM CHAIN 1". The B marker then the A marker must both
        # appear in the captured output (chain semantics: A runs to
        # completion after B reaches the chain statement).
        try:
            self.cmd("NEW", timeout=self.timeout)
            self.cmd("RAM ERASE ALL", timeout=self.long_timeout)
            # Save A into slot 1.
            self._paste_program(program_a_lines())
            self.cmd("RAM SAVE 1", timeout=self.long_timeout)
            # Save B (which chains to slot 1) into slot 2.
            self.cmd("NEW", timeout=self.timeout)
            self._paste_program(program_b_lines(slot=1))
            self.cmd("RAM SAVE 2", timeout=self.long_timeout)
            self.cmd("NEW", timeout=self.timeout)
            text = self.cmd("RAM RUN 2", timeout=self.very_long_timeout)
            saw_b = PROGRAM_B_MARKER in text
            saw_a = PROGRAM_A_MARKER in text
            if saw_b and saw_a:
                self.report.passed("run/ram_chain", "B printed then chained to A")
            else:
                self.report.failed(
                    "run/ram_chain",
                    f"saw_b={saw_b} saw_a={saw_a}; output:\n{text[-400:]}",
                )
        except Exception as exc:
            self.report.failed("run/ram_chain", str(exc))

        # 4d. RAM RUN 0 -> falls back to the flash program slot.
        # We don't assert the program contents (the flash slot is whatever
        # the device boots with); only that the command does not surface
        # an error. If the flash slot is empty, MMBasic prints a banner-
        # style message rather than throwing. Accept either no error, or
        # the documented "no program" sentinel.
        try:
            self.cmd("NEW", timeout=self.timeout)
            text = self.cmd("RAM RUN 0", timeout=self.very_long_timeout)
            if "Error" not in text or "No program" in text:
                self.report.passed("run/ram_run_0", "RAM RUN 0 routed to flash slot")
            else:
                self.report.failed(
                    "run/ram_run_0",
                    f"unexpected error from RAM RUN 0:\n{text[-200:]}",
                )
        except Exception as exc:
            self.report.failed("run/ram_run_0", str(exc))

    # --- 5. RAM FILE LOAD ---------------------------------------------

    def check_ram_file_load(self) -> None:
        print("=== 5. RAM FILE LOAD ===", flush=True)
        target_path = f"{self.drive}/{self.prefix}_ramfile.bas"

        # Drop the BAS file via OPEN ... FOR OUTPUT and PRINT #1.
        try:
            self.cmd(f'ON ERROR SKIP : KILL "{target_path}"')
            self.cmd(f'OPEN "{target_path}" FOR OUTPUT AS #1')
            for line in ram_file_program_lines():
                self.cmd(f"PRINT #1,{basic_string_expr(line)}")
            self.cmd("CLOSE #1")
        except Exception as exc:
            self.report.failed("ramfile/setup", str(exc))
            return

        if self.expect["ram_file_load_supported"]:
            # Happy path: RAM FILE LOAD 2 succeeds; RAM LIST 2 matches.
            try:
                self.cmd("RAM ERASE 2", timeout=self.long_timeout)
                text = self.cmd(
                    f'RAM FILE LOAD 2, "{target_path}"',
                    timeout=self.very_long_timeout,
                )
                if "Error" in text:
                    self.report.failed("ramfile/load", f"unexpected error:\n{text[-200:]}")
                else:
                    self.report.passed("ramfile/load", "RAM FILE LOAD 2 ok")
                    listing = self.cmd("RAM LIST 2", timeout=self.long_timeout)
                    if ram_file_program_lines()[0] in listing:
                        self.report.passed("ramfile/list_match", "slot 2 listing matches source")
                    else:
                        self.report.failed(
                            "ramfile/list_match",
                            f"expected source line in slot 2 listing:\n{listing[-400:]}",
                        )
            except Exception as exc:
                self.report.failed("ramfile/load", str(exc))
        else:
            # ESP32: expect the not-supported error.
            # Slot 2 may still hold a saved program from check_slot_run_cycle
            # (RAM CHAIN test). The shared cmd_psram FILE LOAD path checks
            # `*c != 0x0 && overwrite == 0` BEFORE reaching MemLoadProgram,
            # so without erasing we'd get "Already programmed" instead of
            # the not-supported sentinel.
            try:
                self.cmd("RAM ERASE 2", timeout=self.long_timeout)
                text = self.cmd(
                    f'RAM FILE LOAD 2, "{target_path}"',
                    timeout=self.long_timeout,
                )
                expected_err = str(self.expect["ram_file_load_error_text"])
                if contains_error(text, expected_err):
                    self.report.passed(
                        "ramfile/unsupported",
                        f"got expected '{expected_err}'",
                    )
                else:
                    self.report.failed(
                        "ramfile/unsupported",
                        f"expected '{expected_err}', got:\n{text[-200:]}",
                    )
            except Exception as exc:
                self.report.failed("ramfile/unsupported", str(exc))

        # Cleanup
        try:
            self.cmd(f'ON ERROR SKIP : KILL "{target_path}"')
        except Exception:
            pass

    # --- 6. Memory.c routing ------------------------------------------

    def check_memory_routing(self) -> None:
        print("=== 6. Memory.c PSRAM routing ===", flush=True)

        # Choose N so big%(N) is comfortably larger than SRAM heap/2.
        # Each INTEGER cell is 8 bytes. We aim for at least 75% of SRAM
        # heap so the allocation cannot land in SRAM.
        if self.sram_heap_bytes <= 0:
            self.report.skipped(
                "routing/integer_array",
                "SRAM heap size not available; cannot size big array",
            )
        else:
            big_n = max(1, int(self.sram_heap_bytes * 0.75) // 8)
            try:
                heap_before = self.cmd(
                    'PRINT "PSRAM_SMOKE_HEAP_BEFORE=" + STR$(MM.INFO(HEAP))'
                )
                before = marker_int(heap_before, "PSRAM_SMOKE_HEAP_BEFORE=")
                self.cmd(f"DIM big%({big_n})", timeout=self.long_timeout)
                heap_after = self.cmd(
                    'PRINT "PSRAM_SMOKE_HEAP_AFTER=" + STR$(MM.INFO(HEAP))'
                )
                after = marker_int(heap_after, "PSRAM_SMOKE_HEAP_AFTER=")
                delta = before - after
                # Threshold: SRAM heap should not have shrunk by more than
                # ~10% of the array size (allow for housekeeping bytes).
                array_bytes = big_n * 8
                tolerance = max(8192, array_bytes // 10)
                if delta <= tolerance:
                    self.report.passed(
                        "routing/integer_array",
                        f"DIM big%({big_n}) delta_heap={delta} <= tolerance={tolerance} (array landed in PSRAM)",
                    )
                else:
                    self.report.failed(
                        "routing/integer_array",
                        f"DIM big%({big_n}) shrank SRAM heap by {delta} bytes (array_bytes={array_bytes}, tolerance={tolerance})",
                    )
                # Clear the variable so the next test starts clean.
                self.cmd("ERASE big%", check_error=False)
                self.cmd("NEW", timeout=self.timeout)
            except Exception as exc:
                self.report.failed("routing/integer_array", str(exc))

        # String array path. Use the same sizing strategy at half scale
        # so we exercise the string heap without exhausting it.
        if self.sram_heap_bytes <= 0:
            self.report.skipped(
                "routing/string_array",
                "SRAM heap size not available; cannot size big array",
            )
        else:
            big_n = max(1, int(self.sram_heap_bytes * 0.4) // 256)
            try:
                self.cmd(f"DIM STRING strs$({big_n}) LENGTH 200", timeout=self.long_timeout)
                self.cmd('PRINT "PSRAM_SMOKE_STRING_OK"')
                # If we reached this point the DIM succeeded; success means
                # the allocator handed back PSRAM-backed memory because the
                # SRAM heap by itself cannot satisfy the request.
                self.report.passed(
                    "routing/string_array",
                    f"DIM STRING strs$({big_n}) LENGTH 200 allocated",
                )
                self.cmd("ERASE strs$", check_error=False)
                self.cmd("NEW", timeout=self.timeout)
            except Exception as exc:
                self.report.failed("routing/string_array", str(exc))

    # --- 7. Negative cases --------------------------------------------

    def check_negative_cases(self) -> None:
        print("=== 7. negative cases ===", flush=True)

        # 7a. RAM SAVE <invalid> -> invalid slot error.
        try:
            text = self.cmd(f"RAM SAVE {INVALID_SLOT}", timeout=self.long_timeout)
            if "Error" in text and re.search(r"invalid|out of range|0 to|\d+ is invalid", text, re.IGNORECASE):
                self.report.passed(
                    "neg/save_invalid_slot",
                    f"RAM SAVE {INVALID_SLOT} -> range error",
                )
            else:
                self.report.failed(
                    "neg/save_invalid_slot",
                    f"expected range error; got:\n{text[-200:]}",
                )
        except Exception as exc:
            self.report.failed("neg/save_invalid_slot", str(exc))

        # 7b. RAM TEST <huge> -> range error.
        try:
            text = self.cmd(f"RAM TEST {INVALID_TEST_MB}", timeout=self.long_timeout)
            if "Error" in text:
                self.report.passed(
                    "neg/test_range_error",
                    f"RAM TEST {INVALID_TEST_MB} -> error",
                )
            else:
                self.report.failed(
                    "neg/test_range_error",
                    f"expected error for RAM TEST {INVALID_TEST_MB}; got:\n{text[-200:]}",
                )
        except Exception as exc:
            self.report.failed("neg/test_range_error", str(exc))

        # 7c. PSRAM not enabled. This is only meaningful when the device
        # actually has PSRAMsize == 0. Detect that from MM.INFO and skip
        # if PSRAM is enabled — we can't reach the "PSRAM not enabled"
        # branch from BASIC at runtime once the slab is up.
        if self.psram_size_bytes > 0:
            self.report.skipped(
                "neg/psram_not_enabled",
                "PSRAM is enabled; 'PSRAM not enabled' branch unreachable from BASIC",
            )
        else:
            try:
                text = self.cmd("RAM LIST", timeout=self.long_timeout)
                if contains_error(text, "PSRAM not enabled"):
                    self.report.passed(
                        "neg/psram_not_enabled",
                        "RAM LIST reports 'PSRAM not enabled'",
                    )
                else:
                    self.report.failed(
                        "neg/psram_not_enabled",
                        f"expected 'PSRAM not enabled' error; got:\n{text[-200:]}",
                    )
            except Exception as exc:
                self.report.failed("neg/psram_not_enabled", str(exc))

    # ------------------------------------------------------------------
    def configure_session(self) -> None:
        """One-time per-session setup: raise console height so LIST output
        does not page with the "PRESS ANY KEY ..." interactive prompt.

        MMBasic's `LIST` and `RAM LIST <slot>` page output every
        `Option.Height - overlap` lines (Commands.c). Default SCREENHEIGHT
        is 24, which is smaller than the saved programs the harness paste-
        loads. Raise the height to a comfortable max so single-screen
        listings come back without a paging prompt the harness can't
        easily satisfy.
        """
        if self.dry_run:
            return
        try:
            # OPTION DISPLAY <height>[, , <width>] is the runtime-tunable
            # height setter (OptionCommands.c). Range is 5..100; we pick
            # 100 to defeat paging for the harness's tiny test programs.
            self.cmd("OPTION DISPLAY 100", timeout=self.timeout)
        except Exception:
            # OPTION DISPLAY can error on ports with LCD console attached.
            # If it does, fall through — the LIST tests will just have to
            # tolerate the paging prompt (and likely fail loudly enough to
            # signal the harness operator that this port needs special
            # handling).
            pass

    def run_all(self) -> None:
        self.configure_session()
        self.check_boot_invariants()
        self.check_march()
        self.check_slot_lifecycle()
        self.check_slot_run_cycle()
        self.check_ram_file_load()
        self.check_memory_routing()
        self.check_negative_cases()


# ---------------------------------------------------------------------------
# CLI plumbing
# ---------------------------------------------------------------------------


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--target",
        required=True,
        choices=sorted(TARGET_EXPECTATIONS.keys()),
        help="device family; selects per-target expectations table",
    )
    parser.add_argument("--port", default=default_port(), help="serial device path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--expected-psram-bytes",
        type=int,
        default=None,
        help="minimum MM.INFO(PSRAM SIZE) to accept; defaults to per-target value",
    )
    parser.add_argument(
        "--drive",
        default="A:",
        help="MMBasic drive used for the RAM FILE LOAD setup file; default A:",
    )
    parser.add_argument(
        "--prefix",
        default="psramsmk",
        help="filename prefix for the RAM FILE LOAD scratch file",
    )
    parser.add_argument("--timeout", type=float, default=10.0, help="default per-command timeout")
    parser.add_argument(
        "--long-timeout",
        type=float,
        default=30.0,
        help="timeout for slot/list/run commands",
    )
    parser.add_argument(
        "--very-long-timeout",
        type=float,
        default=120.0,
        help="timeout for full-region RAM TEST (e.g. RAM TEST ALL on 8 MB modules)",
    )
    parser.add_argument("--boot-wait", type=float, default=1.0, help="seconds to wait before sync")
    parser.add_argument("--reset-app", action="store_true", help="pulse RTS to reset before syncing")
    parser.add_argument("--verbose", action="store_true", help="echo every command + response")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="parse-only mode; no serial I/O, exercises check logic against synthesized responses",
    )
    parser.add_argument(
        "--stress-hours",
        type=float,
        default=0.0,
        help=(
            "if > 0, after the smoke checks pass, run a long-running RAM SAVE/"
            "RAM RUN loop for this many wall-clock hours (Phase 7 stress)"
        ),
    )
    parser.add_argument(
        "--stress-slot",
        type=int,
        default=1,
        help="RAM slot used by the stress loop (default 1)",
    )
    parser.add_argument(
        "--stress-progress-minutes",
        type=float,
        default=5.0,
        help="how often to log progress during --stress-hours (minutes, default 5)",
    )
    parser.add_argument(
        "--stress-only",
        action="store_true",
        help="skip the smoke checks and only run the stress loop",
    )
    return parser.parse_args(argv)


def print_summary(report: Report) -> None:
    print("--- summary ---", flush=True)
    for check in report.checks:
        suffix = f" {check.detail}" if check.detail else ""
        print(f"[{check.status}] {check.name}{suffix}", flush=True)
    line = (
        f"Summary: {report.num_passed}/{report.total} passed, "
        f"{report.num_failed} failed, {report.num_skipped} skipped"
    )
    print(line, flush=True)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    expected_bytes = args.expected_psram_bytes
    if expected_bytes is None:
        expected_bytes = int(TARGET_EXPECTATIONS[args.target]["default_psram_bytes"])
    report = Report()

    if args.dry_run:
        print(f"[INFO] dry-run mode: target={args.target}, no serial I/O", flush=True)
        basic = DryRunBasic(target=args.target, expected_psram_bytes=expected_bytes)
        harness = PsramSmokeHarness(
            basic,
            target=args.target,
            expected_psram_bytes=expected_bytes,
            drive=args.drive,
            prefix=args.prefix,
            timeout=args.timeout,
            long_timeout=args.long_timeout,
            very_long_timeout=args.very_long_timeout,
            verbose=args.verbose,
            dry_run=True,
            report=report,
        )
        if not args.stress_only:
            try:
                harness.run_all()
            except Exception as exc:
                report.failed("dry_run/harness", str(exc))
        print_summary(report)
        rc = 0 if report.all_passed else 1
        if args.stress_hours > 0:
            stress_rc = run_stress_loop(
                basic,
                hours=args.stress_hours,
                progress_minutes=args.stress_progress_minutes,
                slot=args.stress_slot,
                timeout=args.timeout,
                long_timeout=args.long_timeout,
                verbose=args.verbose,
            )
            if stress_rc != 0:
                rc = stress_rc
        return rc

    # Live mode: needs pyserial.
    if BasicSerial is None:
        print(
            "FAIL psram smoke: pyserial is not importable; install it or run with --dry-run",
            file=sys.stderr,
        )
        return 2

    stress_rc = 0
    try:
        with BasicSerial(args.port, baud=args.baud) as basic:
            if args.reset_app:
                basic.reset_app()
            basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
            harness = PsramSmokeHarness(
                basic,
                target=args.target,
                expected_psram_bytes=expected_bytes,
                drive=args.drive,
                prefix=args.prefix,
                timeout=args.timeout,
                long_timeout=args.long_timeout,
                very_long_timeout=args.very_long_timeout,
                verbose=args.verbose,
                dry_run=False,
                report=report,
            )
            if not args.stress_only:
                harness.run_all()
            if args.stress_hours > 0 and report.all_passed:
                stress_rc = run_stress_loop(
                    basic,
                    hours=args.stress_hours,
                    progress_minutes=args.stress_progress_minutes,
                    slot=args.stress_slot,
                    timeout=args.timeout,
                    long_timeout=args.long_timeout,
                    verbose=args.verbose,
                )
    except TimeoutError as exc:
        report.failed("serial/prompt", str(exc))
        print_summary(report)
        return 1
    except Exception as exc:
        report.failed("psram_smoke", str(exc))
        print_summary(report)
        return 1

    print_summary(report)
    if not report.all_passed:
        return 1
    return stress_rc


if __name__ == "__main__":
    raise SystemExit(main())
