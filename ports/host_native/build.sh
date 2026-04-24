#!/bin/bash
# build.sh — Build the MMBasic native host test binary.
#
# Transitional: the actual Makefile still lives at host/Makefile while
# the host/ retirement is staged. This script is the port-native entry
# point that docs + CI should use going forward.
#
# Usage:
#   ./build.sh          Build (incremental)
#   ./build.sh clean    Clean and rebuild from scratch
#   ./build.sh rebuild  Same as clean
#
# Output binary: host/mmbasic_test (until retirement step 3).

set -e
cd "$(dirname "$0")/../../host"
exec ./build.sh "$@"
