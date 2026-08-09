#include "lumitokenizer.h"
#include <QChar>
#include <set>

// 낱말 표는 인터프리터 쪽 c-interpreter/src/lumiwords.h 한 곳에만 적는다.
// LSP 서버(lsp.c)와 이 IDE 가 같은 표를 보게 하려는 것 — 예전처럼 두 곳에
// 따로 적어 두면 한쪽만 고쳐져 조용히 어긋난다.
const std::vector<LumiWord> &lumiWords() {
    static const std::vector<LumiWord> s = [] {
        std::vector<LumiWord> v;
        v.reserve(LUMI_WORD_COUNT);
        for (size_t i = 0; i < LUMI_WORD_COUNT; i++)
            v.push_back({ QString::fromUtf8(LUMI_WORDS[i].name),
                          static_cast<LumiWordKind>(LUMI_WORDS[i].kind),
                          QString::fromUtf8(LUMI_WORDS[i].hint) });
        return v;
    }();
    return s;
}

// 이름 뒤에 '(' 가 오면 함수 호출색을 입히는데, 자료형(`int("42")`) 과 print 는 정말
// 그렇게 불리지만 if/while/switch/return 은 아니다 — `if (x):` 를 함수로 칠하지 않도록
// "불려도 되는 낱말" 만 따로 모아 둔다.
static const std::set<QString> &callableWords() {
    static const std::set<QString> s = [] {
        std::set<QString> k;
        for (const LumiWord &w : lumiWords())
            if (w.kind == LW_CAST || w.kind == LW_FUNC) k.insert(w.name);
        return k;
    }();
    return s;
}

// 색칠용. print 는 예약어지만 늘 함수처럼 쓰이므로 위 표에선 LW_FUNC 쪽에 있다.
static const std::set<QString> &keywordSet() {
    static const std::set<QString> s = [] {
        std::set<QString> k;
        for (const LumiWord &w : lumiWords())
            if (w.kind == LW_KEYWORD || w.kind == LW_TYPE) k.insert(w.name);
        k.insert("print");
        return k;
    }();
    return s;
}

// QChar::isLetter: Unicode Letter 카테고리만. 기존 lang.cpp:33 의 기호 오판 버그 수정.
static bool identStart(QChar c) { return c.isLetter() || c == u'_'; }
static bool identPart(QChar c) { return identStart(c) || c.isDigit(); }
static bool isDigit(QChar c) { return c >= u'0' && c <= u'9'; }

std::vector<Tok> lumiTokenize(QStringView text, bool *inBlockComment) {
    std::vector<Tok> out;
    const int len = text.length();
    int i = 0;
    bool afterClassFrom = false;   // 'class Dog' / 'from Animal': the name is a type
    bool inBlock = inBlockComment && *inBlockComment;
    const auto *d = text.data();

    while (i < len) {
        QChar c = d[i];

        // block comment /. ... ./  (여러 줄. 하이라이터는 한 줄씩 부르므로 inBlock 으로 이어 받는다)
        if (inBlock || (c == u'/' && i + 1 < len && d[i + 1] == u'.')) {
            const int start = i;
            if (!inBlock) i += 2;
            inBlock = true;
            while (i < len && !(d[i] == u'.' && i + 1 < len && d[i + 1] == u'/')) i++;
            if (i < len) { i += 2; inBlock = false; }
            out.push_back({start, i - start, TK_COMMENT});
            continue;
        }
        // line comment
        if (c == u'/' && i + 1 < len && d[i + 1] == u'/') {
            int start = i;
            while (i < len && d[i] != u'\n' && d[i] != u'\r') i++;
            out.push_back({start, i - start, TK_COMMENT});
            continue;
        }
        // string — " 는 이스케이프를 읽고, ' 는 있는 그대로 (경로 붙여넣기용)
        if (c == u'"' || c == u'\'') {
            const QChar quote = c;
            const bool raw = c == u'\'';
            int start = i++;
            while (i < len && d[i] != quote && d[i] != u'\n' && d[i] != u'\r') {
                if (!raw && d[i] == u'\\' && i + 1 < len) i++;
                i++;
            }
            if (i < len && d[i] == quote) i++;
            out.push_back({start, i - start, TK_STRING});
            continue;
        }
        // number
        if (isDigit(c)) {
            int start = i;
            if (c == u'0' && i + 1 < len &&
                (d[i + 1] == u'x' || d[i + 1] == u'X' || d[i + 1] == u'b' || d[i + 1] == u'B')) {
                i += 2;
                while (i < len && identPart(d[i])) i++;
            } else {
                while (i < len && isDigit(d[i])) i++;
                if (i + 1 < len && d[i] == u'.' && isDigit(d[i + 1])) {
                    i++;
                    while (i < len && isDigit(d[i])) i++;
                }
            }
            out.push_back({start, i - start, TK_NUMBER});
            continue;
        }
        // identifier / keyword
        if (identStart(c)) {
            int start = i;
            while (i < len && identPart(d[i])) i++;
            QString word = text.mid(start, i - start).toString();

            int look = i;
            while (look < len && (d[look] == u' ' || d[look] == u'\t')) look++;
            bool call = look < len && d[look] == u'(';

            // a name right before '(' is a call — unless it is a keyword that simply
            // wraps its condition in brackets (`if (x):`, `while (n > 0):`).
            const bool isKeyword = keywordSet().count(word) != 0;
            TokKind kind;
            if (afterClassFrom) kind = TK_BUILTIN;
            else if (call && (!isKeyword || callableWords().count(word))) kind = TK_BUILTIN;
            else if (isKeyword) kind = TK_KEYWORD;
            else kind = TK_PLAIN;

            afterClassFrom = (word == "class" || word == "from");
            if (afterClassFrom) kind = TK_KEYWORD;
            out.push_back({start, i - start, kind});
            continue;
        }
        if (c != u' ' && c != u'\t' && c != u'\r' && c != u'\n') afterClassFrom = false;
        i++;
    }
    if (inBlockComment) *inBlockComment = inBlock;
    return out;
}
