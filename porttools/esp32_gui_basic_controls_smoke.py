#!/usr/bin/env python3
"""ESP32-S3 GUI basic-controls smoke.

Uploads two BASIC programs over the serial console:

* gui_basic_controls_auto.bas validates creation, redraw, programmatic
  CTRLVAL updates, hide/show, enable/disable, RUN, and FRUN.
* gui_basic_controls_touch.bas is an on-device manual touch target for
  button/switch/checkbox/radio hit testing.

Run from the repo root:
    python3.11 porttools/esp32_gui_basic_controls_smoke.py --port /dev/cu.usbmodem2101
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass
from typing import Iterable

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_serial import BasicSerial, default_port  # noqa: E402


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


class GuiBasicSmoke:
    def __init__(
        self,
        basic: BasicSerial,
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
        self.auto_path = join_drive(self.drive, "gui_basic_controls_auto.bas")
        self.touch_path = join_drive(self.drive, "gui_basic_controls_touch.bas")

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

    def ensure_gui_controls(self, minimum: int = 8) -> None:
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
            raise RuntimeError(f"missing marker {marker!r} after {run_command!r}\n{output[-500:]}")
        if "GUI_BASIC_FAIL" in output:
            raise RuntimeError(f"program reported failure after {run_command!r}\n{output[-800:]}")
        return output

    def automated(self) -> None:
        run_output = self.run_program(f'RUN "{self.auto_path}"', "GUI_BASIC_AUTO_OK")
        self.note("RUN gui_basic_controls_auto", True, summarise_ctrlvals(run_output))
        frun_output = self.run_program(f'FRUN "{self.auto_path}"', "GUI_BASIC_AUTO_OK")
        self.note("FRUN gui_basic_controls_auto", True, summarise_ctrlvals(frun_output))

    def probe_touch_idle(self) -> None:
        output = self.command(
            'PRINT "GUI_TOUCH_IDLE=";TOUCH(DOWN);",";TOUCH(UP);",";TOUCH(REF);",";TOUCH(LASTREF)',
            timeout=self.timeout,
        )
        match = re.search(r"GUI_TOUCH_IDLE=\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)", output)
        if not match:
            raise RuntimeError(f"missing touch idle marker\n{output[-300:]}")
        self.note("direct TOUCH idle probe", True, ",".join(match.groups()))

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
        self.probe_touch_idle()
        self.cleanup()
        passed = sum(1 for c in self.checks if c.ok)
        total = len(self.checks)
        print(f"\n{passed}/{total} passed")
        if self.keep_files:
            print(f"Manual touch validation program: RUN \"{self.touch_path}\"")
        else:
            print("Manual touch validation program removed by --cleanup")
        return 0 if passed == total else 1


def summarise_ctrlvals(output: str) -> str:
    for line in output.splitlines():
        if line.startswith("GUI_BASIC_VALUES="):
            return line.removeprefix("GUI_BASIC_VALUES=").strip()
    return "marker seen"


def auto_program() -> list[str]:
    return [
        "' gui_basic_controls_auto.bas - ESP32 Phase 3 shared GUI smoke",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Basic Controls\",4,4,312,232,RGB(WHITE)",
        "GUI CAPTION #2,\"Caption\",16,20",
        "GUI BUTTON #3,\"Tap\",18,48,80,34,RGB(BLACK),RGB(GREEN)",
        "GUI SWITCH #4,\"On|Off\",112,48,96,34,RGB(WHITE),RGB(BLUE)",
        "GUI CHECKBOX #5,\"Check\",18,96,24,RGB(YELLOW)",
        "GUI RADIO #6,\"Radio\",18,144,12,RGB(CYAN)",
        "GUI LED #7,\"Led\",150,108,12,RGB(RED)",
        "GUI REDRAW ALL",
        "IF CTRLVAL(3)<>0 THEN PRINT \"GUI_BASIC_FAIL BUTTON_INIT\": END",
        "IF CTRLVAL(4)<>0 THEN PRINT \"GUI_BASIC_FAIL SWITCH_INIT\": END",
        "IF CTRLVAL(5)<>0 THEN PRINT \"GUI_BASIC_FAIL CHECKBOX_INIT\": END",
        "IF CTRLVAL(6)<>0 THEN PRINT \"GUI_BASIC_FAIL RADIO_INIT\": END",
        "IF CTRLVAL(7)<>0 THEN PRINT \"GUI_BASIC_FAIL LED_INIT\": END",
        "IF CTRLVAL(2)<>\"Caption\" THEN PRINT \"GUI_BASIC_FAIL CAPTION_INIT\": END",
        "CTRLVAL(3)=1",
        "CTRLVAL(4)=1",
        "CTRLVAL(5)=1",
        "CTRLVAL(6)=1",
        "CTRLVAL(7)=1",
        "CTRLVAL(2)=\"Caption2\"",
        "IF CTRLVAL(3)<>1 THEN PRINT \"GUI_BASIC_FAIL BUTTON_SET\": END",
        "IF CTRLVAL(4)<>1 THEN PRINT \"GUI_BASIC_FAIL SWITCH_SET\": END",
        "IF CTRLVAL(5)<>1 THEN PRINT \"GUI_BASIC_FAIL CHECKBOX_SET\": END",
        "IF CTRLVAL(6)<>1 THEN PRINT \"GUI_BASIC_FAIL RADIO_SET\": END",
        "IF CTRLVAL(7)<>1 THEN PRINT \"GUI_BASIC_FAIL LED_SET\": END",
        "IF CTRLVAL(2)<>\"Caption2\" THEN PRINT \"GUI_BASIC_FAIL CAPTION_SET\": END",
        "V=0",
        "IF CTRLVAL(3)=1 THEN V=1 : V=V+1",
        "IF V<>2 THEN PRINT \"GUI_BASIC_FAIL CTRLVAL_INLINE_IF\": END",
        "GUI HIDE #7",
        "GUI SHOW #7",
        "GUI DISABLE #5",
        "GUI ENABLE #5",
        "GUI REDRAW ALL",
        "CTRLVAL(3)=0",
        "IF CTRLVAL(3)<>0 THEN PRINT \"GUI_BASIC_FAIL BUTTON_CLEAR\": END",
        "PRINT \"GUI_BASIC_VALUES=\";CTRLVAL(3);\",\";CTRLVAL(4);\",\";CTRLVAL(5);\",\";CTRLVAL(6);\",\";CTRLVAL(7);\",\";CTRLVAL(2)",
        "PRINT \"GUI_BASIC_AUTO_OK\"",
        "GUI DELETE ALL",
        "CLS",
    ]


def touch_program() -> list[str]:
    return [
        "' gui_basic_controls_touch.bas - manual finger hit-test target",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Touch Test\",4,4,312,232,RGB(WHITE)",
        "GUI CAPTION #2,\"Tap controls, press any key to exit\",12,20",
        "GUI BUTTON #3,\"Button\",18,52,92,36,RGB(BLACK),RGB(GREEN)",
        "GUI SWITCH #4,\"On|Off\",126,52,100,36,RGB(WHITE),RGB(BLUE)",
        "GUI CHECKBOX #5,\"Check\",18,112,24,RGB(YELLOW)",
        "GUI RADIO #6,\"Radio\",18,166,12,RGB(CYAN)",
        "GUI LED #7,\"Led\",174,122,12,RGB(RED)",
        "GUI REDRAW ALL",
        "PRINT \"GUI_BASIC_TOUCH_READY\"",
        "PRINT \"Tap button, switch, checkbox, radio; press any key to exit\"",
        "LR=-1",
        "DO",
        "  K$=INKEY$",
        "  IF K$<>\"\" THEN EXIT",
        "  R=TOUCH(LASTREF)",
        "  IF R<>LR THEN",
        "    LR=R",
        "    IF R=4 OR R=5 OR R=6 THEN CTRLVAL(7)=1",
        "    PRINT \"GUI_TOUCH_REF=\";R;\" LAST=\";TOUCH(LASTX);\",\";TOUCH(LASTY);\" VALS=\";CTRLVAL(3);\",\";CTRLVAL(4);\",\";CTRLVAL(5);\",\";CTRLVAL(6);\",\";CTRLVAL(7)",
        "  ENDIF",
        "  PAUSE 50",
        "LOOP",
        "GUI DELETE ALL",
        "CLS",
        "PRINT \"GUI_BASIC_TOUCH_DONE\"",
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--drive", default="A:", help="device drive used for uploaded BASIC programs")
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=45.0)
    parser.add_argument("--cleanup", action="store_true", help="remove uploaded BASIC smoke programs after the run")
    args = parser.parse_args()

    with BasicSerial(args.port) as basic:
        basic.sync(timeout=args.long_timeout, boot_wait=args.boot_wait)
        smoke = GuiBasicSmoke(
            basic,
            drive=args.drive,
            timeout=args.timeout,
            long_timeout=args.long_timeout,
            keep_files=not args.cleanup,
        )
        return smoke.run()


if __name__ == "__main__":
    sys.exit(main())
