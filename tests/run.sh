#!/bin/sh
# Lumi regression tests on Linux / macOS.  The POSIX twin of run.bat.
#
# Both scripts must check THE SAME THINGS in the same order, or a green CI on one
# platform stops meaning anything on the other.  Keep them in step:
#
#   1. examples   every .lumi under examples/ (subfolders too)
#   2. lang       tests/lang/*.lumi through "lumi test"
#   3. lint       examples+libraries clean, lint/bad.lumi matches its golden
#   4. fmt        examples+libraries unchanged, fmt/messy.lumi matches its golden
#   5. dap        a canned DAP conversation matches its golden
#   6. http       server.lumi + client.lumi as two processes
#
# KEY = the file's path under examples/ with / turned into _ and .lumi dropped.
#   examples/hello.lumi        -> hello
#   examples/graph/menu.lumi   -> graph_menu
#
# Goldens are recorded on Windows by record.bat; they must be byte-identical here.

cd "$(dirname "$0")" || exit 1

# CI 가 다른 곳에 빌드했으면 LUMI 로 알려 줄 수 있습니다.
LUMI=${LUMI:-../c-interpreter/bin/lumi}
if [ ! -x "$LUMI" ]; then
    echo "[!] lumi not found at '$LUMI'. Run c-interpreter/build.sh first."
    exit 1
fi
# 아래에서 하위 폴더로 들어가 부르므로 절대 경로로 펴 둡니다.
case "$LUMI" in
    /*) ;;
    *)  LUMI="$(cd "$(dirname "$LUMI")" && pwd)/$(basename "$LUMI")" ;;
esac

mkdir -p actual
PASS=0
FAIL=0

# ----------------------------------------------------------------- 1. examples
for f in $(find ../examples -name '*.lumi' | sort); do
    rel=${f#../examples/}
    key=$(printf '%s' "$rel" | sed 's|/|_|g; s|\.lumi$||')
    out="actual/$key.txt"

    if [ -f "stdin/$key.txt" ]; then
        "$LUMI" "$f" < "stdin/$key.txt" > "$out" 2>&1
    else
        "$LUMI" "$f" < /dev/null > "$out" 2>&1
    fi

    # An example must never end in an uncaught error - checked even when a golden
    # exists, so a bad run can never get frozen into expected/ and stay green.
    if grep -q '^Error:' "$out"; then
        FAIL=$((FAIL + 1))
        echo "[FAIL] $key - error while running (see $out)"
    elif [ -f "expected/$key.txt" ]; then
        if cmp -s "expected/$key.txt" "$out"; then
            PASS=$((PASS + 1))
        else
            FAIL=$((FAIL + 1))
            echo "[FAIL] $key - output differs from the golden file"
            echo "        diff expected/$key.txt $out"
        fi
    else
        PASS=$((PASS + 1))
    fi
done

echo
echo "----------------------------------------"
echo "  examples: passed $PASS  /  failed $FAIL"
echo "----------------------------------------"

# --------------------------------------------------------------- 2. lang tests
echo
echo "Language tests:"
if ! "$LUMI" test lang; then FAIL=$((FAIL + 1)); fi

# --------------------------------------------------------------------- 3. lint
echo
echo "Lint:"
if "$LUMI" lint ../examples > actual/_lint_examples.txt 2>&1; then
    echo "  examples clean"
else
    FAIL=$((FAIL + 1))
    echo "[FAIL] lint complained about examples (see actual/_lint_examples.txt)"
    cat actual/_lint_examples.txt
fi
if "$LUMI" lint ../libraries > actual/_lint_libraries.txt 2>&1; then
    echo "  libraries clean"
else
    FAIL=$((FAIL + 1))
    echo "[FAIL] lint complained about libraries (see actual/_lint_libraries.txt)"
    cat actual/_lint_libraries.txt
fi

# cd into the folder so the printed path is a bare file name - that is what keeps
# the golden identical on Windows and here.
( cd lint && "$LUMI" lint bad.lumi > ../actual/_lint.txt 2>&1 )
if [ -f expected/_lint.txt ]; then
    if cmp -s expected/_lint.txt actual/_lint.txt; then
        echo "  bad.lumi caught as expected"
    else
        FAIL=$((FAIL + 1))
        echo "[FAIL] lint/bad.lumi - findings differ from the golden file"
        echo "        diff expected/_lint.txt actual/_lint.txt"
    fi
else
    echo "[!] expected/_lint.txt missing - run record.bat on Windows"
fi

# ---------------------------------------------------------------------- 4. fmt
echo
echo "Format:"
if "$LUMI" fmt --check ../examples > actual/_fmt.txt 2>&1; then
    echo "  examples unchanged"
else
    FAIL=$((FAIL + 1))
    echo "[FAIL] fmt would rewrite examples (see actual/_fmt.txt)"
    cat actual/_fmt.txt
fi
if "$LUMI" fmt --check ../libraries >> actual/_fmt.txt 2>&1; then
    echo "  libraries unchanged"
else
    FAIL=$((FAIL + 1))
    echo "[FAIL] fmt would rewrite libraries (see actual/_fmt.txt)"
fi

cp -f fmt/messy.src fmt/messy.lumi
( cd fmt && "$LUMI" fmt messy.lumi > /dev/null 2>&1 )
if [ -f expected/_fmt_messy.txt ]; then
    if cmp -s expected/_fmt_messy.txt fmt/messy.lumi; then
        echo "  messy.lumi tidied as expected"
    else
        FAIL=$((FAIL + 1))
        echo "[FAIL] fmt/messy.lumi - tidied output differs from the golden file"
        echo "        diff expected/_fmt_messy.txt fmt/messy.lumi"
    fi
else
    echo "[!] expected/_fmt_messy.txt missing - run record.bat on Windows"
fi
# put the messy file back so the next run starts from the same place
cp -f fmt/messy.src fmt/messy.lumi

# ---------------------------------------------------------------- 5. debugger
echo
echo "Debugger:"
( cd dap && "$LUMI" dap < session.txt > ../actual/_dap.txt 2>&1 )
if [ -f expected/_dap.txt ]; then
    if cmp -s expected/_dap.txt actual/_dap.txt; then
        echo "  dap session as expected"
    else
        FAIL=$((FAIL + 1))
        echo "[FAIL] lumi dap - the conversation differs from the golden file"
        echo "        diff expected/_dap.txt actual/_dap.txt"
    fi
else
    echo "[!] expected/_dap.txt missing - run record.bat on Windows"
fi

# ------------------------------------------------------- 5b. CRLF source file
# A .lumi saved with CRLF must parse exactly like the LF one. The lexer used to
# see a blank line only as a bare newline, so on CRLF a blank line inside a block
# read as column 0 and closed the whole block. crlf/blankline.lumi is CRLF on purpose.
echo
echo "CRLF source:"
"$LUMI" crlf/blankline.lumi > actual/_crlf.txt 2>&1
if grep -q "^Error:" actual/_crlf.txt; then
    FAIL=$((FAIL + 1))
    echo "[FAIL] a CRLF file does not parse (see actual/_crlf.txt)"
    cat actual/_crlf.txt
else
    echo "  CRLF file parses"
fi

# -------------------------------------------------------------------- 6. http
# Two programs at once (one serves, one asks), so it cannot live in tests/lang -
# "lumi test" would sit there waiting.  server.lumi answers 4 requests and exits;
# if the client dies first it would wait forever, so it is killed either way.
echo
echo "HTTP server test:"
if ! command -v curl > /dev/null 2>&1; then
    echo "  [skip] curl not installed - bring http needs it"
else
    "$LUMI" http/server.lumi > /dev/null 2>&1 &
    server_pid=$!
    sleep 1
    "$LUMI" http/client.lumi > actual/_http.txt 2>&1
    cat actual/_http.txt
    if grep -q "FAIL" actual/_http.txt; then FAIL=$((FAIL + 1)); fi
    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
fi

[ "$FAIL" -gt 0 ] && exit 1
exit 0
