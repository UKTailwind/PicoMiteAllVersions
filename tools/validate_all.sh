#!/usr/bin/env bash
# tools/validate_all.sh — the real pre-commit gate.
#
# Runs every build + every test suite the repository ships:
#   1. HAL purity check (tools/check_hal_purity.sh)
#   2. host_native build + ports/host_native/run_tests.sh and host-shim tests
#   3. mmbasic_stdio build + tests/run_tests.sh (smoke corpus)
#   4. mmbasic_ansi build (no test suite exists for ansi)
#   5. host_wasm build, IF emcc is on PATH; otherwise skipped with a
#      clear "skipped — run on CI" note.
#   6. buildall.sh (14 device variants).
#
# Failures are recorded and reported at the end. Dependent tests are
# skipped when their prerequisite build fails; independent stages
# still run so you get a complete picture in one pass.
#
# Run from anywhere. Output is verbose enough to diagnose failures
# without re-running individual stages.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
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
run_section "HAL purity" "$ROOT/tools/check_hal_purity.sh"

# ---- 2. host_native build + tests --------------------------------------
if run_section "host_native build" bash -c "cd '$ROOT/ports/host_native' && ./build.sh"; then
    run_section "host_native tests (run_tests.sh)" \
        bash -c "cd '$ROOT/ports/host_native' && SKIP_HAL_PURITY=1 ./run_tests.sh"
    run_section "host_native host-shim tests" \
        bash -c "cd '$ROOT/ports/host_native' && ./run_host_shim_tests.sh"
    run_section "host_native NEW smoke (porttools)" \
        python3 "$ROOT/porttools/host_new_smoke.py" --no-build
else
    SECTIONS+=("SKIP: host_native tests (build failed)")
    SECTIONS+=("SKIP: host_native host-shim tests (build failed)")
    SECTIONS+=("SKIP: host_native NEW smoke (build failed)")
fi

# ---- 3. mmbasic_stdio build + tests ------------------------------------
if run_section "mmbasic_stdio build" \
    bash -c "cd '$ROOT/ports/mmbasic_stdio' && make"; then
    run_section "mmbasic_stdio tests (smoke corpus)" \
        bash -c "cd '$ROOT/ports/mmbasic_stdio/tests' && ./run_tests.sh"
else
    SECTIONS+=("SKIP: mmbasic_stdio tests (build failed)")
fi

# ---- 4. mmbasic_ansi build (no test suite) -----------------------------
run_section "mmbasic_ansi build" \
    bash -c "cd '$ROOT/ports/mmbasic_ansi' && make"

# ---- 5. host_wasm build (only if emcc available) -----------------------
ensure_emcc() {
    if command -v emcc >/dev/null 2>&1; then
        return 0
    fi

    local env_script
    for env_script in \
        "${EMSDK:-}/emsdk_env.sh" \
        "$HOME/emsdk/emsdk_env.sh"; do
        if [ -n "$env_script" ] && [ -f "$env_script" ]; then
            # shellcheck disable=SC1090
            EMSDK_QUIET=1 . "$env_script" >/dev/null 2>&1
            command -v emcc >/dev/null 2>&1 && return 0
        fi
    done

    return 1
}

if ensure_emcc; then
    run_section "host_wasm build" \
        bash -c "cd '$ROOT/ports/host_wasm' && make"
else
    printf '\n=== host_wasm build ===\nemcc not on PATH; skipping. CI runs this stage.\n'
    SECTIONS+=("SKIP: host_wasm build (emcc not installed)")
fi

# ---- 6. Device buildall (14 variants) ----------------------------------
run_section "buildall.sh (14 device variants)" \
    bash -c "SKIP_HAL_PURITY=1 '$ROOT/buildall.sh'"

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
