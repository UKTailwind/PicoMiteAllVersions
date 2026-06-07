#!/usr/bin/env python3
"""Run the ESP32-S3 GUI control smoke suites in order.

This is a convenience wrapper around the phase-specific smoke runners. It does
not open a device unless invoked without --list or --dry-run.

Examples:
    python3.11 porttools/esp32_gui_controls_smoke.py --dry-run
    python3.11 porttools/esp32_gui_controls_smoke.py --target /dev/cu.usbmodem2101
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PORTTOOLS = Path(__file__).resolve().parent

SUITES = {
    "basic": {
        "script": "esp32_gui_basic_controls_smoke.py",
        "target_arg": "--port",
        "description": "button, switch, checkbox, radio, LED, frame, caption",
    },
    "text": {
        "script": "esp32_gui_text_controls_smoke.py",
        "target_arg": "--target",
        "description": "numberbox, textbox, formatbox, spinbox, displaybox",
    },
    "gauges": {
        "script": "esp32_gui_gauges_msgbox_smoke.py",
        "target_arg": "--target",
        "description": "gauge, bargauge, area, manual MSGBOX target",
    },
}


def build_command(name: str, args: argparse.Namespace) -> list[str]:
    suite = SUITES[name]
    if suite["target_arg"] == "--port" and args.target.startswith("telnet:"):
        raise ValueError("the basic GUI smoke currently requires a serial target")

    command = [
        sys.executable,
        str(PORTTOOLS / suite["script"]),
        suite["target_arg"],
        args.target,
        "--drive",
        args.drive,
        "--boot-wait",
        str(args.boot_wait),
        "--timeout",
        str(args.timeout),
        "--long-timeout",
        str(args.long_timeout),
    ]
    if args.cleanup:
        command.append("--cleanup")
    return command


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "suites",
        nargs="*",
        default=None,
        help="suite names to run; defaults to all in dependency order",
    )
    parser.add_argument(
        "--target",
        default="/dev/cu.usbmodem2101",
        help="serial device; text/gauge suites also accept telnet:host[:port]",
    )
    parser.add_argument("--drive", default="A:", help="device drive used for uploaded BASIC programs")
    parser.add_argument("--boot-wait", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--long-timeout", type=float, default=45.0)
    parser.add_argument("--cleanup", action="store_true", help="remove generated BASIC files after each suite")
    parser.add_argument("--dry-run", action="store_true", help="print commands without opening the target")
    parser.add_argument("--list", action="store_true", help="list available suites and exit")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    suite_names = args.suites or list(SUITES)
    invalid = sorted(set(suite_names) - set(SUITES))
    if invalid:
        print(f"invalid suite name(s): {', '.join(invalid)}", file=sys.stderr)
        print(f"valid suites: {', '.join(SUITES)}", file=sys.stderr)
        return 2
    if args.list:
        for name, suite in SUITES.items():
            print(f"{name:7} {suite['description']}")
        return 0

    commands: list[tuple[str, list[str]]] = []
    for name in suite_names:
        try:
            commands.append((name, build_command(name, args)))
        except ValueError as exc:
            print(f"{name}: {exc}", file=sys.stderr)
            return 2

    if args.dry_run:
        for name, command in commands:
            print(f"# {name}")
            print(" ".join(command))
        return 0

    for name, command in commands:
        print(f"\n=== esp32 gui smoke: {name} ===", flush=True)
        completed = subprocess.run(command, cwd=PORTTOOLS.parent)
        if completed.returncode:
            return completed.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
