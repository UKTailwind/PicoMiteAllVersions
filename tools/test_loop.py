#!/usr/bin/env python3
"""
test_loop.py — flash + boot + run BAS + curl, in a loop.

Each iteration:
  1. Flash build_dvi_wifi/PicoMite.elf via OpenOCD over SWD (skipped with --no-flash)
  2. Wait for target USB-CDC to enumerate
  3. Drain boot banner
  4. Type `RUN "B:server.bas"` (configurable)
  5. Wait for an IPv4 to appear in the output (or use --ip)
  6. curl http://<ip>:<port>/  with a short timeout
  7. Send Ctrl-C, log everything to /tmp/test_loop/iterNNN.log

Usage examples:
    tools/test_loop.py                              # 1 iter, default elf
    tools/test_loop.py --iters 10 --ip 192.168.4.5  # 10 iters, skip IP parse
    tools/test_loop.py --no-flash                   # use whatever's already on the chip
    tools/test_loop.py --bas mytest.bas             # different BAS file

Notes:
  - The dev board is the /dev/cu.usbmodem* that is NOT the Debug Probe.
    Probe is hard-coded as /dev/cu.usbmodem102; override with --probe-port.
  - server.bas's HTTP port defaults to 80; pass --port if your BAS file binds
    elsewhere.
  - All console traffic from the chip (BASIC banner, printf debug traces) is
    captured to the iteration log file *and* echoed live to stdout.
"""

import argparse
import glob
import os
import re
import select
import subprocess
import sys
import termios
import time

OPENOCD     = os.path.expanduser("~/.pico-sdk/openocd/0.12.0+dev/openocd")
OPENOCD_DIR = os.path.expanduser("~/.pico-sdk/openocd/0.12.0+dev/scripts")
DEFAULT_ELF = "/Users/joshv/picocalc/PicoMiteAllVersions/build_dvi_wifi/PicoMite.elf"
DEFAULT_PROBE_PORT = "/dev/cu.usbmodem102"


def flash(elf):
    proc = subprocess.run(
        [OPENOCD,
         "-s", OPENOCD_DIR,
         "-f", "interface/cmsis-dap.cfg",
         "-c", "adapter speed 1000",
         "-f", "target/rp2350.cfg",
         "-c", "init",
         "-c", "halt",
         "-c", f"program {elf} verify reset",
         "-c", "exit"],
        capture_output=True, text=True, timeout=120)
    blob = proc.stdout + proc.stderr
    if "Verified OK" not in blob:
        sys.stderr.write(blob)
        return False
    return True


def wait_for_target_port(probe_port, timeout, stable_for=1.5):
    """Block until a non-probe /dev/cu.usbmodem* appears AND remains stable
    for `stable_for` seconds (filters the transient (dis)appearances around
    a USB reset)."""
    deadline = time.time() + timeout
    candidate = None
    stable_since = None
    while time.time() < deadline:
        ports = [p for p in sorted(glob.glob("/dev/cu.usbmodem*")) if p != probe_port]
        now = time.time()
        if ports:
            current = ports[0]
            if current == candidate:
                if now - stable_since >= stable_for:
                    return current
            else:
                candidate = current
                stable_since = now
        else:
            candidate = None
            stable_since = None
        time.sleep(0.2)
    return None


def open_serial_retry(path, attempts=10, gap=0.2):
    """Open the tty, retrying briefly if it's mid-enumeration."""
    last = None
    for _ in range(attempts):
        try:
            return open_serial(path)
        except (FileNotFoundError, OSError) as e:
            last = e
            time.sleep(gap)
    raise last


def open_serial(path, baud=115200):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    cflag = (cflag & ~termios.CSIZE) | termios.CS8 | termios.CREAD | termios.CLOCAL
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    speed = getattr(termios, f"B{baud}", termios.B115200)
    termios.tcsetattr(fd, termios.TCSANOW, [0, 0, cflag, 0, speed, speed, cc])
    return fd


def read_for(fd, timeout, log_fh, sentinel=None):
    """Read bytes for `timeout` seconds (or until sentinel appears).
    Echoes to log_fh and stdout. Returns the cumulative bytes."""
    buf = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = deadline - time.time()
        rlist, _, _ = select.select([fd], [], [], min(0.2, remaining))
        if rlist:
            chunk = os.read(fd, 4096)
            if chunk:
                buf.extend(chunk)
                log_fh.write(chunk)
                log_fh.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                if sentinel and sentinel in buf:
                    return bytes(buf)
    return bytes(buf)


def write_line(fd, s):
    os.write(fd, (s + "\r\n").encode())


def curl(url, timeout):
    return subprocess.run(
        ["curl", "-sS", "--max-time", str(timeout),
         "-w", "\n--- HTTP_CODE=%{http_code} TIME=%{time_total} ---\n", url],
        capture_output=True, text=True, timeout=timeout + 3)


def run_iteration(it, args, logdir):
    print(f"\n=== iter {it} ===", flush=True)
    log_path = os.path.join(logdir, f"iter{it:03d}.log")
    log_fh = open(log_path, "wb")

    if not args.no_flash:
        print("[flash] ...", flush=True)
        if not flash(args.elf):
            print("[flash] FAILED", flush=True)
            log_fh.close()
            return False

    print("[wait] target USB-CDC ...", flush=True)
    port = wait_for_target_port(args.probe_port, timeout=15)
    if not port:
        print("[wait] target didn't enumerate within 15s", flush=True)
        log_fh.close()
        return False
    print(f"[wait] target = {port}", flush=True)

    fd = open_serial_retry(port)

    print("[banner] draining ...", flush=True)
    read_for(fd, args.banner_timeout, log_fh, sentinel=b">")

    if args.ip:
        ip = args.ip
    else:
        # One-shot query. If --ip isn't given the caller is implicitly OK
        # waiting for WiFi to settle on its own; we ask BASIC for the
        # address and parse it. No polling loop.
        os.write(fd, b"\r\n")
        read_for(fd, 0.4, log_fh)
        write_line(fd, '?MM.INFO$(IP ADDRESS)')
        data = read_for(fd, 2.5, log_fh, sentinel=b">")
        ip = None
        for cand in reversed(re.findall(rb"(\d+\.\d+\.\d+\.\d+)", data)):
            s = cand.decode()
            if not s.startswith(("0.", "127.", "255.")):
                ip = s
                break
        if not ip:
            print("[run] IP query returned no usable address; pass --ip", flush=True)
            os.close(fd)
            log_fh.close()
            return False
    print(f"[run] device IP = {ip}", flush=True)

    url = f"http://{ip}:{args.port}{args.path}"

    def do_curl(label, expect_codes):
        print(f"[curl/{label}] {url}", flush=True)
        try:
            proc = curl(url, timeout=args.curl_timeout)
            log_fh.write(f"\n--- CURL/{label} stdout ---\n".encode())
            log_fh.write(proc.stdout.encode())
            log_fh.write(f"\n--- CURL/{label} stderr ---\n".encode())
            log_fh.write(proc.stderr.encode())
            body_preview = proc.stdout[:200].replace("\n", " | ")
            print(f"[curl/{label}] rc={proc.returncode} {body_preview}", flush=True)
            return proc.returncode == 0 and any(f"HTTP_CODE={c}" in proc.stdout
                                                for c in expect_codes)
        except subprocess.TimeoutExpired:
            log_fh.write(f"\n--- CURL/{label} TIMED OUT ---\n".encode())
            print(f"[curl/{label}] TIMED OUT", flush=True)
            return False

    # Phase 1: REPL only — firmware-side TCP fallback should answer 404.
    pre_ok = do_curl("pre", expect_codes=("404",))
    read_for(fd, 0.3, log_fh)

    if args.skip_run:
        if args.idle_secs > 0:
            print(f"[idle] sleeping {args.idle_secs}s before post-curl", flush=True)
            read_for(fd, args.idle_secs, log_fh)
        if args.repl_cmd:
            print(f"[repl] >> {args.repl_cmd!r}", flush=True)
            write_line(fd, args.repl_cmd)
            read_for(fd, args.repl_wait, log_fh, sentinel=b">")
        print("[run] SKIPPED — going straight to post probes", flush=True)
        post_ok = do_curl("post-no-run", expect_codes=("404",))
        read_for(fd, args.post_curl_drain, log_fh)
        os.close(fd)
        log_fh.close()
        print(f"[log] {log_path}  post(no-run)={'OK' if post_ok else 'FAIL'}", flush=True)
        return post_ok

    # Phase 2: launch BASIC. Either inline (NEW + numbered lines) for
    # bisection or B:<bas>.
    if args.inline:
        print(f"[run] inline: {args.inline}", flush=True)
        write_line(fd, "NEW")
        read_for(fd, 0.3, log_fh, sentinel=b">")
        for i, line in enumerate(args.inline.split(";")):
            write_line(fd, f"{(i+1)*10} {line.strip()}")
            read_for(fd, 0.15, log_fh)
        write_line(fd, "RUN")
        read_for(fd, args.run_settle, log_fh)
    else:
        print(f"[run] B: + RUN \"{args.bas}\"", flush=True)
        write_line(fd, "B:")
        read_for(fd, 0.5, log_fh, sentinel=b">")
        write_line(fd, f'RUN "{args.bas}"')
        read_for(fd, args.run_settle, log_fh)

    post_ok = do_curl("post", expect_codes=("200",))
    # Triangulate where the SYN is getting lost. Time each probe so we
    # can distinguish RST (sub-100 ms) from timeout (== timeout knob).
    def timed(label, args_, timeout_s):
        t0 = time.time()
        p = subprocess.run(args_, capture_output=True, text=True)
        dt = time.time() - t0
        return p, dt
    p, dt = timed("ping", ["ping", "-c", "1", "-W", "1500", ip], 2)
    print(f"[probe/ping]      rc={p.returncode} dt={dt:.3f}s", flush=True)
    log_fh.write(f"\n--- PING dt={dt:.3f}s ---\n".encode()); log_fh.write(p.stdout.encode())
    p, dt = timed("nc1234", ["nc", "-z", "-G", "2", "-w", "2", ip, "1234"], 3)
    print(f"[probe/nc :1234]  rc={p.returncode} dt={dt:.3f}s ({'fast→RST' if dt<0.5 else 'timeout'})", flush=True)
    log_fh.write(f"\n--- NC :1234 rc={p.returncode} dt={dt:.3f}s ---\n".encode())
    p, dt = timed("nc8080", ["nc", "-z", "-G", "2", "-w", "2", ip, str(args.port)], 3)
    print(f"[probe/nc :{args.port}]  rc={p.returncode} dt={dt:.3f}s ({'fast→RST' if dt<0.5 else 'timeout'})", flush=True)
    log_fh.write(f"\n--- NC :{args.port} rc={p.returncode} dt={dt:.3f}s ---\n".encode())

    read_for(fd, args.post_curl_drain, log_fh)

    ok = pre_ok and post_ok

    # Break out of server.bas, query WiFi/TCP link status, re-probe
    os.write(fd, b"\x03")
    read_for(fd, 0.6, log_fh, sentinel=b">")
    write_line(fd, "?MM.INFO(WIFI STATUS), MM.INFO(TCPIP STATUS)")
    read_for(fd, 1.0, log_fh, sentinel=b">")
    p, dt = timed("ping", ["ping", "-c", "1", "-W", "1500", ip], 2)
    print(f"[probe/ping after Ctrl-C]   rc={p.returncode} dt={dt:.3f}s", flush=True)
    log_fh.write(f"\n--- PING after Ctrl-C dt={dt:.3f}s ---\n".encode())
    p, dt = timed("nc8080-after", ["nc", "-z", "-G", "2", "-w", "2", ip, str(args.port)], 3)
    print(f"[probe/nc :{args.port} after Ctrl-C]   rc={p.returncode} dt={dt:.3f}s", flush=True)

    os.close(fd)
    log_fh.close()
    print(f"[log] {log_path}  result={'OK' if ok else 'FAIL'}", flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf",            default=DEFAULT_ELF)
    ap.add_argument("--probe-port",     default=DEFAULT_PROBE_PORT)
    ap.add_argument("--no-flash",       action="store_true")
    ap.add_argument("--ip",             default=None)
    ap.add_argument("--port",           type=int, default=8080)
    ap.add_argument("--path",           default="/")
    ap.add_argument("--bas",            default="server.bas")
    ap.add_argument("--inline",         default=None,
                    help="Inline numbered program; statements separated by ';'. "
                         "Example: 'DO ; PAUSE 5 ; LOOP'")
    ap.add_argument("--skip-run",       action="store_true",
                    help="Don't run any BASIC. Just pre-curl + repeat curl.")
    ap.add_argument("--idle-secs",      type=float, default=0,
                    help="Sleep this many seconds at REPL between pre and post curl.")
    ap.add_argument("--repl-cmd",       default=None,
                    help="(--skip-run) Type one immediate REPL command between curls. e.g. 'PAUSE 100'")
    ap.add_argument("--repl-wait",      type=float, default=2.0,
                    help="(--repl-cmd) seconds to wait for the cmd to complete")
    ap.add_argument("--iters",          type=int, default=1)
    ap.add_argument("--logdir",         default="/tmp/test_loop")
    ap.add_argument("--banner-timeout", type=float, default=8.0,
                    help="seconds to wait for the BASIC `>` prompt after boot")
    ap.add_argument("--ip-wait",        type=float, default=20.0,
                    help="seconds to wait for IPv4 to appear after RUN")
    ap.add_argument("--run-settle",     type=float, default=4.0,
                    help="(--ip mode) seconds to let server.bas bind before curl")
    ap.add_argument("--curl-timeout",   type=float, default=8.0)
    ap.add_argument("--post-curl-drain", type=float, default=2.0,
                    help="seconds of console traffic to capture after curl returns")
    args = ap.parse_args()

    os.makedirs(args.logdir, exist_ok=True)

    fails = 0
    for i in range(1, args.iters + 1):
        if not run_iteration(i, args, args.logdir):
            fails += 1
    print(f"\n=== {args.iters - fails}/{args.iters} OK ===")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
