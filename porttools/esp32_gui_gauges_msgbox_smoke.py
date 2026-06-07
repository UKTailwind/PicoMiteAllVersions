#!/usr/bin/env python3
"""ESP32-S3 GUI gauges/area/msgbox smoke.

Uploads two BASIC programs over serial or telnet:

* gui_gauges_msgbox_auto.bas creates GAUGE, BARGAUGE, and AREA controls,
  repeatedly updates the visible gauges, verifies CTRLVAL state, probes
  rendered pixels, and checks prompt recovery under RUN and FRUN.
* gui_gauges_msgbox_touch.bas is a manual on-device target for AREA touch
  hit testing and MSGBOX button completion.

Run from the repo root:
    python3.11 porttools/esp32_gui_gauges_msgbox_smoke.py --target /dev/cu.usbmodem2101
    python3.11 porttools/esp32_gui_gauges_msgbox_smoke.py --target telnet:192.168.5.140
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from typing import Any, Iterable

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_telnet import open_transport  # noqa: E402


def normalise_drive(drive: str) -> str:
    drive = drive.strip().upper()
    if not drive:
        raise ValueError("drive must not be empty")
    if drive.endswith("/"):
        drive = drive[:-1]
    if not drive.endswith(":"):
        drive += ":"
    return drive


def join_drive(drive: str, name: str) -> str:
    return f"{normalise_drive(drive)}/{name.lstrip('/')}"


@dataclass
class Check:
    name: str
    ok: bool
    detail: str = ""


class GuiGaugeSmoke:
    def __init__(
        self,
        basic: Any,
        *,
        drive: str,
        timeout: float,
        long_timeout: float,
        keep_files: bool,
    ) -> None:
        self.basic = basic
        self.drive = normalise_drive(drive)
        self.timeout = timeout
        self.long_timeout = long_timeout
        self.keep_files = keep_files
        self.checks: list[Check] = []
        self.auto_path = join_drive(self.drive, "gui_gauges_msgbox_auto.bas")
        self.touch_path = join_drive(self.drive, "gui_gauges_msgbox_touch.bas")

    def command(self, command: str, *, timeout: float | None = None, check_error: bool = True) -> str:
        return self.basic.command(
            command,
            timeout=self.timeout if timeout is None else timeout,
            check_error=check_error,
        ).clean_text

    def note(self, name: str, ok: bool, detail: str = "") -> None:
        self.checks.append(Check(name, ok, detail))
        state = "PASS" if ok else "FAIL"
        suffix = f" - {detail}" if detail else ""
        print(f"{state:4} {name}{suffix}", flush=True)

    def ensure_gui_controls(self, minimum: int = 16) -> None:
        options = self.command("OPTION LIST", timeout=self.long_timeout)
        match = re.search(r"OPTION GUI CONTROLS\s+(\d+)", options, re.IGNORECASE)
        configured = int(match.group(1)) if match else 0
        if configured >= minimum:
            self.note("OPTION GUI CONTROLS", True, f"{configured} controls configured")
            return

        self.command(f"OPTION GUI CONTROLS {minimum}", timeout=self.long_timeout, check_error=False)
        self.basic.sync(timeout=self.long_timeout, boot_wait=1.0)
        options = self.command("OPTION LIST", timeout=self.long_timeout)
        match = re.search(r"OPTION GUI CONTROLS\s+(\d+)", options, re.IGNORECASE)
        configured = int(match.group(1)) if match else 0
        if configured < minimum:
            raise RuntimeError(f"OPTION GUI CONTROLS stayed at {configured}, expected >= {minimum}")
        self.note("OPTION GUI CONTROLS", True, f"set to {configured}")

    def write_program(self, path: str, lines: Iterable[str]) -> None:
        serial = self.basic.serial
        assert serial is not None

        self.command("ON ERROR SKIP : CLOSE #1", check_error=False)
        self.command(f'ON ERROR SKIP : KILL "{path}"', check_error=False)
        self.command("NEW")

        source_lines = list(lines)
        serial.reset_input_buffer()
        serial.write(b"AUTOSAVE N\r")
        serial.flush()
        self.basic.read_for(0.5)

        for line in source_lines:
            serial.write(line.encode("latin1") + b"\r")
            serial.flush()
            self.basic.read_for(0.015)
        serial.write(b"\x1a")
        serial.flush()
        raw = self.basic.wait_for_prompt(self.long_timeout)
        if not self.basic._has_prompt(raw):
            tail = raw[-300:].decode("latin1", "replace")
            raise TimeoutError(f"timeout waiting for prompt after AUTOSAVE; tail={tail!r}")

        self.command(f'SAVE "{path}"', timeout=self.long_timeout)
        self.command("NEW")

    def upload_programs(self) -> None:
        self.write_program(self.auto_path, auto_program())
        self.write_program(self.touch_path, touch_program())
        self.note("program upload", True, f"{self.auto_path}, {self.touch_path}")

    def run_program(self, run_command: str, marker: str) -> str:
        output = self.command(run_command, timeout=self.long_timeout)
        if marker not in output:
            raise RuntimeError(f"missing marker {marker!r} after {run_command!r}\n{output[-900:]}")
        if "GUI_PHASE5_FAIL" in output:
            raise RuntimeError(f"program reported failure after {run_command!r}\n{output[-1100:]}")
        return output

    def automated(self) -> None:
        run_output = self.run_program(f'RUN "{self.auto_path}"', "GUI_PHASE5_AUTO_OK")
        self.note("RUN gui_gauges_msgbox_auto", True, summarise_pixels(run_output))
        frun_output = self.run_program(f'FRUN "{self.auto_path}"', "GUI_PHASE5_AUTO_OK")
        self.note("FRUN gui_gauges_msgbox_auto", True, summarise_pixels(frun_output))

    def console_probe(self) -> None:
        output = self.command('PRINT "GUI_PHASE5_CONSOLE=";"ready"', timeout=self.timeout)
        if "GUI_PHASE5_CONSOLE=ready" not in output:
            raise RuntimeError(f"missing console probe marker\n{output[-300:]}")
        self.note("console path after GUI", True, "command echo and prompt recovered")

    def cleanup(self) -> None:
        self.command("ON ERROR SKIP : GUI DELETE ALL", check_error=False)
        self.command("ON ERROR SKIP : CLS", check_error=False)
        if not self.keep_files:
            self.command(f'ON ERROR SKIP : KILL "{self.auto_path}"', check_error=False)
            self.command(f'ON ERROR SKIP : KILL "{self.touch_path}"', check_error=False)

    def run(self) -> int:
        self.ensure_gui_controls()
        self.upload_programs()
        self.automated()
        self.console_probe()
        self.cleanup()
        passed = sum(1 for c in self.checks if c.ok)
        total = len(self.checks)
        print(f"\n{passed}/{total} passed")
        if self.keep_files:
            print(f"Manual AREA/MSGBOX validation program: RUN \"{self.touch_path}\"")
        else:
            print("Manual AREA/MSGBOX validation program removed by --cleanup")
        return 0 if passed == total else 1


def summarise_pixels(output: str) -> str:
    for line in output.splitlines():
        if line.startswith("GUI_PHASE5_PIXELS="):
            return line.removeprefix("GUI_PHASE5_PIXELS=").strip()
    return "marker seen"


def auto_program() -> list[str]:
    return [
        "' gui_gauges_msgbox_auto.bas - ESP32 Phase 5 shared GUI smoke",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Gauges/Areas\",4,4,312,232,RGB(WHITE)",
        "GUI GAUGE #2,82,100,54,RGB(WHITE),RGB(BLACK),0,100,0,\"V\",RGB(GREEN),25,RGB(YELLOW),50,RGB(CYAN),75,RGB(RED)",
        "GUI BARGAUGE #3,16,190,288,24,RGB(WHITE),RGB(BLACK),0,100,RGB(GREEN),25,RGB(YELLOW),50,RGB(CYAN),75,RGB(RED)",
        "GUI AREA #4,222,52,78,54",
        "GUI CAPTION #5,\"AREA\",240,66",
        "GUI REDRAW ALL",
        "FOR I=0 TO 100 STEP 20",
        "  CTRLVAL(2)=I",
        "  CTRLVAL(3)=100-I",
        "  PAUSE 10",
        "NEXT I",
        "FOR I=100 TO 0 STEP -25",
        "  CTRLVAL(2)=I",
        "  CTRLVAL(3)=100-I",
        "  PAUSE 10",
        "NEXT I",
        "CTRLVAL(2)=75",
        "CTRLVAL(3)=70",
        "IF CTRLVAL(2)<>75 THEN PRINT \"GUI_PHASE5_FAIL GAUGE_SET\": END",
        "IF CTRLVAL(3)<>70 THEN PRINT \"GUI_PHASE5_FAIL BARGAUGE_SET\": END",
        "IF CTRLVAL(4)<>0 THEN PRINT \"GUI_PHASE5_FAIL AREA_INIT\": END",
        "GC=0",
        "FOR Y=38 TO 146 STEP 2",
        "  FOR X=26 TO 138 STEP 2",
        "    IF PIXEL(X,Y)<>RGB(BLACK) THEN GC=GC+1",
        "  NEXT X",
        "NEXT Y",
        "BC=0",
        "FOR Y=192 TO 212 STEP 2",
        "  FOR X=18 TO 302 STEP 2",
        "    IF PIXEL(X,Y)<>RGB(BLACK) THEN BC=BC+1",
        "  NEXT X",
        "NEXT Y",
        "IF GC<60 THEN PRINT \"GUI_PHASE5_FAIL GAUGE_PIXELS \";GC: END",
        "IF BC<80 THEN PRINT \"GUI_PHASE5_FAIL BARGAUGE_PIXELS \";BC: END",
        "PRINT \"GUI_PHASE5_VALUES=\";CTRLVAL(2);\",\";CTRLVAL(3);\",\";CTRLVAL(4)",
        "PRINT \"GUI_PHASE5_PIXELS=\";GC;\",\";BC",
        "PRINT \"GUI_PHASE5_AUTO_OK\"",
        "GUI DELETE ALL",
        "CLS",
    ]


def touch_program() -> list[str]:
    return [
        "' gui_gauges_msgbox_touch.bas - manual AREA and MSGBOX target",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Phase 5 Touch\",4,4,312,232,RGB(WHITE)",
        "GUI GAUGE #2,84,104,54,RGB(WHITE),RGB(BLACK),0,100,0,\"V\",RGB(GREEN),25,RGB(YELLOW),50,RGB(CYAN),75,RGB(RED)",
        "GUI BARGAUGE #3,16,190,288,24,RGB(WHITE),RGB(BLACK),0,100,RGB(GREEN),25,RGB(YELLOW),50,RGB(CYAN),75,RGB(RED)",
        "GUI AREA #4,214,54,90,70",
        "GUI CAPTION #5,\"Tap AREA\",222,68",
        "CTRLVAL(2)=55",
        "CTRLVAL(3)=35",
        "GUI REDRAW ALL",
        "PRINT \"GUI_PHASE5_TOUCH_READY\"",
        "PRINT \"Tap AREA, then press any key for MSGBOX\"",
        "LR=-1",
        "DO",
        "  K$=INKEY$",
        "  IF K$<>\"\" THEN EXIT",
        "  R=TOUCH(LASTREF)",
        "  IF R<>LR THEN",
        "    LR=R",
        "    PRINT \"GUI_PHASE5_REF=\";R;\" VALS=\";CTRLVAL(2);\",\";CTRLVAL(3);\",\";CTRLVAL(4)",
        "  ENDIF",
        "  PAUSE 50",
        "LOOP",
        "PRINT \"GUI_PHASE5_MSGBOX_READY\"",
        "R=MSGBOX(\"Phase 5~MSGBOX touch test\",\"OK\",\"Cancel\")",
        "PRINT \"GUI_PHASE5_MSGBOX_RESULT=\";R",
        "GUI DELETE ALL",
        "CLS",
        "PRINT \"GUI_PHASE5_TOUCH_DONE\"",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default="/dev/cu.usbmodem2101", help="serial device or telnet:host[:port]")
    parser.add_argument("--drive", default="A:", help="device drive used for uploaded BASIC programs")
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=45.0)
    parser.add_argument("--cleanup", action="store_true", help="remove uploaded BASIC smoke programs after the run")
    args = parser.parse_args()

    with open_transport(args.target) as basic:
        basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
        smoke = GuiGaugeSmoke(
            basic,
            drive=args.drive,
            timeout=args.timeout,
            long_timeout=args.long_timeout,
            keep_files=not args.cleanup,
        )
        return smoke.run()


if __name__ == "__main__":
    sys.exit(main())
