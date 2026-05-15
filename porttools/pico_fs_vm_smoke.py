#!/usr/bin/env python3
"""Pico-oriented filesystem, BASIC, bytecode VM, and math smoke tests."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from basic_serial import BasicSerial, default_port


def basic_string_expr(text: str) -> str:
    """Return a BASIC string expression that preserves embedded quotes."""
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


def normalise_drive(drive: str) -> str:
    drive = drive.strip().upper()
    if not drive:
        raise ValueError("drive must not be empty")
    if drive.endswith("/"):
        drive = drive[:-1]
    if not drive.endswith(":"):
        drive += ":"
    return drive


def join_drive(drive: str, path: str) -> str:
    return f"{drive}/{path.lstrip('/')}"


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


class PicoSmoke:
    def __init__(
        self,
        basic: BasicSerial,
        *,
        drive: str,
        prefix: str,
        timeout: float,
        long_timeout: float,
        keep_files: bool,
        expect_psram: bool,
        connect_command: str | None,
        flash_slot: int,
    ) -> None:
        self.basic = basic
        self.drive = normalise_drive(drive)
        self.prefix = prefix.strip("/") or "psmoke"
        self.timeout = timeout
        self.long_timeout = long_timeout
        self.keep_files = keep_files
        self.expect_psram = expect_psram
        self.connect_command = connect_command
        self.flash_slot = flash_slot
        self.checks: list[Check] = []

    def command(self, command: str, *, timeout: float | None = None, check_error: bool = True) -> str:
        return self.basic.command(
            command,
            timeout=self.timeout if timeout is None else timeout,
            check_error=check_error,
        ).clean_text

    def note(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append(Check(name, ok, detail))
        state = "ok" if ok else "FAIL"
        suffix = f" - {detail}" if detail else ""
        print(f"{state:4} {name}{suffix}", flush=True)

    def cleanup_temp_tree(self) -> None:
        root = join_drive(self.drive, self.prefix)
        for command in (
            f'ON ERROR SKIP : CLOSE #1',
            f'ON ERROR SKIP : CLOSE #2',
            f'ON ERROR SKIP : CHDIR "{self.drive}/"',
            f'ON ERROR SKIP : KILL "{root}/nested/deep.txt"',
            f'ON ERROR SKIP : KILL "{root}/copy.txt"',
            f'ON ERROR SKIP : KILL "{root}/Mix Case.txt"',
            f'ON ERROR SKIP : KILL "{root}/loop.tmp"',
            f'ON ERROR SKIP : KILL "{root}/ren.txt"',
            f'ON ERROR SKIP : KILL "{root}/root.txt"',
            f'ON ERROR SKIP : KILL "{root}/app.txt"',
            f'ON ERROR SKIP : RMDIR "{root}/nested"',
            f'ON ERROR SKIP : RMDIR "{root}"',
        ):
            self.command(command, check_error=False)

    def cleanup_generated_programs(self) -> None:
        if self.keep_files:
            return
        for path in self.program_paths():
            self.command(f'ON ERROR SKIP : KILL "{path}"', check_error=False)

    def program_paths(self) -> list[str]:
        return [
            join_drive(self.drive, f"{self.prefix}_fs.bas"),
            join_drive(self.drive, f"{self.prefix}_err.bas"),
            join_drive(self.drive, f"{self.prefix}_big.bas"),
            join_drive(self.drive, f"{self.prefix}_save_src.bas"),
            join_drive(self.drive, f"{self.prefix}_save_copy.bas"),
            join_drive(self.drive, f"{self.prefix}_chain_start.bas"),
            join_drive(self.drive, f"{self.prefix}_chain_target.bas"),
            join_drive(self.drive, f"{self.prefix}_flash.bas"),
            join_drive(self.drive, f"{self.prefix}_vm.bas"),
            join_drive(self.drive, f"{self.prefix}_sieve.bas"),
            join_drive(self.drive, f"{self.prefix}_time.bas"),
            join_drive(self.drive, f"{self.prefix}_gfx.bas"),
        ]

    def write_program(self, path: str, lines: Iterable[str]) -> None:
        self.command(f'ON ERROR SKIP : KILL "{path}"', check_error=False)
        self.command(f'OPEN "{path}" FOR OUTPUT AS #1')
        try:
            for line in lines:
                self.command(f"PRINT #1,{basic_string_expr(line)}")
        finally:
            self.command("CLOSE #1", check_error=False)

    def run_program(self, command: str, marker: str, *, timeout: float | None = None) -> str:
        try:
            output = self.command(command, timeout=timeout or self.long_timeout)
        except TimeoutError as exc:
            output = str(exc)
            if marker not in output:
                raise
            try:
                self.basic.sync(timeout=min(5.0, self.timeout))
            except TimeoutError as sync_exc:
                raise RuntimeError(
                    f"{command!r} printed {marker!r}, but the prompt did not recover"
                ) from sync_exc
        if marker not in output:
            raise RuntimeError(f"missing marker {marker!r} in transcript")
        return output

    def device_smoke(self) -> None:
        print("=== device ===", flush=True)
        id_out = self.command('PRINT "PICO_DEVICE_ID=" + MM.INFO$(ID)')
        cpu_out = self.command('PRINT "PICO_CPUSPEED=" + MM.INFO$(CPUSPEED)')
        free_out = self.command('PRINT "PICO_FREE_SPACE=" + STR$(MM.INFO(FREE SPACE))')
        keyboard_out = self.command('PRINT "PICO_KEYBOARD=" + MM.INFO$(OPTION KEYBOARD)')
        battery_out = self.command('PRINT "PICO_BATTERY=" + STR$(MM.INFO(BATTERY))')
        charging_out = self.command('PRINT "PICO_CHARGING=" + STR$(MM.INFO(CHARGING))')
        option_out = self.command("OPTION LIST", timeout=self.long_timeout)
        if self.expect_psram and "OPTION PSRAM" not in option_out:
            raise RuntimeError("expected OPTION PSRAM in OPTION LIST")
        self.command(f'PRINT "PICO_DIR_PROBE=" + DIR$("{self.drive}/*")')
        self.note("prompt and MM.INFO", True, "device responded")
        self.note("PicoCalc hardware info", True, "keyboard, battery, charging")
        if self.expect_psram:
            self.note("PSRAM option", True, "OPTION PSRAM present")
        for line in (id_out + cpu_out + free_out + keyboard_out + battery_out + charging_out).splitlines():
            if line.startswith("PICO_"):
                print(f"     {line}", flush=True)

    def fs_smoke(self) -> None:
        print("=== filesystem ===", flush=True)
        self.cleanup_temp_tree()
        path = join_drive(self.drive, f"{self.prefix}_fs.bas")
        self.write_program(path, fs_program(self.drive, self.prefix))
        self.run_program(f'RUN "{path}"', "PICO_FS_SMOKE_OK")
        self.note("RUN filesystem battery", True, "open/read/write/append/copy/rename/chdir/dir/kill/rmdir")

    def error_smoke(self) -> None:
        print("=== filesystem errors ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_err.bas")
        self.write_program(path, fs_error_program(self.drive, self.prefix))
        self.run_program(f'RUN "{path}"', "PICO_FS_ERROR_SMOKE_OK")
        self.note("filesystem error paths", True, "missing file, duplicate mkdir, non-empty rmdir, rename collision")

    def large_file_smoke(self) -> None:
        print("=== large file ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_big.bas")
        self.write_program(path, large_file_program(self.drive, self.prefix))
        self.run_program(f'RUN "{path}"', "PICO_LARGE_FILE_SMOKE_OK", timeout=self.long_timeout)
        self.note("large file checksum", True, "multi-sector write/read/verify loop")

    def program_lifecycle_smoke(self) -> None:
        print("=== program lifecycle ===", flush=True)
        src = join_drive(self.drive, f"{self.prefix}_save_src.bas")
        dst = join_drive(self.drive, f"{self.prefix}_save_copy.bas")
        self.write_program(src, save_load_source_program())
        self.command(f'ON ERROR SKIP : KILL "{dst}"', check_error=False)
        self.command(f'LOAD "{src}"', timeout=self.long_timeout)
        self.command(f'SAVE "{dst}"', timeout=self.long_timeout)
        self.command("NEW")
        self.command(f'LOAD "{dst}"', timeout=self.long_timeout)
        self.run_program("RUN", "PICO_SAVE_LOAD_OK", timeout=self.long_timeout)
        self.run_program(f'LOAD "{dst}", R', "PICO_SAVE_LOAD_OK", timeout=self.long_timeout)
        self.note("SAVE/LOAD/RUN", True, "program round-trip and autorun load")

    def chain_smoke(self) -> None:
        print("=== chain ===", flush=True)
        chain_start = join_drive(self.drive, f"{self.prefix}_chain_start.bas")
        chain_target = join_drive(self.drive, f"{self.prefix}_chain_target.bas")
        self.write_program(chain_start, chain_start_program(chain_target))
        self.write_program(chain_target, chain_target_program())
        self.run_program(f'RUN "{chain_start}"', "PICO_CHAIN_TARGET_OK payload", timeout=self.long_timeout)
        self.note("CHAIN", True, "chained program and cmdline")

    def flash_smoke(self) -> None:
        print("=== flash slots ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_flash.bas")
        self.write_program(path, flash_source_program(self.flash_slot))
        self.command(f'LOAD "{path}"', timeout=self.long_timeout)
        self.command(f"FLASH ERASE {self.flash_slot}", timeout=self.long_timeout, check_error=False)
        self.command(f"FLASH SAVE {self.flash_slot}", timeout=self.long_timeout)
        listing = self.command(f"FLASH LIST {self.flash_slot}, ALL", timeout=self.long_timeout)
        if "PICO_FLASH_SLOT_OK" not in listing:
            raise RuntimeError(f"FLASH LIST {self.flash_slot} did not show saved program")
        self.command("NEW")
        self.command(f"FLASH LOAD {self.flash_slot}", timeout=self.long_timeout)
        self.run_program("RUN", "PICO_FLASH_SLOT_OK", timeout=self.long_timeout)
        self.run_program(f"FLASH RUN {self.flash_slot}", "PICO_FLASH_SLOT_OK", timeout=self.long_timeout)
        self.command(f"FLASH ERASE {self.flash_slot}", timeout=self.long_timeout)
        self.note("FLASH SAVE/LOAD/RUN/LIST/ERASE", True, f"slot {self.flash_slot}")

    def autosave_smoke(self) -> None:
        print("=== autosave ===", flush=True)
        self.autosave_program(autosave_source_program())
        self.run_program("RUN", "PICO_AUTOSAVE_OK")
        self.note("AUTOSAVE", True, "serial program entry to program memory")

    def autosave_program(self, lines: Sequence[str]) -> None:
        serial = self.basic.serial
        assert serial is not None
        serial.reset_input_buffer()
        serial.write(b"AUTOSAVE N\r")
        serial.flush()
        time.sleep(0.2)
        payload = "\r".join(lines).encode("latin1") + b"\r\x1a"
        serial.write(payload)
        serial.flush()
        raw = self.basic.wait_for_prompt(self.long_timeout)
        if not self.basic._has_prompt(raw):
            tail = raw[-240:].decode("latin1", "replace")
            raise TimeoutError(f"timeout waiting for prompt after AUTOSAVE; tail={tail!r}")

    def vm_smoke(self) -> None:
        print("=== bytecode vm ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_vm.bas")
        vm_temp = join_drive(self.drive, f"{self.prefix}_vm.txt")
        self.command(f'ON ERROR SKIP : KILL "{vm_temp}"', check_error=False)
        self.write_program(path, vm_program(self.drive, self.prefix))
        self.run_program(f'FRUN "{path}"', "PICO_VM_SMOKE_OK")
        self.note("FRUN bytecode VM", True, "integer loops, strings, and VM file I/O")

    def sieve_smoke(self) -> None:
        print("=== sieve ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_sieve.bas")
        self.write_program(path, sieve_program())
        self.run_program(f'FRUN "{path}"', "PICO_SIEVE_OK 168", timeout=self.long_timeout)
        self.note("FRUN sieve math", True, "168 primes <= 1000")

    def timing_smoke(self) -> None:
        print("=== timing ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_time.bas")
        self.write_program(path, timing_program())
        self.run_program(f'RUN "{path}"', "PICO_TIMING_SMOKE_OK", timeout=self.long_timeout)
        self.note("timer/pause/settick", True, "TIMER, PAUSE, SETTICK interrupt")

    def display_smoke(self) -> None:
        print("=== display ===", flush=True)
        path = join_drive(self.drive, f"{self.prefix}_gfx.bas")
        self.write_program(path, display_program())
        self.run_program(f'RUN "{path}"', "PICO_DISPLAY_SMOKE_OK", timeout=self.long_timeout)
        self.note("display/framebuffer", True, "PIXEL readback and framebuffer copy/merge")

    def web_smoke(self) -> None:
        print("=== web ===", flush=True)
        if self.connect_command:
            self.command(self.connect_command, timeout=self.long_timeout)
        status = self.command('PRINT "PICO_WIFI_STATUS=" + STR$(MM.INFO(WIFI STATUS))')
        ip = self.command('PRINT "PICO_IP=" + MM.INFO$(IP ADDRESS)')
        if "0.0.0.0" in ip:
            raise RuntimeError("WiFi IP address is 0.0.0.0")
        self.note("WEB quick status", True, "WiFi status and IP address")
        for line in (status + ip).splitlines():
            if line.startswith("PICO_"):
                print(f"     {line}", flush=True)


def fs_program(drive: str, prefix: str) -> list[str]:
    root = join_drive(drive, prefix)
    return [
        "ON ERROR SKIP : CLOSE #1",
        "ON ERROR SKIP : CLOSE #2",
        f'DRIVE "{drive}"',
        f'ON ERROR SKIP : CHDIR "{drive}/"',
        f'ON ERROR SKIP : KILL "{root}/nested/deep.txt"',
        f'ON ERROR SKIP : KILL "{root}/copy.txt"',
        f'ON ERROR SKIP : KILL "{root}/Mix Case.txt"',
        f'ON ERROR SKIP : KILL "{root}/loop.tmp"',
        f'ON ERROR SKIP : KILL "{root}/ren.txt"',
        f'ON ERROR SKIP : KILL "{root}/root.txt"',
        f'ON ERROR SKIP : KILL "{root}/app.txt"',
        f'ON ERROR SKIP : RMDIR "{root}/nested"',
        f'ON ERROR SKIP : RMDIR "{root}"',
        f'MKDIR "{root}"',
        f'CHDIR "{root}"',
        f'IF INSTR(CWD$, "{prefix}") = 0 THEN ERROR "cwd"',
        'OPEN "root.txt" FOR OUTPUT AS #1',
        'PRINT #1, "alpha"',
        'PRINT #1, 42',
        'PRINT #1, "omega"',
        "CLOSE #1",
        'IF MM.INFO(EXISTS FILE "root.txt") = 0 THEN ERROR "exists file"',
        'IF MM.INFO(FILESIZE "root.txt") <= 0 THEN ERROR "filesize"',
        'OPEN "root.txt" FOR INPUT AS #1',
        "IF LOF(#1) <= 0 THEN ERROR \"lof\"",
        "IF LOC(#1) <> 1 THEN ERROR \"loc start\"",
        "A$ = INPUT$(5, #1)",
        'IF A$ <> "alpha" THEN ERROR "input$"',
        "IF LOC(#1) <> 6 THEN ERROR \"loc after input\"",
        "SEEK #1, 1",
        "LINE INPUT #1, A$",
        "LINE INPUT #1, B$",
        "LINE INPUT #1, C$",
        'IF A$ <> "alpha" THEN ERROR "read alpha"',
        'IF VAL(B$) <> 42 THEN ERROR "read number"',
        'IF C$ <> "omega" THEN ERROR "read omega"',
        "IF EOF(#1) = 0 THEN ERROR \"eof\"",
        "CLOSE #1",
        'OPEN "app.txt" FOR APPEND AS #1',
        'PRINT #1, "first"',
        "CLOSE #1",
        'OPEN "app.txt" FOR APPEND AS #1',
        'PRINT #1, "second"',
        "CLOSE #1",
        'OPEN "app.txt" FOR INPUT AS #1',
        "LINE INPUT #1, A$",
        "LINE INPUT #1, B$",
        "CLOSE #1",
        'IF A$ <> "first" THEN ERROR "append first"',
        'IF B$ <> "second" THEN ERROR "append second"',
        'COPY "root.txt" TO "copy.txt"',
        'OPEN "copy.txt" FOR INPUT AS #1',
        "LINE INPUT #1, A$",
        "CLOSE #1",
        'IF A$ <> "alpha" THEN ERROR "copy content"',
        'RENAME "copy.txt" AS "ren.txt"',
        'IF DIR$("ren.txt", FILE) <> "ren.txt" THEN ERROR "rename missing"',
        'IF LEN(DIR$("copy.txt", FILE)) <> 0 THEN ERROR "rename old exists"',
        'OPEN "Mix Case.txt" FOR OUTPUT AS #1',
        'PRINT #1, "space-name"',
        "CLOSE #1",
        'CHDIR "."',
        'IF DIR$("Mix Case.txt", FILE) <> "Mix Case.txt" THEN ERROR "space filename"',
        "FOR I% = 1 TO 20",
        '  OPEN "loop.tmp" FOR OUTPUT AS #1',
        "  PRINT #1, I%",
        "  CLOSE #1",
        '  KILL "loop.tmp"',
        "NEXT I%",
        'MKDIR "nested"',
        'IF MM.INFO(EXISTS DIR "nested") = 0 THEN ERROR "exists dir"',
        'IF DIR$("nested", DIR) <> "nested" THEN ERROR "dir missing"',
        'CHDIR "nested"',
        'OPEN "deep.txt" FOR OUTPUT AS #1',
        'PRINT #1, "deep"',
        "CLOSE #1",
        'IF DIR$("deep.txt", FILE) <> "deep.txt" THEN ERROR "nested file"',
        'CHDIR ".."',
        'N$ = DIR$("*.txt", FILE)',
        "TC% = 0",
        "DO WHILE LEN(N$) > 0",
        "  TC% = TC% + 1",
        "  N$ = DIR$()",
        "LOOP",
        "IF TC% < 3 THEN ERROR \"wildcard count\"",
        'KILL "ren.txt"',
        'KILL "Mix Case.txt"',
        'KILL "root.txt"',
        'KILL "app.txt"',
        'KILL "nested/deep.txt"',
        'RMDIR "nested"',
        f'CHDIR "{drive}/"',
        f'RMDIR "{prefix}"',
        'PRINT "PICO_FS_SMOKE_OK"',
    ]


def fs_error_program(drive: str, prefix: str) -> list[str]:
    root = join_drive(drive, f"{prefix}err")
    return [
        "ON ERROR SKIP : CLOSE #1",
        f'DRIVE "{drive}"',
        f'ON ERROR SKIP : CHDIR "{drive}/"',
        f'ON ERROR SKIP : KILL "{root}/dup/a.txt"',
        f'ON ERROR SKIP : KILL "{root}/a.txt"',
        f'ON ERROR SKIP : KILL "{root}/b.txt"',
        f'ON ERROR SKIP : RMDIR "{root}/dup"',
        f'ON ERROR SKIP : RMDIR "{root}"',
        f'MKDIR "{root}"',
        f'CHDIR "{root}"',
        "ON ERROR SKIP",
        'OPEN "missing.nope" FOR INPUT AS #1',
        'IF MM.ERRNO = 0 THEN ERROR "missing open did not fail"',
        "IF LEN(MM.ERRMSG$) = 0 THEN ERROR \"missing errmsg\"",
        "ON ERROR CLEAR",
        'MKDIR "dup"',
        "ON ERROR SKIP",
        'MKDIR "dup"',
        'IF MM.ERRNO = 0 THEN ERROR "duplicate mkdir did not fail"',
        'OPEN "dup/a.txt" FOR OUTPUT AS #1',
        'PRINT #1, "busy"',
        "CLOSE #1",
        "ON ERROR SKIP",
        'RMDIR "dup"',
        'IF MM.ERRNO = 0 THEN ERROR "non-empty rmdir did not fail"',
        'OPEN "a.txt" FOR OUTPUT AS #1',
        'PRINT #1, "from-a"',
        "CLOSE #1",
        'OPEN "b.txt" FOR OUTPUT AS #1',
        'PRINT #1, "from-b"',
        "CLOSE #1",
        "ON ERROR SKIP",
        'RENAME "a.txt" AS "b.txt"',
        "IF MM.ERRNO = 0 THEN",
        '  OPEN "b.txt" FOR INPUT AS #1',
        "  LINE INPUT #1, A$",
        "  CLOSE #1",
        '  IF A$ <> "from-a" THEN ERROR "rename overwrite content"',
        "ELSE",
        '  OPEN "b.txt" FOR INPUT AS #1',
        "  LINE INPUT #1, A$",
        "  CLOSE #1",
        '  IF A$ <> "from-b" THEN ERROR "rename collision content"',
        '  KILL "a.txt"',
        "ENDIF",
        'KILL "b.txt"',
        'KILL "dup/a.txt"',
        'RMDIR "dup"',
        f'CHDIR "{drive}/"',
        f'RMDIR "{prefix}err"',
        'PRINT "PICO_FS_ERROR_SMOKE_OK"',
    ]


def large_file_program(drive: str, prefix: str) -> list[str]:
    root = join_drive(drive, f"{prefix}big")
    return [
        "OPTION EXPLICIT",
        "ON ERROR SKIP : CLOSE #1",
        f'DRIVE "{drive}"',
        f'ON ERROR SKIP : CHDIR "{drive}/"',
        f'ON ERROR SKIP : KILL "{root}/big.dat"',
        f'ON ERROR SKIP : RMDIR "{root}"',
        f'MKDIR "{root}"',
        f'CHDIR "{root}"',
        "DIM INTEGER i%",
        "DIM STRING s$, r$",
        'OPEN "big.dat" FOR OUTPUT AS #1',
        "FOR i% = 1 TO 600",
        '  s$ = "PICO-LARGE-LINE-" + STR$(i%) + "-abcdefghijklmnopqrstuvwxyz0123456789"',
        "  PRINT #1, s$",
        "NEXT i%",
        "CLOSE #1",
        'IF MM.INFO(FILESIZE "big.dat") < 24000 THEN ERROR "large file too small"',
        'OPEN "big.dat" FOR INPUT AS #1',
        "FOR i% = 1 TO 600",
        "  LINE INPUT #1, r$",
        '  s$ = "PICO-LARGE-LINE-" + STR$(i%) + "-abcdefghijklmnopqrstuvwxyz0123456789"',
        '  IF r$ <> s$ THEN ERROR "large mismatch"',
        "NEXT i%",
        "IF EOF(#1) = 0 THEN ERROR \"large eof\"",
        "CLOSE #1",
        'KILL "big.dat"',
        f'CHDIR "{drive}/"',
        f'RMDIR "{prefix}big"',
        'PRINT "PICO_LARGE_FILE_SMOKE_OK"',
    ]


def save_load_source_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER a%, b%, c%, total%",
        "READ a%, b%, c%",
        "total% = Add3%(a%, b%, c%)",
        "RESTORE",
        "READ a%",
        'IF a% <> 3 THEN ERROR "restore"',
        "IF total% <> 12 THEN ERROR \"data sum\"",
        'PRINT "PICO_SAVE_LOAD_OK"',
        "END",
        "FUNCTION Add3%(x%, y%, z%)",
        "  Add3% = x% + y% + z%",
        "END FUNCTION",
        "DATA 3,4,5",
    ]


def chain_start_program(target_path: str) -> list[str]:
    return [
        'PRINT "PICO_CHAIN_START_OK"',
        f'CHAIN "{target_path}", "payload"',
    ]


def chain_target_program() -> list[str]:
    return [
        'PRINT "PICO_CHAIN_TARGET_OK " + MM.CMDLINE$',
        "END",
    ]


def flash_source_program(slot: int) -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER slot%",
        f"slot% = {slot}",
        'PRINT "PICO_FLASH_SLOT_OK " + STR$(slot%)',
    ]


def autosave_source_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, t%",
        "FOR i% = 1 TO 5",
        "  t% = t% + i%",
        "NEXT i%",
        'IF t% <> 15 THEN ERROR "autosave sum"',
        'PRINT "PICO_AUTOSAVE_OK"',
    ]


def vm_program(drive: str, prefix: str) -> list[str]:
    vm_file = join_drive(drive, f"{prefix}_vm.txt")
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, total%, arr%(4), data1%, data2%, data3%",
        "DIM FLOAT x!",
        "DIM STRING a$, b$, s$",
        "total% = 0",
        "FOR i% = 1 TO 10",
        "  total% = total% + i% * i%",
        "NEXT i%",
        'IF total% <> 385 THEN ERROR "vm integer sum"',
        "FillArray arr%()",
        "total% = SumArray%(arr%())",
        "SELECT CASE total%",
        "  CASE 30",
        "  CASE ELSE",
        '    ERROR "vm select/array"',
        "END SELECT",
        's$ = "abcdef"',
        'IF LEFT$(s$, 2) <> "ab" THEN ERROR "vm left"',
        'IF MID$(s$, 3, 2) <> "cd" THEN ERROR "vm mid"',
        'IF RIGHT$(s$, 2) <> "ef" THEN ERROR "vm right"',
        "x! = 1.5",
        "x! = x! * 2.0 + 0.25",
        'IF ABS(x! - 3.25) > 0.001 THEN ERROR "vm float"',
        "READ data1%, data2%, data3%",
        "IF data1% + data2% + data3% <> 24 THEN ERROR \"vm data\"",
        "RESTORE",
        "READ data1%",
        "IF data1% <> 7 THEN ERROR \"vm restore\"",
        f'OPEN "{vm_file}" FOR OUTPUT AS #1',
        'PRINT #1, "vmfile"',
        "PRINT #1, total%",
        "CLOSE #1",
        f'OPEN "{vm_file}" FOR INPUT AS #1',
        "LINE INPUT #1, a$",
        "LINE INPUT #1, b$",
        "CLOSE #1",
        'IF a$ <> "vmfile" THEN ERROR "vm file text"',
        'IF VAL(b$) <> 30 THEN ERROR "vm file number"',
        f'KILL "{vm_file}"',
        'PRINT "PICO_VM_SMOKE_OK"',
        "END",
        "SUB FillArray(a%())",
        "  LOCAL INTEGER i%",
        "  FOR i% = 0 TO 4",
        "    a%(i%) = (i% + 1) * 2",
        "  NEXT i%",
        "END SUB",
        "FUNCTION SumArray%(a%())",
        "  LOCAL INTEGER i%, t%",
        "  FOR i% = 0 TO 4",
        "    t% = t% + a%(i%)",
        "  NEXT i%",
        "  SumArray% = t%",
        "END FUNCTION",
        "DATA 7,8,9",
    ]


def sieve_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER sieve%(1000)",
        "SUB ClearSieve(sieve%(), n%)",
        "  LOCAL INTEGER i%",
        "  i% = 0",
        "  DO WHILE i% <= n%",
        "    sieve%(i%) = 1",
        "    i% = i% + 1",
        "  LOOP",
        "END SUB",
        "SUB RunSieve(sieve%(), n%)",
        "  LOCAL INTEGER i%, j%, limit%",
        "  limit% = n%",
        "  i% = 2",
        "  DO WHILE i% * i% <= limit%",
        "    IF sieve%(i%) = 1 THEN",
        "      j% = i% * i%",
        "      '!FAST",
        "      DO WHILE j% <= limit%",
        "        sieve%(j%) = 0",
        "        j% = j% + i%",
        "      LOOP",
        "    ENDIF",
        "    i% = i% + 1",
        "  LOOP",
        "END SUB",
        "FUNCTION CountPrimes%(sieve%(), n%)",
        "  LOCAL INTEGER i%, count%",
        "  count% = 0",
        "  i% = 2",
        "  DO WHILE i% <= n%",
        "    IF sieve%(i%) = 1 THEN count% = count% + 1",
        "    i% = i% + 1",
        "  LOOP",
        "  CountPrimes% = count%",
        "END FUNCTION",
        "DIM INTEGER count%",
        "ClearSieve sieve%(), 1000",
        "RunSieve sieve%(), 1000",
        "count% = CountPrimes%(sieve%(), 1000)",
        'IF count% <> 168 THEN ERROR "sieve count"',
        'PRINT "PICO_SIEVE_OK " + STR$(count%)',
    ]


def timing_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER dt%, ticks%",
        "TIMER = 0",
        "PAUSE 30",
        "dt% = TIMER",
        'IF dt% < 20 THEN ERROR "pause/timer"',
        "SETTICK 10, TickSub, 1",
        "PAUSE 80",
        "SETTICK 0, 0, 1",
        'IF ticks% < 4 THEN ERROR "settick"',
        'PRINT "PICO_TIMING_SMOKE_OK"',
        "END",
        "SUB TickSub",
        "  ticks% = ticks% + 1",
        "END SUB",
    ]


def display_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "ON ERROR SKIP : FRAMEBUFFER CLOSE",
        "DIM INTEGER p%",
        "CLS RGB(BLACK)",
        "PIXEL 2, 2, RGB(RED)",
        "p% = PIXEL(2, 2)",
        'IF p% \\ 65536 < 180 THEN ERROR "pixel red r"',
        'IF (p% \\ 256) MOD 256 > 80 THEN ERROR "pixel red g"',
        'IF p% MOD 256 > 80 THEN ERROR "pixel red b"',
        "LINE 0, 0, 10, 10, 1, RGB(GREEN)",
        "BOX 12, 12, 8, 8, 1, RGB(BLUE), RGB(BLUE)",
        "FRAMEBUFFER CREATE",
        "FRAMEBUFFER WRITE F",
        "CLS RGB(BLACK)",
        "PIXEL 3, 3, RGB(GREEN)",
        "FRAMEBUFFER COPY F, N",
        "FRAMEBUFFER WRITE N",
        "p% = PIXEL(3, 3)",
        'IF p% \\ 65536 > 80 THEN ERROR "framebuffer copy r"',
        'IF (p% \\ 256) MOD 256 < 180 THEN ERROR "framebuffer copy g"',
        'IF p% MOD 256 > 80 THEN ERROR "framebuffer copy b"',
        "FRAMEBUFFER LAYER RGB(BLACK)",
        "FRAMEBUFFER WRITE L",
        "PIXEL 4, 4, RGB(BLUE)",
        "FRAMEBUFFER MERGE RGB(BLACK)",
        "FRAMEBUFFER CLOSE",
        "p% = PIXEL(4, 4)",
        'IF p% \\ 65536 > 80 THEN ERROR "framebuffer merge r"',
        'IF (p% \\ 256) MOD 256 > 80 THEN ERROR "framebuffer merge g"',
        'IF p% MOD 256 < 180 THEN ERROR "framebuffer merge b"',
        'PRINT "PICO_DISPLAY_SMOKE_OK"',
    ]


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "suites",
        nargs="*",
        default=[],
        metavar="{all,device,fs,errors,large,program,chain,flash,autosave,vm,sieve,timing,display,web,network}",
        help="suite(s) to run; default: all",
    )
    parser.add_argument("--port", default=default_port(), help="serial device path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--long-timeout", type=float, default=30.0)
    parser.add_argument("--drive", default="A:", help="MMBasic drive to test; default: A:")
    parser.add_argument("--prefix", default="pfs", help="temporary file/directory prefix")
    parser.add_argument("--flash-slot", type=int, default=3, help="flash program slot used by the flash suite")
    parser.add_argument("--keep-files", action="store_true", help="leave generated BASIC files on the drive")
    parser.add_argument("--reset-app", action="store_true", help="send Ctrl-C before syncing")
    parser.add_argument("--expect-psram", action="store_true", help="fail device smoke unless OPTION PSRAM is present")
    parser.add_argument("--connect-command", help="optional WEB CONNECT command for the web/network suites")
    parser.add_argument("--network-host", help="host/Mac IP passed to network_conformance.py --host")
    parser.add_argument("--device-host", help="device IP/hostname passed to network_conformance.py --device-host")
    return parser.parse_args(argv)


def expand_suites(suites: Sequence[str]) -> list[str]:
    allowed = {
        "all",
        "device",
        "fs",
        "errors",
        "large",
        "program",
        "chain",
        "flash",
        "autosave",
        "vm",
        "sieve",
        "timing",
        "display",
        "web",
        "network",
    }
    unknown = [suite for suite in suites if suite not in allowed]
    if unknown:
        raise ValueError(f"unknown suite(s): {', '.join(unknown)}")
    if not suites or "all" in suites:
        return [
            "device",
            "fs",
            "errors",
            "large",
            "program",
            "flash",
            "autosave",
            "vm",
            "sieve",
            "timing",
            "display",
            "web",
        ]
    return list(dict.fromkeys(suites))


def run_network_conformance(args: argparse.Namespace) -> int:
    script = Path(__file__).with_name("network_conformance.py")
    command = [
        sys.executable,
        str(script),
        "all",
        "--port",
        args.port,
        "--baud",
        str(args.baud),
        "--boot-wait",
        str(args.boot_wait),
        "--timeout",
        str(args.timeout),
        "--long-timeout",
        str(args.long_timeout),
    ]
    if args.connect_command:
        command.extend(["--connect-command", args.connect_command])
    if args.network_host:
        command.extend(["--host", args.network_host])
    if args.device_host:
        command.extend(["--device-host", args.device_host])
    print("=== network conformance ===", flush=True)
    return subprocess.call(command)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    suites = expand_suites(args.suites)
    serial_suites = [suite for suite in suites if suite != "network"]
    try:
        smoke: PicoSmoke | None = None
        if serial_suites:
            with BasicSerial(args.port, baud=args.baud) as basic:
                if args.reset_app:
                    basic.reset_app()
                basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
                smoke = PicoSmoke(
                    basic,
                    drive=args.drive,
                    prefix=args.prefix,
                    timeout=args.timeout,
                    long_timeout=args.long_timeout,
                    keep_files=args.keep_files,
                    expect_psram=args.expect_psram,
                    connect_command=args.connect_command,
                    flash_slot=args.flash_slot,
                )
                runners = {
                    "device": smoke.device_smoke,
                    "fs": smoke.fs_smoke,
                    "errors": smoke.error_smoke,
                    "large": smoke.large_file_smoke,
                    "program": smoke.program_lifecycle_smoke,
                    "chain": smoke.chain_smoke,
                    "flash": smoke.flash_smoke,
                    "autosave": smoke.autosave_smoke,
                    "vm": smoke.vm_smoke,
                    "sieve": smoke.sieve_smoke,
                    "timing": smoke.timing_smoke,
                    "display": smoke.display_smoke,
                    "web": smoke.web_smoke,
                }
                for suite in serial_suites:
                    runners[suite]()
                smoke.cleanup_generated_programs()
                failed = [check for check in smoke.checks if not check.ok]
                print("--- checks ---", flush=True)
                for check in smoke.checks:
                    state = "PASS" if check.ok else "FAIL"
                    suffix = f" - {check.detail}" if check.detail else ""
                    print(f"{state:4} {check.name}{suffix}", flush=True)
                if failed:
                    return 1
        if "network" in suites:
            return run_network_conformance(args)
        return 0
    except Exception as exc:
        print(f"FAIL pico smoke: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
