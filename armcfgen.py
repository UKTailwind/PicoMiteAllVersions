#!/usr/bin/env python3
"""armcfgen.py - convert an ARM ELF object into PicoMite CSUB hex.

A clean re-implementation of armcfgenV144.bas using pyelftools, plus an
optional --compile front-end that runs the toolchain for you (C -> CSUB in
one step).

Modes (matching the BAS):
  join   each function gets its own  CSUB name / 00000000 / hex / End CSUB
         block (the entry symbol, default 'main', is treated as a dummy and
         dropped).  Constant data (.rodata) is rejected, as in the original.
  merge  one  CSUB name  containing every function in address order, with the
         entry-offset word pointing at the entry symbol so functions can call
         each other.  .rodata (if any) is appended.

Bug fixes vs the BAS:
  * dinit/rodata padding is computed from the real text->rodata address gap,
    not the (mis-typed) dinit heuristic.
  * rodata-comment name truncation uses the '.' position, not the symbol count.
  * symbol selection decodes ELF st_info (STT_FUNC) instead of magic numbers,
    so -ffunction-sections / multiple .text.* inputs also work.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
import os

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.relocation import RelocationSection

GCC = "arm-none-eabi-gcc"
# Text-relative PIC: the blob is fully self-contained and position independent,
# rodata included (reached PC-relative, resolved at link time - no GOT, no
# load-time relocation). The -O level is added per call (default -O0; higher -O
# can rearrange the rodata/PIC layout, so -O0 is the safe, simple default).
CFLAGS = ["-c", "-mcpu=cortex-m0plus", "-mthumb", "-ffreestanding", "-fno-exceptions",
          "-fpie", "-mpic-data-is-text-relative", "-msingle-pic-base",
          # each function in its own section so the linker can 4-byte align them
          # (SUBALIGN below) -> any function is valid as a merge entry. -falign-
          # functions is ignored at -Os, so this is the reliable route.
          "-ffunction-sections"]

# Linker script: one resolved .text at 0 holding code AND rodata contiguously, so
# intra-blob bl's are fixed up, .text.startup/.text.* merge in, and the PC-
# relative rodata offsets stay correct and are captured by objcopy -j .text.
LINK_SCRIPT = """SECTIONS
{
  . = 0;
  .text : SUBALIGN(4) {
    *(.text.startup) *(.text*) *(.glue_7) *(.glue_7t)
    *(.rodata*)
  }
  /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}
"""


def words(data):
    """Bytes -> list of little-endian 32-bit words (the firmware's on-disk form)."""
    if len(data) % 4:
        data += b"\x00" * (4 - len(data) % 4)
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def collect(elf):
    """Return (text_addr, text_bytes, funcs, rodata_addr, rodata_bytes).

    funcs is a sorted list of (addr_no_thumb, name) for every STT_FUNC symbol
    that lives inside .text, in address order.
    """
    text = elf.get_section_by_name(".text")
    if text is None:
        sys.exit("error: no .text section")

    # Refuse an unlinked object in any mode: its inter-function bl's aren't fixed.
    for sec in elf.iter_sections():
        if isinstance(sec, RelocationSection) and "text" in sec.name:
            sys.exit("error: %s present -> object is not fully linked. "
                     "Re-run with --compile, or link it first." % sec.name)

    text_addr = text["sh_addr"]
    text_bytes = text.data()

    rodata = elf.get_section_by_name(".rodata")
    rodata_addr = rodata["sh_addr"] if rodata and rodata["sh_size"] else None
    rodata_bytes = rodata.data() if rodata and rodata["sh_size"] else b""

    symtab = elf.get_section_by_name(".symtab")
    if not isinstance(symtab, SymbolTableSection):
        sys.exit("error: no symbol table (don't strip the object)")

    funcs = {}
    for sym in symtab.iter_symbols():
        if sym["st_info"]["type"] != "STT_FUNC" or not sym.name:
            continue
        addr = sym["st_value"] & ~1            # strip the Thumb bit
        if text_addr <= addr < text_addr + len(text_bytes):
            funcs.setdefault(addr, sym.name)   # first symbol at an address wins
    funcs = sorted(funcs.items())
    if not funcs:
        sys.exit("error: no function symbols found in .text")
    return text_addr, text_bytes, funcs, rodata_addr, rodata_bytes


def fmt_words(ws, indent="\t"):
    """Yield output lines of up to 8 words each, trailing space as the BAS does."""
    for i in range(0, len(ws), 8):
        yield indent + " ".join("%08X" % w for w in ws[i:i + 8]) + " "


def gen_join(sname, entry, text_addr, text_bytes, funcs, rodata_bytes):
    if rodata_bytes:
        sys.exit("error: constant data (.rodata) not allowed in JOIN mode")
    out = []
    end = text_addr + len(text_bytes)
    starts = [a for a, _ in funcs] + [end]
    first = True
    for idx, (addr, name) in enumerate(funcs):
        if name == entry:                       # entry is a dummy in join mode
            continue
        lo = addr - text_addr
        hi = starts[idx + 1] - text_addr
        if not first:
            out.append("'")
        first = False
        out.append("CSUB " + name)
        out.append("\t00000000")
        out.extend(fmt_words(words(text_bytes[lo:hi])))
        out.append("End CSUB")
    return out


def gen_merge(sname, entry, text_addr, text_bytes, funcs,
              rodata_addr, rodata_bytes):
    entry_addr = next((a for a, n in funcs if n == entry), funcs[0][0])
    if (entry_addr - text_addr) % 4:
        sys.exit("error: entry '%s' is not 4-byte aligned (offset word is "
                 "word-granular); make it the first function." % entry)
    entry_off = (entry_addr - text_addr) // 4

    out = ["CSUB " + sname, "\t%08X" % entry_off]
    starts = dict(funcs)
    ws = words(text_bytes)
    line = []
    for idx, w in enumerate(ws):
        addr = text_addr + idx * 4
        if addr in starts:
            if line:
                out.append("\t" + " ".join("%08X" % x for x in line) + " ")
                line = []
            out.append("\t'" + starts[addr])
        line.append(w)
        if len(line) == 8:
            out.append("\t" + " ".join("%08X" % x for x in line) + " ")
            line = []
    if line:
        out.append("\t" + " ".join("%08X" % x for x in line) + " ")

    # A separately-sectioned .rodata (e.g. from a pre-linked .elf) is appended
    # contiguously, at its linked offset, so the PC-relative rodata references
    # stay correct. With this tool's own linker script rodata already lives
    # inside .text, so this is normally empty.
    if rodata_bytes:
        pad = (rodata_addr - (text_addr + len(text_bytes)))
        if pad < 0:
            sys.exit("error: .rodata overlaps .text (unexpected layout)")
        if pad:
            out.append("\t'.pad")
            out.extend(fmt_words(words(b"\x00" * pad)))
        out.append("\t'.rodata")
        out.extend(fmt_words(words(rodata_bytes)))

    out.append("End CSUB")
    return out


def compile_c(src, includes=(), opt="0"):
    """Compile a .c to an object, return its path."""
    obj = tempfile.NamedTemporaryFile(suffix=".o", delete=False).name
    inc = []
    for d in includes:
        inc += ["-I", d]
    subprocess.run([GCC, *CFLAGS, "-O" + opt, *inc, "-o", obj, src], check=True)
    return obj


def link(objs, entry):
    """Link relocatable objects into one resolved ELF (single .text at 0)."""
    ld = tempfile.NamedTemporaryFile(suffix=".ld", mode="w", delete=False)
    ld.write(LINK_SCRIPT)
    ld.close()
    elf = tempfile.NamedTemporaryFile(suffix=".elf", delete=False).name
    try:
        subprocess.run([GCC, "-nostartfiles", "-nostdlib",
                        "-Wl,-T," + ld.name, "-Wl,-e," + entry, "-Wl,--no-warn-rwx-segments",
                        "-o", elf, *objs], check=True)
    finally:
        os.unlink(ld.name)
    return elf


def is_relocatable(path):
    with open(path, "rb") as f:
        return ELFFile(f)["e_type"] == "ET_REL"


def main():
    ap = argparse.ArgumentParser(description="ELF/C -> PicoMite CSUB hex")
    ap.add_argument("input", nargs="+", help=".elf/.o object(s), or .c with --compile")
    ap.add_argument("-n", "--name", help="CSUB name (default: input stem, upper)")
    ap.add_argument("-m", "--mode", choices=["join", "merge"], default="merge")
    ap.add_argument("-e", "--entry", default="main",
                    help="entry symbol for merge offset / dummy in join (default main)")
    ap.add_argument("-c", "--compile", action="store_true",
                    help="treat inputs as C source and compile them first")
    ap.add_argument("-I", "--include", action="append", default=[], metavar="DIR",
                    help="add a header search dir for --compile (e.g. the firmware tree "
                         "holding PicoCFunctions.h); repeatable")
    ap.add_argument("-O", "--opt", default="0", metavar="LEVEL",
                    help="optimisation level for --compile (default 0; use s or 2 for "
                         "compute-heavy CSUBs)")
    ap.add_argument("-o", "--output", help="write to file (default stdout)")
    args = ap.parse_args()

    stem = os.path.splitext(os.path.basename(args.input[0]))[0]
    sname = (args.name or stem).upper()

    tmps = []
    objs = list(args.input)
    if args.compile:
        objs = [compile_c(s, args.include, args.opt) for s in objs]
        tmps += objs
    # link relocatable objects (resolves intra-blob bl, merges .text.* fragments)
    if len(objs) > 1 or any(is_relocatable(o) for o in objs):
        elf = link(objs, args.entry)
        tmps.append(elf)
    else:
        elf = objs[0]

    try:
        with open(elf, "rb") as f:
            text_addr, text_bytes, funcs, rodata_addr, rodata_bytes = collect(ELFFile(f))
    finally:
        for t in tmps:
            try:
                os.unlink(t)
            except OSError:
                pass

    sys.stderr.write("Functions found:\n")
    for a, n in funcs:
        sys.stderr.write("  %08X  %s\n" % (a, n))

    if args.mode == "join":
        lines = gen_join(sname, args.entry, text_addr, text_bytes, funcs, rodata_bytes)
    else:
        lines = gen_merge(sname, args.entry, text_addr, text_bytes, funcs,
                          rodata_addr, rodata_bytes)

    text = "\n".join(lines) + "\n"
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
        sys.stderr.write("Created %s\n" % args.output)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
