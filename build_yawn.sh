#!/usr/bin/env bash
# build_yawn.sh — Build YAWN (Debug or Release)
# Usage: ./build_yawn.sh [Release|Debug] [--clean] [--test]
#
# Optional deps (auto-detected if available):
#   - FFmpeg + libav*  → video clip import/playback
#   - libavdevice      → live video input (webcam)
#   - Ableton Link     → network beat/tempo sync (fetched automatically)
#   - VST3 SDK         → third-party plugin hosting (fetched automatically)
#
# CMake options:
#   -DYAWN_VST3=OFF       → disable VST3 plugin hosting
#   -DYAWN_HAS_LINK=OFF   → disable Ableton Link support
#   -DYAWN_HAS_VIDEO=OFF  → disable video support (no FFmpeg)
#   -DYAWN_HAS_MODEL3D=OFF → disable 3D model loading

set -e

BUILD_TYPE="${1:-Release}"
BUILD_TYPE="${BUILD_TYPE#--}"  # strip leading dashes if present
[ "$BUILD_TYPE" != "Release" ] && [ "$BUILD_TYPE" != "Debug" ] && BUILD_TYPE="Release"

DO_CLEAN=0
DO_TEST=0
for arg in "$@"; do
    case "$arg" in
        --clean) DO_CLEAN=1 ;;
        --test)  DO_TEST=1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CONFIG="$BUILD_DIR/CMakeCache.txt"

echo "=== Building YAWN ($BUILD_TYPE) ==="

if [ "$DO_CLEAN" -eq 1 ] || [ ! -f "$CONFIG" ]; then
    [ "$DO_CLEAN" -eq 1 ] && echo "-- Cleaning build directory..." && rm -rf "$BUILD_DIR"
    echo "-- Configuring CMake..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

echo ""
echo "=== Build complete ==="
echo "Executable: $BUILD_DIR/bin/YAWN"
[ -f "$BUILD_DIR/bin/yawn_tests" ] && echo "Tests:      $BUILD_DIR/bin/yawn_tests"

if [ "$DO_TEST" -eq 1 ]; then
    echo ""
    echo "=== Running tests ==="
    cd "$BUILD_DIR" && ctest --output-on-failure -C "$BUILD_TYPE" --parallel
fi
