#!/bin/bash
# run_frontend_tests.sh -- VM-owned source frontend smoke tests.

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
    local tmpfile
    name=$(basename "$testfile" .bas)
    printf "  %-30s " "$name"

    tmpfile=$(mktemp)
    set +e
    "$BINARY" "$testfile" --source-compare > "$tmpfile" 2>&1
    local ec=$?
    set -e

    if [ $ec -eq 0 ]; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        rm -f "$tmpfile"
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name ---\n$(cat "$tmpfile")\n"
    rm -f "$tmpfile"
    return 1
}

echo "MMBasic VM Source Frontend Tests"
echo "================================"
echo ""

for testfile in tests/frontend/*.bas; do
    [ -f "$testfile" ] || continue
    case "$(basename "$testfile")" in
        t036_mulshr_call_sqrshr_peephole.bas|t037_mulshr_call_sqrshr_opt_equiv.bas)
            continue
            ;;
    esac
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
