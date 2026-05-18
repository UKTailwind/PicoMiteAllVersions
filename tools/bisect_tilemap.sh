#!/bin/bash
# tools/bisect_tilemap.sh — rebuild WASM at HEAD, then wait for your verdict.
#
# Usage:
#   ./tools/bisect_tilemap.sh start      # kick off: marks main as good, HEAD as bad
#   ./tools/bisect_tilemap.sh good       # reload sim; ball shows OK -> good
#   ./tools/bisect_tilemap.sh bad        # reload sim; ball stuck at (0,0) -> bad
#   ./tools/bisect_tilemap.sh reset      # abandon bisect, back to real-hal
#
# After each good/bad, it rebuilds the wasm at the next bisect-selected
# commit so you just reload the browser sim page.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

case "${1:-}" in
    start)
        git bisect start
        git bisect bad HEAD
        git bisect good main
        ;;
    good|bad)
        git bisect "$1"
        ;;
    reset)
        git bisect reset
        exit 0
        ;;
    *)
        echo "Usage: $0 {start|good|bad|reset}" >&2
        exit 2
        ;;
esac

echo
echo "=== Rebuilding WASM at $(git rev-parse --short HEAD) ==="
echo "Commit: $(git log -1 --oneline)"
echo
cd ports/host_wasm && make clean > /dev/null 2>&1 && ./build.sh 2>&1 | tail -3
echo
echo "WASM built. Reload the sim page, run FRUN \"pico_blocks_tilemap.bas\""
echo "Then run: ./tools/bisect_tilemap.sh good   (ball shows correctly)"
echo "    or:   ./tools/bisect_tilemap.sh bad    (ball stuck at top-left)"
