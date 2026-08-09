#pragma once
#include <QString>

// VS Code-style subsequence match. "opf" finds "Open File".
// 매칭이 시작된 위치 반환 (낮을수록 좋음), 불일치 -1.
int fuzzyRank(const QString &query, const QString &text);
