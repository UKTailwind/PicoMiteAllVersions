#!/bin/bash
# run_tests.sh — Run all .bas test programs through the MMBasic host build
#
# Usage:
#   ./run_tests.sh                                 Compare both engines (default)
#   ./run_tests.sh --interp                        Run all tests with interpreter only
#   ./run_tests.sh --vm                            Run all tests with bytecode VM only
#   ./run_tests.sh tests/t01_print.bas             Run a single test (compare mode)
#   ./run_tests.sh tests/t01_print.bas --vm        Run a single test with specific engine
#
# Exit code: 0 if all tests pass, 1 if any test fails.

set -e
cd "$(dirname "$0")"

# Pin DATE$ / TIME$ to deterministic values so interpreter vs VM oracle
# comparisons match. Real wall-clock time is used when these are unset
# (e.g. under --sim, so the clock demo shows the actual time).
export MMBASIC_HOST_DATE=${MMBASIC_HOST_DATE:-02-01-2024}
export MMBASIC_HOST_TIME=${MMBASIC_HOST_TIME:-03:04:05}

BINARY=${BINARY:-./mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

# Default to comparison mode (both engines)
MODE=""
SINGLE_FILE=""
PASSED=0
FAILED=0
ERRORS=""

# Parse arguments
if [ $# -ge 1 ]; then
    if [ -f "$1" ] 2>/dev/null; then
        # Single file mode
        SINGLE_FILE="$1"
        MODE="${2:-}"
    else
        MODE="$1"
    fi
fi

TEST_TIMEOUT=${TEST_TIMEOUT:-10}

run_one_test() {
    local testfile="$1"
    local mode="$2"
    local name
    local run_args
    local extra_args=()
    name=$(basename "$testfile" .bas)
    run_args=$(sed -n '1s/^.*RUN_ARGS: //p' "$testfile")
    if [ -n "$run_args" ]; then
        read -r -a extra_args <<< "$run_args"
    fi

    printf "  %-30s " "$name"

    # Run with timeout to catch infinite loops (polls every 0.1s)
    local tmpfile
    tmpfile=$(mktemp)
    $BINARY "$testfile" "${extra_args[@]}" $mode > "$tmpfile" 2>&1 &
    local PID=$!
    local elapsed=0
    while kill -0 $PID 2>/dev/null; do
        sleep 0.1
        elapsed=$((elapsed + 1))
        if [ $elapsed -ge $((TEST_TIMEOUT * 10)) ]; then
            kill -9 $PID 2>/dev/null
            wait $PID 2>/dev/null
            local output
            output=$(cat "$tmpfile")
            rm -f "$tmpfile"
            echo "HUNG (timeout ${TEST_TIMEOUT}s)"
            FAILED=$((FAILED + 1))
            ERRORS="${ERRORS}\n--- $name ($mode) --- HUNG after ${TEST_TIMEOUT}s\n"
            return 1
        fi
    done
    wait $PID
    local ec=$?
    local output
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    # Extract memory peak from output (e.g. "VM heap: 0 / 131072 bytes (peak 78336, 60%)")
    local mem_info=""
    local peak
    peak=$(echo "$output" | sed -n 's/.*peak \([0-9]*\),.*/\1/p' | tail -1)
    if [ -n "$peak" ]; then
        local peak_kb=$((peak / 1024))
        local cap
        cap=$(echo "$output" | sed -n 's/.*\/ \([0-9]*\) bytes.*/\1/p' | tail -1)
        if [ -n "$cap" ] && [ "$cap" -gt 0 ]; then
            local pct=$((peak * 100 / cap))
            mem_info=" [${peak_kb}KB/${pct}%]"
        else
            mem_info=" [${peak_kb}KB]"
        fi
    fi

    if [ $ec -eq 0 ]; then
        echo "PASS${mem_info}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo "FAIL${mem_info}"
        FAILED=$((FAILED + 1))
        ERRORS="${ERRORS}\n--- $name ($mode) ---\n${output}\n"
        return 1
    fi
}

display_mode="compare (interpreter vs VM)"
if [ "$MODE" = "--interp" ]; then
    display_mode="interpreter only"
elif [ "$MODE" = "--vm" ]; then
    display_mode="bytecode VM only"
fi

echo "MMBasic Host Test Runner"
echo "========================"
echo ""

if [ -n "$SINGLE_FILE" ]; then
    echo "Running: $SINGLE_FILE ($display_mode)"
    echo ""
    # Honour the test file's RUN_ARGS: header the same way the full-suite
    # runner does. Without this, spot-checking a named test silently skips
    # --sd-root / --resolution / other mode flags and returns PASS when the
    # suite run would have gone red. Caught this after 20a8629 lied about
    # the pass count.
    single_run_args=$(sed -n '1s/^.*RUN_ARGS: //p' "$SINGLE_FILE")
    single_extra=()
    if [ -n "$single_run_args" ]; then
        read -r -a single_extra <<< "$single_run_args"
    fi
    $BINARY "$SINGLE_FILE" "${single_extra[@]}" $MODE
    exit $?
fi

echo "Mode: $display_mode"
echo ""

# Run all test files in sorted order
for testfile in tests/t*.bas; do
    [ -f "$testfile" ] || continue
    run_one_test "$testfile" "$MODE" || true
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
