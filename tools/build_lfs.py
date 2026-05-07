#!/usr/bin/env python3
"""
Build a littlefs image matching PicoMite's HDMI (rp2350, no-USBKEYBOARD) geometry,
populated with demo programs, then emit a UF2 ready to flash.

PicoMite littlefs config (FileIO.c / configuration.h):
  read_size      = 1
  prog_size      = 256
  block_size     = 4096
  cache_size     = 256
  lookahead_size = 256

Flash layout for HDMI (PICOMITEVGA + rp2350 + HDMI, no USBKEYBOARD), with the
FLASH_TARGET_OFFSET fix applied (configuration.h):
  FLASH_TARGET_OFFSET   = 1024 KiB    = 0x00100000
  FLASH_ERASE_SIZE      =    4 KiB    = 0x00001000
  SAVEDVARS_FLASH_SIZE  =   16 KiB    = 0x00004000
  MAX_PROG_SIZE         =  164 KiB    = 0x00029000   (= HEAP_MEMORY_SIZE)
  MAXFLASHSLOTS         = 3           (4 slots reserved with +1 library)
  TOP_OF_SYSTEM_FLASH   = OFFSET + ERASE + SAVED + (MAXFLASHSLOTS+1)*PROG
                        = 0x001A9000
  XIP_BASE              = 0x10000000
  Flash size            = 16 MiB      (W25Q128)
  LFS region            = 16 MiB - TOP_OF_SYSTEM_FLASH
                        = 15036416 B  = 3671 blocks

Outputs:
  lfs.bin   raw filesystem image (3671 * 4096 = 15036416 bytes)
  lfs.uf2   UF2 of the same, family rp2350-arm-s, absolute address 0x101A9000
"""

import os
import struct
import sys
from pathlib import Path

from littlefs import LittleFS

# --- geometry (must match PicoMite firmware) ---------------------------------
# HDMI no-USB:  FLASH_TARGET_OFFSET = 1024 KiB  -> TOP_OF_SYSTEM_FLASH = 0x1A9000
# HDMIUSB:      FLASH_TARGET_OFFSET = 1056 KiB  -> TOP_OF_SYSTEM_FLASH = 0x1B1000
VARIANT = os.environ.get("PICOMITE_VARIANT", "HDMI")
FLASH_TARGET_OFFSET   = (1056 if VARIANT == "HDMIUSB" else 1024) * 1024
FLASH_ERASE_SIZE      = 4096
SAVEDVARS_FLASH_SIZE  = 16384
MAX_PROG_SIZE         = 164 * 1024
MAXFLASHSLOTS         = 3
FLASH_SIZE_BYTES      = 16 * 1024 * 1024
XIP_BASE              = 0x10000000

TOP_OF_SYSTEM_FLASH   = (FLASH_TARGET_OFFSET
                         + FLASH_ERASE_SIZE
                         + SAVEDVARS_FLASH_SIZE
                         + (MAXFLASHSLOTS + 1) * MAX_PROG_SIZE)

LFS_FLASH_OFFSET      = TOP_OF_SYSTEM_FLASH                      # 0x1A9000
LFS_REGION_SIZE       = FLASH_SIZE_BYTES - LFS_FLASH_OFFSET
BLOCK_SIZE            = 4096
BLOCK_COUNT           = LFS_REGION_SIZE // BLOCK_SIZE             # 3671
PROG_SIZE             = 256
READ_SIZE             = 1
CACHE_SIZE            = 256
LOOKAHEAD_SIZE        = 256

LFS_ABS_ADDRESS       = XIP_BASE + LFS_FLASH_OFFSET               # 0x101A9000

# UF2 family ID for rp2350 ARM Secure
UF2_FAMILY_RP2350_ARM_S = 0xe48bff59
UF2_MAGIC1 = 0x0a324655
UF2_MAGIC2 = 0x9e5d5157
UF2_MAGIC_END = 0x0ab16f30
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_PAYLOAD = 256


def gather_files(root: Path) -> list[tuple[Path, str]]:
    """Returns [(source_path, target_name)].

    - demo_*.bas, sieve*.bas from local picocalc tree
    - host/bench_*.bas → BENCH/ subdir
    - mand.bas + companion CSUB demos from github.com/jvanderberg/picocalc_csub_helpers
    - pico_blocks.bas from github.com/jvanderberg/pico_blocks
      (these GitHub versions work on stock firmware; the locally bundled
       mand.bas and pico_blocks.bas in picocalc/ do not, so we replace them.)
    """
    csub = Path("/tmp/pmsrc/picocalc_csub_helpers")
    blocks = Path("/tmp/pmsrc/pico_blocks")

    sources: list[tuple[Path, str]] = []
    for p in sorted(root.glob("demo_*.bas")):
        sources.append((p, p.name))
    for name in ("sieve.bas", "sieveasm.bas"):
        p = root / name
        if p.exists():
            sources.append((p, name))
    for p in sorted((root / "host").glob("bench_*.bas")):
        sources.append((p, f"BENCH/{p.name}"))
    for name in ("mand.bas", "test_mandel.bas", "slow_mand.bas",
                 "test_pow.bas", "blocks.bas"):
        p = csub / name
        if p.exists():
            sources.append((p, name))
    p = blocks / "pico_blocks.bas"
    if p.exists():
        sources.append((p, "pico_blocks.bas"))
    return sources


def build_image(files: list[tuple[Path, str]]) -> bytes:
    fs = LittleFS(
        block_size=BLOCK_SIZE,
        block_count=BLOCK_COUNT,
        read_size=READ_SIZE,
        prog_size=PROG_SIZE,
        cache_size=CACHE_SIZE,
        lookahead_size=LOOKAHEAD_SIZE,
        # PicoMite ships littlefs lib 2.5 with LFS_DISK_VERSION = 0x00020000.
        # Newer littlefs-python defaults to 2.1, which the older lib refuses
        # to mount and falls through to format().
        disk_version=0x00020000,
    )
    dirs_made: set[str] = set()
    for src, target in files:
        parent = "/".join(target.split("/")[:-1])
        if parent and parent not in dirs_made:
            fs.makedirs(parent, exist_ok=True)
            dirs_made.add(parent)
        with fs.open(target, "wb") as dst:
            dst.write(src.read_bytes())
        print(f"  + {target} ({src.stat().st_size} B)")
    return fs.context.buffer


def emit_uf2(image: bytes, abs_addr: int, out_path: Path) -> None:
    """Emit only chunks that aren't fully erased (0xFF). Skipping
    erased pages keeps the UF2 small; flash is already in the erased
    state after picotool's sector-level erase, so omitted pages remain
    0xFF — exactly what littlefs expects."""
    erased = b"\xff" * UF2_PAYLOAD
    n_total = (len(image) + UF2_PAYLOAD - 1) // UF2_PAYLOAD
    populated = []
    for i in range(n_total):
        chunk = image[i * UF2_PAYLOAD:(i + 1) * UF2_PAYLOAD].ljust(
            UF2_PAYLOAD, b"\xff")
        if chunk != erased:
            populated.append((i, chunk))

    n = len(populated)
    blocks = []
    for seq, (i, chunk) in enumerate(populated):
        addr = abs_addr + i * UF2_PAYLOAD
        block = struct.pack(
            "<8I",
            UF2_MAGIC1, UF2_MAGIC2,
            UF2_FLAG_FAMILY_ID_PRESENT,
            addr, UF2_PAYLOAD, seq, n,
            UF2_FAMILY_RP2350_ARM_S,
        )
        block += chunk + b"\x00" * (476 - UF2_PAYLOAD)
        block += struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == 512
        blocks.append(block)
    out_path.write_bytes(b"".join(blocks))
    print(f"  uf2 pages: {n} populated / {n_total} total "
          f"({100 * n / n_total:.1f}% non-erased)")


def main() -> None:
    repo_root = Path("/Users/joshv/picocalc/PicoMiteAllVersions")
    out_dir = repo_root / "build_lfs"
    out_dir.mkdir(exist_ok=True)

    files = gather_files(repo_root)
    if not files:
        print("no source files found", file=sys.stderr)
        sys.exit(1)

    print(f"littlefs region: {LFS_REGION_SIZE} B "
          f"= {BLOCK_COUNT} x {BLOCK_SIZE} B blocks")
    print(f"flash offset:    0x{LFS_FLASH_OFFSET:08x} "
          f"(absolute 0x{LFS_ABS_ADDRESS:08x})")
    print(f"adding {len(files)} files:")

    image = build_image(files)
    assert len(image) == LFS_REGION_SIZE, \
        f"image is {len(image)} B, expected {LFS_REGION_SIZE}"

    suffix = f"_{VARIANT}" if VARIANT != "HDMI" else ""
    bin_path = out_dir / f"lfs{suffix}.bin"
    uf2_path = out_dir / f"lfs{suffix}.uf2"
    bin_path.write_bytes(image)
    emit_uf2(image, LFS_ABS_ADDRESS, uf2_path)

    print(f"\nwrote {bin_path} ({bin_path.stat().st_size} B)")
    print(f"wrote {uf2_path} ({uf2_path.stat().st_size} B)")
    print(f"\nFlash with:  picotool load -v {uf2_path}")


if __name__ == "__main__":
    main()
