/* Self-check for the Ctrl+P / Ctrl+Shift+P matcher.
   Built and run by:  build.bat test                                          */
#include "lumina.h"
#include <assert.h>
#include <stdio.h>

int main() {
    // empty query keeps everything, in the order it was given
    assert(fuzzyRank(L"", L"anything") == 0);

    // subsequence, not substring: "opf" finds "Open File"
    assert(fuzzyRank(L"opf", L"File: Open File...") >= 0);
    assert(fuzzyRank(L"run", L"Run: Run Lumi File") == 0);
    assert(fuzzyRank(L"zzz", L"File: Save") == -1);

    // case-insensitive both ways
    assert(fuzzyRank(L"SAVE", L"File: Save") >= 0);
    assert(fuzzyRank(L"save", L"FILE: SAVE") >= 0);

    // order matters - "lfie" is not a subsequence of "File"
    assert(fuzzyRank(L"lfie", L"File") == -1);

    // an earlier match ranks ahead of a later one, so sorting puts it first
    assert(fuzzyRank(L"term", L"Terminal") < fuzzyRank(L"term", L"View: Show Terminal"));

    // characters are consumed, so a repeat needs a second occurrence
    assert(fuzzyRank(L"ll", L"Lumi") == -1);
    assert(fuzzyRank(L"ll", L"Hello all") >= 0);

    // Hangul file names work the same way (names may hold any letter)
    assert(fuzzyRank(L"예제", L"예제.lumi") >= 0);

    printf("fuzzy matcher ok\n");
    return 0;
}
