#!/usr/bin/env python3
"""Small MMBasic serial command runner for port bringup smoke tests."""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from dataclasses import dataclass
from typing import Iterable

ANSI_RE = re.compile(rb"\x1b\[[0-9;:?]*[A-Za-z]")


def _import_serial():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required. On this machine try python3.11, or install pyserial "
            "for the Python you are using."
        ) from exc
    return serial


def strip_ansi(data: bytes) -> bytes:
    return ANSI_RE.sub(b"", data)


def default_port() -> str:
    if os.environ.get("BASIC_PORT"):
        return os.environ["BASIC_PORT"]
    for candidate in ("/dev/cu.usbmodem101", "/dev/ttyACM0", "/dev/ttyUSB0"):
        if os.path.exists(candidate):
            return candidate
    return "/dev/cu.usbmodem101"


@dataclass
class CommandResult:
    command: str
    raw: bytes

    @property
    def text(self) -> str:
        return self.raw.decode("latin1", "replace")

    @property
    def clean_text(self) -> str:
        return strip_ansi(self.raw).decode("latin1", "replace")


class BasicSerial:
    def __init__(self, port: str, baud: int = 115200, read_timeout: float = 0.03):
        self.port = port
        self.baud = baud
        self.read_timeout = read_timeout
        self.serial = None

    def __enter__(self) -> "BasicSerial":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def open(self) -> None:
        serial = _import_serial()
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baud
        ser.timeout = self.read_timeout
        ser.write_timeout = 2
        ser.dsrdtr = False
        ser.rtscts = False
        ser.dtr = True
        ser.rts = False
        ser.open()
        ser.dtr = True
        ser.rts = False
        self.serial = ser

    def close(self) -> None:
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def reset_app(self) -> None:
        assert self.serial is not None
        self.serial.dtr = False
        self.serial.rts = False
        time.sleep(0.2)
        self.serial.rts = True
        time.sleep(0.25)
        self.serial.rts = False
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def read_for(self, seconds: float) -> bytes:
        assert self.serial is not None
        end = time.monotonic() + seconds
        out = bytearray()
        while time.monotonic() < end:
            chunk = self.serial.read(4096)
            if chunk:
                out.extend(chunk)
        return bytes(out)

    @staticmethod
    def _has_prompt(raw: bytes) -> bool:
        """The BASIC prompt is exactly `>` at the start of a line. Don't
        match an in-the-middle `>` like the one in `<DIR>` — ESP32's
        faster serial output reliably exposes chunks ending mid-`<DIR>`
        line, which a naive endswith(`>`) trips on and returns early
        with a truncated transcript."""
        clean = strip_ansi(raw).replace(b"\r", b"\n")
        tail = clean[-300:].rstrip(b" \t")  # keep newlines for boundary
        # Valid prompt shapes: end of stream with "\n>" or just ">".
        if tail.endswith(b"\n>") or tail == b">":
            return True
        return False

    def wait_for_prompt(self, timeout: float) -> bytes:
        assert self.serial is not None
        end = time.monotonic() + timeout
        out = bytearray()
        while time.monotonic() < end:
            chunk = self.serial.read(4096)
            if chunk:
                out.extend(chunk)
                if self._has_prompt(bytes(out)):
                    return bytes(out)
            else:
                time.sleep(0.005)
        return bytes(out)

    def sync(self, timeout: float = 8.0, boot_wait: float = 0.0) -> bytes:
        assert self.serial is not None
        out = bytearray()
        if boot_wait:
            out.extend(self.read_for(boot_wait))
        self.serial.write(b"\x03\r")
        self.serial.flush()
        out.extend(self.wait_for_prompt(timeout))
        if not self._has_prompt(bytes(out)):
            self.serial.write(b"\r")
            self.serial.flush()
            out.extend(self.wait_for_prompt(timeout))
        if not self._has_prompt(bytes(out)):
            clean_tail = strip_ansi(bytes(out))[-240:].decode("latin1", "replace")
            raise TimeoutError(
                f"could not sync to BASIC prompt; captured {len(out)} bytes; "
                f"tail={clean_tail!r}"
            )
        return bytes(out)

    def command(self, command: str, timeout: float = 10.0, check_error: bool = True) -> CommandResult:
        assert self.serial is not None
        self.serial.write((command + "\r").encode("latin1"))
        self.serial.flush()
        raw = self.wait_for_prompt(timeout)
        result = CommandResult(command, raw)
        if check_error and ("Error :" in result.clean_text or "Error:" in result.clean_text):
            raise RuntimeError(f"BASIC error after {command!r}\n{result.clean_text}")
        if not self._has_prompt(raw):
            raise TimeoutError(f"timeout waiting for prompt after {command!r}\n{result.clean_text}")
        return result


def load_script(path: str) -> list[str]:
    commands: list[str] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            commands.append(line)
    return commands


def run_commands(
    port: str,
    commands: Iterable[str],
    *,
    baud: int,
    sync: bool,
    boot_wait: float,
    timeout: float,
    long_timeout: float,
    quiet: bool,
    reset_app: bool,
) -> str:
    transcript: list[str] = []
    with BasicSerial(port, baud) as basic:
        if reset_app:
            basic.reset_app()
        if sync:
            raw = basic.sync(timeout=long_timeout, boot_wait=boot_wait)
            text = raw.decode("latin1", "replace")
            transcript.append(text)
            if not quiet:
                print(text, end="" if text.endswith("\n") else "\n")
        for command in commands:
            if not quiet:
                print(f">>> {command}", flush=True)
            cmd_timeout = long_timeout if command.upper().startswith(("WEB CONNECT", "PAUSE ")) else timeout
            result = basic.command(command, timeout=cmd_timeout)
            transcript.append(result.text)
            if not quiet:
                print(result.text, end="" if result.text.endswith("\n") else "\n", flush=True)
    return "".join(transcript)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=default_port())
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-sync", action="store_true", help="do not send Ctrl-C/Enter before commands")
    parser.add_argument("--reset-app", action="store_true", help="pulse RTS to reset the board before syncing")
    parser.add_argument("--boot-wait", type=float, default=0.0, help="seconds to capture boot output before sync")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=35.0)
    parser.add_argument("--cmd", action="append", default=[], help="BASIC command to run; may repeat")
    parser.add_argument("--script", help="line-oriented BASIC command script; blank/# lines are skipped")
    parser.add_argument("--expect", action="append", default=[], help="regex that must match the transcript")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    commands = list(args.cmd)
    if args.script:
        commands.extend(load_script(args.script))
    if not commands:
        parser.error("provide at least one --cmd or --script")

    transcript = run_commands(
        args.port,
        commands,
        baud=args.baud,
        sync=not args.no_sync,
        boot_wait=args.boot_wait,
        timeout=args.timeout,
        long_timeout=args.long_timeout,
        quiet=args.quiet,
        reset_app=args.reset_app,
    )
    clean = strip_ansi(transcript.encode("latin1", "replace")).decode("latin1", "replace")
    for pattern in args.expect:
        if not re.search(pattern, clean, re.MULTILINE):
            print(f"missing expected pattern: {pattern}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
