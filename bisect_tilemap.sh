#!/bin/bash
# bisect_tilemap.sh — rebuild WASM at HEAD, then wait for your verdict.
#
# Usage:
#   ./bisect_tilemap.sh start      # kick off: marks main as good, HEAD as bad
#   ./bisect_tilemap.sh good       # reload sim; ball shows OK → good
#   ./bisect_tilemap.sh bad        # reload sim; ball stuck at (0,0) → bad
#   ./bisect_tilemap.sh reset      # abandon bisect, back to real-hal
#
# After each good/bad, it rebuilds the wasm at the next bisect-selected
# commit so you just reload the browser sim page.

set -e
cd "$(dirname "$0")"

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
echo "Then run: ./bisect_tilemap.sh good   (ball shows correctly)"
echo "    or:   ./bisect_tilemap.sh bad    (ball stuck at top-left)"
