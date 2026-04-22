#!/usr/bin/env bash
#
# tools/hal_scoreboard.sh
#
# Prints the scoreboard table from docs/real-hal-plan.md for the current tree.
# Counts #if/#ifdef/#ifndef/#elif lines that reference a hardware target macro
# in each tracked core file.
#
# This is a progress metric, not the purity gate. The gate is
# tools/check_hal_purity.sh. A phase ends with an updated row appended to the
# plan's scoreboard; this script produces that row.
#
# Usage:
#   tools/hal_scoreboard.sh          # human-readable table
#   tools/hal_scoreboard.sh --row    # single "label" row for appending to plan
#
# Macros mirrored from tools/check_hal_purity.sh.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

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

macro_alt="$(IFS='|'; echo "${TARGET_MACROS[*]}")"
pattern="^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b(${macro_alt})\b"

# Columns shown in the plan's scoreboard, in the same order.
FILES=(
  Draw.c
  MM_Misc.c
  External.c
  FileIO.c
  Commands.c
  Memory.c
  Functions.c
  Audio.c
)

count_file() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    echo 0
    return
  fi
  local c
  c="$(grep -cE "$pattern" "$file" || true)"
  echo "${c:-0}"
}

mode="table"
if [[ "${1:-}" == "--row" ]]; then
  mode="row"
fi

declare -a counts
total=0
for f in "${FILES[@]}"; do
  c="$(count_file "$f")"
  counts+=("$c")
  total=$(( total + c ))
done

if [[ "$mode" == "row" ]]; then
  # Emit a row formatted like the plan's scoreboard. The label is left to the caller:
  #
  #   tools/hal_scoreboard.sh --row | sed 's/^/1       /'
  #
  # produces "1       <counts>  <total>".
  printf '%-7s' ''
  for c in "${counts[@]}"; do
    printf '%-8s' "$c"
  done
  printf '%s\n' "$total"
  exit 0
fi

echo "== HAL scoreboard (raw #ifdef count, trend metric) =="
echo
printf '%-7s' 'Phase'
for f in "${FILES[@]}"; do
  # Shorten Draw.c → Draw, MM_Misc.c → MM_Misc, etc. to match plan column widths.
  short="${f%.c}"
  printf '%-8s' "$short"
done
printf '%s\n' 'Total'

printf '%-7s' 'now'
for c in "${counts[@]}"; do
  printf '%-8s' "$c"
done
printf '%s\n' "$total"

echo
echo "Current-branch per-macro totals across the scored files:"
for m in "${TARGET_MACROS[@]}"; do
  # Count occurrences of this specific macro in scored files only.
  n=0
  for f in "${FILES[@]}"; do
    [[ -f "$f" ]] || continue
    k="$(grep -cE "^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b${m}\b" "$f" || true)"
    n=$(( n + ${k:-0} ))
  done
  printf '    %-16s %s\n' "$m" "$n"
done
