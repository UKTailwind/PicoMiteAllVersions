#!/bin/bash
# build.sh — Build the MMBasic native host test binary.
#
# Output: ports/host_native/build/mmbasic_test

set -e
cd "$(dirname "$0")"

case "${1:-}" in
    clean|rebuild)
        echo "Cleaning..."
        make clean
        echo "Building..."
        make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
        ;;
    *)
        make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
        ;;
esac

echo ""
echo "Build complete: ports/host_native/build/mmbasic_test"
echo "Run ports/host_native/run_tests.sh to execute the test suite."
