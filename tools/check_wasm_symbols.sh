#!/usr/bin/env bash
#
# tools/check_wasm_symbols.sh — guard against duplicate-definition bugs
# in the WASM build that wasm-ld silences via --allow-multiple-definition.
#
# The wasm-ld flag is required for MMBasic's tentative-definition merging
# (gui_bcolour and friends) but it also masks accidental double-defines:
# if both ports/host_native/host_sim_slowdown.c and ports/host_wasm/
# host_sim_slowdown.c land in the same link line, the linker silently
# picks one and ignores the other — a symptom-free way to ship the wrong
# slowdown impl in the browser.
#
# This script verifies each WASM-port hook has exactly one definition
# across the wasm_obj/ tree. Run after a WASM build.
#
# Exit codes:
#   0  every hook has exactly one definition
#   1  duplicate or missing definitions
#   2  llvm-nm not found

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OBJ_DIR="$REPO_ROOT/ports/host_wasm/wasm_obj"
LLVM_NM="${LLVM_NM:-$HOME/emsdk/upstream/bin/llvm-nm}"

if [[ ! -x "$LLVM_NM" ]]; then
  echo "check_wasm_symbols: $LLVM_NM not executable" >&2
  echo "Set LLVM_NM env var or activate emsdk." >&2
  exit 2
fi

if [[ ! -d "$OBJ_DIR" ]]; then
  echo "check_wasm_symbols: $OBJ_DIR not found — build WASM first" >&2
  exit 1
fi

# Hook symbols that must be uniquely defined in the WASM link.
# - host_sim_apply_slowdown / host_fastgfx_resync_after_sleep: driver
#   pairs (one impl per build, picked by file selection in the Makefile).
# - host_sim_emit_* / host_sim_cmds_target_is_front: weak no-op stubs in
#   host_sim_emit_stub.c. WASM doesn't link host_sim_server.c, so each
#   symbol should resolve to exactly one (weak) definition. Catches
#   accidental strong-def landings that --allow-multiple-definition
#   would otherwise silently pick.
HOOKS=(
  host_sim_apply_slowdown
  host_fastgfx_resync_after_sleep
  host_sim_emit_blit
  host_sim_emit_pixel
  host_sim_emit_rect
  host_sim_emit_scroll
  host_sim_emit_cls
  host_sim_cmds_target_is_front
)

fail=0
echo "== WASM symbol uniqueness check =="

# Accept either text-section (T, strong) or weak (W) defines — the
# stub file uses __attribute__((weak)). `|| true` keeps the pipeline
# alive on zero-match so set -e doesn't kill the loop.
all_defs=$(find "$OBJ_DIR" -name '*.o' -print0 \
           | xargs -0 "$LLVM_NM" --defined-only 2>/dev/null \
           || true)

for sym in "${HOOKS[@]}"; do
  defs=$(printf '%s\n' "$all_defs" \
         | grep -E "[[:space:]][TW][[:space:]]+${sym}\$" \
         | wc -l \
         | tr -d ' ' || true)
  defs="${defs:-0}"
  if [[ "$defs" == "1" ]]; then
    echo "    $sym: 1 definition ✓"
  else
    echo "    $sym: $defs definitions (expected 1)" >&2
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  echo "== WASM SYMBOL CHECK FAILED ==" >&2
  exit 1
fi
echo "== WASM symbol check passed =="
