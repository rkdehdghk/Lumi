#include "lumihighlighter.h"
#include "../core/lumitokenizer.h"
#include "../theme/theme.h"

LumiHighlighter::LumiHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {}

void LumiHighlighter::highlightBlock(const QString &text) {
    // QSyntaxHighlighter 는 가시 블록만 호출 → 대용량 파일 성능 자동 해소 (버그).
    bool inBlock = previousBlockState() == 1;   // 앞 줄에서 /. 가 안 닫혔으면 이어 칠한다
    auto toks = lumiTokenize(QStringView(text), &inBlock);
    for (const Tok &t : toks) {
        QTextCharFormat fmt;
        fmt.setForeground(tokColor(t.kind));
        if (tokBold(t.kind)) fmt.setFontWeight(QFont::Bold);
        setFormat(t.start, t.len, fmt);
    }
    setCurrentBlockState(inBlock ? 1 : 0);
}

void LumiHighlighter::refreshTheme() { rehighlight(); }
