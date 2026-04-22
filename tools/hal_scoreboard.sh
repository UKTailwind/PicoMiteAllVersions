#!/usr/bin/env bash
#
# tools/hal_scoreboard.sh
#
# Prints the scoreboard row for the current tree: a count of *every*
# preprocessor conditional (#if / #ifdef / #ifndef / #elif) in each
# tracked core file, regardless of which macro it references.
#
# Why "every directive" and not "occurrences of target macros":
# The first cut of this script only matched target macros (rp2350,
# PICOMITE, ...). Phase 3b exploited that by renaming
#   #ifdef rp2350
# to
#   #if HAL_PORT_PWM_SLICE_COUNT > 8
# and hiding the original #ifdef rp2350 inside hal/hal_port_config.h.
# The scoreboard happily reported -79 while zero conditional-compilation
# was actually eliminated. See docs/real-hal-fixup-plan.md F1.
#
# The metric enforced here is "preprocessor conditionals in core,"
# period. Renaming a gate doesn't help. Moving the body into a HAL
# impl does.
#
# This is a progress metric, not the purity gate. The gate is
# tools/check_hal_purity.sh.
#
# Usage:
#   tools/hal_scoreboard.sh                # human-readable table + breakdown
#   tools/hal_scoreboard.sh --row          # one "<counts>  <total>" row
#   tools/hal_scoreboard.sh --breakdown    # per-file: target / port-config / other
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Columns shown in the scoreboard, in the same order as docs/real-hal/scoreboard.md.
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

# Any conditional-compilation directive. This is the primary metric.
#   ^\s*#\s*(if|ifdef|ifndef|elif)\b
ALL_IFDEF_RE='^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)\b'

# Target macros — informational breakdown only. These are the macros that
# the HAL is supposed to abstract away. Mirrors check_hal_purity.sh.
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
target_alt="$(IFS='|'; echo "${TARGET_MACROS[*]}")"
TARGET_IFDEF_RE="${ALL_IFDEF_RE}.*\\b(${target_alt})\\b"

# Port-config macros — also forbidden as preprocessor gates per the fixup
# standard. Anything starting with HAL_PORT_ or PORT_.
PORT_IFDEF_RE="${ALL_IFDEF_RE}.*\\b(HAL_PORT_[A-Za-z0-9_]+|PORT_[A-Za-z0-9_]+)\\b"

count_re() {
  local file="$1"
  local re="$2"
  if [[ ! -f "$file" ]]; then
    echo 0
    return
  fi
  local c
  c="$(grep -cE "$re" "$file" || true)"
  echo "${c:-0}"
}

mode="table"
case "${1:-}" in
  --row)        mode="row" ;;
  --breakdown)  mode="breakdown" ;;
  "")           mode="table" ;;
  *)
    echo "usage: $0 [--row | --breakdown]" >&2
    exit 2
    ;;
esac

declare -a total_counts target_counts port_counts
grand_total=0
for f in "${FILES[@]}"; do
  t="$(count_re "$f" "$ALL_IFDEF_RE")"
  g="$(count_re "$f" "$TARGET_IFDEF_RE")"
  p="$(count_re "$f" "$PORT_IFDEF_RE")"
  total_counts+=("$t")
  target_counts+=("$g")
  port_counts+=("$p")
  grand_total=$(( grand_total + t ))
done

if [[ "$mode" == "row" ]]; then
  printf '%-7s' ''
  for c in "${total_counts[@]}"; do
    printf '%-10s' "$c"
  done
  printf '%s\n' "$grand_total"
  exit 0
fi

echo "== HAL scoreboard — all preprocessor conditionals in tracked core files =="
echo
printf '%-7s' 'Phase'
for f in "${FILES[@]}"; do
  short="${f%.c}"
  printf '%-10s' "$short"
done
printf '%s\n' 'Total'

printf '%-7s' 'now'
for c in "${total_counts[@]}"; do
  printf '%-10s' "$c"
done
printf '%s\n' "$grand_total"

if [[ "$mode" == "breakdown" ]]; then
  echo
  echo "Breakdown per file (total / target-macro / port-config / other):"
  printf '    %-14s %-8s %-10s %-10s %s\n' file total target port other
  for i in "${!FILES[@]}"; do
    f="${FILES[$i]}"
    t="${total_counts[$i]}"
    g="${target_counts[$i]}"
    p="${port_counts[$i]}"
    o=$(( t - g - p ))
    printf '    %-14s %-8s %-10s %-10s %s\n' "$f" "$t" "$g" "$p" "$o"
  done
  exit 0
fi

echo
echo "Per-macro references across tracked core files (informational):"
for m in "${TARGET_MACROS[@]}"; do
  n=0
  for f in "${FILES[@]}"; do
    [[ -f "$f" ]] || continue
    k="$(grep -cE "${ALL_IFDEF_RE}.*\\b${m}\\b" "$f" || true)"
    n=$(( n + ${k:-0} ))
  done
  printf '    %-16s %s\n' "$m" "$n"
done

# Aggregate port-config hits — one line.
pc_total=0
for c in "${port_counts[@]}"; do
  pc_total=$(( pc_total + c ))
done
printf '    %-16s %s\n' 'HAL_PORT_*/PORT_*' "$pc_total"
