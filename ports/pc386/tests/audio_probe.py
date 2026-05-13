#!/usr/bin/env python3
"""
ports/pc386/tests/audio_probe.py — QEMU SB16 WAV-output probe.

Boots pc386, drives BASIC over COM1, records QEMU's SB16 backend to a WAV
file, and verifies that PLAY TONE produces non-silent PCM samples.

Run:
  python3 ports/pc386/tests/audio_probe.py
  python3 ports/pc386/tests/audio_probe.py --out /tmp/pc386-sb16.wav
"""

import argparse
import array
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time
import wave

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL = os.path.join(PORT_DIR, "build", "mmbasic.elf")
C_IMG = os.path.join(PORT_DIR, "test_disks", "c.img")
DEFAULT_OUT = os.path.join(PORT_DIR, "build", "audio_probe-sb16.wav")

ANSI_RE = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")
PROMPT_TAIL_RE = re.compile(rb"(?:\r?\n)?> (?:\x1b\[\?25h)?\Z")


def strip_ansi(data: bytes) -> bytes:
    return ANSI_RE.sub(b"", data)


def read_until_prompt(proc: subprocess.Popen, timeout: float, label: str) -> bytes:
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rfds, _, _ = select.select([proc.stdout], [], [], 0.05)
        if proc.stdout in rfds:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
        elif PROMPT_TAIL_RE.search(buf):
            return buf
        if proc.poll() is not None:
            break
    raise TimeoutError(
        f"{label}: timeout waiting for prompt\n"
        + strip_ansi(buf)[-2000:].decode("utf-8", errors="replace")
    )


def send_basic(proc: subprocess.Popen, line: str, timeout: float = 20) -> str:
    proc.stdin.write((line + "\n").encode("utf-8"))
    proc.stdin.flush()
    out = strip_ansi(read_until_prompt(proc, timeout, line)).decode("utf-8", errors="replace")
    if "Error :" in out or "*** PANIC" in out or "*** EXCEPTION" in out:
        raise RuntimeError(f"BASIC command failed: {line}\n{out}")
    return out


def wav_stats(path: str) -> tuple[int, int, int, int]:
    with wave.open(path, "rb") as fp:
        frames = fp.getnframes()
        width = fp.getsampwidth()
        data = fp.readframes(frames)

    if width == 1:
        vals = [b - 128 for b in data]
    elif width == 2:
        vals = array.array("h")
        vals.frombytes(data)
        if sys.byteorder != "little":
            vals.byteswap()
    else:
        raise ValueError(f"unsupported sample width {width}")

    if not vals:
        return frames, 0, 0, 0
    mn = min(vals)
    mx = max(vals)
    avg_abs = sum(abs(v) for v in vals) // len(vals)
    return frames, mn, mx, avg_abs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=DEFAULT_OUT, help="WAV path to write")
    args = parser.parse_args()

    if not os.path.exists(KERNEL):
        print(f"error: {KERNEL} not found. Run `make -C ports/pc386` first.", file=sys.stderr)
        return 2
    if not os.path.exists(C_IMG):
        print(f"error: {C_IMG} not found. Run `ports/pc386/build_disks.sh` first.", file=sys.stderr)
        return 2

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    try:
        os.unlink(args.out)
    except FileNotFoundError:
        pass

    sb_base_raw = os.environ.get("PC386_SB_BASE", "0x220")
    sb_base_basic = f"&H{int(sb_base_raw, 0):X}"
    sb_irq = os.environ.get("PC386_SB_IRQ", "5")
    sb_dma = os.environ.get("PC386_SB_DMA", "1")
    sb_dma16 = os.environ.get("PC386_SB_DMA16", "5")

    with tempfile.TemporaryDirectory(prefix="pc386-audio-") as td:
        c_img = os.path.join(td, "c.img")
        shutil.copyfile(C_IMG, c_img)
        proc = subprocess.Popen(
            [
                "qemu-system-i386",
                "-kernel", KERNEL,
                "-m", "16M",
                "-vga", "std",
                "-display", "none",
                "-serial", "stdio",
                "-parallel", f"file:{os.path.join(td, 'lpt1.out')}",
                "-no-reboot", "-no-shutdown",
                "-d", "guest_errors",
                "-drive", f"file={c_img},format=raw,if=ide,index=0",
                "-machine", "pc",
                "-audiodev", f"wav,id=sb16,path={os.path.abspath(args.out)}",
                "-device", f"sb16,audiodev=sb16,iobase={sb_base_raw},irq={sb_irq},dma={sb_dma},dma16={sb_dma16}",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        try:
            read_until_prompt(proc, 20, "boot")
            send_basic(proc, f"SB16 {sb_base_basic}, {sb_irq}, {sb_dma}, {sb_dma16}")
            send_basic(proc, "PLAY TONE 440, 440, 1500", 25)
            send_basic(proc, "PLAY STOP")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    if not os.path.exists(args.out):
        print("[FAIL] QEMU did not create WAV output")
        return 1
    frames, mn, mx, avg_abs = wav_stats(args.out)
    print(f"wav: {args.out}")
    print(f"frames={frames} min={mn} max={mx} avg_abs={avg_abs}")
    if frames < 20000 or avg_abs < 1000 or mx <= mn:
        print("[FAIL] WAV output is too short or silent")
        return 1
    print("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
