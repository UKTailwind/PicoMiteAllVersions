#!/usr/bin/env python3
"""
ports/pc386/tests/repl_expect.py — interactive REPL test harness.

Boots mmbasic.elf under QEMU with COM1 piped to a pty, waits for the
prompt, sends a line, captures output until the next prompt, compares
to expected, repeats.

Each test is a list of (input_line, expected_output_substr) pairs.
Empty `expected_output_substr` means "any output is fine, just expect
the prompt back."

Run:
  python3 ports/pc386/tests/repl_expect.py            # all tests
  python3 ports/pc386/tests/repl_expect.py basic      # just "basic"
  PC386_BOOT=kernel python3 ports/pc386/tests/repl_expect.py arith files
"""

import os
import re
import shutil
import select
import subprocess
import sys
import tempfile
import time

PORT_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL    = os.path.join(PORT_DIR, "build", "mmbasic.elf")
C_IMG     = os.environ.get("PC386_C_IMG", os.path.join(PORT_DIR, "test_disks", "c.img"))
F_IMG     = os.environ.get("PC386_FLOPPY_IMG", os.path.join(PORT_DIR, "test_disks", "pc386-floppy.img"))

ANSI_RE   = re.compile(rb"\x1b\[[0-9;?]*[a-zA-Z]")
# The MMBasic prompt is "> " followed (in interactive REPL mode) by the
# show-cursor escape \x1b[?25h. We anchor on the END of the buffer so we
# don't match stale prompt bytes left over from earlier commands.
PROMPT_TAIL_RE = re.compile(rb"(?:\r?\n)?> (?:\x1b\[\?25h)?\Z")


def lpt1_out_path(tmpdir: str) -> str:
    return os.path.join(tmpdir, "lpt1.out")


def strip_ansi(b: bytes) -> bytes:
    return ANSI_RE.sub(b"", b)


def qemu_args(tmpdir: str) -> list[str]:
    boot_mode = os.environ.get("PC386_BOOT", "floppy")
    audio_backend = os.environ.get("PC386_AUDIO", "auto")
    sb_base = os.environ.get("PC386_SB_BASE", "0x220")
    sb_irq = os.environ.get("PC386_SB_IRQ", "5")
    sb_dma = os.environ.get("PC386_SB_DMA", "1")
    sb_dma16 = os.environ.get("PC386_SB_DMA16", "5")
    args = [
        "qemu-system-i386",
        "-m", "16M",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-parallel", f"file:{lpt1_out_path(tmpdir)}",
        "-no-reboot", "-no-shutdown",
        "-d", "guest_errors",
    ]
    if audio_backend == "auto":
        args += [
            "-machine", "pc,pcspk-audiodev=pcaudio",
            "-audiodev", "none,id=pcaudio",
            "-device", f"sb16,audiodev=pcaudio,iobase={sb_base},irq={sb_irq},dma={sb_dma},dma16={sb_dma16}",
        ]
    elif audio_backend == "pcspk":
        args += ["-machine", "pc,pcspk-audiodev=pcspk", "-audiodev", "none,id=pcspk"]
    elif audio_backend == "sb16":
        args += [
            "-machine", "pc",
            "-audiodev", "none,id=sb16",
            "-device", f"sb16,audiodev=sb16,iobase={sb_base},irq={sb_irq},dma={sb_dma},dma16={sb_dma16}",
        ]
    else:
        raise ValueError("PC386_AUDIO must be one of: auto pcspk sb16")
    if os.path.exists(F_IMG):
        dst = os.path.join(tmpdir, "pc386-floppy.img")
        shutil.copyfile(F_IMG, dst)
        args += ["-drive", f"file={dst},format=raw,if=floppy,index=0"]
        f_img = dst
    else:
        f_img = None
    if os.path.exists(C_IMG):
        dst = os.path.join(tmpdir, "c.img")
        shutil.copyfile(C_IMG, dst)
        args += ["-drive", f"file={dst},format=raw,if=ide,index=0"]
    if boot_mode == "kernel":
        args[1:1] = ["-kernel", KERNEL]
    elif boot_mode == "floppy":
        if not f_img:
            raise FileNotFoundError(f"{F_IMG} not found. Run `ports/pc386/build_disks.sh` first.")
        args[1:1] = ["-boot", "a"]
    else:
        raise ValueError("PC386_BOOT must be one of: kernel floppy")
    return args


def read_until_prompt(proc, timeout: float, label: str) -> bytes:
    """Read from proc.stdout until the buffer ENDS with a prompt sequence."""
    buf = b""
    deadline = time.monotonic() + timeout
    last_page_ack_at = -1
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        rfds, _, _ = select.select([proc.stdout], [], [], min(remaining, 0.05))
        if proc.stdout in rfds:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
        else:
            # No new bytes for 50ms. If buffer ends with a prompt, accept.
            if PROMPT_TAIL_RE.search(buf):
                return buf
            stripped = strip_ansi(buf)
            if b"PRESS ANY KEY" in stripped and len(buf) != last_page_ack_at:
                proc.stdin.write(b" ")
                proc.stdin.flush()
                last_page_ack_at = len(buf)
        if proc.poll() is not None:
            break
    raise TimeoutError(
        f"{label}: timeout after {timeout}s waiting for prompt.\n"
        f"--- captured {len(buf)} bytes ---\n"
        f"{strip_ansi(buf)[-2000:].decode('utf-8', errors='replace')}\n"
        "--------------------------------"
    )


def run_steps(name: str, steps: list[tuple[str, str]], td: str) -> bool:
    """Boot the kernel once in tmpdir, run each step, report."""
    proc = subprocess.Popen(
        qemu_args(td),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )
    # non-blocking stdout
    os.set_blocking(proc.stdout.fileno(), False)

    try:
        # Wait for the first prompt (after banner).
        boot_timeout = 20 if os.environ.get("PC386_BOOT") == "floppy" else 10
        boot_out = read_until_prompt(proc, timeout=boot_timeout, label="boot")

        for idx, (cmd, expected) in enumerate(steps, start=1):
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
            try:
                got = read_until_prompt(proc, timeout=15, label=f"step {idx} `{cmd}`")
            except TimeoutError as e:
                print(f"  STEP {idx} `{cmd}`: TIMEOUT")
                print(f"  {e}")
                return False
            stripped = strip_ansi(got).decode("utf-8", errors="replace")
            expected_seen = (expected == "") or (expected in stripped)
            # Empty expected strings mean "no interesting output", not
            # "an MMBasic error is acceptable". Tests that intentionally
            # exercise an error must name the expected error text.
            expected_error = (expected != "") and (expected in stripped)
            unexpected_error = (
                "*** PANIC" in stripped
                or "*** EXCEPTION" in stripped
                or ("Error :" in stripped and not expected_error)
            )
            ok = expected_seen and not unexpected_error
            mark = "OK  " if ok else "FAIL"
            preview = stripped.replace("\r\n", " | ").strip()[:200]
            print(f"  [{mark}] `{cmd}` → {preview}")
            if not ok:
                print(f"          expected substring: {expected!r}")
                if unexpected_error:
                    print("          unexpected MMBasic error/exception detected")
                print(f"          --- full output (no ANSI) ---")
                for line in stripped.splitlines():
                    print(f"          {line}")
                print(f"          ------------------------------")
                return False
        return True
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        # QEMU's disk-image file lock takes a moment to release after the
        # process is gone — without this short settle, the next test's
        # qemu-system-i386 trips "Failed to get write lock" mid-boot.
        time.sleep(0.5)


def run_test(name: str, steps: list[tuple[str, str]]) -> bool:
    """Boot the kernel, run each (input, expected_substr) step, report."""
    print(f"\n=== {name} ===")
    with tempfile.TemporaryDirectory(prefix="pc386-repl-") as td:
        ok = run_steps(name, steps, td)
        if ok and name in FILE_EXPECTATIONS:
            path, expected = FILE_EXPECTATIONS[name]
            actual_path = os.path.join(td, path)
            try:
                with open(actual_path, "rb") as f:
                    actual = f.read()
            except FileNotFoundError:
                print(f"  [FAIL] expected output file {path!r} was not created")
                return False
            if actual != expected:
                print(f"  [FAIL] output file {path!r} mismatch: got {actual!r}, expected {expected!r}")
                return False
            print(f"  [OK  ] output file {path!r} matched {expected!r}")
        return ok


def run_reboot_test(name: str, boots: list[list[tuple[str, str]]]) -> bool:
    print(f"\n=== {name} ===")
    with tempfile.TemporaryDirectory(prefix="pc386-repl-") as td:
        for idx, steps in enumerate(boots, start=1):
            print(f"  -- boot {idx} --")
            if not run_steps(name, steps, td):
                return False
        return True


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

SB16_TEST_BASE = int(os.environ.get("PC386_SB_BASE", "0x220"), 0)
SB16_TEST_IRQ = os.environ.get("PC386_SB_IRQ", "5")
SB16_TEST_DMA = os.environ.get("PC386_SB_DMA", "1")
SB16_TEST_DMA16 = os.environ.get("PC386_SB_DMA16", "5")
SB16_TEST_BASE_HEX = f"&H{SB16_TEST_BASE:X}"
SB16_ALT_BASE = 0x240 if SB16_TEST_BASE != 0x240 else 0x220
SB16_ALT_BASE_HEX = f"&H{SB16_ALT_BASE:X}"

TESTS = {
    "arith":  [
        ("PRINT 1+1",          " 2"),
        ("PRINT 3*4+5",        " 17"),
        ("PRINT 2^10",         " 1024"),
    ],

    "math":   [
        ("PRINT SIN(0)",       " 0"),
        ("PRINT COS(0)",       " 1"),
        ("PRINT SQR(2)",       "1.414"),
        ("PRINT INT(3.7)",     " 3"),
    ],

    "vars":   [
        ("LET X = 42",         ""),
        ("PRINT X",            " 42"),
        ("LET Y = X * 2",      ""),
        ("PRINT Y",            " 84"),
        ("LET S$ = \"hello\"", ""),
        ("PRINT S$",           "hello"),
    ],

    "loops":  [
        ("FOR I=1 TO 3 : PRINT I, I*I : NEXT I", "3"),
    ],

    "strings": [
        ("PRINT LEN(\"hello\")",                 " 5"),
        ("PRINT UCASE$(\"abc\")",                "ABC"),
        ("PRINT LEFT$(\"PicoMite\", 4)",         "Pico"),
    ],

    "files":  [
        # A: is a real floppy/FDC image. C: is the primary IDE hard disk.
        ("A:",                  ""),
        ("FILES",               "HELLO.BAS"),
        ("C:",                  ""),
        ("FILES",               "PROGRAMS"),
        ("FILES \"C:/*\"",      "README.TXT"),
        ("FILES \"A:/*\"",      "HELLO.BAS"),
    ],

    "files_a_alias": [
        ("A:",                  ""),
        ("FILES",               "HELLO.BAS"),
    ],

    "editor": [
        # Enter the full-screen editor, then ESC back to the prompt.
        ("EDIT\n\x1b",           "EDIT MODE"),
        ("PRINT 1",              " 1"),
    ],

    "editor_file": [
        ("C:",                    ""),
        ("EDIT \"README.txt\"\n\x1b", "EDIT MODE"),
        ("PRINT 1",              " 1"),
    ],

    "editor_file_save": [
        ("C:", ""),
        ("EDIT \"C:/EDPC386.TXT\"\neditor save ok\x11", "EDIT MODE"),
        ("OPEN \"C:/EDPC386.TXT\" FOR INPUT AS #1 : LINE INPUT #1, E$ : CLOSE #1", ""),
        ("IF E$ <> \"editor save ok\" THEN ERROR \"editor save failed\"", ""),
        ("KILL \"C:/EDPC386.TXT\"", ""),
        ("PRINT \"editor file save ok\"", "editor file save ok"),
    ],

    "file_io_core": [
        ("C:", ""),
        ("OPEN \"C:/PC386IO.TMP\" FOR OUTPUT AS #1 : PRINT #1, \"alpha\" : PRINT #1, 42 : CLOSE #1", ""),
        ("OPEN \"C:/PC386IO.TMP\" FOR INPUT AS #1 : LINE INPUT #1, A$ : INPUT #1, N : CLOSE #1", ""),
        ("IF A$ <> \"alpha\" THEN ERROR \"line input mismatch\"", ""),
        ("IF N <> 42 THEN ERROR \"numeric input mismatch\"", ""),
        ("OPEN \"C:/PC386IO.TMP\" FOR APPEND AS #1 : PRINT #1, \"omega\" : CLOSE #1", ""),
        ("OPEN \"C:/PC386IO.TMP\" FOR INPUT AS #1 : LINE INPUT #1, A$ : LINE INPUT #1, B$ : LINE INPUT #1, C$ : CLOSE #1", ""),
        ("IF A$ <> \"alpha\" THEN ERROR \"append first line\"", ""),
        ("IF B$ <> \" 42\" THEN ERROR \"append numeric line\"", ""),
        ("IF C$ <> \"omega\" THEN ERROR \"append last line\"", ""),
        ("KILL \"C:/PC386IO.TMP\"", ""),
        ("PRINT \"file io core ok\"", "file io core ok"),
    ],

    "file_handles": [
        ("C:", ""),
        ("OPEN \"C:/PC386A.TMP\" FOR OUTPUT AS #1 : OPEN \"C:/PC386B.TMP\" FOR OUTPUT AS #2", ""),
        ("PRINT #1, \"a1\" : PRINT #2, \"b1\" : PRINT #1, \"a2\" : PRINT #2, \"b2\"", ""),
        ("CLOSE #1 : CLOSE #2", ""),
        ("OPEN \"C:/PC386A.TMP\" FOR INPUT AS #1 : OPEN \"C:/PC386B.TMP\" FOR INPUT AS #2", ""),
        ("LINE INPUT #1, A1$ : LINE INPUT #2, B1$ : LINE INPUT #1, A2$ : LINE INPUT #2, B2$ : CLOSE #1 : CLOSE #2", ""),
        ("IF A1$ + A2$ + B1$ + B2$ <> \"a1a2b1b2\" THEN ERROR \"multi-handle mismatch\"", ""),
        ("KILL \"C:/PC386A.TMP\" : KILL \"C:/PC386B.TMP\"", ""),
        ("PRINT \"file handles ok\"", "file handles ok"),
    ],

    "file_dirs_copy_rename": [
        ("C:", ""),
        ("MKDIR \"C:/PC386DIR\"", ""),
        ("CHDIR \"C:/PC386DIR\"", ""),
        ("OPEN \"HELLO.TXT\" FOR OUTPUT AS #1 : PRINT #1, \"copied\" : CLOSE #1", ""),
        ("OPEN \"HELLO.TXT\" FOR INPUT AS #1 : LINE INPUT #1, A$ : CLOSE #1", ""),
        ("IF A$ <> \"copied\" THEN ERROR \"chdir relative file read\"", ""),
        ("CHDIR \"..\"", ""),
        ("COPY \"C:/PC386DIR/HELLO.TXT\" TO \"C:/PC386CP.TXT\"", ""),
        ("RENAME \"C:/PC386CP.TXT\" AS \"C:/PC386RN.TXT\"", ""),
        ("FILES \"C:/PC386RN.TXT\"", "PC386RN.TXT"),
        ("OPEN \"C:/PC386RN.TXT\" FOR INPUT AS #1 : LINE INPUT #1, A$ : CLOSE #1", ""),
        ("IF A$ <> \"copied\" THEN ERROR \"renamed file read\"", ""),
        ("KILL \"C:/PC386DIR/HELLO.TXT\" : KILL \"C:/PC386RN.TXT\" : RMDIR \"C:/PC386DIR\"", ""),
        ("PRINT \"file dirs copy rename ok\"", "file dirs copy rename ok"),
    ],

    "file_seek_loc": [
        ("C:", ""),
        ("OPEN \"C:/PC386SK.TMP\" FOR OUTPUT AS #1 : PRINT #1, \"abcdef\" : CLOSE #1", ""),
        ("OPEN \"C:/PC386SK.TMP\" FOR INPUT AS #1 : SEEK #1, 4 : LINE INPUT #1, A$ : CLOSE #1", ""),
        ("IF A$ <> \"def\" THEN ERROR \"seek mismatch\"", ""),
        ("OPEN \"C:/PC386SK.TMP\" FOR INPUT AS #1", ""),
        ("IF LOF(#1) <> 8 THEN ERROR \"lof mismatch\"", ""),
        ("IF LOC(#1) <> 1 THEN ERROR \"loc start mismatch\"", ""),
        ("IF EOF(#1) <> 0 THEN ERROR \"early eof\"", ""),
        ("S$ = INPUT$(3, #1)", ""),
        ("IF S$ <> \"abc\" THEN ERROR \"input string mismatch\"", ""),
        ("IF LOC(#1) <> 4 THEN ERROR \"loc after input mismatch\"", ""),
        ("R$ = INPUT$(8, #1)", ""),
        ("IF EOF(#1) = 0 THEN ERROR \"eof mismatch\"", ""),
        ("CLOSE #1 : KILL \"C:/PC386SK.TMP\"", ""),
        ("PRINT \"file seek loc eof ok\"", "file seek loc eof ok"),
    ],

    "file_c_drive": [
        ("C:", ""),
        ("CHDIR \"PROGRAMS\"", ""),
        ("CHDIR \"..\"", ""),
        ("OPEN \"C:/PC386C.TXT\" FOR OUTPUT AS #1 : PRINT #1, \"drive c\" : CLOSE #1", ""),
        ("FILES \"C:/PC386C.TXT\"", "PC386C.TXT"),
        ("OPEN \"C:/PC386C.TXT\" FOR INPUT AS #1 : LINE INPUT #1, C$ : CLOSE #1", ""),
        ("IF C$ <> \"drive c\" THEN ERROR \"c drive read mismatch\"", ""),
        ("KILL \"C:/PC386C.TXT\"", ""),
        ("PRINT \"file c drive ok\"", "file c drive ok"),
    ],

    "file_dir_nav": [
        ("C:", ""),
        ("MKDIR \"C:/PC386NAV\"", ""),
        ("CHDIR \"PC386NAV\"", ""),
        ("OPEN \"REL.TXT\" FOR OUTPUT AS #1 : PRINT #1, \"relative c\" : CLOSE #1", ""),
        ("FILES", "REL.TXT"),
        ("OPEN \"REL.TXT\" FOR INPUT AS #1 : LINE INPUT #1, R$ : CLOSE #1", ""),
        ("IF R$ <> \"relative c\" THEN ERROR \"relative chdir read mismatch\"", ""),
        ("CHDIR \"..\"", ""),
        ("KILL \"C:/PC386NAV/REL.TXT\" : RMDIR \"C:/PC386NAV\"", ""),
        ("MKDIR \"C:/PC386NAV\" : RMDIR \"C:/PC386NAV\"", ""),
        ("PRINT \"file dir nav ok\"", "file dir nav ok"),
    ],

    "load_run": [
        ("LOAD \"A:\\HELLO.BAS\"", ""),
        ("LIST",                   "Print"),
        ("RUN",                    "Hello"),
    ],

    "fizzbuzz_run": [
        # Multi-command session: switch drive, list, load, run a more
        # interesting program, then verify FizzBuzz output.
        ("A:",                       ""),
        ("LOAD \"FIZZBUZZ.BAS\"",    ""),
        ("RUN",                      "FizzBuzz"),
    ],

    "session_short": [
        ("PRINT 6*7",                " 42"),
        ("A:",                       ""),
        ("FILES",                    "HELLO.BAS"),
    ],

    "graphics": [
        ("PRINT MM.HRES, MM.VRES",       "320"),
        ("MODE",                         "1:320x200"),
        ("MODE",                         "2:640x480"),
        ("CLS RGB(0,0,0) : PIXEL 300,180,RGB(255,0,0)", ""),
        ("PRINT PIXEL(300,180)",         "16711680"),
        ("BOX 30,130,80,180,1,RGB(0,255,0),RGB(0,255,0)", ""),
        ("PRINT PIXEL(40,160)",          "65280"),
        ("LINE 120,170,220,170,1,RGB(0,0,255)", ""),
        ("PRINT PIXEL(160,170)",         "255"),
        ("CIRCLE 260,150,12,,,RGB(255,255,0),RGB(255,255,0)", ""),
        ("PRINT PIXEL(260,150)",         "16776960"),
        ("MODE 2",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "640"),
        ("CLS RGB(0,0,0) : PIXEL 630,470,RGB(255,0,0)", ""),
        ("PRINT PIXEL(630,470)",         "16711680"),
        ("MODE 5",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "480\t 480"),
        ("CLS RGB(0,0,0) : PIXEL 479,479,RGB(255,0,255)", ""),
        ("PRINT PIXEL(479,479)",         "16711935"),
        ("MODE 1",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "320"),
        ("FRAMEBUFFER CREATE",           "FRAMEBUFFER not available on mode 13h display"),
    ],

    "graphics_vbe": [
        ("MODE",                         "3:800x600"),
        ("MODE 3",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "800"),
        ("CLS RGB(0,0,0) : PIXEL 790,590,RGB(0,255,255)", ""),
        ("PRINT PIXEL(790,590)",         "65535"),
        ("MODE 4",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "1024"),
        ("CLS RGB(0,0,0) : PIXEL 1000,740,RGB(0,255,255)", ""),
        ("PRINT PIXEL(1000,740)",        "65535"),
        ("MODE 6",                       ""),
        ("PRINT MM.HRES, MM.VRES",       "320\t 320"),
        ("CLS RGB(0,0,0) : PIXEL 319,319,RGB(255,255,255)", ""),
        ("PRINT PIXEL(319,319)",         "16777215"),
    ],

    "fastgfx": [
        ("MODE 1",                        ""),
        ("CLS RGB(0,0,0)",                ""),
        ("FASTGFX CREATE",                ""),
        ("FASTGFX FPS 50",                ""),
        ("PIXEL 10,10,RGB(255,0,0)",      ""),
        ("BOX 20,20,8,8,0,,RGB(0,255,0)", ""),
        ("FASTGFX SWAP",                  ""),
        ("FASTGFX SYNC",                  ""),
        ("PRINT PIXEL(10,10)",            "16711680"),
        ("PRINT PIXEL(23,23)",            "65280"),
        ("FASTGFX CLOSE",                 ""),
        ("PIXEL 10,10,RGB(0,0,255)",      ""),
        ("PRINT PIXEL(10,10)",            "255"),
        ("PRINT \"fastgfx ok\"",          "fastgfx ok"),
    ],

    "pico_blocks_smoke": [
        ("MODE 6",                        ""),
        ("C:",                            ""),
        ("RUN \"pico_blocks.bas\"\n \x1b", "Thanks for playing"),
    ],

    "audio": [
        ("PLAY TONE 440, 440, 20",        ""),
        ("PLAY TONE 440, 400, 20",        ""),
        ("PLAY TONE 660, 660",            ""),
        ("PLAY TONE 700,700 : PAUSE 40 : PLAY STOP", ""),
        ("PLAY PAUSE",                    ""),
        ("PLAY RESUME",                   ""),
        ("PLAY STOP",                     ""),
        ("PRINT \"audio ok\"",            "audio ok"),
    ],

    "options_ini": [
        (f"OPTION SB16 {SB16_ALT_BASE_HEX}, {SB16_TEST_IRQ}, {SB16_TEST_DMA}, {SB16_TEST_DMA16}", ""),
        ("OPTION KEYBOARD REPEAT 250,50",  ""),
        ("LIST OPTIONS",                   "OPTION SB16"),
        ("LIST OPTIONS",                   "OPTION KEYBOARD REPEAT 250,50"),
        ("FILES \"C:/OPTIONS.INI\"",       "OPTIONS.INI"),
        ("OPEN \"C:/OPTIONS.INI\" FOR INPUT AS #1 : LINE INPUT #1, O$ : CLOSE #1", ""),
        ("PRINT O$",                       "PicoMite PC386 options"),
        ("OPEN \"C:/OPTIONS.INI\" FOR INPUT AS #1 : O$ = INPUT$(LOF(#1), #1) : CLOSE #1", ""),
        ("IF INSTR(O$, \"pc386_sb_base=\") = 0 THEN ERROR \"sb base override missing\"", ""),
        ("IF INSTR(O$, \"RepeatStart=250\") = 0 THEN ERROR \"repeat start override missing\"", ""),
        ("IF INSTR(O$, \"RepeatRate=50\") = 0 THEN ERROR \"repeat rate override missing\"", ""),
        ("IF INSTR(O$, \"DefaultFont=\") <> 0 THEN ERROR \"default font persisted\"", ""),
        ("PRINT \"options ini ok\"",       "options ini ok"),
    ],

    "sb16": [
        (f"SB16 {SB16_TEST_BASE}, {SB16_TEST_IRQ}, {SB16_TEST_DMA}, {SB16_TEST_DMA16}", ""),
        (f"SB16 {SB16_TEST_BASE_HEX}, {SB16_TEST_IRQ}, {SB16_TEST_DMA}, {SB16_TEST_DMA16}", ""),
        ("SB16",                           f"IRQ {SB16_TEST_IRQ}"),
        ("SB16",                           "detected"),
        ("PLAY TONE 440, 400, 20",         ""),
        ("PLAY STOP",                      ""),
        ("PRINT \"sb16 ok\"",              "sb16 ok"),
    ],

    "sb16_sound": [
        ("PLAY SOUND 1, B, Q, 440, 20",    ""),
        ("PLAY SOUND 2, L, T, 660, 12",    ""),
        ("PLAY SOUND 3, R, W, 330, 12",    ""),
        ("PLAY SOUND 4, B, S, 220, 10",    ""),
        ("PLAY SOUND 1, B, O",             ""),
        ("PLAY SOUND 2, L, O",             ""),
        ("PLAY SOUND 3, R, O",             ""),
        ("PLAY SOUND 4, B, O",             ""),
        ("PLAY SOUND 1, B, N, 100, 8",     ""),
        ("PLAY STOP",                      ""),
        ("PRINT \"sb16 sound ok\"",         "sb16 sound ok"),
    ],

    "sb16_noise_sfx": [
        ("PLAY SOUND 1, B, N, 100, 25",     ""),
        ("FOR V=25 TO 0 STEP -5 : PLAY SOUND 1, B, N, 100, V : PAUSE 10 : NEXT V", ""),
        ("PLAY SOUND 1, B, P, 80, 12",      ""),
        ("PAUSE 20",                        ""),
        ("PLAY STOP",                       ""),
        ("PRINT \"sb16 noise sfx ok\"",      "sb16 noise sfx ok"),
    ],

    "lpt1_gpio": [
        ("SETPIN 2, DOUT",                    ""),
        ("PIN(2)=1",                          ""),
        ("PRINT PIN(2)",                      " 1"),
        ("PIN(2)=0",                          ""),
        ("PRINT PIN(2)",                      " 0"),
        ("SETPIN 1, DOUT",                    ""),
        ("PIN(1)=1",                          ""),
        ("PRINT PIN(1)",                      " 1"),
        ("SETPIN 1, OFF",                     ""),
        ("SETPIN 10, DIN",                    ""),
        ("PRINT PIN(10)",                     ""),
        ("SETPIN 10, DOUT",                   "Pin is read-only"),
        ("PIN(10)=1",                         "Pin is not an output"),
        ("PRINT \"lpt gpio ok\"",             "lpt gpio ok"),
    ],

    "lpt1_print": [
        ("OPEN \"LPT1:\" FOR OUTPUT AS #1 : PRINT #1, \"LPT-OK\"; : CLOSE #1", ""),
        ("PRINT \"lpt print ok\"",            "lpt print ok"),
    ],

    "lpt1_copy": [
        ("OPEN \"C:/LPTCP.TXT\" FOR OUTPUT AS #1 : PRINT #1, \"COPY-OK\"; : CLOSE #1", ""),
        ("COPY \"C:/LPTCP.TXT\" TO \"LPT1:\"", ""),
        ("KILL \"C:/LPTCP.TXT\"", ""),
        ("PRINT \"lpt copy ok\"",             "lpt copy ok"),
    ],

    "lpt1_copy_c": [
        ("COPY \"C:/README.TXT\" TO \"LPT1:\"", ""),
        ("PRINT \"lpt copy c ok\"",           "lpt copy c ok"),
    ],

    "sys_c": [
        ("SYS C:",                            "SYS C: boot files updated"),
        ("OPEN \"C:/BOOT/MMBASIC.ELF\" FOR INPUT AS #1 : CLOSE #1", ""),
        ("OPEN \"C:/BOOT/LIMINE.CONF\" FOR INPUT AS #1 : CLOSE #1", ""),
        ("OPEN \"C:/BOOT/LIMINE-BIOS.SYS\" FOR INPUT AS #1 : CLOSE #1", ""),
        ("PRINT \"sys c ok\"",               "sys c ok"),
    ],

    "errors_unsupported": [
        # Stage 5+ has graphics and basic PC-speaker tones; unsupported
        # FRAMEBUFFER is still unsupported because mode 13h is itself
        # the scanout buffer. It should produce a clear error and bounce
        # back to the prompt, not halt the kernel.
        ("FRAMEBUFFER CREATE",       "FRAMEBUFFER not available on mode 13h display"),
        # Verify the prompt comes back and arithmetic still works after
        # any of those errors.
        ("PRINT 99 + 1",             " 100"),
    ],

    "session": [
        # Simulate a real interactive session: do some math, set a var,
        # switch drives, list, load, run, define an inline program.
        ("PRINT 6*7",                " 42"),
        ("LET N = 10",               ""),
        ("PRINT N * N",              " 100"),
        ("A:",                       ""),
        ("FILES",                    "HELLO.BAS"),
        ("LOAD \"HELLO.BAS\"",       ""),
        ("RUN",                      "Hello"),
        # After RUN, the program is still in ProgMemory; LIST should still work.
        ("LIST",                     "Print"),
        ("PRINT \"after run\"",      "after run"),
    ],
}

FILE_EXPECTATIONS = {
    "lpt1_print": ("lpt1.out", b"LPT-OK"),
    "lpt1_copy": ("lpt1.out", b"COPY-OK"),
}

REBOOT_TESTS = {
    "options_persist": [
        [
            (f"OPTION SB16 {SB16_TEST_BASE_HEX}, {SB16_TEST_IRQ}, {SB16_TEST_DMA}, {SB16_TEST_DMA16}", ""),
            ("PRINT \"first boot saved\"", "first boot saved"),
        ],
        [
            ("LIST OPTIONS", f"OPTION SB16 {SB16_TEST_BASE_HEX}, {SB16_TEST_IRQ}, {SB16_TEST_DMA}, {SB16_TEST_DMA16}"),
            ("SB16", f"SB16 {SB16_TEST_BASE_HEX}, IRQ {SB16_TEST_IRQ}, DMA {SB16_TEST_DMA}, DMA16 {SB16_TEST_DMA16}"),
            ("PRINT \"options persist ok\"", "options persist ok"),
        ],
    ],
}

DEFAULT_TESTS = [
    name for name in TESTS
    if name not in {"graphics_vbe", "pico_blocks_smoke"}
]


def main() -> int:
    boot_mode = os.environ.get("PC386_BOOT", "floppy")
    if boot_mode == "kernel" and not os.path.exists(KERNEL):
        print(f"error: {KERNEL} not found. Run `make -C ports/pc386` first.",
              file=sys.stderr)
        return 1
    if boot_mode == "floppy" and not os.path.exists(F_IMG):
        print(f"error: {F_IMG} not found. Run `ports/pc386/build_disks.sh` first.",
              file=sys.stderr)
        return 1

    selected = sys.argv[1:] or DEFAULT_TESTS
    unknown = [s for s in selected if s not in TESTS and s not in REBOOT_TESTS]
    if unknown:
        print(f"error: unknown tests: {unknown}", file=sys.stderr)
        print(f"available: {list(TESTS.keys()) + list(REBOOT_TESTS.keys())}", file=sys.stderr)
        return 1

    failed = []
    for name in selected:
        if name in TESTS:
            ok = run_test(name, TESTS[name])
        else:
            ok = run_reboot_test(name, REBOOT_TESTS[name])
        if not ok:
            failed.append(name)

    print()
    if failed:
        print(f"FAILED: {failed}")
        return 1
    print(f"PASSED ({len(selected)}/{len(selected)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
