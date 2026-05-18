#!/bin/bash
# run_host_shim_tests.sh -- Host-only shim behavior tests.

set -e
cd "$(dirname "$0")"

BINARY=${BINARY:-./build/mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

export MMBASIC_HOST_DATE=${MMBASIC_HOST_DATE:-02-01-2024}
export MMBASIC_HOST_TIME=${MMBASIC_HOST_TIME:-03:04:05}

PASSED=0
FAILED=0
ERRORS=""

run_expect_contains() {
    local name="$1"
    local expected="$2"
    shift 2
    local tmpfile
    local output
    local ec

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    "$@" > "$tmpfile" 2>&1
    ec=$?
    set -e
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    if [ $ec -eq 0 ] && printf "%s" "$output" | grep -F "$expected" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- expected substring: ${expected}\n${output}\n"
    return 1
}

run_expect_internal_timeout() {
    local name="$1"
    shift
    local tmpfile
    local output
    local ec
    local pid
    local elapsed

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    "$@" > "$tmpfile" 2>&1 &
    pid=$!
    elapsed=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 0.1
        elapsed=$((elapsed + 1))
        if [ "$elapsed" -ge 10 ]; then
            kill -9 "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            output=$(cat "$tmpfile")
            rm -f "$tmpfile"
            set -e
            echo "FAIL"
            FAILED=$((FAILED + 1))
            ERRORS="${ERRORS}\n--- $name --- expected internal timeout, process hung\n${output}\n"
            return 1
        fi
    done
    wait "$pid"
    ec=$?
    set -e
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    if [ "$ec" -eq 124 ] && printf "%s" "$output" | grep -F -- "--- Timed Out ---" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- expected exit 124 with internal timeout\n${output}\n"
    return 1
}

echo "MMBasic Host Shim Tests"
echo "======================="
echo ""

run_expect_contains \
    "date_time_fixed" \
    "02-01-2024 03:04:05" \
    "$BINARY" tests/host_shims/date_time_fixed.bas --interp || true

run_expect_contains \
    "delayed_inkey_compare" \
    "Output: MATCH" \
    "$BINARY" tests/host_shims/delayed_inkey.bas --keys-after-ms 25 q --timeout-ms 500 || true

run_expect_contains \
    "delayed_keydown_interp" \
    "z" \
    "$BINARY" tests/host_shims/delayed_keydown.bas --interp --keys-after-ms 25 z --timeout-ms 500 || true

run_expect_internal_timeout \
    "vm_loop_internal_timeout" \
    "$BINARY" tests/host_shims/vm_loop_timeout.bas --vm --timeout-ms 50 || true

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
