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

run_expect_not_contains() {
    local name="$1"
    local forbidden="$2"
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

    if [ $ec -eq 0 ] && ! printf "%s" "$output" | grep -F "$forbidden" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- must not contain: ${forbidden}\n${output}\n"
    return 1
}

run_repl_piped() {
    local name="$1"
    local expected="$2"
    local input="$3"
    shift 3
    local tmpfile
    local output
    local ec

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    printf "%b" "$input" | "$BINARY" --repl "$@" > "$tmpfile" 2>&1
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

run_repl_piped_not_contains() {
    local name="$1"
    local forbidden="$2"
    local input="$3"
    shift 3
    local tmpfile
    local output
    local ec

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    printf "%b" "$input" | "$BINARY" --repl "$@" > "$tmpfile" 2>&1
    ec=$?
    set -e
    output=$(cat "$tmpfile")
    rm -f "$tmpfile"

    if [ $ec -eq 0 ] && ! printf "%s" "$output" | grep -F "$forbidden" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- must not contain: ${forbidden}\n${output}\n"
    return 1
}

run_repl_piped_list_not_contains() {
    local name="$1"
    local forbidden="$2"
    local input="$3"
    shift 3
    local tmpfile
    local output
    local list_output
    local ec

    printf "  %-30s " "$name"
    tmpfile=$(mktemp)
    set +e
    printf "%b" "$input" | "$BINARY" --repl "$@" > "$tmpfile" 2>&1
    ec=$?
    set -e
    output=$(cat "$tmpfile")
    list_output=$(sed -n '/LIST/,$ p' "$tmpfile")
    rm -f "$tmpfile"

    if [ $ec -eq 0 ] && ! printf "%s" "$list_output" | grep -F "$forbidden" >/dev/null; then
        echo "PASS"
        PASSED=$((PASSED + 1))
        return 0
    fi

    echo "FAIL"
    FAILED=$((FAILED + 1))
    ERRORS="${ERRORS}\n--- $name --- LIST output must not contain: ${forbidden}\n${output}\n"
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

run_expect_contains \
    "bridge_scalar_dim_timing" \
    "3 Variables" \
    "$BINARY" tests/host_shims/bridge_scalar_dim_timing.bas --vm || true

run_expect_internal_timeout \
    "vm_loop_internal_timeout" \
    "$BINARY" tests/host_shims/vm_loop_timeout.bas --vm --timeout-ms 50 || true

# --- Autosave tests ---
# Bug 3: Ctrl-Z should exit autosave cleanly, no "Error" message
run_repl_piped_not_contains \
    "autosave_ctrlz_no_error" \
    "Error" \
    "AUTOSAVE\nPRINT \"hello\"\n\x1aLIST\n" || true

# Baseline: autosave + Ctrl-Z saves the program and LIST shows it
run_repl_piped \
    "autosave_saves_program" \
    'Print "hello"' \
    "AUTOSAVE\nPRINT \"hello\"\n\x1aLIST\n" || true

# Bug 2: Ctrl-C should abort autosave without saving; LIST shows empty program.
# When Ctrl-C is ignored, autosave swallows "LIST" as program text and the
# prompt never appears — so checking for "> LIST" distinguishes the two cases.
run_repl_piped \
    "autosave_ctrlc_aborts" \
    "> LIST" \
    "AUTOSAVE\nPRINT \"hello\"\n\x03LIST\n" || true

# Multi-line program: all 15 lines should survive autosave intact
MULTI_PROG="AUTOSAVE\n"
for i in $(seq 1 15); do
    MULTI_PROG="${MULTI_PROG}PRINT \"line ${i}\"\n"
done
MULTI_PROG="${MULTI_PROG}\x1aLIST\n"
run_repl_piped \
    "autosave_multiline" \
    'Print "line 15"' \
    "$MULTI_PROG" || true

# AUTOSAVE CRUNCH: comments and extra spaces should be stripped
run_repl_piped \
    "autosave_crunch_strips" \
    'Print "ok"' \
    "AUTOSAVE C\n' this is a comment\nPRINT  \"ok\"\n\x1aLIST\n" || true

# AUTOSAVE CRUNCH: LIST output should not contain the comment
# (the comment appears in the echo during input, which is fine)
run_repl_piped_list_not_contains \
    "autosave_crunch_no_comment" \
    "comment" \
    "AUTOSAVE C\n' this is a comment\nPRINT \"ok\"\n\x1aLIST\n" || true

# AUTOSAVE APPEND: new lines added after existing program
run_repl_piped \
    "autosave_append" \
    'Print "second"' \
    "AUTOSAVE\nPRINT \"first\"\n\x1aAUTOSAVE APPEND\nPRINT \"second\"\n\x1aLIST\n" || true

# AUTOSAVE APPEND preserves the original program too
run_repl_piped \
    "autosave_append_keeps_old" \
    'Print "first"' \
    "AUTOSAVE\nPRINT \"first\"\n\x1aAUTOSAVE APPEND\nPRINT \"second\"\n\x1aLIST\n" || true

# AUTOSAVE N: no-echo mode still saves the program
run_repl_piped \
    "autosave_noecho_saves" \
    'Print "quiet"' \
    "AUTOSAVE N\nPRINT \"quiet\"\n\x1aLIST\n" || true

# Ctrl-C during AUTOSAVE CRUNCH also aborts without saving
run_repl_piped \
    "autosave_crunch_ctrlc" \
    "> LIST" \
    "AUTOSAVE C\nPRINT \"nope\"\n\x03LIST\n" || true

# Ctrl-Z after AUTOSAVE APPEND produces no error
run_repl_piped_not_contains \
    "autosave_append_no_error" \
    "Error" \
    "AUTOSAVE\nPRINT \"first\"\n\x1aAUTOSAVE APPEND\nPRINT \"second\"\n\x1aLIST\n" || true

echo ""
echo "Results: $PASSED passed, $FAILED failed"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failures:"
    echo -e "$ERRORS"
    exit 1
fi
