#!/bin/bash
set -e

# List of common build directories to check
BUILD_DIRS=("build" "build_test" "build_cmake" "cmake-build-debug")
TEST_EXECUTABLES=(
    "tests/unit_tests/tst_state_and_commands"
    "tests/unit_tests/tst_stateimport"
    "tests/unit_tests/tst_stateexport"
    "tests/unit_tests/tst_clipboard_and_network"
)

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
        exit 1
    fi

    echo "Running unit tests from: $FOUND_EXECUTABLE"
    QT_QPA_PLATFORM=offscreen "$FOUND_EXECUTABLE"
done
