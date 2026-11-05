#!/bin/bash
# Incremental build and headless unit-test runner for VChar64 pre-commit hooks.
#
# Resolves the active CMake build directory (via VCHAR64_BUILD_DIR or by selecting
# the most recently modified configured build directory containing CMakeCache.txt
# and CTestTestfile.cmake), performs a parallel incremental build so stale binaries
# are never executed, and runs the full CTest suite headlessly.

set -euo pipefail

BUILD_DIRS=("build" "build_test" "build_cmake" "build_ci" "cmake-build-debug")
ACTIVE_BUILD_DIR="${VCHAR64_BUILD_DIR:-}"

if [ -z "$ACTIVE_BUILD_DIR" ]; then
    LATEST_MTIME=0
    for dir in "${BUILD_DIRS[@]}"; do
        if [ ! -f "$dir/CMakeCache.txt" ] || [ ! -f "$dir/CTestTestfile.cmake" ]; then
            continue
        fi
        mtime=$(stat -f "%m" "$dir/CMakeCache.txt" 2>/dev/null || stat -c "%Y" "$dir/CMakeCache.txt" 2>/dev/null || echo 0)
        if [ "$mtime" -ge "$LATEST_MTIME" ]; then
            LATEST_MTIME="$mtime"
            ACTIVE_BUILD_DIR="$dir"
        fi
    done
fi

if [ -z "$ACTIVE_BUILD_DIR" ]; then
    echo "Error: No configured CMake build directory found (checked: ${BUILD_DIRS[*]})."
    echo "Run 'cmake -B build' first."
    exit 1
fi

echo "Building in: $ACTIVE_BUILD_DIR"
cmake --build "$ACTIVE_BUILD_DIR" --parallel

echo "Running CTest suite in: $ACTIVE_BUILD_DIR"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$ACTIVE_BUILD_DIR" --output-on-failure
