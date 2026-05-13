#!/usr/bin/env python3
"""Run host-native network conformance checks."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=repo_root())
    parser.add_argument("--make", default="make")
    args = parser.parse_args(argv)

    root = args.repo.resolve()
    hal_cmd = [args.make, "-C", str(root / "ports" / "host_native"), "net-hal-test"]
    rc = subprocess.call(hal_cmd, cwd=root)
    if rc != 0:
        return rc
    basic_cmd = [
        sys.executable,
        str(root / "porttools" / "host_basic_network_conformance.py"),
        "--repo",
        str(root),
        "--make",
        args.make,
        "--no-build",
    ]
    return subprocess.call(basic_cmd, cwd=root)


if __name__ == "__main__":
    raise SystemExit(main())
