#!/bin/bash
set -e

# List of common build directories to check
BUILD_DIRS=("build_test" "build" "build_cmake" "cmake-build-debug")
TEST_EXECUTABLES=("tests/unit_tests/test_stateimport" "tests/unit_tests/test_stateexport")

for test_exe in "${TEST_EXECUTABLES[@]}"; do
    FOUND_EXECUTABLE=""
    for dir in "${BUILD_DIRS[@]}"; do
        if [ -f "$dir/$test_exe" ]; then
            FOUND_EXECUTABLE="$dir/$test_exe"
            break
        fi
    done

    if [ -z "$FOUND_EXECUTABLE" ]; then
        echo "Error: Unit test executable $test_exe not found in common build directories."
        echo "Checked: ${BUILD_DIRS[*]}"
        # Don't exit immediately, try finding other tests (or maybe exit? implies build failed)
        exit 1
    fi

    echo "Running unit tests from: $FOUND_EXECUTABLE"
    "$FOUND_EXECUTABLE"
done
