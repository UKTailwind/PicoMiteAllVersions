#!/usr/bin/env python3
"""Browser/proxy network conformance harness for host-WASM.

This runner keeps the browser-facing conformance gate in one place while
delegating the detailed BASIC/Playwright interactions to the existing
host/web smoke suites.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class CommandStep:
    name: str
    command: tuple[str, ...]


@dataclass(frozen=True)
class SmokeSuite:
    key: str
    label: str
    script: str
    proxy: bool


PROXY_SUITES: tuple[SmokeSuite, ...] = (
    SmokeSuite("tcp-client", "proxy TCP client HTTP request", "host/web/smoke_network_proxy.mjs", True),
    SmokeSuite("tcp-stream", "proxy TCP client stream", "host/web/smoke_network_proxy_tcp_stream.mjs", True),
    SmokeSuite("tcp-server", "proxy TCP server/transmit helpers", "host/web/smoke_network_proxy_tcp_server.mjs", True),
    SmokeSuite("udp-ntp", "proxy UDP and NTP", "host/web/smoke_network_proxy_udp_ntp.mjs", True),
    SmokeSuite("tftp", "proxy TFTP", "host/web/smoke_network_proxy_tftp.mjs", True),
    SmokeSuite("telnet", "proxy Telnet", "host/web/smoke_network_proxy_telnet.mjs", True),
    SmokeSuite("mqtt", "proxy plain MQTT", "host/web/smoke_network_proxy_mqtt.mjs", True),
)

STATIC_SUITES: tuple[SmokeSuite, ...] = (
    SmokeSuite("static-http", "static browser HTTP/unsupported network surface", "host/web/smoke_network_unsupported.mjs", False),
    SmokeSuite("mqtt-ws", "static browser MQTT over WebSocket", "host/web/smoke_network_mqtt_ws.mjs", False),
)

SUITE_ALIASES: dict[str, tuple[str, ...]] = {
    "all": tuple(s.key for s in PROXY_SUITES) + tuple(s.key for s in STATIC_SUITES),
    "proxy": tuple(s.key for s in PROXY_SUITES),
    "static": tuple(s.key for s in STATIC_SUITES),
    "tcp-client": ("tcp-client",),
    "tcp-stream": ("tcp-stream",),
    "tcp-server": ("tcp-server",),
    "udp": ("udp-ntp",),
    "ntp": ("udp-ntp",),
    "udp-ntp": ("udp-ntp",),
    "tftp": ("tftp",),
    "telnet": ("telnet",),
    "mqtt": ("mqtt",),
    "mqtt-ws": ("mqtt-ws",),
    "static-http": ("static-http",),
}

SUITES_BY_KEY = {suite.key: suite for suite in (*PROXY_SUITES, *STATIC_SUITES)}


@dataclass
class StepResult:
    name: str
    command: tuple[str, ...]
    returncode: int
    elapsed: float


def format_command(command: Iterable[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def find_python311_or_current() -> str:
    preferred = shutil.which("python3.11")
    if preferred:
        return preferred
    return sys.executable


def build_steps(args: argparse.Namespace) -> list[CommandStep]:
    if args.skip_build:
        return []
    return [
        CommandStep("build wasm proxy", (args.make, "-C", "ports/host_native", "wasm-proxy")),
        CommandStep("build host WASM", ("ports/host_wasm/build.sh",)),
    ]


def expand_suites(requested: list[str], *, skip_static: bool) -> list[SmokeSuite]:
    selected: list[SmokeSuite] = []
    seen: set[str] = set()
    for item in requested:
        if item not in SUITE_ALIASES:
            valid = ", ".join(sorted(SUITE_ALIASES))
            raise SystemExit(f"unknown suite {item!r}; valid suites: {valid}")
        for key in SUITE_ALIASES[item]:
            suite = SUITES_BY_KEY[key]
            if skip_static and not suite.proxy:
                continue
            if key not in seen:
                selected.append(suite)
                seen.add(key)
    return selected


def validate_prerequisites(steps: list[CommandStep], suites: list[SmokeSuite], args: argparse.Namespace) -> None:
    if not shutil.which(args.node):
        raise SystemExit(f"node executable not found: {args.node}")
    if not args.skip_build and not shutil.which(args.make):
        raise SystemExit(f"make executable not found: {args.make}")

    for step in steps:
        if os.sep in step.command[0] and not (REPO_ROOT / step.command[0]).exists():
            raise SystemExit(f"required command not found: {step.command[0]}")

    missing_scripts = [suite.script for suite in suites if not (REPO_ROOT / suite.script).is_file()]
    if missing_scripts:
        raise SystemExit("missing smoke script(s): " + ", ".join(missing_scripts))

    if args.skip_build and any(suite.proxy for suite in suites):
        proxy = REPO_ROOT / "host/wasm_network_proxy"
        if not proxy.exists():
            raise SystemExit(f"{proxy.relative_to(REPO_ROOT)} is missing; rerun without --skip-build")


def run_step(step: CommandStep, *, timeout: float | None, dry_run: bool) -> StepResult:
    print(f"\n==> {step.name}", flush=True)
    print(f"$ {format_command(step.command)}", flush=True)
    start = time.monotonic()
    if dry_run:
        elapsed = time.monotonic() - start
        print(f"<dry-run> {step.name}: skipped", flush=True)
        return StepResult(step.name, step.command, 0, elapsed)
    try:
        completed = subprocess.run(step.command, cwd=REPO_ROOT, timeout=timeout)
        returncode = completed.returncode
    except subprocess.TimeoutExpired:
        returncode = 124
        print(f"TIMEOUT after {timeout:.0f}s: {step.name}", flush=True)
    elapsed = time.monotonic() - start
    status = "PASS" if returncode == 0 else f"FAIL ({returncode})"
    print(f"<== {status}: {step.name} in {elapsed:.1f}s", flush=True)
    return StepResult(step.name, step.command, returncode, elapsed)


def smoke_steps(suites: list[SmokeSuite], args: argparse.Namespace) -> list[CommandStep]:
    return [
        CommandStep(suite.label, (args.node, suite.script))
        for suite in suites
    ]


def print_summary(results: list[StepResult]) -> None:
    print("\nSummary:", flush=True)
    for result in results:
        status = "PASS" if result.returncode == 0 else f"FAIL ({result.returncode})"
        print(f"  {status:10} {result.elapsed:7.1f}s  {result.name}", flush=True)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run host-WASM browser/proxy network conformance suites.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Suites:\n"
            "  all          proxy suites plus static browser smokes\n"
            "  proxy        all proxy suites only\n"
            "  static       static browser HTTP/unsupported and MQTT-over-WebSocket smokes\n"
            "  tcp-client   proxy WEB TCP CLIENT REQUEST / HTTP\n"
            "  tcp-stream   proxy WEB OPEN TCP STREAM / WEB TCP CLIENT STREAM\n"
            "  tcp-server   proxy TCP server, transmit helpers, RUN preservation\n"
            "  udp, ntp     proxy UDP/NTP combined smoke\n"
            "  tftp         proxy TFTP WRQ/RRQ\n"
            "  telnet       proxy Telnet console\n"
            "  mqtt         proxy plain MQTT over TCP\n"
        ),
    )
    parser.add_argument(
        "suites",
        nargs="*",
        default=["all"],
        help="suite name(s) to run; default: all",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="do not run make -C ports/host_native wasm-proxy or ports/host_wasm/build.sh",
    )
    parser.add_argument(
        "--skip-static",
        action="store_true",
        help="when using all, omit static browser smokes for local proxy-only iteration",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=900.0,
        help="timeout in seconds for each child command; default: 900",
    )
    parser.add_argument("--node", default="node", help="node executable to use; default: node")
    parser.add_argument("--make", default="make", help="make executable to use; default: make")
    parser.add_argument("--keep-going", action="store_true", help="continue after a failed child command")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running them")
    parser.add_argument("--list-suites", action="store_true", help="list suites and exit")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(list(argv) if argv is not None else sys.argv[1:])
    if args.list_suites:
        for name in sorted(SUITE_ALIASES):
            print(name)
        return 0

    suites = expand_suites(args.suites, skip_static=args.skip_static)
    if not suites:
        raise SystemExit("no suites selected")

    steps = build_steps(args) + smoke_steps(suites, args)
    validate_prerequisites(steps, suites, args)

    print(f"repo root: {REPO_ROOT}", flush=True)
    print(f"python: {find_python311_or_current()}", flush=True)
    print("selected suites: " + ", ".join(suite.key for suite in suites), flush=True)

    results: list[StepResult] = []
    started = time.monotonic()
    for step in steps:
        result = run_step(step, timeout=args.timeout, dry_run=args.dry_run)
        results.append(result)
        if result.returncode != 0 and not args.keep_going:
            break

    print_summary(results)
    total = time.monotonic() - started
    failures = [result for result in results if result.returncode != 0]
    if failures:
        print(f"\nFAILED in {total:.1f}s: {len(failures)} command(s) failed", flush=True)
        return failures[0].returncode or 1
    if len(results) != len(steps):
        print(f"\nFAILED in {total:.1f}s: stopped before all commands ran", flush=True)
        return 1
    print(f"\nAll selected WASM proxy network conformance steps passed in {total:.1f}s.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
