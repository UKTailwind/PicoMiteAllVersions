#!/bin/bash
# serve.sh — Local static server for the PicoMite web bundle.
#
# Delegates to serve.py, which is a subclass of http.server that adds
# the COOP + COEP headers needed for SharedArrayBuffer.
#
# Usage: ./serve.sh [port]
#        ./serve.sh --port PORT
#        ./serve.sh -p PORT
# Defaults to port 8000.

set -e
cd "$(dirname "$0")"

case "${1:-}" in
    --port|-p)
        if [ -z "${2:-}" ]; then
            echo "usage: ./serve.sh [--port PORT|-p PORT|PORT]" >&2
            exit 2
        fi
        exec python3 ./serve.py "$2"
        ;;
    --help|-h)
        echo "usage: ./serve.sh [--port PORT|-p PORT|PORT]"
        exit 0
        ;;
    *)
        exec python3 ./serve.py "$@"
        ;;
esac
