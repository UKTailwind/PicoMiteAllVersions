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
# Scope:
#   HAL-header scope (hal/*.h) — any preprocessor conditional (#if / #ifdef /
#   #ifndef / #elif) fails the gate, regardless of which macro it references.
#   HAL headers are pure C declarations. This is stricter than the core-file
#   scope because a HAL header with an ifdef inside leaks the exact abstraction
#   the HAL is supposed to hide. The only tolerated preprocessor is the
#   double-inclusion guard #ifndef HAL_FOO_H / #define HAL_FOO_H / #endif;
#   the header-guard is detected by filename match, not by exempting all ifdefs.
#   See docs/real-hal-fixup-plan.md F1.
#
#   Strict core scope — files that must be 0 on target-macro ifdefs, 0 on
#   port-config-macro ifdefs, AND 0 on `if (HAL_PORT_*)` runtime folds.
#   Populated as phases land. Initially empty; each phase that migrates a
#   core file adds the file to STRICT_FILES below.
#
#   Informational core scope — all core files. Violations print as counts
#   but do not fail. Progress-tracking view, not the gate. Reports separate
#   counts for target ifdefs, port-config ifdefs, and runtime folds.
#
# Target macros:
#   PICOMITE, PICOMITEVGA, PICOMITEWEB, HDMI, rp2350, PICO_RP2350, USBKEYBOARD,
#   MMBASIC_HOST, MMBASIC_WASM, PICOMITEPLUS, PICOCALC
#
# Port-config macros (per fixup plan):
#   Anything matching HAL_PORT_[A-Za-z0-9_]+ or PORT_[A-Za-z0-9_]+ as a
#   preprocessor-conditional operand. Allowed as *values* in C expressions and
#   array sizes; not allowed as preprocessor gates. Renaming #ifdef rp2350 to
#   #if HAL_PORT_PWM_SLICE_COUNT > 8 does not count as elimination.
#
# Runtime folds (rule 3 in docs/real-hal-plan.md "The standard"):
#   `if (HAL_PORT_X) { ... }` and `if (!HAL_PORT_X) { ... }` count as gates.
#   The compiler folds them to dead code on the 0/1 value, but the source
#   still hard-codes a port-shape branch — that's gaming, not eliminating.
#   Acceptable elimination strategies: HAL hooks with real-impl + stub
#   driver pair, file relocation, universal Option.* field guards, per-port
#   palette macros / per-port port_*.h files. Runtime checks on Option.*
#   fields (e.g. `if (Option.TOUCH_CS)`) are fine — those reflect user
#   state, not port shape.
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

# Any conditional-compilation directive. Used for the hal/*.h strict check.
ALL_IFDEF_RE='^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)\b'

# Join target macros into an alternation for the regex.
macro_alt="$(IFS='|'; echo "${TARGET_MACROS[*]}")"
# Match lines like: #if / #ifdef / #ifndef / #elif ... containing one of the target
# macros. Word-boundary on both sides so "PICOMITE" doesn't match "PICOMITEVGA"
# unintentionally (both are in the list anyway, but the regex should still be precise).
pattern="^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)\\b.*\\b(${macro_alt})\\b"

# Match preprocessor conditionals on port-config macros. Forbidden as gates in
# core per the fixup-plan standard.
PORT_IFDEF_RE="${ALL_IFDEF_RE}.*\\b(HAL_PORT_[A-Za-z0-9_]+|PORT_[A-Za-z0-9_]+)\\b"

# Files whose purity is currently enforced (fail on any hit).
# Empty at Phase 0 start — populated as phases migrate files.
STRICT_FILES=(
  core/mmbasic/External.c  # F2 close: zero target-macro, zero port-config ifdefs.
  core/mmbasic/FileIO.c    # F3 close: zero target-macro, zero port-config ifdefs.
  core/mmbasic/MM_Misc.c   # F4 close: zero target-macro, zero port-config ifdefs.
  shared/audio/Audio.c     # Phase 6b close: zero target-macro, zero port-config ifdefs.
  core/mmbasic/Draw.c      # Phase 7b close: zero target-macro, zero port-config ifdefs.
              # Remaining 3 #ifdef GUICONTROLS gates are feature flags
              # (not target macros), permitted by the strict check.
  core/mmbasic/Memory.c    # Phase 10 close: zero target-macro, zero port-config
              # ifdefs. Remaining 1 #ifdef GUICONTROLS is a feature
              # flag, permitted by the strict check.
  core/mmbasic/Commands.c  # Phase 11 close: zero target-macro, zero port-config
              # ifdefs.
  core/mmbasic/Functions.c # Phase 11 close: zero target-macro, zero port-config
              # ifdefs.
  runtime/vm/bc_runtime.c # Phase 11 step 12 close: zero target-macro, zero
              # port-config ifdefs. Source-load and source-free moved
              # to port_bc_frun_*, port_bc_run_file_*,
              # port_bc_runtime_free_source hooks.
  runtime/vm/bc_bridge.c # Phase 11 step 13 close: zero target-macro, zero
              # port-config ifdefs. rp2350 funtbl[] subfun-hash rebuild
              # moved to port_bc_bridge_{clear,rehash}_subfun hooks.
  runtime/vm/vm_sys_graphics.c # Phase 11 step 14 close: zero target-macro, zero
              # port-config ifdefs. Host's DrawCircle-delegating fork
              # deleted — the 170-line VM-local draw_circle impl works
              # on host too (uses only shared DrawPixel/DrawRectangle
              # plus the VM's own scratch allocator, both unconditional).
  core/mmbasic/MMBasic.c   # Phase 11 step 20 close: zero target-macro, zero
              # port-config ifdefs. funtbl[] + hashlabels + hash-based
              # FindSubFun / findlabel / findvar collision check moved
              # to ports/pico_sdk_common/funtbl_port.c behind
              # port_try_* hooks.  error()'s console-surface + LCD
              # banner helpers extracted to clear_runtime_port.c.
              # Remaining 3 #ifdef gates are feature flags (MMFAMILY,
              # GUICONTROLS, __PIC32MX__), permitted by the strict
              # check.
)

# Host-port WASM-clean scope: ports/host_native/*.c must not bleed
# WASM-specific code. The narrower check (vs. STRICT_FILES) lets the
# gate land before pre-existing rp2350 simulation ifdefs in
# host_peripheral_stubs.c / host_runtime.c are cleaned up — those are
# unrelated to the WASM/native split. Headers are skipped because
# port_config.h legitimately defines HAL_PORT_* values.
HOST_WASM_CLEAN_MACROS=(MMBASIC_WASM __EMSCRIPTEN__)
shopt -s nullglob
HOST_NATIVE_FILES=(ports/host_native/*.c)
shopt -u nullglob

# ESP32 port scope: the port may reference its own MMBASIC_ESP32 identity
# macro, but must not grow target-macro gates for other ports or inherit
# host_native runtime/peripheral sources or header paths again.
shopt -s nullglob
ESP32_MAIN_C_FILES=(ports/esp32_s3/main/*.c)
shopt -u nullglob
ESP32_CMAKE=ports/esp32_s3/main/CMakeLists.txt
ESP32_PLATFORM=ports/esp32_s3/main/esp32_platform.h

# Files tracked informationally (report counts, do not fail).
INFO_FILES=(
  core/mmbasic/Draw.c
  core/mmbasic/MM_Misc.c
  core/mmbasic/External.c
  core/mmbasic/FileIO.c
  core/mmbasic/Commands.c
  core/mmbasic/Memory.c
  core/mmbasic/Functions.c
  shared/audio/Audio.c
  core/mmbasic/Operators.c
  core/mmbasic/MMBasic.c
  runtime/vm/bc_source.c
  runtime/vm/bc_vm.c
  runtime/vm/bc_runtime.c
  runtime/vm/bc_alloc.c
  runtime/vm/bc_bridge.c
  runtime/vm/bc_compiler_core.c
  runtime/vm/bc_debug.c
  runtime/vm/vm_sys_graphics.c
  runtime/vm/vm_sys_file.c
  runtime/vm/vm_sys_pin.c
  runtime/vm/vm_sys_time.c
  runtime/vm/vm_sys_input.c
  core/state/display_state.c
)

fail=0

# Header-guard detection: for a file hal/hal_foo.h, the tolerated guard is
# exactly "#ifndef HAL_FOO_H" (or HAL_FOO_H_) with its matching #define /
# #endif. Any other ifdef in the file fails the strict HAL-header gate.
header_guard_macro() {
  local file="$1"
  local base
  base="$(basename "$file" .h)"
  # Convert hal_foo -> HAL_FOO_H. Uppercase the name, append _H.
  # Use tr for portability with bash 3.2 (default on macOS).
  echo "$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')_H"
}

# Strict check for hal/*.h: every preprocessor conditional must either be the
# file's own include guard, or an __cplusplus linkage guard. Anything else
# (target macros, port-config macros, feature gates, platform ifdefs) fails.
check_hal_header() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  local guard
  guard="$(header_guard_macro "$file")"
  local offenders
  # Strip tolerated ifdefs:
  #   - #ifndef HAL_FOO_H        (include guard, possibly with trailing _)
  #   - #ifdef __cplusplus       (C/C++ linkage guard)
  #   - #if defined(__cplusplus) (same, alternate spelling)
  offenders="$(grep -nE "$ALL_IFDEF_RE" "$file" \
    | grep -vE "^[0-9]+:[[:space:]]*#[[:space:]]*ifndef[[:space:]]+(${guard}|${guard}_)\b" \
    | grep -vE "^[0-9]+:[[:space:]]*#[[:space:]]*(ifdef|ifndef)[[:space:]]+__cplusplus\b" \
    | grep -vE "^[0-9]+:[[:space:]]*#[[:space:]]*if[[:space:]]+(!)?defined\([[:space:]]*__cplusplus[[:space:]]*\)" \
    | grep -vE "^[0-9]+:[[:space:]]*#[[:space:]]*ifndef[[:space:]]+HAL_PORT_CONFIG_INCLUDED\b" \
    || true)"
  if [[ -n "$offenders" ]]; then
    echo "HAL-PURITY FAIL: $file — HAL headers must be preprocessor-clean"
    echo "    (only #ifndef-guard and __cplusplus extern-C guards are tolerated)"
    echo "$offenders" | sed 's/^/    /'
    fail=1
  fi
}

# Strict check for files added to STRICT_FILES: zero target-macro ifdefs AND
# zero port-config ifdefs.
check_file_strict() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  local hits
  hits="$(grep -nE "$pattern" "$file" || true)"
  if [[ -n "$hits" ]]; then
    echo "HAL-PURITY FAIL: $file contains target-macro #ifdefs"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
  local port_hits
  port_hits="$(grep -nE "$PORT_IFDEF_RE" "$file" || true)"
  if [[ -n "$port_hits" ]]; then
    echo "HAL-PURITY FAIL: $file contains port-config #ifdefs (forbidden as gates)"
    echo "$port_hits" | sed 's/^/    /'
    fail=1
  fi
  # Rule 3: zero `if (HAL_PORT_*)` runtime folds. The compiler folds
  # them to dead code on a 0/1 value, but the source still hard-codes
  # a port-shape branch — that's gaming, not eliminating. Only catches
  # C-statement form; the comment-and-doc form `if (HAL_PORT_X)` in a
  # /* ... */ block is intentionally not flagged (grep can't tell).
  local fold_hits
  fold_hits="$(grep -nE 'if[[:space:]]*\([[:space:]]*!?[[:space:]]*HAL_PORT_' "$file" \
               | grep -vE '^\s*[0-9]+:\s*[/ ]*\*' \
               | grep -vE '^\s*[0-9]+:\s*//' \
               || true)"
  if [[ -n "$fold_hits" ]]; then
    echo "HAL-PURITY FAIL: $file contains if (HAL_PORT_*) runtime folds"
    echo "$fold_hits" | sed 's/^/    /'
    fail=1
  fi
}

check_file_no_wasm() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  local alt
  alt="$(IFS='|'; echo "${HOST_WASM_CLEAN_MACROS[*]}")"
  local pat="${ALL_IFDEF_RE}.*\\b(${alt})\\b"
  local hits
  hits="$(grep -nE "$pat" "$file" || true)"
  if [[ -n "$hits" ]]; then
    echo "HAL-PURITY FAIL: $file references WASM-specific macros"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
}

check_esp32_no_host_native_reuse() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  local hits
  hits="$(grep -n 'ports/host_native' "$file" \
    | grep -vE '^[0-9]+:[[:space:]]*#' \
    || true)"
  if [[ -n "$hits" ]]; then
    echo "HAL-PURITY FAIL: $file references host_native in the ESP32 component"
    echo "    (no host_native sources or include paths are allowed)"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
}

check_esp32_no_host_identity() {
  local file="$1"
  [[ -f "$file" ]] || return 0
  local hits
  hits="$(grep -nE '(^[[:space:]]*#define[[:space:]]+MMBASIC_HOST\b|(^|[[:space:]])-DMMBASIC_HOST\b|^[[:space:]]*MMBASIC_HOST[[:space:]]*$)' "$file" || true)"
  if [[ -n "$hits" ]]; then
    echo "HAL-PURITY FAIL: $file defines MMBASIC_HOST for the ESP32 build"
    echo "    (ESP32 must build as MMBASIC_ESP32 plus HAL/port feature macros)"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
}

check_file_info() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    return 0
  fi
  local count port_count fold_count
  count="$(grep -cE "$pattern" "$file" || true)"
  count="${count:-0}"
  port_count="$(grep -cE "$PORT_IFDEF_RE" "$file" || true)"
  port_count="${port_count:-0}"
  fold_count="$(grep -cE 'if[[:space:]]*\([[:space:]]*!?[[:space:]]*HAL_PORT_' "$file" || true)"
  fold_count="${fold_count:-0}"
  if [[ "$port_count" -gt 0 || "$fold_count" -gt 0 ]]; then
    printf '    %-22s target=%-4s port-config=%-4s runtime-fold=%s\n' \
      "$file" "$count" "$port_count" "$fold_count"
  else
    printf '    %-22s target=%s\n' "$file" "$count"
  fi
}

echo "== HAL purity check (Phase 0 raw-grep mode) =="
echo
echo "HAL-header scope (hal/*.h must be preprocessor-clean, include guards aside):"
shopt -s nullglob
hal_headers=(hal/*.h)
shopt -u nullglob
if [[ ${#hal_headers[@]} -eq 0 ]]; then
  echo "    (no hal/*.h yet — Phase 0 scaffolding)"
else
  for f in "${hal_headers[@]}"; do
    check_hal_header "$f"
  done
fi

echo
echo "Strict core scope (must be 0 target-macro and 0 port-config #ifdefs):"
if [[ ${#STRICT_FILES[@]} -eq 0 ]]; then
  echo "    (no core files promoted to strict scope yet)"
fi
for f in ${STRICT_FILES[@]+"${STRICT_FILES[@]}"}; do
  check_file_strict "$f"
done
if [[ $fail -eq 0 ]]; then
  echo "    (all strict files clean)"
fi

echo
echo "Host-port WASM-clean scope (ports/host_native/*.c must not bleed WASM):"
if [[ ${#HOST_NATIVE_FILES[@]} -eq 0 ]]; then
  echo "    (no host_native sources found)"
else
  pre_fail=$fail
  for f in "${HOST_NATIVE_FILES[@]}"; do
    check_file_no_wasm "$f"
  done
  if [[ $fail -eq $pre_fail ]]; then
    echo "    (all host_native sources WASM-clean)"
  fi
fi

echo
echo "ESP32 port scope (strict target gates + no host_native runtime reuse):"
if [[ ${#ESP32_MAIN_C_FILES[@]} -eq 0 ]]; then
  echo "    (no ESP32 main sources found)"
else
  pre_fail=$fail
  for f in "${ESP32_MAIN_C_FILES[@]}"; do
    check_file_strict "$f"
  done
  check_esp32_no_host_native_reuse "$ESP32_CMAKE"
  check_esp32_no_host_identity "$ESP32_CMAKE"
  check_esp32_no_host_identity "$ESP32_PLATFORM"
  if [[ $fail -eq $pre_fail ]]; then
    echo "    (all ESP32 port files clean)"
  fi
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
