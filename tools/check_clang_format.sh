#!/usr/bin/env bash
set -euo pipefail

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required but was not found in PATH" >&2
    exit 127
fi

clang_format_version=$(clang-format --version)
case "$clang_format_version" in
    "clang-format version 21.1.8"*)
        ;;
    *)
        echo "clang-format 21.1.8 is required, found: $clang_format_version" >&2
        exit 1
        ;;
esac

crlf_files=$(git grep -Il $'\r' -- . \
    ':(exclude)third_party/**' \
    ':(exclude)ports/host_native/vendor/**' || true)
if [ -n "$crlf_files" ]; then
    echo "Files with CRLF or CR line endings found:" >&2
    echo "$crlf_files" >&2
    exit 1
fi

found=0
while IFS= read -r -d '' file; do
    case "$file" in
        third_party/* | ports/host_native/vendor/*)
            continue
            ;;
    esac
    found=1
    clang-format --dry-run --Werror "$file"
done < <(git ls-files -z -- '*.c' '*.h' '*.cc' '*.cpp' '*.cxx' '*.hh' '*.hpp' '*.hxx')

if [ "$found" -eq 0 ]; then
    echo "No C/C++ files found."
    exit 0
fi
