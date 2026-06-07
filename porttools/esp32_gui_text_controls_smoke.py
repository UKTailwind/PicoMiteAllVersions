#!/usr/bin/env python3
"""ESP32-S3 GUI text/numeric-controls smoke.

Uploads three BASIC programs over serial or telnet:

* gui_text_controls_auto.bas creates NUMBERBOX, TEXTBOX, FORMATBOX,
  SPINBOX, and DISPLAYBOX, then validates CTRLVAL updates through RUN
  and FRUN.
* gui_text_controls_edit.bas prompts through the active console and writes
  the entered value into TEXTBOX and DISPLAYBOX controls under RUN and FRUN.
* gui_text_controls_touch.bas is a manual on-device target for touch
  popup keypad/keyboard editing.

Run from the repo root:
    python3.11 porttools/esp32_gui_text_controls_smoke.py --target /dev/cu.usbmodem2101
    python3.11 porttools/esp32_gui_text_controls_smoke.py --target telnet:192.168.5.140
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass
from typing import Iterable, Any

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from basic_telnet import open_transport  # noqa: E402

ANSI_RE = re.compile(rb"\x1b\[[0-9;:?]*[A-Za-z]")


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


class GuiTextSmoke:
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
        self.auto_path = join_drive(self.drive, "gui_text_controls_auto.bas")
        self.edit_path = join_drive(self.drive, "gui_text_controls_edit.bas")
        self.touch_path = join_drive(self.drive, "gui_text_controls_touch.bas")

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

    def ensure_gui_controls(self, minimum: int = 12) -> None:
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
        self.write_program(self.edit_path, edit_program())
        self.write_program(self.touch_path, touch_program())
        self.note("program upload", True, f"{self.auto_path}, {self.edit_path}, {self.touch_path}")

    def run_program(self, run_command: str, marker: str) -> str:
        output = self.command(run_command, timeout=self.long_timeout)
        if marker not in output:
            raise RuntimeError(f"missing marker {marker!r} after {run_command!r}\n{output[-700:]}")
        if "GUI_TEXT_FAIL" in output:
            raise RuntimeError(f"program reported failure after {run_command!r}\n{output[-900:]}")
        return output

    def automated(self) -> None:
        run_output = self.run_program(f'RUN "{self.auto_path}"', "GUI_TEXT_AUTO_OK")
        self.note("RUN gui_text_controls_auto", True, summarise_values(run_output))
        frun_output = self.run_program(f'FRUN "{self.auto_path}"', "GUI_TEXT_AUTO_OK")
        self.note("FRUN gui_text_controls_auto", True, summarise_values(frun_output))

    def command_with_input(self, command: str, prompt_marker: str, response: str) -> str:
        serial = self.basic.serial
        assert serial is not None

        serial.reset_input_buffer()
        serial.write((command + "\r").encode("latin1"))
        serial.flush()

        sent_response = False
        raw = bytearray()
        deadline = time.monotonic() + self.long_timeout
        while time.monotonic() < deadline:
            chunk = serial.read(4096)
            if chunk:
                raw.extend(chunk)
                clean = ANSI_RE.sub(b"", bytes(raw)).decode("latin1", "replace")
                if not sent_response and prompt_marker in clean:
                    serial.write((response + "\r").encode("latin1"))
                    serial.flush()
                    sent_response = True
                if sent_response and self.basic._has_prompt(bytes(raw)):
                    if "Error :" in clean or "Error:" in clean:
                        raise RuntimeError(f"BASIC error after {command!r}\n{clean}")
                    return clean
            else:
                time.sleep(0.005)

        clean = ANSI_RE.sub(b"", bytes(raw)).decode("latin1", "replace")
        raise TimeoutError(f"timeout waiting for prompted input program after {command!r}\n{clean[-700:]}")

    def console_edit_probe(self) -> None:
        response = "Edited 42"
        run_output = self.command_with_input(f'RUN "{self.edit_path}"', "GUI_TEXT_EDIT>", response)
        if "GUI_TEXT_EDIT_OK" not in run_output or f"GUI_TEXT_EDIT_VALUE={response},{response}" not in run_output:
            raise RuntimeError(f"RUN edit probe did not report expected control values\n{run_output[-900:]}")
        self.note("RUN console edit into controls", True, response)

        frun_output = self.command_with_input(f'FRUN "{self.edit_path}"', "GUI_TEXT_EDIT>", response)
        if "GUI_TEXT_EDIT_OK" not in frun_output or f"GUI_TEXT_EDIT_VALUE={response},{response}" not in frun_output:
            raise RuntimeError(f"FRUN edit probe did not report expected control values\n{frun_output[-900:]}")
        self.note("FRUN console edit into controls", True, response)

    def console_probe(self) -> None:
        output = self.command('PRINT "GUI_TEXT_CONSOLE=";"serial/telnet-ready"', timeout=self.timeout)
        if "GUI_TEXT_CONSOLE=serial/telnet-ready" not in output:
            raise RuntimeError(f"missing console probe marker\n{output[-300:]}")
        self.note("console path after GUI", True, "command echo and prompt recovered")

    def cleanup(self) -> None:
        self.command("ON ERROR SKIP : GUI DELETE ALL", check_error=False)
        self.command("ON ERROR SKIP : CLS", check_error=False)
        if not self.keep_files:
            self.command(f'ON ERROR SKIP : KILL "{self.auto_path}"', check_error=False)
            self.command(f'ON ERROR SKIP : KILL "{self.edit_path}"', check_error=False)
            self.command(f'ON ERROR SKIP : KILL "{self.touch_path}"', check_error=False)

    def run(self) -> int:
        self.ensure_gui_controls()
        self.upload_programs()
        self.automated()
        self.console_edit_probe()
        self.console_probe()
        self.cleanup()
        passed = sum(1 for c in self.checks if c.ok)
        total = len(self.checks)
        print(f"\n{passed}/{total} passed")
        if self.keep_files:
            print(f"Manual touch/keypad validation program: RUN \"{self.touch_path}\"")
        else:
            print("Manual touch/keypad validation program removed by --cleanup")
        return 0 if passed == total else 1


def summarise_values(output: str) -> str:
    for line in output.splitlines():
        if line.startswith("GUI_TEXT_VALUES="):
            return line.removeprefix("GUI_TEXT_VALUES=").strip()
    return "marker seen"


def auto_program() -> list[str]:
    return [
        "' gui_text_controls_auto.bas - ESP32 Phase 4 shared GUI smoke",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Text/Numeric\",4,4,312,232,RGB(WHITE)",
        "GUI NUMBERBOX #2,18,28,92,24,RGB(WHITE),RGB(BLUE)",
        "GUI TEXTBOX #3,126,28,150,24,RGB(BLACK),RGB(CYAN)",
        "GUI FORMATBOX #4,\"2hHh(:)5m9m\",18,72,116,24,RGB(WHITE),RGB(BLUE)",
        "GUI SPINBOX #5,150,72,106,24,RGB(WHITE),RGB(BLUE),2,-4,10",
        "GUI DISPLAYBOX #6,18,118,230,24,RGB(BLACK),RGB(YELLOW)",
        "GUI REDRAW ALL",
        "CTRLVAL(2)=12.5",
        "CTRLVAL(3)=\"Alpha\"",
        "CTRLVAL(4)=\"12:34\"",
        "CTRLVAL(5)=4",
        "CTRLVAL(6)=\"Display A\"",
        "IF CTRLVAL(2)<>12.5 THEN PRINT \"GUI_TEXT_FAIL NUMBERBOX_SET\": END",
        "IF CTRLVAL(3)<>\"Alpha\" THEN PRINT \"GUI_TEXT_FAIL TEXTBOX_SET\": END",
        "IF CTRLVAL(4)<>\"12:34\" THEN PRINT \"GUI_TEXT_FAIL FORMATBOX_SET\": END",
        "IF CTRLVAL(5)<>4 THEN PRINT \"GUI_TEXT_FAIL SPINBOX_SET\": END",
        "IF CTRLVAL(6)<>\"Display A\" THEN PRINT \"GUI_TEXT_FAIL DISPLAYBOX_SET\": END",
        "CTRLVAL(2)=-3.25",
        "CTRLVAL(3)=\"Beta 42\"",
        "CTRLVAL(4)=\"23:59\"",
        "CTRLVAL(5)=999",
        "CTRLVAL(6)=\"Display B\"",
        "IF CTRLVAL(2)<>-3.25 THEN PRINT \"GUI_TEXT_FAIL NUMBERBOX_UPDATE\": END",
        "IF CTRLVAL(3)<>\"Beta 42\" THEN PRINT \"GUI_TEXT_FAIL TEXTBOX_UPDATE\": END",
        "IF CTRLVAL(4)<>\"23:59\" THEN PRINT \"GUI_TEXT_FAIL FORMATBOX_UPDATE\": END",
        "IF CTRLVAL(5)<>10 THEN PRINT \"GUI_TEXT_FAIL SPINBOX_CLAMP\": END",
        "IF CTRLVAL(6)<>\"Display B\" THEN PRINT \"GUI_TEXT_FAIL DISPLAYBOX_UPDATE\": END",
        "IF CTRLVAL(2)<0 THEN CTRLVAL(6)=\"Numeric IF OK\"",
        "IF CTRLVAL(6)<>\"Numeric IF OK\" THEN PRINT \"GUI_TEXT_FAIL CTRLVAL_NUMERIC_IF\": END",
        "GUI HIDE #3,#4",
        "GUI SHOW #3,#4",
        "GUI DISABLE #2,#5",
        "GUI ENABLE #2,#5",
        "GUI REDRAW ALL",
        "PRINT \"GUI_TEXT_VALUES=\";CTRLVAL(2);\",\";CTRLVAL(3);\",\";CTRLVAL(4);\",\";CTRLVAL(5);\",\";CTRLVAL(6)",
        "PRINT \"GUI_TEXT_AUTO_OK\"",
        "GUI DELETE ALL",
        "CLS",
    ]


def edit_program() -> list[str]:
    return [
        "' gui_text_controls_edit.bas - console input updates text controls",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Console Edit\",4,4,312,232,RGB(WHITE)",
        "GUI TEXTBOX #2,18,46,150,24,RGB(BLACK),RGB(CYAN)",
        "GUI DISPLAYBOX #3,18,92,226,24,RGB(BLACK),RGB(YELLOW)",
        "CTRLVAL(2)=\"before\"",
        "CTRLVAL(3)=\"before\"",
        "GUI REDRAW ALL",
        "INPUT \"GUI_TEXT_EDIT>\";E$",
        "CTRLVAL(2)=E$",
        "CTRLVAL(3)=E$",
        "IF CTRLVAL(2)<>E$ THEN PRINT \"GUI_TEXT_FAIL EDIT_TEXTBOX\": END",
        "IF CTRLVAL(3)<>E$ THEN PRINT \"GUI_TEXT_FAIL EDIT_DISPLAYBOX\": END",
        "PRINT \"GUI_TEXT_EDIT_VALUE=\";CTRLVAL(2);\",\";CTRLVAL(3)",
        "PRINT \"GUI_TEXT_EDIT_OK\"",
        "GUI DELETE ALL",
        "CLS",
    ]


def touch_program() -> list[str]:
    return [
        "' gui_text_controls_touch.bas - manual touch popup-keyboard target",
        "GUI DELETE ALL",
        "CLS",
        "COLOUR RGB(WHITE), RGB(BLACK)",
        "GUI FRAME #1,\"Edit Controls\",4,4,312,232,RGB(WHITE)",
        "GUI CAPTION #2,\"Tap a box; use popup keys; press any key to exit\",12,20",
        "GUI NUMBERBOX #3,18,46,96,24,RGB(WHITE),RGB(BLUE)",
        "GUI TEXTBOX #4,132,46,150,24,RGB(BLACK),RGB(CYAN)",
        "GUI FORMATBOX #5,\"2hHh(:)5m9m\",18,92,116,24,RGB(WHITE),RGB(BLUE)",
        "GUI SPINBOX #6,150,92,106,24,RGB(WHITE),RGB(BLUE),1,0,20",
        "GUI DISPLAYBOX #7,18,138,226,24,RGB(BLACK),RGB(YELLOW)",
        "CTRLVAL(3)=7",
        "CTRLVAL(4)=\"text\"",
        "CTRLVAL(5)=\"08:15\"",
        "CTRLVAL(6)=3",
        "CTRLVAL(7)=\"values update below\"",
        "GUI REDRAW ALL",
        "PRINT \"GUI_TEXT_TOUCH_READY\"",
        "PRINT \"Tap NUMBERBOX/TEXTBOX/FORMATBOX/SPINBOX; press any key to exit\"",
        "LR=-1",
        "DO",
        "  K$=INKEY$",
        "  IF K$<>\"\" THEN EXIT",
        "  R=TOUCH(LASTREF)",
        "  IF R<>LR THEN",
        "    LR=R",
        "    CTRLVAL(7)=STR$(CTRLVAL(3))+\",\"+CTRLVAL(4)+\",\"+CTRLVAL(5)+\",\"+STR$(CTRLVAL(6))",
        "    PRINT \"GUI_TEXT_REF=\";R;\" VALS=\";CTRLVAL(3);\",\";CTRLVAL(4);\",\";CTRLVAL(5);\",\";CTRLVAL(6)",
        "  ENDIF",
        "  PAUSE 50",
        "LOOP",
        "GUI DELETE ALL",
        "CLS",
        "PRINT \"GUI_TEXT_TOUCH_DONE\"",
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
        smoke = GuiTextSmoke(
            basic,
            drive=args.drive,
            timeout=args.timeout,
            long_timeout=args.long_timeout,
            keep_files=not args.cleanup,
        )
        return smoke.run()


if __name__ == "__main__":
    sys.exit(main())
