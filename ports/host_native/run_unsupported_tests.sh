#!/bin/bash
# run_unsupported_tests.sh — VM-only negative tests for unsupported syscalls.
#
# Each test must fail under --vm and include a first-line marker:
#   ' EXPECT_ERROR: expected substring

set -e
cd "$(dirname "$0")"

BINARY=${BINARY:-./build/mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

PASSED=0
FAILED=0
ERRORS=""

run_one_test() {
    local testfile="$1"
    local name
    local expected
    local tmpfile
    local ec
    local output

    name=$(basename "$testfile" .bas)
    expected=$(sed -n '1s/^.*EXPECT_ERROR: //p' "$testfile")
    if [ -z "$expected" ]; then
        echo "  $name missing EXPECT_ERROR marker"
        FAILED=$((FAILED + 1))
        return 1
    fi

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    $BINARY "$testfile" --vm > "$tmpfile" 2>&1
    ec=$?
    set -e
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    if [ $ec -ne 0 ] && printf "%s" "$output" | grep -F "$expected" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- expected error containing: ${expected}\n${output}\n"
    return 1
}

echo "MMBasic VM Unsupported Syscall Tests"
echo "===================================="
echo ""

for testfile in tests/unsupported/*.bas; do
    [ -f "$testfile" ] || continue
    run_one_test "$testfile" || true
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
