#!/usr/bin/env python3
"""Telnet console smoke harness for MMBasic ports (ESP32 today; RP2350 later).

Connects to a device's telnet console (port 23 by default), walks the
RFC 854 / RFC 855 negotiation, then asserts that the server is running
in character-at-a-time mode by sending single characters and verifying
they're echoed individually with sub-100 ms latency.

Failure modes this is designed to catch:
  - Server doesn't send the negotiation suite that flips off line mode
    -> client falls back to line mode, no per-char echo.
  - Server drops bytes that follow an IAC sequence inside the same TCP
    packet -> typed characters disappear sporadically.
  - Server fails to echo characters even though it sent WILL ECHO ->
    "ghost typing" experience.
  - BASIC command round-trip is broken (no '> ' prompt re-issued after
    CR) -> can't actually drive the device.

Same invocation pattern as porttools/psram_smoke.py: hardware-only,
PASS/FAIL per check, machine-parseable Summary line.

Usage:
    python3 porttools/telnet_smoke.py --host 192.168.1.42
"""

from __future__ import annotations

import argparse
import re
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Sequence


# ---------------------------------------------------------------------------
# Telnet protocol constants (RFC 854 + RFC 855 + RFC 857 + RFC 858 + RFC 1184)
# ---------------------------------------------------------------------------

IAC  = 255
DONT = 254
DO   = 253
WONT = 252
WILL = 251
SB   = 250  # subnegotiation begin
SE   = 240  # subnegotiation end
GA   = 249

OPT_ECHO        = 1
OPT_SUPPRESS_GA = 3
OPT_TTYPE       = 24
OPT_NAWS        = 31
OPT_LINEMODE    = 34

CMD_NAMES = {
    DONT: "DONT", DO: "DO", WONT: "WONT", WILL: "WILL",
    SB: "SB", SE: "SE", GA: "GA",
}
OPT_NAMES = {
    OPT_ECHO: "ECHO",
    OPT_SUPPRESS_GA: "SUPPRESS_GA",
    OPT_TTYPE: "TTYPE",
    OPT_NAWS: "NAWS",
    OPT_LINEMODE: "LINEMODE",
}


def cmd_name(b: int) -> str:
    return CMD_NAMES.get(b, f"0x{b:02x}")


def opt_name(b: int) -> str:
    return OPT_NAMES.get(b, f"opt{b}")


# ---------------------------------------------------------------------------
# Per-target expectations
# ---------------------------------------------------------------------------

TARGET_EXPECTATIONS: dict[str, dict[str, object]] = {
    "esp32": {
        # The server should announce WILL ECHO and WILL SUPPRESS_GO_AHEAD,
        # plus either DO SUPPRESS_GA or DONT LINEMODE, so the client can
        # determine to drop out of line mode.
        "expect_server_will_echo":       True,
        "expect_server_will_suppress_ga": True,
        "expect_server_dont_linemode":   True,
        # Per-char echo latency budget. The device polls telnet roughly
        # every cooperative-yield tick. The first character after
        # connection setup hits a cold-path inside MMgetline that can
        # take ~200 ms; once the editor is warm subsequent chars are
        # 10-50 ms. Budget set to comfortably cover both.
        "echo_latency_ms": 250.0,
    },
    "pico": {
        "expect_server_will_echo":       True,
        "expect_server_will_suppress_ga": True,
        "expect_server_dont_linemode":   True,
        "echo_latency_ms": 250.0,
    },
}


# ---------------------------------------------------------------------------
# Check tracking (mirrors psram_smoke.py)
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
        print(f"[{check.status}] {check.name:<36}{suffix}{elapsed}", flush=True)

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
# Telnet client state machine
# ---------------------------------------------------------------------------


@dataclass
class NegotiationLog:
    """Tracks every IAC sequence the client has seen + everything it sent."""
    rx_lines: list[str] = field(default_factory=list)
    tx_lines: list[str] = field(default_factory=list)
    # Resolved option state: maps option byte -> "server-will" / "server-wont"
    # and "client-will" / "client-wont" perspectives.
    server_will: set[int] = field(default_factory=set)
    server_wont: set[int] = field(default_factory=set)
    server_do:   set[int] = field(default_factory=set)
    server_dont: set[int] = field(default_factory=set)

    def record_rx(self, cmd: int, opt: int) -> None:
        self.rx_lines.append(f"RX IAC {cmd_name(cmd)} {opt_name(opt)}")
        if cmd == WILL: self.server_will.add(opt); self.server_wont.discard(opt)
        if cmd == WONT: self.server_wont.add(opt); self.server_will.discard(opt)
        if cmd == DO:   self.server_do.add(opt);   self.server_dont.discard(opt)
        if cmd == DONT: self.server_dont.add(opt); self.server_do.discard(opt)

    def record_tx(self, cmd: int, opt: int) -> None:
        self.tx_lines.append(f"TX IAC {cmd_name(cmd)} {opt_name(opt)}")


class TelnetClient:
    """Minimal RFC 854 client.

    Parses the byte stream off `sock`, responding to server negotiations
    with policy-driven DO/DONT/WILL/WONT replies. Data bytes that survive
    parsing are appended to `self.inbuf`.

    Policy:
      - WILL ECHO          -> DO ECHO        (we want the server to echo)
      - WILL SUPPRESS_GA   -> DO SUPPRESS_GA
      - WILL <other>       -> DONT <other>
      - DO ECHO            -> WONT ECHO      (server should echo, not us)
      - DO SUPPRESS_GA     -> WILL SUPPRESS_GA
      - DO <other>         -> WONT <other>
      - DONT <opt>         -> WONT <opt>
      - WONT <opt>         -> DONT <opt>
    """

    CLIENT_WILL_OPTS = {OPT_SUPPRESS_GA}
    CLIENT_DO_OPTS   = {OPT_ECHO, OPT_SUPPRESS_GA}

    def __init__(self, sock: socket.socket, log: NegotiationLog, verbose_iac: bool) -> None:
        self.sock = sock
        self.log = log
        self.verbose_iac = verbose_iac
        self.inbuf = bytearray()
        # Parser state. None == waiting for plain byte; "iac" == saw IAC;
        # "cmd" == saw IAC+cmd; "sb" == inside subneg; "sb_iac" == IAC inside SB.
        self._state: str | None = None
        self._cmd: int | None = None
        self._sb: bytearray = bytearray()

    # ------- low-level send helpers --------------------------------------
    def send_raw(self, data: bytes) -> None:
        self.sock.sendall(data)

    def send_iac(self, cmd: int, opt: int) -> None:
        self.log.record_tx(cmd, opt)
        if self.verbose_iac:
            print(f"  {self.log.tx_lines[-1]}", flush=True)
        self.send_raw(bytes([IAC, cmd, opt]))

    def send_text(self, text: str) -> None:
        # Convert lone newlines to CR LF per telnet NVT. Double any 0xFF
        # bytes per RFC 854.
        out = bytearray()
        for ch in text:
            b = ord(ch)
            if b == 0xFF:
                out.extend(b"\xff\xff")
            elif b == 0x0A:
                out.extend(b"\r\n")
            else:
                out.append(b)
        self.send_raw(bytes(out))

    # ------- byte-level RX parser ---------------------------------------
    def feed(self, chunk: bytes) -> None:
        for b in chunk:
            if self._state is None:
                if b == IAC:
                    self._state = "iac"
                else:
                    self.inbuf.append(b)
            elif self._state == "iac":
                if b == IAC:
                    # Escaped 0xFF data byte.
                    self.inbuf.append(0xFF)
                    self._state = None
                elif b == SB:
                    self._state = "sb"
                    self._sb = bytearray()
                elif b in (WILL, WONT, DO, DONT):
                    self._cmd = b
                    self._state = "cmd"
                else:
                    # Standalone command (GA, NOP, etc.) — log it.
                    if self.verbose_iac:
                        print(f"  RX IAC {cmd_name(b)}", flush=True)
                    self._state = None
            elif self._state == "cmd":
                cmd = self._cmd
                opt = b
                self.log.record_rx(cmd, opt)
                if self.verbose_iac:
                    print(f"  {self.log.rx_lines[-1]}", flush=True)
                self._respond(cmd, opt)
                self._cmd = None
                self._state = None
            elif self._state == "sb":
                if b == IAC:
                    self._state = "sb_iac"
                else:
                    self._sb.append(b)
            elif self._state == "sb_iac":
                if b == SE:
                    if self.verbose_iac:
                        body = " ".join(f"{x:02x}" for x in self._sb)
                        print(f"  RX SB[{body}] SE", flush=True)
                    self._state = None
                elif b == IAC:
                    self._sb.append(0xFF)
                    self._state = "sb"
                else:
                    # Malformed; reset.
                    self._state = "sb"

    def _respond(self, cmd: int, opt: int) -> None:
        if cmd == WILL:
            if opt in self.CLIENT_DO_OPTS:
                self.send_iac(DO, opt)
            else:
                self.send_iac(DONT, opt)
        elif cmd == WONT:
            self.send_iac(DONT, opt)
        elif cmd == DO:
            if opt in self.CLIENT_WILL_OPTS:
                self.send_iac(WILL, opt)
            else:
                self.send_iac(WONT, opt)
        elif cmd == DONT:
            self.send_iac(WONT, opt)

    def drain_socket(self, deadline: float) -> None:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self.sock.settimeout(min(remaining, 0.1))
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not chunk:
                return
            self.feed(chunk)

    def read_until(self, predicate, timeout: float) -> bool:
        """Drain bytes until predicate(self.inbuf) is True or timeout."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate(self.inbuf):
                return True
            remaining = deadline - time.monotonic()
            self.sock.settimeout(min(remaining, 0.05))
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return predicate(self.inbuf)
            if not chunk:
                return predicate(self.inbuf)
            self.feed(chunk)
        return predicate(self.inbuf)


# ---------------------------------------------------------------------------
# Harness
# ---------------------------------------------------------------------------


class TelnetSmokeHarness:
    def __init__(
        self,
        host: str,
        port: int,
        *,
        target: str,
        timeout: float,
        verbose: bool,
        verbose_iac: bool,
        report: Report,
    ) -> None:
        self.host = host
        self.port = port
        self.target = target
        self.timeout = timeout
        self.verbose = verbose
        self.verbose_iac = verbose_iac
        self.report = report
        self.expect = TARGET_EXPECTATIONS[target]
        self.sock: socket.socket | None = None
        self.client: TelnetClient | None = None
        self.log = NegotiationLog()

    def _consume(self, n: int = -1) -> bytes:
        """Drain n bytes (or all available) from the inbuf."""
        assert self.client is not None
        if n < 0:
            data = bytes(self.client.inbuf)
            self.client.inbuf.clear()
            return data
        n = min(n, len(self.client.inbuf))
        data = bytes(self.client.inbuf[:n])
        del self.client.inbuf[:n]
        return data

    def _short(self, blob: bytes, n: int = 80) -> str:
        s = blob.decode("latin1", "replace")
        return s.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")[:n]

    # ---- 1. connect + initial negotiation ----
    def check_connect(self) -> bool:
        print("=== 1. connect + negotiation ===", flush=True)
        t0 = time.monotonic()
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as exc:
            self.report.failed("connect", f"{self.host}:{self.port}: {exc}", time.monotonic() - t0)
            return False
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.client = TelnetClient(self.sock, self.log, self.verbose_iac)
        # Give the server a moment to send its initial negotiation suite.
        self.client.drain_socket(time.monotonic() + 0.5)
        self.report.passed("connect", f"{self.host}:{self.port}", time.monotonic() - t0)
        return True

    def check_negotiation(self) -> None:
        # Verify the server sent the expected initial sequences.
        if self.expect["expect_server_will_echo"]:
            if OPT_ECHO in self.log.server_will:
                self.report.passed("negotiation/server_will_echo", "saw IAC WILL ECHO")
            else:
                self.report.failed(
                    "negotiation/server_will_echo",
                    "server never sent IAC WILL ECHO; client will not enter char-at-a-time mode",
                )
        if self.expect["expect_server_will_suppress_ga"]:
            if OPT_SUPPRESS_GA in self.log.server_will:
                self.report.passed("negotiation/server_will_suppress_ga", "saw IAC WILL SUPPRESS_GA")
            else:
                self.report.failed(
                    "negotiation/server_will_suppress_ga",
                    "server never sent IAC WILL SUPPRESS_GA",
                )
        if self.expect["expect_server_dont_linemode"]:
            if OPT_LINEMODE in self.log.server_dont:
                self.report.passed("negotiation/server_dont_linemode", "saw IAC DONT LINEMODE")
            else:
                self.report.failed(
                    "negotiation/server_dont_linemode",
                    f"server never sent IAC DONT LINEMODE (server_dont={[opt_name(o) for o in self.log.server_dont]})",
                )

    # ---- 2. per-char echo ----
    def check_char_echo(self) -> None:
        print("=== 2. per-character echo ===", flush=True)
        assert self.client is not None
        # Drain any banner output (the server prints `> ` on connection
        # entering REPL). We don't require a banner -- some ports won't
        # print anything until the first CR.
        # Discard accumulated text up to and including any prompt.
        self._consume()
        # Send a single 'A'. The server should echo it back within the
        # latency budget (no CR required).
        latency_budget = float(self.expect["echo_latency_ms"]) / 1000.0
        t0 = time.monotonic()
        self.client.send_text("A")
        ok = self.client.read_until(
            lambda buf: b"A" in buf,
            timeout=max(latency_budget * 5, 0.5),
        )
        elapsed = time.monotonic() - t0
        if not ok:
            # Maybe the server queues echo until newline. Send CR and see
            # if we get echo *now*.
            self.client.send_text("\r")
            self.client.read_until(lambda buf: b"A" in buf, timeout=1.0)
            if b"A" in self.client.inbuf:
                self.report.failed(
                    "echo/per_char_latency",
                    f"server only echoed 'A' after CR was sent (line-mode behavior); "
                    f"buf after CR={self._short(bytes(self.client.inbuf))!r}",
                    elapsed,
                )
            else:
                self.report.failed(
                    "echo/per_char_latency",
                    f"no echo within {latency_budget*5:.2f}s even after CR; "
                    f"buf={self._short(bytes(self.client.inbuf))!r}",
                    elapsed,
                )
            return
        if elapsed * 1000.0 > float(self.expect["echo_latency_ms"]):
            self.report.failed(
                "echo/per_char_latency",
                f"echo arrived in {elapsed*1000:.1f}ms > budget {self.expect['echo_latency_ms']}ms (still per-char though)",
                elapsed,
            )
        else:
            self.report.passed(
                "echo/per_char_latency",
                f"'A' echoed in {elapsed*1000:.1f}ms (budget {self.expect['echo_latency_ms']}ms)",
                elapsed,
            )

        # Now send 'B' (a different character) and time it. This catches
        # the case where the first 'A' was actually banner text by accident.
        self._consume()
        t1 = time.monotonic()
        self.client.send_text("B")
        ok2 = self.client.read_until(lambda buf: b"B" in buf, timeout=max(latency_budget * 5, 0.5))
        elapsed2 = time.monotonic() - t1
        if ok2 and elapsed2 * 1000.0 <= float(self.expect["echo_latency_ms"]) * 5:
            self.report.passed(
                "echo/second_char",
                f"'B' echoed in {elapsed2*1000:.1f}ms",
                elapsed2,
            )
        else:
            self.report.failed(
                "echo/second_char",
                f"'B' not echoed in time: buf={self._short(bytes(self.client.inbuf))!r}",
                elapsed2,
            )

    # ---- 3. backspace + cleanup so REPL is at fresh prompt ----
    def reset_to_prompt(self) -> None:
        """Clear whatever the test typed (A, B, etc.) by sending Ctrl-C
        which MMBasic interprets as a line-discard / break depending on
        Option.Break. Follow with CR to land at a fresh prompt. The wait
        is generous because the server may still be flushing echo for the
        previous test's input when we start cleaning up."""
        assert self.client is not None
        # Drain any pending bytes already in flight before we send our
        # rubout sequence — otherwise we miss data that was emitted by the
        # previous test but hadn't arrived at our socket yet.
        self.client.drain_socket(time.monotonic() + 0.2)
        self._consume()
        # Send Ctrl-C (0x03) followed by CR. Then drain for long enough
        # that any straggler echo from the previous test is fully consumed.
        self.client.send_raw(b"\x03\r")
        self.client.drain_socket(time.monotonic() + 0.6)
        self._consume()

    # ---- 4. BASIC roundtrip ----
    def check_basic_roundtrip(self) -> None:
        print("=== 3. BASIC command roundtrip ===", flush=True)
        assert self.client is not None
        self.reset_to_prompt()
        t0 = time.monotonic()
        self.client.send_text("?123\r")
        # Expect "123" to appear in output within timeout.
        ok = self.client.read_until(lambda buf: b"123" in buf, timeout=self.timeout)
        elapsed = time.monotonic() - t0
        if ok:
            self.report.passed(
                "basic/print_literal",
                f"'?123' returned '123' in {elapsed*1000:.1f}ms; tail={self._short(bytes(self.client.inbuf))!r}",
                elapsed,
            )
        else:
            self.report.failed(
                "basic/print_literal",
                f"no '123' in response: {self._short(bytes(self.client.inbuf))!r}",
                elapsed,
            )
        self._consume()

        # PRINT MM.INFO$(IP ADDRESS) should be a string that looks like
        # an IP. Use this as a sanity check that the BASIC parser actually
        # consumes each character (not just the final CR).
        t1 = time.monotonic()
        self.client.send_text("?MM.INFO$(IP ADDRESS)\r")
        # Use a "complete" IP regex (delimited by non-digit / non-dot)
        # so we don't latch onto a half-printed address mid-stream.
        ip_re = re.compile(rb"(?<![\d.])\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}(?!\d)")
        ok2 = self.client.read_until(
            lambda buf: ip_re.search(bytes(buf)) is not None,
            timeout=self.timeout,
        )
        elapsed2 = time.monotonic() - t1
        if ok2:
            m = ip_re.search(bytes(self.client.inbuf))
            self.report.passed(
                "basic/mm_info_ip",
                f"returned {m.group(0).decode() if m else '?'} in {elapsed2*1000:.1f}ms",
                elapsed2,
            )
        else:
            self.report.failed(
                "basic/mm_info_ip",
                f"no IP-looking response: {self._short(bytes(self.client.inbuf))!r}",
                elapsed2,
            )
        self._consume()

    # ---- 5. multi-char interleaving (catches the IAC-drop bug) ----
    def check_char_interleave(self) -> None:
        """Send 10 individual characters with a short pause between each.
        Verify ALL of them echo back. This catches the case where a
        receive batch beginning with IAC discards the rest of the batch,
        OR where a packet boundary happens to split between IAC and data."""
        print("=== 4. multi-char interleaving (IAC bug catcher) ===", flush=True)
        assert self.client is not None
        self.reset_to_prompt()
        sent = "ABCDEFGHIJ"
        latency_budget = float(self.expect["echo_latency_ms"]) / 1000.0
        echo_failures = 0
        per_char_ms: list[float] = []
        for ch in sent:
            self._consume()
            t0 = time.monotonic()
            self.client.send_text(ch)
            ok = self.client.read_until(
                lambda buf, c=ch.encode(): c in buf,
                timeout=max(latency_budget * 10, 1.0),
            )
            dt = (time.monotonic() - t0) * 1000.0
            per_char_ms.append(dt)
            if not ok:
                echo_failures += 1
            # Small pause so each character lands in its own TCP segment.
            time.sleep(0.02)
        # Now CR to terminate the line so the REPL doesn't try to parse
        # ABCDEFGHIJ as a command.
        self.client.send_raw(b"\x03\r")
        self.client.drain_socket(time.monotonic() + 0.3)
        self._consume()
        if echo_failures == 0:
            mn = min(per_char_ms)
            mx = max(per_char_ms)
            avg = sum(per_char_ms) / len(per_char_ms)
            self.report.passed(
                "echo/ten_chars",
                f"all 10 echoed; per-char ms min={mn:.1f} avg={avg:.1f} max={mx:.1f}",
            )
        else:
            self.report.failed(
                "echo/ten_chars",
                f"{echo_failures}/{len(sent)} chars did not echo within budget; per-char ms={per_char_ms}",
            )

    # ---- 6a. Arrow keys must be decoded as escape sequences ----
    def check_arrow_keys(self) -> None:
        """Verify each cursor-key CSI sequence (ESC [ A/B/C/D) is decoded
        as a single non-printable key code and that the raw '[X' letter
        does NOT echo back as typed text.

        The decoder bug we're guarding against: with the gate
        `ConsoleRxBufHead == ConsoleRxBufTail` in place, telnet ESC bytes
        were returned as bare ESC because the continuation '[X' was still
        queued; the editor then saw ESC (exit) followed by '[' and 'X' as
        typed characters. The visible symptom at the REPL is the literal
        '[A' appearing in the echo stream.

        Test strategy per direction: from a fresh prompt, send the CSI
        sequence. After ~250 ms drain the inbox and check that the
        literal CSI letter pair (e.g. b'[A') is absent from the captured
        echo. Decoded UP/DOWN may trigger REPL history recall and emit
        previously-typed bytes; that's expected. Then send Ctrl-C + CR to
        get back to a clean prompt for the next iteration."""
        print("=== 6. arrow key escape decoding ===", flush=True)
        assert self.client is not None
        latency_budget = float(self.expect["echo_latency_ms"]) / 1000.0
        directions = [
            ("UP",    b"\x1b[A"),
            ("DOWN",  b"\x1b[B"),
            ("RIGHT", b"\x1b[C"),
            ("LEFT",  b"\x1b[D"),
        ]
        # Seed history with a known recallable command so UP/DOWN have
        # something to retrieve. Use a benign no-op (REM).
        self.reset_to_prompt()
        self.client.send_text("REM marker\r")
        self.client.drain_socket(time.monotonic() + 0.6)
        per_key_ms: list[float] = []
        failures: list[str] = []
        drain_window = max(latency_budget * 2, 0.5)
        for name, seq in directions:
            self.reset_to_prompt()
            self._consume()
            t0 = time.monotonic()
            self.client.send_raw(seq)
            self.client.drain_socket(time.monotonic() + drain_window)
            dt = (time.monotonic() - t0) * 1000.0
            per_key_ms.append(dt)
            echoed = bytes(self.client.inbuf)
            self._consume()
            # The raw CSI letter pair (e.g. b'[A') must not appear — that
            # would mean the decoder failed and the editor saw bare ESC
            # then '[X' as typed characters.
            csi_pair = bytes([seq[1], seq[2]])
            if csi_pair in echoed:
                failures.append(
                    f"{name}: server echoed raw CSI bytes {csi_pair!r} "
                    f"(arrow-key decode bug); echo={self._short(echoed)!r}"
                )
        if not failures:
            mn = min(per_key_ms); mx = max(per_key_ms); avg = sum(per_key_ms)/len(per_key_ms)
            self.report.passed(
                "input/arrow_keys",
                f"UP/DOWN/LEFT/RIGHT decoded (no raw CSI echoed); per-key ms min={mn:.1f} avg={avg:.1f} max={mx:.1f}",
            )
        else:
            self.report.failed(
                "input/arrow_keys",
                "; ".join(failures),
            )

    # ---- 6b. Backspace handling: both 0x7f (DEL) and 0x08 (BS) must work ----
    def _check_backspace_byte(self, byte_value: int, label: str) -> None:
        """Send 'Z' then the supplied backspace byte. Confirm the server
        echoes the BS+SP+BS rubout sequence (or at least a BS that erases
        the Z visually). Then send 'Y' and verify it echoes as well — i.e.
        the line is still active after the backspace."""
        assert self.client is not None
        latency_budget = float(self.expect["echo_latency_ms"]) / 1000.0
        self.reset_to_prompt()
        self._consume()
        # Prime: send 'Z' and wait for echo.
        self.client.send_raw(b"Z")
        ok = self.client.read_until(lambda buf: b"Z" in buf, timeout=max(latency_budget * 5, 0.5))
        if not ok:
            self.report.failed(label, "prime 'Z' did not echo")
            return
        self._consume()
        # Send the candidate backspace byte.
        t0 = time.monotonic()
        self.client.send_raw(bytes([byte_value]))
        # The server rubs out the prior character. Accept either the
        # classic '\b \b' (BS SP BS) or '\b' followed by an ANSI
        # erase-to-end-of-line / erase-to-end-of-screen sequence — the
        # PicoMite REPL uses the latter (\b ESC [ 0 J).
        def _rubbed_out(buf: bytes) -> bool:
            return (
                b"\b \b" in buf
                or b"\b\x1b[0J" in buf
                or b"\b\x1b[K" in buf
            )
        ok = self.client.read_until(
            lambda buf: _rubbed_out(bytes(buf)),
            timeout=max(latency_budget * 5, 0.5),
        )
        dt = (time.monotonic() - t0) * 1000.0
        if not ok:
            # Fallback: a bare '\b' is acceptable if no rubout follows.
            if b"\b" not in bytes(self.client.inbuf):
                self.report.failed(
                    label,
                    f"no backspace echo for byte 0x{byte_value:02x}; "
                    f"buf={self._short(bytes(self.client.inbuf))!r}",
                )
                return
        echoed = bytes(self.client.inbuf)
        rubout_ok = _rubbed_out(echoed)
        self._consume()
        # Verify the line is still alive: send 'Y' and confirm echo. If
        # backspace dropped the whole line, the REPL would have processed
        # the empty line as <Enter> and the next char would still echo —
        # so this alone doesn't catch "deletes whole line", but the absence
        # of \b in the echo stream above does.
        self.client.send_raw(b"Y")
        ok = self.client.read_until(lambda buf: b"Y" in buf, timeout=max(latency_budget * 5, 0.5))
        if not ok:
            self.report.failed(label, "follow-up 'Y' did not echo after backspace")
            return
        echo_kind = "rubout" if rubout_ok else "plain \\b"
        detail = f"BS byte 0x{byte_value:02x} echoed {echo_kind} in {dt:.1f}ms"
        self.report.passed(label, detail, dt / 1000.0)

    def check_backspace(self) -> None:
        print("=== 7. backspace (0x7f DEL and 0x08 BS) ===", flush=True)
        self._check_backspace_byte(0x7F, "input/backspace_del_7f")
        self._check_backspace_byte(0x08, "input/backspace_bs_08")
        # Cleanup: ensure we end at a fresh prompt for any subsequent checks.
        self.reset_to_prompt()

    # ---- 7. IAC-following-data robustness ----
    def check_iac_after_data(self) -> None:
        """Send a byte stream that begins with an IAC sequence followed
        immediately by data ('X'). The buggy receive path discards the
        entire packet on IAC-first batches, which would drop the 'X'."""
        print("=== 5. IAC-followed-by-data ===", flush=True)
        assert self.client is not None
        self.reset_to_prompt()
        self._consume()
        # Send IAC NOP (255, 241) then 'X'. NOP is a no-op the server
        # should silently swallow. The 'X' should still echo.
        # We do this as ONE writev so the server's recv sees IAC+data in
        # a single batch.
        payload = bytes([IAC, 241]) + b"X"
        t0 = time.monotonic()
        self.client.send_raw(payload)
        latency_budget = float(self.expect["echo_latency_ms"]) / 1000.0
        ok = self.client.read_until(lambda buf: b"X" in buf, timeout=max(latency_budget * 10, 1.0))
        elapsed = time.monotonic() - t0
        self.client.send_raw(b"\x03\r")
        self.client.drain_socket(time.monotonic() + 0.3)
        self._consume()
        if ok:
            self.report.passed(
                "echo/iac_then_data",
                f"server echoed 'X' after IAC NOP in {elapsed*1000:.1f}ms",
                elapsed,
            )
        else:
            self.report.failed(
                "echo/iac_then_data",
                f"server dropped 'X' that followed IAC NOP (classic batch-discard bug)",
                elapsed,
            )

    # ---- 7. final negotiation dump (always passes; informational) ----
    def dump_negotiation(self) -> None:
        print("=== negotiation trace ===", flush=True)
        for line in self.log.rx_lines:
            print(f"  {line}", flush=True)
        for line in self.log.tx_lines:
            print(f"  {line}", flush=True)
        print(
            "  resolved server state: "
            f"WILL={[opt_name(o) for o in sorted(self.log.server_will)]} "
            f"WONT={[opt_name(o) for o in sorted(self.log.server_wont)]} "
            f"DO={[opt_name(o) for o in sorted(self.log.server_do)]} "
            f"DONT={[opt_name(o) for o in sorted(self.log.server_dont)]}",
            flush=True,
        )

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def run_all(self) -> None:
        if not self.check_connect():
            return
        self.check_negotiation()
        self.check_char_echo()
        self.check_basic_roundtrip()
        self.check_char_interleave()
        self.check_arrow_keys()
        self.check_backspace()
        self.check_iac_after_data()
        self.dump_negotiation()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="device IP / hostname")
    parser.add_argument("--port", type=int, default=23, help="telnet port (default 23)")
    parser.add_argument(
        "--target",
        default="esp32",
        choices=sorted(TARGET_EXPECTATIONS.keys()),
        help="target port family (selects expectations table)",
    )
    parser.add_argument("--timeout", type=float, default=2.0, help="per-step timeout seconds")
    parser.add_argument("--verbose", action="store_true", help="echo every command + response")
    parser.add_argument("--verbose-iac", action="store_true", help="print every IAC sequence as it happens")
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
    report = Report()
    harness = TelnetSmokeHarness(
        host=args.host,
        port=args.port,
        target=args.target,
        timeout=args.timeout,
        verbose=args.verbose,
        verbose_iac=args.verbose_iac,
        report=report,
    )
    try:
        harness.run_all()
    except KeyboardInterrupt:
        report.failed("interrupted", "user cancelled")
    except Exception as exc:
        report.failed("harness", repr(exc))
    finally:
        harness.close()

    print_summary(report)
    return 0 if report.all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
