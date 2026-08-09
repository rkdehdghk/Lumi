/* The Lumi language: the same tokens the Monaco version coloured. */
#include "lumina.h"
#include <set>

static const wchar_t* KEYWORDS[] = {
    L"val", L"if", L"elif", L"else", L"while", L"func", L"return",
    L"true", L"false", L"and", L"or", L"not", L"print",
    L"bring", L"up", L"for", L"in", L"break", L"continue",
    L"global", L"local", L"switch", L"case", L"default",
    L"class", L"from", L"this", L"super",
};

static const wchar_t* TYPE_NAMES[] = {
    L"int", L"float", L"num", L"char", L"str", L"text", L"seq", L"bytes",
};

/* The builtin list the Monaco version carried is gone on purpose: a name in
   front of '(' was coloured the same whether it was a builtin or a call, so
   the list never changed a single pixel. */

static std::set<std::wstring>& keywordSet() {
    static std::set<std::wstring> s;
    if (s.empty()) {
        for (auto k : KEYWORDS) s.insert(k);
        for (auto k : TYPE_NAMES) s.insert(k);
    }
    return s;
}

/* Names may hold any letter, Hangul included:  val 이름 = "루미" */
static bool identStart(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_' ||
           c >= 0x00C0;
}
static bool identPart(wchar_t c) {
    return identStart(c) || (c >= L'0' && c <= L'9');
}
static bool isDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

void lumiTokenize(const wchar_t* text, int len, std::vector<Tok>& out) {
    out.clear();
    int i = 0;
    bool afterClassFrom = false;   // 'class Dog' / 'from Animal': the name is a type
    while (i < len) {
        wchar_t c = text[i];

        if (c == L'/' && i + 1 < len && text[i + 1] == L'/') {
            int start = i;
            while (i < len && text[i] != L'\n' && text[i] != L'\r') i++;
            out.push_back({start, i - start, TK_COMMENT});
            continue;
        }
        if (c == L'"') {
            int start = i++;
            while (i < len && text[i] != L'"' && text[i] != L'\n' && text[i] != L'\r') {
                if (text[i] == L'\\' && i + 1 < len) i++;
                i++;
            }
            if (i < len && text[i] == L'"') i++;
            out.push_back({start, i - start, TK_STRING});
            continue;
        }
        if (isDigit(c)) {
            int start = i;
            if (c == L'0' && i + 1 < len && (text[i + 1] == L'x' || text[i + 1] == L'X' ||
                                             text[i + 1] == L'b' || text[i + 1] == L'B')) {
                i += 2;
                while (i < len && identPart(text[i])) i++;
            } else {
                while (i < len && isDigit(text[i])) i++;
                if (i + 1 < len && text[i] == L'.' && isDigit(text[i + 1])) {
                    i++;
                    while (i < len && isDigit(text[i])) i++;
                }
            }
            out.push_back({start, i - start, TK_NUMBER});
            continue;
        }
        if (identStart(c)) {
            int start = i;
            while (i < len && identPart(text[i])) i++;
            std::wstring word(text + start, i - start);

            int look = i;                        // a name right before '(' is a call
            while (look < len && (text[look] == L' ' || text[look] == L'\t')) look++;
            bool call = look < len && text[look] == L'(';

            TokKind kind;
            if (afterClassFrom) kind = TK_BUILTIN;
            else if (call) kind = TK_BUILTIN;    // builtin, keyword-call or a type name
            else if (keywordSet().count(word)) kind = TK_KEYWORD;
            else kind = TK_PLAIN;

            afterClassFrom = (word == L"class" || word == L"from");
            if (afterClassFrom) kind = TK_KEYWORD;
            out.push_back({start, i - start, kind});
            continue;
        }
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') afterClassFrom = false;
        i++;
    }
}

COLORREF tokColor(TokKind k) {
    switch (k) {
        case TK_KEYWORD: return T.synKeyword;
        case TK_STRING:  return T.synString;
        case TK_NUMBER:  return T.synNumber;
        case TK_COMMENT: return T.synComment;
        case TK_BUILTIN: return T.synBuiltin;
        default:         return T.editorFg;
    }
}

bool tokBold(TokKind k) { return k == TK_KEYWORD || k == TK_BUILTIN; }
