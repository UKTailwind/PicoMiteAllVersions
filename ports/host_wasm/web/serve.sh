#!/bin/bash
# serve.sh — Local static server for the PicoMite web bundle.
#
# Delegates to serve.py, which is a subclass of http.server that adds
# the COOP + COEP headers needed for SharedArrayBuffer.
#
# Usage: ./serve.sh [port]
# Defaults to port 8000.

set -e
cd "$(dirname "$0")"
exec python3 ./serve.py "$@"
