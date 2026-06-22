"""
Post-build size/layout verifier for PicoMite firmware.

Original behaviour: report the highest written flash address from a .hex
file. Extended to also automate the two checks that were previously done
by hand against configuration.h and CMakeLists.txt:

  1. The firmware image ends before FLASH_TARGET_OFFSET (so SAVE-TO-FLASH
     and the program-storage area aren't overwritten).
  2. Static BSS end + PICO_HEAP_SIZE + PICO_CORE0_STACK_SIZE fits in RAM
     (catches the heap-BSS overlap class of bug).
  3. The MMBasic heap backing store (AllMemory / Heap) is 256-byte aligned.
     The firmware assumes every GetMemory() block is page (256-byte) aligned
     -- arrays and MEMORY PACK/COPY/POKE INTEGER rely on it. If the linker
     places the symbol off a 256-byte boundary, every allocation inherits
     the offset and VARADDR returns non-8-aligned addresses.

Inputs:
  hexfile  -- PicoMite.hex (required, positional)
  mapfile  -- PicoMite.elf.map (optional positional; enables the checks)

The map file is parsed for:
  * The Memory Configuration FLASH origin (for the FLASH base address).
  * `.heap <addr> <size>`           -> __bss_end__ and PICO_HEAP_SIZE.
  * `AllMemory` / `Heap` symbol      -> MMBasic heap base address.
  * `_fw_flash_target_offset` symbol -> FLASH_TARGET_OFFSET from
    configuration.h, surfaced into the link via an asm `.set` in Memory.c.

PICO_CORE0_STACK_SIZE and RAM_END come from build_limits.txt, written
alongside the map file by CMakeLists.txt at configure time.

Exit code:
  0 = both checks pass (or map file not supplied: legacy mode).
  1 = a check failed.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path


def parse_hex(filename):
    highest = 0
    ext_addr = 0
    with open(filename, "r") as f:
        for line in f:
            if not line.startswith(":"):
                continue
            length = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rectype = int(line[7:9], 16)
            if rectype == 4:  # Extended Linear Address
                ext_addr = int(line[9:13], 16)
            elif rectype == 0:  # Data record
                full_addr = (ext_addr << 16) + addr
                end_addr = full_addr + length
                if end_addr > highest:
                    highest = end_addr
    return highest


def round_up(value, boundary):
    return (value + boundary - 1) // boundary * boundary


_HEAP_RE = re.compile(
    r"^\.heap\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)"
)
_FLASH_REGION_RE = re.compile(
    r"^FLASH\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)"
)
# The MMBasic heap backing store. Matches the symbol *definition* line which
# in the map is: <spaces>0x<addr><spaces><name><eol>. Non-VGA builds use
# `AllMemory`; the RP2040 VGA build names it `Heap` (section .bss.zheap).
_HEAP_BASE_RE = re.compile(
    r"^\s+(0x[0-9a-fA-F]+)\s+(AllMemory|Heap)\s*$"
)

# Allocation granularity of the MMBasic heap (PAGESIZE in Memory.h). Every
# GetMemory() block is a multiple of this from the heap base, so the base
# itself must be aligned to it.
HEAP_PAGE_SIZE = 256


def parse_map(mapfile):
    """Return dict with keys: bss_end, pico_heap_size, flash_base. Any value
    may be None if the corresponding line was not found in the map."""
    out = {
        "bss_end": None,
        "pico_heap_size": None,
        "flash_base": None,
        "heap_base": None,
        "heap_base_sym": None,
    }
    with open(mapfile, "r") as f:
        for line in f:
            m = _HEAP_RE.match(line)
            if m and out["bss_end"] is None:
                out["bss_end"] = int(m.group(1), 16)
                out["pico_heap_size"] = int(m.group(2), 16)
                continue
            m = _FLASH_REGION_RE.match(line)
            if m and out["flash_base"] is None:
                out["flash_base"] = int(m.group(1), 16)
                continue
            m = _HEAP_BASE_RE.match(line)
            if m and out["heap_base"] is None:
                out["heap_base"] = int(m.group(1), 16)
                out["heap_base_sym"] = m.group(2)
                continue
    return out


def read_flash_target_offset(elffile):
    """Run arm-none-eabi-nm on the ELF and return the value of the absolute
    symbol _fw_flash_target_offset (created by an asm .set in Memory.c).
    Returns None if the toolchain or symbol is not available. We use nm
    rather than the map file because ld -Map omits absolute (sectionless)
    symbols from its output."""
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None or not Path(elffile).is_file():
        return None
    try:
        out = subprocess.run([nm, str(elffile)], capture_output=True,
                             text=True, check=True).stdout
    except (subprocess.CalledProcessError, OSError):
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == "_fw_flash_target_offset":
            return int(parts[0], 16)
    return None


def parse_build_limits(path):
    """Read KEY=VALUE pairs (one per line) from build_limits.txt. Integer
    values may be given in decimal or 0x-prefixed hex."""
    result = {}
    with open(path, "r") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip()
            try:
                result[key] = int(value, 0)
            except ValueError:
                result[key] = value
    return result


def fmt_signed_kb(delta_bytes):
    sign = "+" if delta_bytes >= 0 else "-"
    return f"{sign}{abs(delta_bytes)} bytes ({sign}{abs(delta_bytes) / 1024:.1f} KB)"


def check_flash(highest_hex, flash_base, flash_target_offset):
    if flash_target_offset is None:
        print("FLASH: SKIP -- _fw_flash_target_offset symbol not found in ELF "
              "(rebuild after pulling the Memory.c change, or arm-none-eabi-nm "
              "is not on PATH).")
        return None
    used = highest_hex - flash_base
    margin = flash_target_offset - used
    status = "PASS" if margin >= 0 else "FAIL"
    print(f"FLASH: used 0x{used:X} / limit 0x{flash_target_offset:X} "
          f"(FLASH_TARGET_OFFSET = {flash_target_offset // 1024} KB)")
    print(f"       margin: {fmt_signed_kb(margin)}  [{status}]")
    return status == "PASS"


def check_ram(map_info, limits):
    bss_end = map_info["bss_end"]
    pico_heap_size = map_info["pico_heap_size"]
    stack_size = limits.get("PICO_CORE0_STACK_SIZE")
    stack_top = limits.get("STACK_TOP")
    if bss_end is None or pico_heap_size is None:
        print("RAM:   SKIP -- could not parse .heap line from map.")
        return None
    if stack_size is None or stack_top is None:
        print("RAM:   SKIP -- build_limits.txt missing "
              "PICO_CORE0_STACK_SIZE / STACK_TOP.")
        return None
    # Stack grows down from STACK_TOP (= top of SCRATCH_Y). Collision occurs
    # if it grows past heap_end (bss_end + PICO_HEAP_SIZE).
    total_top = bss_end + pico_heap_size + stack_size
    margin = stack_top - total_top
    status = "PASS" if margin >= 0 else "FAIL"
    print(f"RAM:   bss_end 0x{bss_end:X} + heap 0x{pico_heap_size:X} "
          f"+ stack 0x{stack_size:X}")
    print(f"       = 0x{total_top:X} / STACK_TOP 0x{stack_top:X}")
    print(f"       margin: {fmt_signed_kb(margin)}  [{status}]")
    return status == "PASS"


def check_heap_alignment(map_info):
    heap_base = map_info["heap_base"]
    sym = map_info["heap_base_sym"] or "AllMemory/Heap"
    if heap_base is None:
        print("HEAP:  SKIP -- AllMemory/Heap symbol not found in map.")
        return None
    offset = heap_base % HEAP_PAGE_SIZE
    status = "PASS" if offset == 0 else "FAIL"
    print(f"HEAP:  {sym} base 0x{heap_base:X} "
          f"(must be {HEAP_PAGE_SIZE}-byte aligned)")
    if offset == 0:
        print(f"       aligned: offset 0  [{status}]")
    else:
        print(f"       MISALIGNED: base % {HEAP_PAGE_SIZE} = {offset} "
              f"(% 8 = {heap_base % 8}) -- every GetMemory() block inherits "
              f"this offset, so VARADDR is not 8-aligned  [{status}]")
    return status == "PASS"


def main(argv):
    if len(argv) < 2:
        print("Usage: python GetHighestHexAddress.py firmware.hex [PicoMite.elf.map]")
        return 1

    hexfile = argv[1]
    mapfile = argv[2] if len(argv) > 2 else None

    highest = parse_hex(hexfile)
    safe16 = round_up(highest, 16)
    safe16k = round_up(highest, 0x4000)
    flash_base = 0x10000000
    blocks = (safe16k - flash_base) // 1024
    print(f"Highest written address: 0x{highest:X}")
    print(f"Rounded up to 16-byte boundary: 0x{safe16:X}")
    print(f"Rounded up to 16KB boundary: 0x{safe16k:X}")
    print(f"Equivalent number of 1KB blocks from flash base: {blocks}")

    if mapfile is None:
        return 0

    map_path = Path(mapfile)
    if not map_path.is_file():
        print(f"Map file not found: {mapfile}")
        return 1
    map_info = parse_map(map_path)
    limits_path = map_path.parent / "build_limits.txt"
    limits = parse_build_limits(limits_path) if limits_path.is_file() else {}
    if not limits:
        print(f"Note: build_limits.txt not found next to map "
              f"(expected at {limits_path}). RAM check will be skipped.")

    if map_info["flash_base"]:
        flash_base = map_info["flash_base"]
        blocks = (safe16k - flash_base) // 1024

    # The ELF lives next to the map (PicoMite.elf vs PicoMite.elf.map).
    elf_path = map_path.with_suffix("") if map_path.suffix == ".map" else None
    flash_target_offset = read_flash_target_offset(elf_path) if elf_path else None

    print()
    print(f"COMPILE = {limits.get('COMPILE', '?')}")
    print()
    flash_ok = check_flash(highest, flash_base, flash_target_offset)
    ram_ok = check_ram(map_info, limits)
    heap_ok = check_heap_alignment(map_info)
    if flash_ok is False or ram_ok is False or heap_ok is False:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
