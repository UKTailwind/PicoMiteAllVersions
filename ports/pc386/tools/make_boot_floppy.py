#!/usr/bin/env python3
"""Build a bootable 1.44 MB FAT12 pc386 floppy image."""

import argparse
import os
import struct
import sys

BYTES_PER_SECTOR = 512
TOTAL_SECTORS = 2880
RESERVED_SECTORS = 128
FATS = 2
SECTORS_PER_FAT = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = 14
DATA_START_LBA = RESERVED_SECTORS + FATS * SECTORS_PER_FAT + ROOT_SECTORS
FIRST_DATA_CLUSTER = 2
MAX_ROOT_BYTES = ROOT_SECTORS * BYTES_PER_SECTOR
IMAGE_BYTES = TOTAL_SECTORS * BYTES_PER_SECTOR


def dos_datetime():
    year, month, day = 2026, 5, 11
    hour, minute, second = 12, 0, 0
    date = ((year - 1980) << 9) | (month << 5) | day
    time = (hour << 11) | (minute << 5) | (second // 2)
    return time, date


def name83(name: str) -> bytes:
    if name == ".":
        return b".          "
    if name == "..":
        return b"..         "
    base, dot, ext = name.partition(".")
    base = base.upper()
    ext = ext.upper() if dot else ""
    if not (1 <= len(base) <= 8) or len(ext) > 3:
        raise ValueError(f"not an 8.3 name: {name}")
    return base.encode("ascii").ljust(8, b" ") + ext.encode("ascii").ljust(3, b" ")


class Fat12Image:
    def __init__(self, boot_sector: bytes, stage2: bytes):
        if len(boot_sector) != BYTES_PER_SECTOR:
            raise ValueError("boot sector must be exactly 512 bytes")
        max_stage2 = (RESERVED_SECTORS - 1) * BYTES_PER_SECTOR
        if len(stage2) > max_stage2:
            raise ValueError(f"stage2 is {len(stage2)} bytes, max is {max_stage2}")
        self.img = bytearray(IMAGE_BYTES)
        self.img[:BYTES_PER_SECTOR] = boot_sector
        self.img[BYTES_PER_SECTOR:BYTES_PER_SECTOR + len(stage2)] = stage2
        self.fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
        self.fat[0:3] = b"\xF0\xFF\xFF"
        self.next_cluster = FIRST_DATA_CLUSTER
        self.root = bytearray(MAX_ROOT_BYTES)
        self.root_index = 0

    def cluster_lba(self, cluster: int) -> int:
        return DATA_START_LBA + (cluster - FIRST_DATA_CLUSTER)

    def set_fat(self, cluster: int, value: int) -> None:
        off = cluster + cluster // 2
        if cluster & 1:
            self.fat[off] = (self.fat[off] & 0x0F) | ((value << 4) & 0xF0)
            self.fat[off + 1] = (value >> 4) & 0xFF
        else:
            self.fat[off] = value & 0xFF
            self.fat[off + 1] = (self.fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)

    def alloc_clusters(self, count: int) -> int:
        if count <= 0:
            return 0
        first = self.next_cluster
        for c in range(first, first + count):
            self.set_fat(c, c + 1 if c < first + count - 1 else 0xFFF)
        self.next_cluster += count
        if self.cluster_lba(self.next_cluster) > TOTAL_SECTORS:
            raise ValueError("floppy image is full")
        return first

    def write_cluster_data(self, first: int, data: bytes) -> None:
        sectors = (len(data) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
        for i in range(sectors):
            lba = self.cluster_lba(first + i)
            start = lba * BYTES_PER_SECTOR
            chunk = data[i * BYTES_PER_SECTOR:(i + 1) * BYTES_PER_SECTOR]
            self.img[start:start + len(chunk)] = chunk

    def dir_entry(self, name: str, attr: int, first_cluster: int, size: int) -> bytes:
        t, d = dos_datetime()
        ent = bytearray(32)
        ent[0:11] = name83(name)
        ent[11] = attr
        struct.pack_into("<H", ent, 14, t)
        struct.pack_into("<H", ent, 16, d)
        struct.pack_into("<H", ent, 22, t)
        struct.pack_into("<H", ent, 24, d)
        struct.pack_into("<H", ent, 26, first_cluster)
        struct.pack_into("<I", ent, 28, size)
        return bytes(ent)

    def add_root_entry(self, entry: bytes) -> None:
        off = self.root_index * 32
        if off + 32 > len(self.root):
            raise ValueError("root directory is full")
        self.root[off:off + 32] = entry
        self.root_index += 1

    def add_file_to_dir(self, directory: bytearray, index: int, name: str, path: str) -> int:
        data = open(path, "rb").read()
        clusters = max(1, (len(data) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR)
        first = self.alloc_clusters(clusters)
        self.write_cluster_data(first, data)
        directory[index * 32:index * 32 + 32] = self.dir_entry(name, 0x20, first, len(data))
        return index + 1

    def add_root_file(self, name: str, path: str) -> None:
        data = open(path, "rb").read()
        clusters = max(1, (len(data) + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR)
        first = self.alloc_clusters(clusters)
        self.write_cluster_data(first, data)
        self.add_root_entry(self.dir_entry(name, 0x20, first, len(data)))

    def add_boot_dir(self, kernel: str, limine_conf: str, limine_sys: str) -> None:
        first = self.alloc_clusters(1)
        if first != 2:
            raise ValueError("BOOT directory must be cluster 2 for the fixed loader layout")
        self.add_root_entry(self.dir_entry("BOOT", 0x10, first, 0))
        directory = bytearray(BYTES_PER_SECTOR)
        directory[0:32] = self.dir_entry(".", 0x10, first, 0)
        directory[32:64] = self.dir_entry("..", 0x10, 0, 0)
        idx = 2
        idx = self.add_file_to_dir(directory, idx, "MMBASIC.ELF", kernel)
        if idx != 3:
            raise ValueError("MMBASIC.ELF must start at cluster 3 for the fixed loader layout")
        idx = self.add_file_to_dir(directory, idx, "LIMINE.CNF", limine_conf)
        self.add_file_to_dir(directory, idx, "LIMINE.SYS", limine_sys)
        self.write_cluster_data(first, bytes(directory))

    def finish(self, out: str) -> None:
        fat_start = RESERVED_SECTORS * BYTES_PER_SECTOR
        for i in range(FATS):
            start = fat_start + i * len(self.fat)
            self.img[start:start + len(self.fat)] = self.fat
        root_start = (RESERVED_SECTORS + FATS * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        self.img[root_start:root_start + len(self.root)] = self.root
        with open(out, "wb") as fp:
            fp.write(self.img)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot-sector", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--limine-conf", required=True)
    ap.add_argument("--limine-sys", required=True)
    ap.add_argument("--hello", required=True)
    ap.add_argument("--fizzbuzz", required=True)
    ap.add_argument("--readme", required=True)
    ap.add_argument("out")
    ns = ap.parse_args()

    boot_sector = open(ns.boot_sector, "rb").read()
    stage2 = open(ns.stage2, "rb").read()
    image = Fat12Image(boot_sector, stage2)
    image.add_boot_dir(ns.kernel, ns.limine_conf, ns.limine_sys)
    image.add_root_file("HELLO.BAS", ns.hello)
    image.add_root_file("FIZZBUZZ.BAS", ns.fizzbuzz)
    image.add_root_file("README.TXT", ns.readme)
    image.finish(ns.out)
    print(f"built bootable floppy {ns.out} ({IMAGE_BYTES} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
