#!/bin/bash
set -e

# Configuration
BUILD_DIR="build_cmake"
INSTALL_DIR="bin"

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Configure with CMake
echo "Configuring project..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

# Build
echo "Building project..."
cmake --build "$BUILD_DIR" --parallel

# Run (optional, based on success)
echo "Build successful."

OS_NAME=$(uname)

if [ "$OS_NAME" = "Darwin" ]; then
    EXECUTABLE="$BUILD_DIR/src/vchar64.app/Contents/MacOS/vchar64"
else
    EXECUTABLE="$BUILD_DIR/src/vchar64"
fi

echo "Executable should be in $EXECUTABLE"

if [ -f "$EXECUTABLE" ]; then
    echo "Running vchar64..."
    "$EXECUTABLE" &
else
    echo "Could not find application bundle to run."
fi
