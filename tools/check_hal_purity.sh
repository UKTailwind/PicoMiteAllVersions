#!/usr/bin/env bash
#
# tools/check_hal_purity.sh
#
# Fails if any hardware target macro is referenced in files that are supposed
# to be hardware-clean per docs/real-hal-plan.md.
#
# Usage:
#   tools/check_hal_purity.sh                # check all in-scope files
#   tools/check_hal_purity.sh path [path...] # check specific files
#
# Exit codes:
#   0  all clean
#   1  one or more violations
#   2  usage / configuration error
#
# Scope (Phase 0 baseline):
#   Strict scope — files that must be 100% clean NOW:
#     - hal/*.h
#   Expanding scope — populated as phases land. Initially empty; each phase that
#   migrates a core file to HAL-only usage adds the file to STRICT_FILES below.
#
#   Informational scope — all core files. Violations print as warnings but do
#   not fail the build. This is the progress-tracking view, not the gate.
#
# Target macros checked:
#   PICOMITE, PICOMITEVGA, PICOMITEWEB, HDMI, rp2350, PICO_RP2350, USBKEYBOARD,
#   MMBASIC_HOST, MMBASIC_WASM, PICOMITEPLUS, PICOCALC
#
# Detection:
#   Phase 0: raw grep on #if/#ifdef/#ifndef/#elif lines. Does NOT catch macro
#   indirection (e.g. `#define HAL_FOR_RP2350(x) x`). Phase 13 replaces this
#   with a preprocessor-expanded check that parses per-target compile_commands.json.
#   The raw check is a lower bound; the preprocessor-expanded check is the real
#   gate and lives in tools/check_hal_purity_expanded.sh (unimplemented, Phase 13).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Macros that must not appear in hardware-clean files.
TARGET_MACROS=(
  PICOMITE
  PICOMITEVGA
  PICOMITEWEB
  HDMI
  rp2350
  PICO_RP2350
  USBKEYBOARD
  MMBASIC_HOST
  MMBASIC_WASM
  PICOMITEPLUS
  PICOCALC
)

# Join macros into an alternation for the regex.
macro_alt="$(IFS='|'; echo "${TARGET_MACROS[*]}")"
# Match lines like: #if / #ifdef / #ifndef / #elif ... containing one of the macros.
# Word-boundary on both sides so "PICOMITE" doesn't match "PICOMITEVGA" unintentionally
# (both are in the list anyway, but the regex should still be precise).
pattern="^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b(${macro_alt})\b"

# Files whose purity is currently enforced (fail on any hit).
# Empty at Phase 0 start — populated as phases migrate files.
STRICT_FILES=(
  # hal/*.h is checked dynamically below.
)

# Files tracked informationally (report counts, do not fail).
INFO_FILES=(
  Draw.c
  MM_Misc.c
  External.c
  FileIO.c
  Commands.c
  Memory.c
  Functions.c
  Audio.c
  Operators.c
  MMBasic.c
  bc_source.c
  bc_vm.c
  bc_runtime.c
  bc_alloc.c
  bc_bridge.c
  bc_compiler_core.c
  bc_debug.c
  vm_sys_graphics.c
  vm_sys_file.c
  vm_sys_pin.c
  vm_sys_time.c
  vm_sys_input.c
  core/state/display_state.c
)

fail=0

check_file_strict() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    return 0
  fi
  local hits
  hits="$(grep -nE "$pattern" "$file" || true)"
  if [[ -n "$hits" ]]; then
    echo "HAL-PURITY FAIL: $file contains target-macro #ifdefs"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
}

check_file_info() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    return 0
  fi
  local count
  count="$(grep -cE "$pattern" "$file" || true)"
  count="${count:-0}"
  printf '    %-22s %s\n' "$file" "$count"
}

echo "== HAL purity check (Phase 0 raw-grep mode) =="
echo
echo "Strict scope (must be 0):"
# Always check hal/*.h as strict scope.
shopt -s nullglob
hal_headers=(hal/*.h)
shopt -u nullglob
if [[ ${#hal_headers[@]} -eq 0 ]]; then
  echo "    (no hal/*.h yet — Phase 0 scaffolding)"
else
  for f in "${hal_headers[@]}"; do
    check_file_strict "$f"
  done
fi
for f in ${STRICT_FILES[@]+"${STRICT_FILES[@]}"}; do
  check_file_strict "$f"
done
if [[ $fail -eq 0 ]]; then
  echo "    (all strict files clean)"
fi

echo
echo "Informational scope (progress tracking, non-failing):"
for f in "${INFO_FILES[@]}"; do
  check_file_info "$f"
done

echo
if [[ $fail -ne 0 ]]; then
  echo "== HAL PURITY CHECK FAILED =="
  exit 1
fi

echo "== HAL purity check passed =="
exit 0
