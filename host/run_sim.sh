#!/bin/bash
# run_sim.sh — Launch the PicoCalc simulator with sensible defaults.
#
# Serves web/ (HTTP + WebSocket) and drops into a terminal REPL so you
# can type BASIC commands, LOAD/RUN demos, etc. The browser canvas is
# the display; the terminal is a second console (keystrokes in the
# browser and the terminal both reach MMBasic).
#
# Defaults:
#   Port:          5150
#   Listen addr:   127.0.0.1   (use --listen 0.0.0.0 to share on LAN)
#   SD root:       repo demos/ (so `RUN "graphics/demo_gfx_shapes"` finds demos)
#   Web root:      ../web
#   Resolution:    320x320     (PicoCalc-native)
#   Slowdown:      0           (uncapped; try 5–50 for device-ish pacing)
#
# All defaults can be overridden by passing flags through, e.g.:
#   ./run_sim.sh --port 8080 --slowdown 20
#   ./run_sim.sh --resolution 480x320
#   ./run_sim.sh --listen 0.0.0.0 --port 5150
#
# Then open http://localhost:<port>/ in any modern browser.

set -e
cd "$(dirname "$0")"

if [ ! -x ./mmbasic_sim ]; then
    echo "Simulator binary not found. Building..."
    ./build_sim.sh
fi

REPO_ROOT="$(cd .. && pwd)"
WEB_ROOT="$REPO_ROOT/web"

exec ./mmbasic_sim \
    --sim \
    --port 5150 \
    --listen 127.0.0.1 \
    --web-root "$WEB_ROOT" \
    --sd-root  "$REPO_ROOT/demos" \
    --resolution 320x320 \
    "$@"
