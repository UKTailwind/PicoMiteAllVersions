#!/usr/bin/env bash
# validate_all.sh — the real pre-commit gate.
#
# Runs every build + every test suite the repository ships:
#   1. HAL purity check (tools/check_hal_purity.sh)
#   2. host_native build + host/run_tests.sh (239 BASIC tests)
#   3. mmbasic_stdio build + tests/run_tests.sh (smoke corpus)
#   4. mmbasic_ansi build (no test suite exists for ansi)
#   5. host_wasm build, IF emcc is on PATH; otherwise skipped with a
#      clear "skipped — run on CI" note.
#   6. buildall.sh (14 device variants).
#
# Exits non-zero on the first failure. Green means every layer of
# the project compiles + every test suite passes. Until this script
# is green, no claim of "passing" should be made.
#
# Run from anywhere. Output is verbose enough to diagnose failures
# without re-running individual stages.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

fail=0
SECTIONS=()

run_section() {
    local name="$1"; shift
    printf '\n=== %s ===\n' "$name"
    if ! ( "$@" ); then
        printf '\n*** FAIL: %s\n' "$name"
        fail=1
        SECTIONS+=("FAIL: $name")
        return 1
    fi
    SECTIONS+=("OK:   $name")
    return 0
}

# ---- 1. HAL purity gate -------------------------------------------------
run_section "HAL purity" "$ROOT/tools/check_hal_purity.sh" || true

# ---- 2. host_native build + tests --------------------------------------
run_section "host_native build" bash -c "cd '$ROOT/host' && ./build.sh" || true
if [ -x "$ROOT/host/mmbasic_test" ]; then
    run_section "host_native tests (run_tests.sh)" \
        bash -c "cd '$ROOT/host' && SKIP_HAL_PURITY=1 ./run_tests.sh" || true
    run_section "host_native NEW smoke (porttools)" \
        python3 "$ROOT/porttools/host_new_smoke.py" --no-build || true
else
    SECTIONS+=("SKIP: host_native tests (binary missing)")
    SECTIONS+=("SKIP: host_native NEW smoke (binary missing)")
fi

# ---- 3. mmbasic_stdio build + tests ------------------------------------
run_section "mmbasic_stdio build" \
    bash -c "cd '$ROOT/ports/mmbasic_stdio' && make" || true
if [ -x "$ROOT/ports/mmbasic_stdio/mmbasic_stdio" ]; then
    run_section "mmbasic_stdio tests (smoke corpus)" \
        bash -c "cd '$ROOT/ports/mmbasic_stdio/tests' && ./run_tests.sh" || true
else
    SECTIONS+=("SKIP: mmbasic_stdio tests (binary missing)")
fi

# ---- 4. mmbasic_ansi build (no test suite) -----------------------------
run_section "mmbasic_ansi build" \
    bash -c "cd '$ROOT/ports/mmbasic_ansi' && make" || true

# ---- 5. host_wasm build (only if emcc available) -----------------------
if command -v emcc >/dev/null 2>&1; then
    run_section "host_wasm build" \
        bash -c "cd '$ROOT/ports/host_wasm' && make" || true
else
    printf '\n=== host_wasm build ===\nemcc not on PATH; skipping. CI runs this stage.\n'
    SECTIONS+=("SKIP: host_wasm build (emcc not installed)")
fi

# ---- 6. Device buildall (14 variants) ----------------------------------
run_section "buildall.sh (14 device variants)" \
    bash -c "SKIP_HAL_PURITY=1 '$ROOT/buildall.sh'" || true

# ---- summary -----------------------------------------------------------
printf '\n\n=== validate_all.sh summary ===\n'
for line in "${SECTIONS[@]}"; do
    printf '  %s\n' "$line"
done
echo

if [ "$fail" = 0 ]; then
    echo "ALL VALIDATION GREEN."
    exit 0
else
    echo "VALIDATION FAILED — see sections above."
    exit 1
fi
