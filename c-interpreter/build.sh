#!/bin/sh
# Build the Lumi interpreter on Linux / macOS  ->  bin/lumi
#
#   ./build.sh          release build (default)
#   ./build.sh debug    debug build
#
# Needs: cc (gcc or clang). cmake is used when available, otherwise a direct cc call.
# On Windows use build.bat (MSVC) instead.

set -e
cd "$(dirname "$0")"

BUILD_TYPE=Release
[ "$1" = "debug" ] && BUILD_TYPE=Debug

SRC="src/platform.c src/value.c src/regex.c src/unicode.c src/lexer.c
     src/parser.c src/interp.c src/pkg.c src/lsp.c src/dap.c src/lint.c src/fmt.c src/ffi.c src/bundle.c src/main.c"

if command -v cmake >/dev/null 2>&1; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build build -j
else
    echo "cmake not found - falling back to a direct cc call"
    mkdir -p bin
    CFLAGS="-std=c17 -O2 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration"
    [ "$BUILD_TYPE" = "Debug" ] && CFLAGS="-std=c17 -g -O0 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration"

    # iconv (needed for cp949 / euc-kr) lives in libc on glibc, but is a separate
    # library on macOS, Cygwin/MSYS2, musl and the BSDs. Try plain first, then -liconv.
    if cc $CFLAGS -o bin/lumi $SRC -lm -lpthread 2>/dev/null; then
        :
    else
        echo "linking again with -liconv"
        cc $CFLAGS -o bin/lumi $SRC -lm -lpthread -liconv
    fi
fi

echo
echo "Build complete: c-interpreter/bin/lumi"
