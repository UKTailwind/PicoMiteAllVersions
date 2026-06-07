#!/usr/bin/env python3
"""ESP32-S3 filesystem, BASIC, VM, flash, GPIO, PSRAM, and opt-in smoke suites."""

from __future__ import annotations

import argparse
import subprocess
import sys
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
    status: str
    detail: str = ""

    @property
    def ok(self) -> bool:
        return self.status in {"PASS", "SKIP"}


class Esp32Smoke:
    def __init__(
        self,
        basic: BasicSerial,
        *,
        drive: str,
        prefix: str,
        timeout: float,
        long_timeout: float,
        boot_wait: float,
        keep_files: bool,
        flash_slot: int,
        ws2812_visual: bool,
        var_save: bool,
        psram_bytes: int,
    ) -> None:
        self.basic = basic
        self.drive = normalise_drive(drive)
        self.prefix = prefix.strip("/") or "esmoke"
        self.timeout = timeout
        self.long_timeout = long_timeout
        self.boot_wait = boot_wait
        self.keep_files = keep_files
        self.flash_slot = flash_slot
        self.ws2812_visual = ws2812_visual
        self.var_save = var_save
        self.psram_bytes = psram_bytes
        self.checks: list[Check] = []

    def command(self, command: str, *, timeout: float | None = None, check_error: bool = True) -> str:
        return self.basic.command(
            command,
            timeout=self.timeout if timeout is None else timeout,
            check_error=check_error,
        ).clean_text

    def select_drive(self) -> None:
        self.command(self.drive, check_error=False)

    def note(self, name: str, status: str, detail: str = "") -> None:
        self.checks.append(Check(name, status, detail))
        suffix = f" - {detail}" if detail else ""
        print(f"{status:4} {name}{suffix}", flush=True)

    def pass_check(self, name: str, detail: str = "") -> None:
        self.note(name, "PASS", detail)

    def skip_check(self, name: str, detail: str = "") -> None:
        self.note(name, "SKIP", detail)

    def cleanup_temp_tree(self) -> None:
        root = join_drive(self.drive, self.prefix)
        self.select_drive()
        for command in (
            "ON ERROR SKIP : CLOSE #1",
            "ON ERROR SKIP : CLOSE #2",
            f'ON ERROR SKIP : CHDIR "{self.drive}/"',
            f'ON ERROR SKIP : KILL "{root}/nested/deep.txt"',
            f'ON ERROR SKIP : KILL "{root}/copy.txt"',
            f'ON ERROR SKIP : KILL "{root}/Mix Case.txt"',
            f'ON ERROR SKIP : KILL "{root}/loop.tmp"',
            f'ON ERROR SKIP : KILL "{root}/ren.txt"',
            f'ON ERROR SKIP : KILL "{root}/root.txt"',
            f'ON ERROR SKIP : KILL "{root}/app.txt"',
            f'ON ERROR SKIP : KILL "{root}/overwrite.txt"',
            f'ON ERROR SKIP : KILL "{root}/a.txt"',
            f'ON ERROR SKIP : KILL "{root}/b.txt"',
            f'ON ERROR SKIP : RMDIR "{root}/dupe"',
            f'ON ERROR SKIP : RMDIR "{root}/nested"',
            f'ON ERROR SKIP : RMDIR "{root}"',
        ):
            self.command(command, check_error=False)

    def program_paths(self) -> list[str]:
        return [
            join_drive(self.drive, f"{self.prefix}_fs.bas"),
            join_drive(self.drive, f"{self.prefix}_save_src.bas"),
            join_drive(self.drive, f"{self.prefix}_save_copy.bas"),
            join_drive(self.drive, f"{self.prefix}_empty.bas"),
            join_drive(self.drive, f"{self.prefix}_flash.bas"),
            join_drive(self.drive, f"{self.prefix}_bridge_dim.bas"),
            join_drive(self.drive, f"{self.prefix}_vm.bas"),
            join_drive(self.drive, f"{self.prefix}_sieve.bas"),
        ]

    def cleanup_generated_programs(self) -> None:
        if self.keep_files:
            return
        self.select_drive()
        for path in self.program_paths():
            self.command(f'ON ERROR SKIP : KILL "{path}"', check_error=False)

    def write_program(self, path: str, lines: Iterable[str]) -> None:
        self.select_drive()
        self.command(f'ON ERROR SKIP : KILL "{path}"', check_error=False)
        self.command(f'OPEN "{path}" FOR OUTPUT AS #1')
        try:
            for line in lines:
                self.command(f"PRINT #1,{basic_string_expr(line)}")
        finally:
            self.command("CLOSE #1", check_error=False)

    def run_program(self, command: str, marker: str, *, timeout: float | None = None) -> str:
        output = self.command(command, timeout=timeout or self.long_timeout)
        if marker not in output:
            raise RuntimeError(f"missing marker {marker!r} after {command!r}")
        return output

    def info_smoke(self) -> None:
        print("=== info ===", flush=True)
        outputs = [
            self.command('PRINT "ESP32_ID=" + MM.INFO$(ID)'),
            self.command('PRINT "ESP32_CPUSPEED=" + STR$(MM.INFO(CPUSPEED))'),
            self.command('PRINT "ESP32_HEAP=" + STR$(MM.INFO(HEAP))'),
            self.command('PRINT "ESP32_STACK=" + STR$(MM.INFO(STACK))'),
            self.command('PRINT "ESP32_FREE_SPACE=" + STR$(MM.INFO(FREE SPACE))'),
        ]
        self.pass_check("prompt and MM.INFO", "ID, CPUSPEED, HEAP, STACK, FREE SPACE")
        for line in "\n".join(outputs).splitlines():
            if line.startswith("ESP32_"):
                print(f"     {line}", flush=True)
        self.detect_b_drive()

    def detect_b_drive(self) -> None:
        listing = self.basic.command_with_pagination(
            'FILES "B:"',
            timeout=self.long_timeout,
            check_error=False,
        ).clean_text
        lower = listing.lower()
        if "error" in lower or "not configured" in lower or "no sd" in lower or "mount" in lower:
            detail = "B: absent or unconfigured; SD card is not required"
            first_error = next((line.strip() for line in listing.splitlines() if "Error" in line), "")
            self.skip_check("B:/SD detection", first_error or detail)
        else:
            self.pass_check("B:/SD detection", "B: listed without requiring an expected card file")

    def fs_smoke(self) -> None:
        print("=== filesystem ===", flush=True)
        self.select_drive()
        self.cleanup_temp_tree()
        path = join_drive(self.drive, f"{self.prefix}_fs.bas")
        self.write_program(path, fs_program(self.drive, self.prefix))
        self.run_program(f'RUN "{path}"', "ESP32_FS_SMOKE_OK", timeout=self.long_timeout)
        self.pass_check(
            f"{self.drive} filesystem and BASIC file I/O",
            "mkdir/chdir/open/read/write/append/copy/rename/dir/kill/rmdir/errors",
        )

    def program_smoke(self) -> None:
        print("=== program lifecycle ===", flush=True)
        self.select_drive()
        src = join_drive(self.drive, f"{self.prefix}_save_src.bas")
        dst = join_drive(self.drive, f"{self.prefix}_save_copy.bas")
        empty = join_drive(self.drive, f"{self.prefix}_empty.bas")
        self.write_program(src, save_load_source_program())
        self.command(f'ON ERROR SKIP : KILL "{dst}"', check_error=False)
        self.command(f'ON ERROR SKIP : KILL "{empty}"', check_error=False)
        self.command(f'LOAD "{src}"', timeout=self.long_timeout)
        self.command(f'SAVE "{dst}"', timeout=self.long_timeout)
        self.command("NEW")
        empty_result = self.command(f'SAVE "{empty}"', timeout=self.long_timeout, check_error=False)
        if "Error" not in empty_result:
            raise RuntimeError("empty-program SAVE did not report an error")
        recovered = self.command('PRINT "ESP32_SAVE_ERROR_RECOVERED"')
        if "ESP32_SAVE_ERROR_RECOVERED" not in recovered:
            raise RuntimeError("prompt did not recover after empty-program SAVE")
        self.command(f'LOAD "{dst}"', timeout=self.long_timeout)
        self.run_program("RUN", "ESP32_SAVE_LOAD_OK", timeout=self.long_timeout)
        autorun = self.command(f'LOAD "{dst}", R', timeout=self.long_timeout)
        if "ESP32_SAVE_LOAD_OK" not in autorun:
            raise RuntimeError(
                "LOAD ...,R returned to the prompt without autorun; ESP32 firmware "
                "needs cmd_load_post_cleanup/autorun handling aligned with device LOAD semantics"
            )
        self.run_program(f'FRUN "{dst}"', "ESP32_SAVE_LOAD_OK", timeout=self.long_timeout)
        self.pass_check("LOAD/SAVE/RUN/FRUN", "round-trip, autorun load, empty-save refusal")

    def vm_smoke(self) -> None:
        print("=== bytecode vm ===", flush=True)
        self.select_drive()
        bridge_dim_path = join_drive(self.drive, f"{self.prefix}_bridge_dim.bas")
        self.write_program(bridge_dim_path, bridged_dim_regression_program("ESP32_BRIDGE_DIM_OK"))
        self.command("NEW")
        self.run_program(f'FRUN "{bridge_dim_path}"', "ESP32_BRIDGE_DIM_OK", timeout=self.long_timeout)
        self.pass_check(
            "FRUN bridged DIM regression",
            "mixed scalar/array DIM does not predeclare compile-known globals",
        )

        path = join_drive(self.drive, f"{self.prefix}_vm.bas")
        vm_temp = join_drive(self.drive, f"{self.prefix}_vm.txt")
        self.command(f'ON ERROR SKIP : KILL "{vm_temp}"', check_error=False)
        self.write_program(path, vm_program(self.drive, self.prefix))
        self.command("NEW")
        self.run_program(f'FRUN "{path}"', "ESP32_VM_SMOKE_OK", timeout=self.long_timeout)
        self.pass_check("FRUN bytecode VM", "arithmetic, strings, arrays, SUB/FUNCTION, SELECT, DATA, file I/O")

        sieve_path = join_drive(self.drive, f"{self.prefix}_sieve.bas")
        self.write_program(sieve_path, sieve_program())
        self.command("NEW")
        self.run_program(f'FRUN "{sieve_path}"', "ESP32_SIEVE_OK 168", timeout=self.long_timeout)
        self.pass_check("FRUN sieve/math benchmark", "168 primes <= 1000")

    def flash_smoke(self) -> None:
        print("=== flash ===", flush=True)
        self.select_drive()
        path = join_drive(self.drive, f"{self.prefix}_flash.bas")
        self.write_program(path, flash_source_program(self.flash_slot))
        self.command(f'LOAD "{path}"', timeout=self.long_timeout)
        self.command(f"FLASH ERASE {self.flash_slot}", timeout=self.long_timeout, check_error=False)
        self.command(f"FLASH SAVE {self.flash_slot}", timeout=self.long_timeout)
        listing = self.command(f"FLASH LIST {self.flash_slot}, ALL", timeout=self.long_timeout)
        if "ESP32_FLASH_SLOT_OK" not in listing:
            raise RuntimeError(f"FLASH LIST {self.flash_slot} did not show saved program")
        self.reset_and_resync()
        self.command(f"FLASH LOAD {self.flash_slot}", timeout=self.long_timeout)
        self.run_program("RUN", "ESP32_FLASH_SLOT_OK", timeout=self.long_timeout)
        self.run_program(f"FLASH RUN {self.flash_slot}", "ESP32_FLASH_SLOT_OK", timeout=self.long_timeout)
        self.command(f"FLASH ERASE {self.flash_slot}", timeout=self.long_timeout)
        self.pass_check("FLASH SAVE/reset/LOAD/RUN", f"slot {self.flash_slot}")

        if self.var_save:
            self.command("NEW")
            self.command("DIM INTEGER ESP32SMKVAR%")
            self.command("ESP32SMKVAR%=31415")
            self.command("VAR SAVE ESP32SMKVAR%", timeout=self.long_timeout)
            self.reset_and_resync()
            self.command("NEW")
            self.command("VAR RESTORE", timeout=self.long_timeout)
            restored = self.command('PRINT "ESP32_VAR_SAVE=" + STR$(ESP32SMKVAR%)')
            if "ESP32_VAR_SAVE=31415" not in restored:
                raise RuntimeError("VAR SAVE/RESTORE did not recover ESP32SMKVAR%")
            self.pass_check("VAR SAVE/RESTORE", "persistent variable restored after reset")
        else:
            self.skip_check("VAR SAVE/RESTORE", "pass --var-save to add a persistent saved variable")

    def reset_and_resync(self) -> None:
        self.basic.reset_app()
        self.basic.sync(timeout=self.long_timeout, boot_wait=max(self.boot_wait, 1.0))

    def find_setpin_candidate(self, mode: str, candidates: Sequence[str], options: str = "") -> str:
        suffix = f", {options}" if options else ""
        last = ""
        for pin in candidates:
            self.command(f"SETPIN {pin}, OFF", check_error=False)
            result = self.command(f"SETPIN {pin}, {mode}{suffix}", check_error=False)
            last = result
            if "Error :" not in result and "Error:" not in result:
                return pin
        detail = " ".join(line.strip() for line in last.splitlines() if line.strip())
        raise RuntimeError(f"no usable GPIO candidate for SETPIN {mode}; last={detail}")

    def gpio_smoke(self) -> None:
        print("=== gpio ===", flush=True)
        safe_digital = ("GP2", "GP3", "GP9", "GP14", "GP21", "GP35", "GP36", "GP37")
        safe_analog = ("GP2", "GP3", "GP9", "GP14")
        dout_pin = self.find_setpin_candidate("DOUT", safe_digital)
        self.command(f"PIN({dout_pin})=1")
        high = self.command(f'PRINT "ESP32_DOUT_HIGH=" + STR$(PIN({dout_pin}))')
        self.command(f"PIN({dout_pin})=0")
        low = self.command(f'PRINT "ESP32_DOUT_LOW=" + STR$(PIN({dout_pin}))')
        if marker_int(high, "ESP32_DOUT_HIGH=") != 1 or marker_int(low, "ESP32_DOUT_LOW=") != 0:
            raise RuntimeError(f"{dout_pin} DOUT latch readback failed")
        din_pin = self.find_setpin_candidate("DIN", safe_digital, "PULLUP")
        din = self.command(f'PRINT "ESP32_DIN=" + STR$(PIN({din_pin}))')
        if "ESP32_DIN=" not in din:
            raise RuntimeError(f"{din_pin} DIN readback did not produce a marker")
        araw_pin = self.find_setpin_candidate("ARAW", safe_analog)
        araw = self.command(f'PRINT "ESP32_ARAW=" + STR$(PIN({araw_pin}))')
        value = marker_int(araw, "ESP32_ARAW=")
        if value < 0 or value > 65535:
            raise RuntimeError(f"{araw_pin} ARAW read out of range: {value}")
        self.command(f"SETPIN {dout_pin}, OFF", check_error=False)
        pwm = self.command(f"SETPIN {dout_pin}, PWM", check_error=False)
        if "PWM not supported on this port yet" not in pwm:
            raise RuntimeError("SETPIN PWM did not report the expected unsupported error")
        servo = self.command("SERVO 0, 50", check_error=False)
        if "Servo not supported on this port yet" not in servo:
            raise RuntimeError("SERVO did not report the expected unsupported error")
        self.command(f"SETPIN {dout_pin}, OFF", check_error=False)
        self.command(f"SETPIN {din_pin}, OFF", check_error=False)
        self.command(f"SETPIN {araw_pin}, OFF", check_error=False)
        self.pass_check("GPIO DOUT/DIN/ARAW and unsupported PWM/SERVO",
                        f"{dout_pin}, {din_pin}, {araw_pin}")

    def ws2812_smoke(self) -> None:
        print("=== ws2812 ===", flush=True)
        if not self.ws2812_visual:
            self.skip_check("WS2812 GP46 visual sequence", "pass --ws2812-visual to opt in")
            return
        for colour in ("&HFF0000", "&H00FF00", "&H0000FF", "&H000000"):
            self.command(f"WS2812 B, GP46, 1, {colour}", timeout=self.long_timeout)
            self.command("PAUSE 150", timeout=self.long_timeout)
        self.pass_check("WS2812 GP46 visual sequence", "red/green/blue/off commands accepted")

    def psram_smoke(self) -> None:
        print("=== psram ===", flush=True)
        psram_size_resp = self.command('PRINT "ESP32_PSRAM_SIZE=" + STR$(MM.INFO(PSRAM SIZE))')
        psram_size = marker_int(psram_size_resp, "ESP32_PSRAM_SIZE=")
        if psram_size <= 0:
            raise RuntimeError(f"PSRAM SIZE unexpectedly reports {psram_size}")
        # RAM TEST takes the test span in megabytes; round --psram-bytes up.
        mb = max(1, (self.psram_bytes + (1024 * 1024) - 1) // (1024 * 1024))
        max_mb = psram_size // (1024 * 1024)
        if mb > max_mb:
            raise RuntimeError(
                f"requested PSRAM smoke march {mb} MB exceeds PSRAM size {max_mb} MB"
            )
        marched = self.command(f"RAM TEST {mb}", timeout=self.long_timeout)
        if "RAM TEST OK" not in marched:
            raise RuntimeError(f"RAM TEST {mb} did not report OK; got:\n{marched}")
        prompt = self.command('PRINT "ESP32_PSRAM_PROMPT_OK"')
        if "ESP32_PSRAM_PROMPT_OK" not in prompt:
            raise RuntimeError("BASIC prompt did not recover after RAM TEST")
        self.pass_check(
            "ESP32 PSRAM RAM TEST march",
            f"PSRAM SIZE={psram_size} marched_mb={mb}",
        )


def marker_int(text: str, marker: str) -> int:
    for line in text.splitlines():
        if line.startswith(marker):
            return int(float(line[len(marker) :].strip()))
    raise RuntimeError(f"missing integer marker {marker!r}")


def fs_program(drive: str, prefix: str) -> list[str]:
    root = join_drive(drive, prefix)
    return [
        "OPTION EXPLICIT",
        drive,
        "ON ERROR SKIP : CLOSE #1",
        "ON ERROR SKIP : CLOSE #2",
        f'ON ERROR SKIP : CHDIR "{drive}/"',
        f'ON ERROR SKIP : KILL "{root}/nested/deep.txt"',
        f'ON ERROR SKIP : KILL "{root}/copy.txt"',
        f'ON ERROR SKIP : KILL "{root}/Mix Case.txt"',
        f'ON ERROR SKIP : KILL "{root}/loop.tmp"',
        f'ON ERROR SKIP : KILL "{root}/ren.txt"',
        f'ON ERROR SKIP : KILL "{root}/root.txt"',
        f'ON ERROR SKIP : KILL "{root}/app.txt"',
        f'ON ERROR SKIP : KILL "{root}/overwrite.txt"',
        f'ON ERROR SKIP : KILL "{root}/a.txt"',
        f'ON ERROR SKIP : KILL "{root}/b.txt"',
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
        f'IF MM.INFO(EXISTS FILE "{root}/root.txt") = 0 THEN ERROR "exists file"',
        f'IF MM.INFO(FILESIZE "{root}/root.txt") <= 0 THEN ERROR "filesize"',
        'OPEN "root.txt" FOR INPUT AS #1',
        'IF LOF(#1) <= 0 THEN ERROR "lof"',
        'IF LOC(#1) <> 1 THEN ERROR "loc start"',
        "DIM STRING a$, b$, c$, n$",
        "DIM INTEGER tc%, i%",
        "a$ = INPUT$(5, #1)",
        'IF a$ <> "alpha" THEN ERROR "input$"',
        'IF LOC(#1) <> 6 THEN ERROR "loc after input"',
        "SEEK #1, 1",
        "LINE INPUT #1, a$",
        "LINE INPUT #1, b$",
        "LINE INPUT #1, c$",
        'IF a$ <> "alpha" THEN ERROR "read alpha"',
        'IF VAL(b$) <> 42 THEN ERROR "read number"',
        'IF c$ <> "omega" THEN ERROR "read omega"',
        'IF EOF(#1) = 0 THEN ERROR "eof"',
        "CLOSE #1",
        'OPEN "app.txt" FOR APPEND AS #1',
        'PRINT #1, "first"',
        "CLOSE #1",
        'OPEN "app.txt" FOR APPEND AS #1',
        'PRINT #1, "second"',
        "CLOSE #1",
        'OPEN "app.txt" FOR INPUT AS #1',
        "LINE INPUT #1, a$",
        "LINE INPUT #1, b$",
        "CLOSE #1",
        'IF a$ <> "first" THEN ERROR "append first"',
        'IF b$ <> "second" THEN ERROR "append second"',
        'OPEN "overwrite.txt" FOR OUTPUT AS #1',
        'PRINT #1, "first version"',
        "CLOSE #1",
        'OPEN "overwrite.txt" FOR OUTPUT AS #1',
        'PRINT #1, "second"',
        "CLOSE #1",
        'OPEN "overwrite.txt" FOR INPUT AS #1',
        "LINE INPUT #1, a$",
        "CLOSE #1",
        'IF a$ <> "second" THEN ERROR "overwrite output"',
        'COPY "root.txt" TO "copy.txt"',
        'OPEN "copy.txt" FOR INPUT AS #1',
        "LINE INPUT #1, a$",
        "CLOSE #1",
        'IF a$ <> "alpha" THEN ERROR "copy content"',
        'RENAME "copy.txt" AS "ren.txt"',
        'IF DIR$("ren.txt", FILE) <> "ren.txt" THEN ERROR "rename missing"',
        'IF LEN(DIR$("copy.txt", FILE)) <> 0 THEN ERROR "rename old exists"',
        'OPEN "Mix Case.txt" FOR OUTPUT AS #1',
        'PRINT #1, "space-name"',
        "CLOSE #1",
        'IF DIR$("Mix Case.txt", FILE) <> "Mix Case.txt" THEN ERROR "space filename"',
        "FOR i% = 1 TO 20",
        '  OPEN "loop.tmp" FOR OUTPUT AS #1',
        "  PRINT #1, i%",
        "  CLOSE #1",
        '  KILL "loop.tmp"',
        "NEXT i%",
        'MKDIR "nested"',
        f'IF MM.INFO(EXISTS DIR "{root}/nested") = 0 THEN ERROR "exists dir"',
        'IF DIR$("nested", DIR) <> "nested" THEN ERROR "dir missing"',
        'CHDIR "nested"',
        'OPEN "deep.txt" FOR OUTPUT AS #1',
        'PRINT #1, "deep"',
        "CLOSE #1",
        'IF DIR$("deep.txt", FILE) <> "deep.txt" THEN ERROR "nested file"',
        'CHDIR ".."',
        'n$ = DIR$("*.txt", FILE)',
        "tc% = 0",
        "DO WHILE LEN(n$) > 0",
        "  tc% = tc% + 1",
        "  n$ = DIR$()",
        "LOOP",
        'IF tc% < 4 THEN ERROR "wildcard count"',
        "ON ERROR SKIP",
        'OPEN "missing.nope" FOR INPUT AS #1',
        'IF MM.ERRNO = 0 THEN ERROR "missing open did not fail"',
        "ON ERROR CLEAR",
        'MKDIR "dupe"',
        "ON ERROR SKIP",
        'MKDIR "dupe"',
        'IF MM.ERRNO = 0 THEN ERROR "duplicate mkdir did not fail"',
        "ON ERROR CLEAR",
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
        "  LINE INPUT #1, a$",
        "  CLOSE #1",
        '  IF a$ <> "from-a" THEN ERROR "rename overwrite content"',
        "ELSE",
        '  OPEN "b.txt" FOR INPUT AS #1',
        "  LINE INPUT #1, a$",
        "  CLOSE #1",
        '  IF a$ <> "from-b" THEN ERROR "rename collision content"',
        '  KILL "a.txt"',
        "ENDIF",
        "ON ERROR CLEAR",
        'KILL "b.txt"',
        'KILL "ren.txt"',
        'KILL "Mix Case.txt"',
        'KILL "root.txt"',
        'KILL "app.txt"',
        'KILL "overwrite.txt"',
        'RMDIR "dupe"',
        'KILL "nested/deep.txt"',
        'RMDIR "nested"',
        f'CHDIR "{drive}/"',
        f'RMDIR "{prefix}"',
        'PRINT "ESP32_FS_SMOKE_OK"',
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
        'IF total% <> 12 THEN ERROR "data sum"',
        'PRINT "ESP32_SAVE_LOAD_OK"',
        "END",
        "FUNCTION Add3%(x%, y%, z%)",
        "  Add3% = x% + y% + z%",
        "END FUNCTION",
        "DATA 3,4,5",
    ]


def flash_source_program(slot: int) -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER slot%",
        f"slot% = {slot}",
        'PRINT "ESP32_FLASH_SLOT_OK " + STR$(slot%)',
    ]


def bridged_dim_regression_program(marker: str) -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, total%, arr%(4), doubled%",
        "FOR i% = 0 TO 4: arr%(i%) = i% + 1: NEXT i%",
        "FOR i% = 0 TO 4: total% = total% + arr%(i%): NEXT i%",
        "doubled% = total% * 2",
        'IF doubled% <> 30 THEN ERROR "bridged dim total"',
        f'PRINT "{marker}"',
    ]


def vm_program(drive: str, prefix: str) -> list[str]:
    vm_file = join_drive(drive, f"{prefix}_vm.txt")
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER i%, total%, arr%(4), data1%, data2%, data3%",
        "DIM FLOAT x!",
        "DIM STRING a$, b$",
        "FOR i% = 1 TO 10: total% = total% + i% * i%: NEXT i%",
        'IF total% <> 385 THEN ERROR "vm integer sum"',
        "FillArray arr%()",
        "total% = SumArray%(arr%())",
        'SELECT CASE total%: CASE 30: CASE ELSE: ERROR "vm select/array": END SELECT',
        'a$ = "abcdef"',
        'IF LEFT$(a$, 2) <> "ab" THEN ERROR "vm left"',
        'IF MID$(a$, 3, 2) <> "cd" THEN ERROR "vm mid"',
        'IF RIGHT$(a$, 2) <> "ef" THEN ERROR "vm right"',
        "x! = 1.5: x! = x! * 2.0 + 0.25",
        'IF ABS(x! - 3.25) > 0.001 THEN ERROR "vm float"',
        "READ data1%, data2%, data3%",
        'IF data1% + data2% + data3% <> 24 THEN ERROR "vm data"',
        "RESTORE: READ data1%",
        'IF data1% <> 7 THEN ERROR "vm restore"',
        f'OPEN "{vm_file}" FOR OUTPUT AS #1: PRINT #1, "vmfile": PRINT #1, total%: CLOSE #1',
        f'OPEN "{vm_file}" FOR INPUT AS #1: LINE INPUT #1, a$: LINE INPUT #1, b$: CLOSE #1',
        'IF a$ <> "vmfile" THEN ERROR "vm file text"',
        'IF VAL(b$) <> 30 THEN ERROR "vm file number"',
        f'KILL "{vm_file}"',
        'PRINT "ESP32_VM_SMOKE_OK"',
        "END",
        "SUB FillArray(a%())",
        "  LOCAL INTEGER i%",
        "  FOR i% = 0 TO 4: a%(i%) = (i% + 1) * 2: NEXT i%",
        "END SUB",
        "FUNCTION SumArray%(a%())",
        "  LOCAL INTEGER i%, t%",
        "  FOR i% = 0 TO 4: t% = t% + a%(i%): NEXT i%",
        "  SumArray% = t%",
        "END FUNCTION",
        "DATA 7,8,9",
    ]


def sieve_program() -> list[str]:
    return [
        "OPTION EXPLICIT",
        "DIM INTEGER sieve%(1000)",
        "DIM INTEGER i%, j%, count%",
        "FOR i% = 0 TO 1000: sieve%(i%) = 1: NEXT i%",
        "sieve%(0) = 0: sieve%(1) = 0",
        "FOR i% = 2 TO 31",
        "  IF sieve%(i%) THEN",
        "    FOR j% = i% * i% TO 1000 STEP i%: sieve%(j%) = 0: NEXT j%",
        "  ENDIF",
        "NEXT i%",
        "FOR i% = 2 TO 1000: IF sieve%(i%) THEN count% = count% + 1",
        "NEXT i%",
        'IF count% <> 168 THEN ERROR "sieve count"',
        'PRINT "ESP32_SIEVE_OK " + STR$(count%)',
    ]


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "suites",
        nargs="*",
        default=[],
        metavar="{all,info,fs,program,vm,flash,gpio,ws2812,psram,network}",
        help="suite(s) to run; default: all",
    )
    parser.add_argument("--port", default=default_port(), help="serial device path")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--long-timeout", type=float, default=30.0)
    parser.add_argument("--drive", default="A:", help="MMBasic drive to test; default: A:")
    parser.add_argument("--prefix", default="efs", help="temporary file/directory prefix")
    parser.add_argument("--flash-slot", type=int, default=3, help="flash program slot used by the flash suite")
    parser.add_argument("--keep-files", action="store_true", help="leave generated BASIC files on the drive")
    parser.add_argument("--reset-app", action="store_true", help="pulse RTS before syncing")
    parser.add_argument("--ws2812-visual", action="store_true", help="run GP46 red/green/blue/off visual sequence")
    parser.add_argument("--var-save", action="store_true", help="include persistent VAR SAVE/RESTORE in flash suite")
    parser.add_argument(
        "--psram-bytes",
        type=int,
        default=4 * 1024 * 1024,
        help="SPIRAM allocation size for the psram suite; default: 4194304",
    )
    parser.add_argument("--run-network", action="store_true", help="run network_conformance.py for the network suite")
    parser.add_argument(
        "--network-suite",
        default="all",
        choices=("all", "tcp-client", "tcp-server", "udp", "tftp", "telnet", "ntp", "mqtt"),
        help="network_conformance.py suite to run when --run-network is passed; default: all",
    )
    parser.add_argument("--connect-command", help="optional WEB CONNECT command passed to network_conformance.py")
    parser.add_argument("--network-host", help="host/Mac IP passed to network_conformance.py --host")
    parser.add_argument("--device-host", help="device IP/hostname passed to network_conformance.py --device-host")
    parser.add_argument("--network-suite-retries", type=int, help="network_conformance.py --suite-retries")
    parser.add_argument("--network-suite-timeout", type=float, help="network_conformance.py --suite-timeout")
    return parser.parse_args(argv)


def expand_suites(suites: Sequence[str]) -> list[str]:
    allowed = {"all", "info", "fs", "program", "vm", "flash", "gpio", "ws2812", "psram", "network"}
    unknown = [suite for suite in suites if suite not in allowed]
    if unknown:
        raise ValueError(f"unknown suite(s): {', '.join(unknown)}")
    if not suites or "all" in suites:
        return ["info", "fs", "program", "vm", "gpio"]
    return list(dict.fromkeys(suites))


def run_network_conformance(args: argparse.Namespace, checks: list[Check]) -> int:
    if not args.run_network:
        checks.append(Check("network conformance", "SKIP", "pass --run-network and WiFi options/credentials to opt in"))
        print("SKIP network conformance - pass --run-network and WiFi options/credentials to opt in", flush=True)
        return 0

    script = Path(__file__).with_name("network_conformance.py")
    command = [
        sys.executable,
        str(script),
        args.network_suite,
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
    if args.network_suite_retries:
        command.extend(["--suite-retries", str(args.network_suite_retries)])
    if args.network_suite_timeout:
        command.extend(["--suite-timeout", str(args.network_suite_timeout)])
    print("=== network conformance ===", flush=True)
    print(">>> " + " ".join(command), flush=True)
    result = subprocess.call(command)
    checks.append(
        Check(
            "network conformance",
            "PASS" if result == 0 else "FAIL",
            f"network_conformance.py {args.network_suite}",
        )
    )
    return result


def print_summary(checks: Sequence[Check]) -> None:
    print("--- summary ---", flush=True)
    for check in checks:
        suffix = f" - {check.detail}" if check.detail else ""
        print(f"{check.status:4} {check.name}{suffix}", flush=True)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        suites = expand_suites(args.suites)
    except ValueError as exc:
        print(f"FAIL esp32 smoke: {exc}", file=sys.stderr)
        return 2

    serial_suites = [suite for suite in suites if suite != "network"]
    all_checks: list[Check] = []
    try:
        smoke: Esp32Smoke | None = None
        if serial_suites:
            with BasicSerial(args.port, baud=args.baud) as basic:
                if args.reset_app:
                    basic.reset_app()
                basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
                smoke = Esp32Smoke(
                    basic,
                    drive=args.drive,
                    prefix=args.prefix,
                    timeout=args.timeout,
                    long_timeout=args.long_timeout,
                    boot_wait=args.boot_wait,
                    keep_files=args.keep_files,
                    flash_slot=args.flash_slot,
                    ws2812_visual=args.ws2812_visual,
                    var_save=args.var_save,
                    psram_bytes=args.psram_bytes,
                )
                runners = {
                    "info": smoke.info_smoke,
                    "fs": smoke.fs_smoke,
                    "program": smoke.program_smoke,
                    "vm": smoke.vm_smoke,
                    "flash": smoke.flash_smoke,
                    "gpio": smoke.gpio_smoke,
                    "ws2812": smoke.ws2812_smoke,
                    "psram": smoke.psram_smoke,
                }
                for suite in serial_suites:
                    runners[suite]()
                smoke.cleanup_generated_programs()
                all_checks.extend(smoke.checks)
        if "network" in suites:
            result = run_network_conformance(args, all_checks)
            if result:
                print_summary(all_checks)
                return result
        print_summary(all_checks)
        return 0 if all(check.ok for check in all_checks) else 1
    except TimeoutError as exc:
        if smoke is not None:
            all_checks.extend(smoke.checks)
        all_checks.append(Check("serial prompt", "FAIL", str(exc)))
        print_summary(all_checks)
        print(f"FAIL esp32 smoke: prompt timeout; stopped on-device attempt: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        if smoke is not None:
            all_checks.extend(smoke.checks)
        all_checks.append(Check("esp32 smoke", "FAIL", str(exc)))
        print_summary(all_checks)
        print(f"FAIL esp32 smoke: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
