#!/bin/sh
# Build the Lumina IDE on Linux / macOS  ->  bin/Lumina
#
#   ./build.sh          release build (default)
#   ./build.sh debug    debug build
#
# Needs: cmake, a C++17 compiler, and Qt6 (Widgets + Svg).
# On Windows use build.bat (MSVC + Qt6) instead.
#
# If cmake cannot find Qt6, point it at your install, for example:
#   Qt6_DIR=~/Qt/6.7.2/gcc_64/lib/cmake/Qt6 ./build.sh
#   CMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake ./build.sh
#
# Package names, if Qt6 is missing:
#   Debian/Ubuntu  sudo apt install qt6-base-dev qt6-svg-dev cmake g++
#   Fedora         sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel cmake gcc-c++
#   Arch           sudo pacman -S qt6-base qt6-svg cmake
#   macOS          brew install qt cmake

set -e
cd "$(dirname "$0")"

BUILD_TYPE=Release
[ "$1" = "debug" ] && BUILD_TYPE=Debug

if ! command -v cmake >/dev/null 2>&1; then
    echo "[!] cmake not found. Install it and run this again."
    exit 1
fi

# The IDE runs the interpreter, so build that first when it is not there yet.
if [ ! -x ../c-interpreter/bin/lumi ]; then
    echo "lumi interpreter not built yet - building it first"
    ( cd ../c-interpreter && ./build.sh "$1" )
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j

echo
echo "Build complete: ide-cpp/bin/Lumina"
