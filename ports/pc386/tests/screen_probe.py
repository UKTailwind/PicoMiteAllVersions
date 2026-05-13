#!/usr/bin/env python3
"""
ports/pc386/tests/screen_probe.py — QEMU VGA screenshot probe.

Boots pc386 under QEMU, drives BASIC over COM1, asks QEMU/QMP for a
screen dump, parses the PPM, and samples known pixels from the actual
VGA scanout surface.

Run:
  python3 ports/pc386/tests/screen_probe.py
  python3 ports/pc386/tests/screen_probe.py --out /tmp/pc386.ppm
  PC386_BOOT=kernel python3 ports/pc386/tests/screen_probe.py
"""

import argparse
import os
import re
import shutil
import select
import subprocess
import sys
import tempfile
import time

PORT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL = os.path.join(PORT_DIR, "build", "mmbasic.elf")
C_IMG = os.environ.get("PC386_C_IMG", os.path.join(PORT_DIR, "test_disks", "c.img"))
F_IMG = os.environ.get("PC386_FLOPPY_IMG", os.path.join(PORT_DIR, "test_disks", "pc386-floppy.img"))
QEMU_VGA = os.environ.get("PC386_QEMU_VGA", "std")
DEFAULT_OUT = os.path.join(PORT_DIR, "build", "screen_probe.ppm")

ANSI_RE = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")
PROMPT_TAIL_RE = re.compile(rb"(?:\r?\n)?> (?:\x1b\[\?25h)?\Z")


def strip_ansi(data: bytes) -> bytes:
    return ANSI_RE.sub(b"", data)


def read_until_prompt_fd(fd: int, timeout: float, label: str) -> bytes:
    buf = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        rfds, _, _ = select.select([fd], [], [], min(remaining, 0.05))
        if fd in rfds:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            buf += chunk
        else:
            if PROMPT_TAIL_RE.search(buf):
                return buf
    raise TimeoutError(
        f"{label}: timeout after {timeout}s waiting for prompt.\n"
        f"--- captured {len(buf)} bytes ---\n"
        f"{strip_ansi(buf)[-2000:].decode('utf-8', errors='replace')}\n"
        "--------------------------------"
    )


class Hmp:
    def __init__(self, proc: subprocess.Popen):
        self.proc = proc
        self._read_prompt()

    def close(self) -> None:
        return None

    def _read_prompt(self, timeout: float = 5.0) -> bytes:
        buf = b""
        deadline = time.monotonic() + timeout
        fd = self.proc.stdout.fileno()
        while time.monotonic() < deadline:
            rfds, _, _ = select.select([fd], [], [], 0.05)
            if fd in rfds:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                buf += chunk
                if buf.rstrip().endswith(b"(qemu)"):
                    return buf
            if self.proc.poll() is not None:
                break
        raise RuntimeError(
            "QEMU monitor prompt not available.\n"
            + buf[-2000:].decode("utf-8", errors="replace")
        )

    def cmd(self, command: str) -> bytes:
        self.proc.stdin.write((command + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        return self._read_prompt()

    def screendump(self, filename: str) -> None:
        self.cmd(f"screendump {filename}")

    def send_key(self, qcode: str) -> None:
        self.cmd(f"sendkey {qcode}")


def qemu_args(serial_base: str, tmpdir: str) -> list[str]:
    boot_mode = os.environ.get("PC386_BOOT", "floppy")
    args = [
        "qemu-system-i386",
        "-m", "16M",
        "-vga", QEMU_VGA,
        "-display", "none",
        "-monitor", "stdio",
        "-serial", f"pipe:{serial_base}",
        "-no-reboot", "-no-shutdown",
        "-d", "guest_errors",
    ]
    if os.path.exists(F_IMG):
        dst = os.path.join(tmpdir, "pc386-floppy.img")
        shutil.copyfile(F_IMG, dst)
        args += ["-drive", f"file={dst},format=raw,if=floppy,index=0"]
    if os.path.exists(C_IMG):
        dst = os.path.join(tmpdir, "c.img")
        shutil.copyfile(C_IMG, dst)
        args += ["-drive", f"file={dst},format=raw,if=ide,index=0"]
    if boot_mode == "kernel":
        args[1:1] = ["-kernel", KERNEL]
    elif boot_mode == "floppy":
        if not os.path.exists(F_IMG):
            raise FileNotFoundError(f"{F_IMG} not found. Run `ports/pc386/build_disks.sh` first.")
        args[1:1] = ["-boot", "a"]
    else:
        raise ValueError("PC386_BOOT must be one of: kernel floppy")
    return args


def send_basic(serial_in_fd: int, serial_out_fd: int, line: str) -> str:
    os.write(serial_in_fd, (line + "\n").encode("utf-8"))
    got = read_until_prompt_fd(serial_out_fd, 15, line)
    text = strip_ansi(got).decode("utf-8", errors="replace")
    if "Error :" in text:
        raise RuntimeError(f"BASIC command failed: {line}\n{text}")
    return text


def parse_ppm(path: str) -> tuple[int, int, bytes]:
    with open(path, "rb") as fp:
        data = fp.read()

    tokens: list[bytes] = []
    i = 0
    while len(tokens) < 4:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if i < len(data) and data[i] == ord("#"):
            while i < len(data) and data[i] not in b"\r\n":
                i += 1
            continue
        start = i
        while i < len(data) and data[i] not in b" \t\r\n":
            i += 1
        tokens.append(data[start:i])

    if tokens[0] != b"P6":
        raise ValueError(f"{path}: expected P6 PPM, got {tokens[0]!r}")
    width = int(tokens[1])
    height = int(tokens[2])
    maxval = int(tokens[3])
    if maxval != 255:
        raise ValueError(f"{path}: unsupported maxval {maxval}")
    while i < len(data) and data[i] in b" \t\r\n":
        i += 1
    pixels = data[i:]
    expected = width * height * 3
    if len(pixels) < expected:
        raise ValueError(f"{path}: truncated pixel data")
    return width, height, pixels[:expected]


def pixel(pixels: bytes, width: int, x: int, y: int,
          scale: int = 1) -> tuple[int, int, int]:
    sx = x * scale + scale // 2
    sy = y * scale + scale // 2
    off = (sy * width + sx) * 3
    return pixels[off], pixels[off + 1], pixels[off + 2]


def logical_scale(width: int, height: int, logical_w: int, logical_h: int) -> int:
    if width < logical_w or height < logical_h:
        return 0
    if logical_w == 320 and logical_h == 200 and width == 640 and height >= 400:
        return 2
    if width % logical_w == 0 and height % logical_h == 0:
        sx = width // logical_w
        sy = height // logical_h
        if sx == sy:
            return sx
    return 1


def close_enough(got: tuple[int, int, int], want: tuple[int, int, int],
                 tolerance: int = 12) -> bool:
    return all(abs(g - w) <= tolerance for g, w in zip(got, want))


def nonblack_rows(pixels: bytes, width: int, height: int) -> list[int]:
    rows: list[int] = []
    for y in range(height):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        if any(row[i] or row[i + 1] or row[i + 2]
               for i in range(0, len(row), 3)):
            rows.append(y)
    return rows


def diff_count(a: bytes, b: bytes, width: int,
               x1: int, y1: int, x2: int, y2: int) -> int:
    changed = 0
    for y in range(y1, y2 + 1):
        row = y * width * 3
        for x in range(x1, x2 + 1):
            off = row + x * 3
            if a[off:off + 3] != b[off:off + 3]:
                changed += 1
    return changed


def bbox_for_new_pixels(pixels: bytes, width: int, height: int) -> tuple[int, int, int, int] | None:
    x_min, y_min = width, height
    x_max, y_max = -1, -1
    for y in range(height):
        row = y * width * 3
        for x in range(width):
            off = row + x * 3
            if pixels[off] or pixels[off + 1] or pixels[off + 2]:
                if x < x_min: x_min = x
                if y < y_min: y_min = y
                if x > x_max: x_max = x
                if y > y_max: y_max = y
    if x_max < 0:
        return None
    return x_min, y_min, x_max, y_max


def line_bbox(pixels: bytes, width: int, y1: int, y2: int) -> tuple[int, int, int, int] | None:
    height = len(pixels) // (width * 3)
    y1 = max(0, y1)
    y2 = min(height - 1, y2)
    x_min, y_min = width, height
    x_max, y_max = -1, -1
    for y in range(y1, y2 + 1):
        row = y * width * 3
        for x in range(width):
            off = row + x * 3
            if pixels[off] or pixels[off + 1] or pixels[off + 2]:
                if x < x_min: x_min = x
                if y < y_min: y_min = y
                if x > x_max: x_max = x
                if y > y_max: y_max = y
    if x_max < 0:
        return None
    return x_min, y_min, x_max, y_max


def check_aa_backspace(hmp: Hmp, serial_in_fd: int, serial_out_fd: int,
                       out_dir: str,
                       prefix: str, send_a, send_backspace,
                       font_w: int, font_h: int, prompt_cols: int,
                       scale: int, origin_y: int = 0,
                       reset: bool = True) -> bool:
    if reset:
        send_basic(serial_in_fd, serial_out_fd, "CLS RGB(0,0,0)")
    send_a()
    time.sleep(0.08)
    one_a_dump = os.path.join(out_dir, f"screen_probe-{prefix}-one-a.ppm")
    hmp.screendump(one_a_dump)
    one_w, one_h, one_pixels = parse_ppm(one_a_dump)
    bbox = bbox_for_new_pixels(one_pixels, one_w, one_h)
    if bbox is None:
        print(f"[FAIL] {prefix}: first 'a' produced no pixels")
        return False

    send_a()
    time.sleep(0.08)
    send_backspace()
    time.sleep(0.12)
    backspace_dump = os.path.join(out_dir, f"screen_probe-{prefix}-aa-backspace.ppm")
    hmp.screendump(backspace_dump)
    after_w, after_h, after_pixels = parse_ppm(backspace_dump)
    if after_w != one_w or after_h != one_h:
        print(f"[FAIL] {prefix}: dimensions changed after backspace: {after_w}x{after_h}")
        return False

    changed = diff_count(one_pixels, after_pixels, one_w, *bbox)
    first_a_x1 = prompt_cols * font_w * scale
    first_a_y1 = origin_y
    first_a_x2 = first_a_x1 + font_w * scale - 1
    first_a_y2 = first_a_y1 + font_h * scale - 1
    changed_cell = diff_count(one_pixels, after_pixels, one_w,
                              first_a_x1, first_a_y1,
                              first_a_x2, first_a_y2)
    print(f"{prefix} aa/backspace screenshots: {one_a_dump}, {backspace_dump}")
    print(f"{prefix} first 'a' bbox: {bbox}, changed pixels after redraw: {changed}")
    print(f"{prefix} first 'a' cell: ({first_a_x1},{first_a_y1})..({first_a_x2},{first_a_y2}), changed pixels={changed_cell}")
    if changed_cell:
        print(f"[FAIL] {prefix}: surviving 'a' changed shape after aa+backspace")
        return False
    send_backspace()
    time.sleep(0.08)
    return True


def send_ps2_text(hmp: Hmp, text: str) -> None:
    for ch in text:
        if ch == " ":
            hmp.send_key("spc")
        elif ch == "\n":
            hmp.send_key("ret")
        elif "a" <= ch <= "z" or "0" <= ch <= "9":
            hmp.send_key(ch)
        else:
            raise ValueError(f"unsupported qcode character: {ch!r}")
        time.sleep(0.03)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="PPM screenshot path to write")
    parser.add_argument("--roundtrip-only", action="store_true",
                        help="only test VBE mode and return to MODE 1")
    parser.add_argument("--vbe-all", action="store_true",
                        help="test displayed pixels in all advertised VBE modes")
    parser.add_argument("--mode-stress", action="store_true",
                        help="switch modes repeatedly and verify a drawn ball remains stable")
    args = parser.parse_args()

    if not os.path.exists(KERNEL):
        print(f"error: {KERNEL} not found. Run `./build.sh` in ports/pc386.",
              file=sys.stderr)
        return 2

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="pc386-screen-", dir="/tmp") as td:
        serial_base = os.path.join(td, "serial")
        os.mkfifo(serial_base + ".in")
        os.mkfifo(serial_base + ".out")
        serial_in_fd = os.open(serial_base + ".in", os.O_RDWR | os.O_NONBLOCK)
        serial_out_fd = os.open(serial_base + ".out", os.O_RDWR | os.O_NONBLOCK)
        proc = subprocess.Popen(
            qemu_args(serial_base, td),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        os.set_blocking(proc.stdout.fileno(), False)
        hmp = None
        try:
            hmp = Hmp(proc)
            read_until_prompt_fd(serial_out_fd, 10, "boot")

            boot_dump = os.path.splitext(os.path.abspath(args.out))[0] + "-boot.ppm"
            hmp.screendump(boot_dump)
            boot_w, boot_h, boot_pixels = parse_ppm(boot_dump)
            nonblack = 0
            for i in range(0, len(boot_pixels), 3):
                if boot_pixels[i] or boot_pixels[i + 1] or boot_pixels[i + 2]:
                    nonblack += 1
            if nonblack < 100:
                print(f"[FAIL] boot console visible pixels: {nonblack}")
                return 1
            print(f"boot screenshot: {boot_dump}")
            print(f"boot dimensions: {boot_w}x{boot_h}, nonblack pixels={nonblack}")

            send_basic(serial_in_fd, serial_out_fd,
                       "CLS RGB(0,0,0) : "
                       "PIXEL 300,180,RGB(255,0,0) : "
                       "BOX 30,130,80,180,1,RGB(0,255,0),RGB(0,255,0) : "
                       "LINE 120,170,220,170,1,RGB(0,0,255) : "
                       "CIRCLE 260,150,12,,,RGB(255,255,0),RGB(255,255,0)")

            hmp.screendump(os.path.abspath(args.out))
            width, height, pixels = parse_ppm(args.out)
            failed = []
            scale = logical_scale(width, height, 320, 200)
            if scale == 0:
                print(f"[FAIL] unexpected screen dimensions {width}x{height}")
                failed.append("dimensions")
                scale = 1
            samples = [
                ("background", 300, 10, (0, 0, 0)),
                ("red pixel", 300, 180, (255, 0, 0)),
                ("green box", 40, 170, (0, 255, 0)),
                ("blue line", 160, 170, (0, 0, 255)),
                ("yellow circle", 260, 150, (255, 255, 0)),
            ]
            print(f"screenshot: {args.out}")
            print(f"dimensions: {width}x{height} (logical scale {scale}x)")
            for name, x, y, want in samples:
                got = pixel(pixels, width, x, y, scale)
                ok = close_enough(got, want)
                mark = "OK  " if ok else "FAIL"
                print(f"[{mark}] {name:13s} ({x:3d},{y:3d}) got={got} want={want}")
                if not ok:
                    failed.append(name)

            if failed:
                print(f"FAILED: {failed}")
                return 1

            if args.roundtrip_only:
                mode_listing = send_basic(serial_in_fd, serial_out_fd, "MODE")
                if "2:640x480" not in mode_listing:
                    print("mode roundtrip: skipped (VBE/VGA640 not available in this boot path)")
                    print("PASSED")
                    return 0
                send_basic(serial_in_fd, serial_out_fd,
                           "MODE 2 : CLS RGB(0,0,0) : "
                           "PIXEL 630,470,RGB(255,0,255)")
                mode2_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                          "screen_probe-mode2.ppm")
                hmp.screendump(mode2_dump)
                mode2_w, mode2_h, mode2_pixels = parse_ppm(mode2_dump)
                mode2_scale = logical_scale(mode2_w, mode2_h, 640, 480)
                if mode2_scale == 0:
                    print(f"[FAIL] MODE 2 unexpected dimensions: {mode2_w}x{mode2_h}")
                    return 1
                got = pixel(mode2_pixels, mode2_w, 630, 470, mode2_scale)
                print(f"mode 2 screenshot: {mode2_dump}")
                print(f"mode 2 dimensions: {mode2_w}x{mode2_h} (logical scale {mode2_scale}x)")
                if not close_enough(got, (255, 0, 255)):
                    print(f"[FAIL] MODE 2 magenta pixel got={got} want={(255, 0, 255)}")
                    return 1
                send_basic(serial_in_fd, serial_out_fd,
                           "MODE 1 : CLS RGB(0,0,0) : "
                           "PIXEL 300,180,RGB(255,0,0)")
                time.sleep(0.25)
                mode1_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                          "screen_probe-mode1-return.ppm")
                hmp.screendump(mode1_dump)
                mode1_w, mode1_h, mode1_pixels = parse_ppm(mode1_dump)
                mode1_scale = logical_scale(mode1_w, mode1_h, 320, 200)
                if mode1_scale == 0:
                    print(f"[FAIL] MODE 1 return unexpected dimensions: {mode1_w}x{mode1_h}")
                    return 1
                got = pixel(mode1_pixels, mode1_w, 300, 180, mode1_scale)
                print(f"mode 1 return screenshot: {mode1_dump}")
                print(f"mode 1 return dimensions: {mode1_w}x{mode1_h} (logical scale {mode1_scale}x)")
                if not close_enough(got, (255, 0, 0)):
                    print(f"[FAIL] MODE 1 return red pixel got={got} want={(255, 0, 0)}")
                    return 1
                print("PASSED")
                return 0

            if args.vbe_all:
                mode_listing = send_basic(serial_in_fd, serial_out_fd, "MODE")
                mode_checks = [
                    (2, "2:640x480", 640, 480, 630, 470, (255, 0, 255), 640, 480, 0, 0, 1),
                    (3, "3:800x600", 800, 600, 790, 590, (0, 255, 255), 800, 600, 0, 0, 1),
                    (4, "4:1024x768", 1024, 768, 1000, 740, (255, 255, 255), 1024, 768, 0, 0, 1),
                    (5, "5:480x480", 480, 480, 479, 479, (255, 0, 255), 640, 480, 80, 0, 1),
                    (6, "6:320x320x2", 320, 320, 319, 319, (255, 255, 255), 1024, 768, 192, 64, 2),
                ]
                for mode, marker, logical_w, logical_h, px, py, want, hw_w, hw_h, x_off, y_off, scale in mode_checks:
                    if marker not in mode_listing:
                        print(f"mode {mode} screenshot: skipped (not advertised)")
                        continue
                    send_basic(serial_in_fd, serial_out_fd,
                               f"MODE {mode} : CLS RGB(0,0,0) : "
                               f"PIXEL {px},{py},RGB({want[0]},{want[1]},{want[2]})")
                    mode_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                             f"screen_probe-mode{mode}.ppm")
                    hmp.screendump(mode_dump)
                    mode_w, mode_h, mode_pixels = parse_ppm(mode_dump)
                    if mode_w != hw_w or mode_h != hw_h:
                        print(f"[FAIL] MODE {mode} unexpected dimensions: {mode_w}x{mode_h}")
                        return 1
                    sample_x = x_off + px * scale + scale // 2
                    sample_y = y_off + py * scale + scale // 2
                    got = pixel(mode_pixels, mode_w, sample_x, sample_y)
                    print(f"mode {mode} screenshot: {mode_dump}")
                    print(f"mode {mode} dimensions: {mode_w}x{mode_h} (logical {logical_w}x{logical_h})")
                    if not close_enough(got, want):
                        print(f"[FAIL] MODE {mode} pixel got={got} want={want}")
                        return 1
                print("PASSED")
                return 0

            if args.mode_stress:
                mode_listing = send_basic(serial_in_fd, serial_out_fd, "MODE")
                stress_modes = [
                    (1, 320, 200, 0, 0, 2),
                    (3, 800, 600, 0, 0, 1),
                    (4, 1024, 768, 0, 0, 1),
                    (6, 320, 320, 192, 64, 2),
                    (1, 320, 200, 0, 0, 2),
                    (5, 480, 480, 80, 0, 1),
                    (3, 800, 600, 0, 0, 1),
                    (4, 1024, 768, 0, 0, 1),
                    (1, 320, 200, 0, 0, 2),
                ]
                for mode, logical_w, logical_h, x_off, y_off, scale in stress_modes:
                    if mode != 1 and f"{mode}:" not in mode_listing:
                        print(f"mode stress {mode}: skipped (not advertised)")
                        continue
                    cx = min(80, logical_w // 2)
                    cy = min(80, logical_h // 2)
                    send_basic(serial_in_fd, serial_out_fd,
                               f"MODE {mode} : CLS RGB(0,0,0) : "
                               f"CIRCLE {cx},{cy},6,0,1.0,,RGB(255,0,0) : "
                               f"CIRCLE {cx - 2},{cy - 2},1,0,1.0,,RGB(255,255,255)")
                    dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                        f"screen_probe-stress-mode{mode}.ppm")
                    hmp.screendump(dump)
                    w, h, data = parse_ppm(dump)
                    px0 = x_off + (cx - 8) * scale
                    py0 = y_off + (cy - 8) * scale
                    px1 = x_off + (cx + 8) * scale
                    py1 = y_off + (cy + 8) * scale
                    red = 0
                    white = 0
                    for yy in range(max(0, py0), min(h, py1 + 1)):
                        for xx in range(max(0, px0), min(w, px1 + 1)):
                            got = pixel(data, w, xx, yy)
                            if close_enough(got, (255, 0, 0), 24):
                                red += 1
                            if close_enough(got, (255, 255, 255), 24):
                                white += 1
                    print(f"mode stress {mode}: {dump} red={red} white={white}")
                    if red < 40 * scale * scale or white < scale * scale:
                        print(f"[FAIL] MODE {mode} ball pixels look corrupted")
                        return 1
                print("PASSED")
                return 0

            send_basic(serial_in_fd, serial_out_fd, "CLS RGB(0,0,0)")
            typed = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            prev_pixels = None
            font_w = 8
            font_h = 12
            prompt_cols = 2
            line_origin_y = 0
            line_dump = ""
            for idx, ch in enumerate(typed, start=1):
                os.write(serial_in_fd, bytes([ch]))
                time.sleep(0.05)
                line_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                         f"screen_probe-line-{idx:02d}.ppm")
                hmp.screendump(line_dump)
                line_w, line_h, line_pixels = parse_ppm(line_dump)
                if line_w != width or line_h != height:
                    print(f"[FAIL] dimensions changed while typing: {line_w}x{line_h}")
                    return 1
                if prev_pixels is not None:
                    stable_x2 = (prompt_cols + idx - 1) * font_w * scale - 1
                    changed = diff_count(prev_pixels, line_pixels, width,
                                         0, line_origin_y,
                                         stable_x2, line_origin_y + font_h * scale - 1)
                    if changed:
                        print(f"[FAIL] prior input pixels changed after byte {idx}: {changed} pixels")
                        print(f"last line editor screenshot: {line_dump}")
                        return 1
                prev_pixels = line_pixels
            rows = nonblack_rows(prev_pixels, width, height) if prev_pixels else []
            print(f"line editor screenshot: {line_dump}")
            if not rows:
                print("[FAIL] line editor produced no visible pixels")
                return 1
            span = rows[-1] - rows[0] + 1
            print(f"line editor active rows: {rows[0]}..{rows[-1]} (span {span})")
            if span > font_h * scale:
                print("[FAIL] input line wrapped or dirtied extra rows")
                return 1

            os.write(serial_in_fd, b"\x7f" * len(typed))
            time.sleep(0.2)

            out_dir = os.path.dirname(os.path.abspath(args.out))
            if not check_aa_backspace(
                hmp, serial_in_fd, serial_out_fd, out_dir, "serial",
                lambda: os.write(serial_in_fd, b"a"),
                lambda: os.write(serial_in_fd, b"\x7f"),
                font_w, font_h, prompt_cols, scale, line_origin_y,
            ):
                return 1
            if not check_aa_backspace(
                hmp, serial_in_fd, serial_out_fd, out_dir, "ps2",
                lambda: hmp.send_key("a"),
                lambda: hmp.send_key("backspace"),
                font_w, font_h, prompt_cols, scale, line_origin_y,
            ):
                return 1

            send_basic(serial_in_fd, serial_out_fd, "CLS RGB(0,0,0)")
            send_ps2_text(hmp, "clear\n")
            read_until_prompt_fd(serial_out_fd, 10, "ps2 invalid command")
            time.sleep(0.1)
            invalid_dump = os.path.join(out_dir, "screen_probe-ps2-after-invalid.ppm")
            hmp.screendump(invalid_dump)
            inv_w, inv_h, inv_pixels = parse_ppm(invalid_dump)
            if inv_w != width or inv_h != height:
                print(f"[FAIL] dimensions changed after invalid command: {inv_w}x{inv_h}")
                return 1
            rows_after_invalid = nonblack_rows(inv_pixels, inv_w, inv_h)
            print(f"ps2 invalid-command screenshot: {invalid_dump}")
            if not rows_after_invalid:
                print("[FAIL] invalid command left blank screen")
                return 1
            print(f"ps2 invalid-command active rows: {rows_after_invalid[0]}..{rows_after_invalid[-1]}")
            if not check_aa_backspace(
                hmp, serial_in_fd, serial_out_fd, out_dir, "ps2-after-invalid",
                lambda: hmp.send_key("a"),
                lambda: hmp.send_key("backspace"),
                font_w, font_h, prompt_cols, scale, line_origin_y, reset=False,
            ):
                return 1

            mode_listing = send_basic(serial_in_fd, serial_out_fd, "MODE")
            if "2:640x480" in mode_listing:
                send_basic(serial_in_fd, serial_out_fd,
                           "MODE 2 : CLS RGB(0,0,0) : "
                           "PIXEL 630,470,RGB(255,0,255)")
                mode2_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                          "screen_probe-mode2.ppm")
                hmp.screendump(mode2_dump)
                mode2_w, mode2_h, mode2_pixels = parse_ppm(mode2_dump)
                mode2_scale = logical_scale(mode2_w, mode2_h, 640, 480)
                if mode2_scale == 0:
                    print(f"[FAIL] MODE 2 unexpected dimensions: {mode2_w}x{mode2_h}")
                    return 1
                got = pixel(mode2_pixels, mode2_w, 630, 470, mode2_scale)
                print(f"mode 2 screenshot: {mode2_dump}")
                print(f"mode 2 dimensions: {mode2_w}x{mode2_h} (logical scale {mode2_scale}x)")
                if not close_enough(got, (255, 0, 255)):
                    print(f"[FAIL] MODE 2 magenta pixel got={got} want={(255, 0, 255)}")
                    return 1
                send_basic(serial_in_fd, serial_out_fd,
                           "MODE 1 : CLS RGB(0,0,0) : "
                           "PIXEL 300,180,RGB(255,0,0)")
                time.sleep(0.25)
                mode1_dump = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                          "screen_probe-mode1-return.ppm")
                hmp.screendump(mode1_dump)
                mode1_w, mode1_h, mode1_pixels = parse_ppm(mode1_dump)
                mode1_scale = logical_scale(mode1_w, mode1_h, 320, 200)
                if mode1_scale == 0:
                    print(f"[FAIL] MODE 1 return unexpected dimensions: {mode1_w}x{mode1_h}")
                    return 1
                got = pixel(mode1_pixels, mode1_w, 300, 180, mode1_scale)
                print(f"mode 1 return screenshot: {mode1_dump}")
                print(f"mode 1 return dimensions: {mode1_w}x{mode1_h} (logical scale {mode1_scale}x)")
                if not close_enough(got, (255, 0, 0)):
                    print(f"[FAIL] MODE 1 return red pixel got={got} want={(255, 0, 0)}")
                    return 1
            else:
                print("mode 2 screenshot: skipped (VBE/VGA640 not available in this boot path)")

            print("PASSED")
            return 0
        finally:
            if hmp is not None:
                hmp.close()
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            os.close(serial_in_fd)
            os.close(serial_out_fd)
            time.sleep(0.5)


if __name__ == "__main__":
    sys.exit(main())
