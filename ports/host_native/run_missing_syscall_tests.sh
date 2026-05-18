#!/bin/bash
# run_missing_syscall_tests.sh -- Intentionally-red tests for VM syscall gaps.
#
# These programs are expected to run under the legacy interpreter but fail in
# compare mode until the VM has native implementations for the referenced
# syscall/command/function. This suite is not part of run_tests.sh.

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
TEST_TIMEOUT=${TEST_TIMEOUT:-5}

run_one_test() {
    local testfile="$1"
    local name
    local tmpfile
    local ec=""
    local output
    local run_args
    local extra_args=()

    name=$(basename "$testfile" .bas)
    run_args=$(sed -n '1s/^.*RUN_ARGS: //p' "$testfile")
    if [ -n "$run_args" ]; then
        read -r -a extra_args <<< "$run_args"
    fi
    printf "  %-34s " "$name"

    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" "${extra_args[@]}" > "$tmpfile" 2>&1 &
    local pid=$!
    local elapsed=0
    while kill -0 $pid 2>/dev/null; do
        sleep 0.1
        elapsed=$((elapsed + 1))
        if [ $elapsed -ge $((TEST_TIMEOUT * 10)) ]; then
            kill -9 $pid 2>/dev/null
            wait $pid 2>/dev/null
            ec=124
            break
        fi
    done
    if [ -z "$ec" ]; then
        wait $pid
        ec=$?
    fi
    set -e
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    if [ $ec -eq 0 ]; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    if [ $ec -eq 124 ]; then
        echo "HUNG"
    else
        echo "FAIL"
    fi
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name ---\n${output}\n"
    return 1
}

echo "MMBasic VM Missing Syscall Tests"
echo "================================"
echo ""
echo "These tests are expected to fail until native VM syscall support is added."
echo ""

for testfile in tests/missing_syscalls/*.bas; do
    [ -f "$testfile" ] || continue
    run_one_test "$testfile" || true
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Missing syscall failures:"
    echo -e "$ERRORS"
    exit 1
fi
