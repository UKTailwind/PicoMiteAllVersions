#!/bin/bash
# ports/mmbasic_stdio/tests/run_tests.sh — smoke corpus for the stdio port.
#
# Each *.bas file has comment lines of the form
#   ' EXPECT: <literal expected output line>
# placed after the code. The sentinel is exactly `' EXPECT: ` (one
# leading space before the apostrophe is optional; one space after
# the colon is consumed). Anything after that — including leading
# whitespace — is the literal expected line.
#
# The harness runs the program through ./mmbasic_stdio, strips ANSI
# escape sequences (error() emits cursor-show / color-reset codes),
# and compares against the concatenated EXPECT lines.

set -e
cd "$(dirname "$0")"

BINARY="$(cd .. && pwd)/mmbasic_stdio"
if [ ! -x "$BINARY" ]; then
    echo "Binary not found: $BINARY"
    echo "Build it with: (cd .. && make)"
    exit 1
fi

# Strip ANSI CSI sequences (ESC [ ... letter) and bare CRs from a file.
# do_end's SSPrintString emits e.g. \e[?25h (show cursor) and the
# prompt colour restore when an error ends a program; host_runtime.c's
# SerialConsolePutC translates \n to \r\n in raw mode. Neither is part
# of the BASIC-visible output.
strip_ansi() {
    perl -pe 's/\x1b\[[0-9;?]*[A-Za-z]//g; s/\r//g' "$1"
}

PASS=0
FAIL=0
ERRORS=""

for prog in *.bas; do
    [ -f "$prog" ] || continue
    name=$(basename "$prog" .bas)
    printf "  %-32s " "$name"

    # Extract EXPECT lines; the sentinel consumes one space after the colon.
    expected_file=$(mktemp)
    sed -n "s/^' EXPECT: //p" "$prog" > "$expected_file"

    # Run, capture stdout+stderr, strip ANSI.
    raw_file=$(mktemp)
    "$BINARY" "$prog" > "$raw_file" 2>&1 || true
    actual_file=$(mktemp)
    strip_ansi "$raw_file" > "$actual_file"

    if diff -u "$expected_file" "$actual_file" > /dev/null 2>&1; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
        diff_out=$(diff -u "$expected_file" "$actual_file" || true)
        ERRORS="${ERRORS}
--- $name ---
$diff_out
"
    fi

    rm -f "$expected_file" "$raw_file" "$actual_file"
done

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ $FAIL -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo "$ERRORS"
    exit 1
fi
