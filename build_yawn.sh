#!/usr/bin/env bash
# build_yawn.sh — Build YAWN (Debug or Release)
# Usage: ./build_yawn.sh [Release|Debug]

set -e

BUILD_TYPE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== Building YAWN ($BUILD_TYPE) ==="

if [ ! -d "$BUILD_DIR" ]; then
    echo "-- Configuring CMake..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

echo ""
echo "=== Build complete ==="
echo "Executable: $BUILD_DIR/bin/$BUILD_TYPE/YAWN"
echo "Tests:      $BUILD_DIR/bin/$BUILD_TYPE/yawn_tests"
