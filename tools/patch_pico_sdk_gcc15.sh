#!/usr/bin/env bash
#
# Pico SDK 2.1.1's pioasm sources rely on transitive <cstdint> includes.
# Newer host GCC/libstdc++ combinations no longer provide those by accident,
# so patch the checked-out SDK in CI before CMake builds pioasm.

set -euo pipefail

sdk="${1:-${PICO_SDK_PATH:-}}"
if [[ -z "$sdk" ]]; then
    echo "usage: tools/patch_pico_sdk_gcc15.sh <pico-sdk-path>" >&2
    echo "or set PICO_SDK_PATH" >&2
    exit 2
fi

if [[ ! -d "$sdk/tools/pioasm" ]]; then
    echo "error: '$sdk' does not look like a Pico SDK checkout" >&2
    exit 2
fi

python3 - "$sdk" <<'PY'
from pathlib import Path
import sys

sdk = Path(sys.argv[1])
targets = [
    sdk / "tools/pioasm/output_format.h",
    sdk / "tools/pioasm/pio_types.h",
    sdk / "tools/pioasm/pio_assembler.cpp",
]

for path in targets:
    text = path.read_text()
    if "#include <cstdint>" in text:
        continue

    lines = text.splitlines(keepends=True)
    insert_at = None
    for i, line in enumerate(lines):
        if line.startswith("#include "):
            insert_at = i + 1
    if insert_at is None:
        for i, line in enumerate(lines):
            if line.startswith("#pragma once"):
                insert_at = i + 1
                break
    if insert_at is None:
        insert_at = 0

    lines.insert(insert_at, "#include <cstdint>\n")
    path.write_text("".join(lines))
    print(f"patched {path}")
PY
