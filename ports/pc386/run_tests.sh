#!/usr/bin/env bash
# ports/pc386/run_tests.sh — golden-output test harness for the pc386 port.
#
# Mirrors host/run_tests.sh UX: discovers tests/<stage>/*.bas, boots
# the kernel headless in QEMU with the BASIC program loaded, captures
# COM1 output, and compares against the matching .ok file.
#
# Stage 0 has no tests yet — this is a stub that prints a clear message.
# The real harness lands in Stage 2, when BASIC programs first run on
# bare metal via the lifted mmbasic_stdio HAL surface.
#
# Usage:
#   ./run_tests.sh                        # run all tests/<stage>/*.bas
#   ./run_tests.sh tests/stage-2          # run one stage's tests
#   ./run_tests.sh tests/stage-2/foo.bas  # run one test

set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL="$PORT_DIR/build/mmbasic.elf"
TESTS_DIR="$PORT_DIR/tests"
TIMEOUT_SECS=30

# --- Stage 0 stub ------------------------------------------------------------
# Until Stage 2 lands the BASIC-program-as-input harness, there are no
# test corpora to iterate over. Detect that and exit cleanly with a
# pointer to the plan, so CI doesn't spuriously fail on a freshly-
# scaffolded port.

if [[ ! -d "$TESTS_DIR" ]] || [[ -z "$(find "$TESTS_DIR" -name '*.bas' -print -quit 2>/dev/null)" ]]; then
    cat <<EOF
pc386 test harness: no tests yet.

Test corpus arrives with Stage 2 (mmbasic_stdio HAL surface lifted to
bare metal). See docs/pc386-plan.md for the staged delivery.

To smoke-test the kernel scaffold (Stage 0):
    ./build.sh
    ./run_headless.sh --timeout 5

EOF
    exit 0
fi

# --- Stage 2+ harness (placeholder shape) ------------------------------------
# When tests land, this section iterates them. Kept as a sketch so the
# Stage 2 author has the obvious shape to fill in:
#
#   for bas in "$@"; do
#       ok="${bas%.bas}.ok"
#       actual=$(./run_headless.sh --timeout "$TIMEOUT_SECS" "$KERNEL" \
#           </dev/null 2>/dev/null | sed -n '/^---PROGRAM---/,$p')
#       if diff -u "$ok" <(printf '%s\n' "$actual") >/dev/null; then
#           echo "PASS  $bas"
#       else
#           echo "FAIL  $bas"
#           failures+=("$bas")
#       fi
#   done
#
# The exact contract — how the kernel ingests a BASIC program at boot
# (multiboot module? embedded blob? FAT16 disk image?) — is decided in
# Stage 2.

echo "Stage 2+ test harness not yet implemented." >&2
exit 1
