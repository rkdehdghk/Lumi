#pragma once
#include <QString>
#include <QStringList>
#include <QStringView>
#include <vector>

enum TokKind { TK_PLAIN, TK_KEYWORD, TK_STRING, TK_NUMBER, TK_COMMENT, TK_BUILTIN };

struct Tok {
    int start;      // text 내 문자 오프셋
    int len;
    TokKind kind;
};

// Lumi 토크나이저.
// 버그 수정(기존 lang.cpp:33): identStart 의 `c >= 0x00C0` 조건은
// ×(0x00D7) ÷(0x00F7) 등 Latin-1 기호를 식별자 시작으로 오판했다.
// QChar::isLetter 는 Unicode Letter 카테고리만 허용하도록 수정.
// inBlockComment: 여는 주석 `/. ./` 은 여러 줄에 걸치므로, 한 줄씩 칠할 때
// 앞 줄에서 열린 채로 끝났는지를 넣고 받는다 (한 줄짜리 호출은 nullptr).
std::vector<Tok> lumiTokenize(QStringView text, bool *inBlockComment = nullptr);

// 낱말 갈래(LW_*)와 낱말 표는 인터프리터 쪽 한 곳에만 적는다.
// LSP 서버와 이 IDE 가 같은 표를 본다.
extern "C" {
#include "../../../c-interpreter/src/lumiwords.h"
}

struct LumiWord {
    QString name;
    LumiWordKind kind;
    QString hint;   // 쓰는 형식 한 줄 (자동완성 창에 보여 준다). IDE 글자는 영어.
};

// 자동완성이 늘 아는 이름들. 언어에 이름이 늘면
// c-interpreter/src/lumiwords.h 에 한 줄 넣으면 IDE 와 LSP 가 함께 알게 된다.
// int 처럼 두 몫을 하는 이름은 LW_TYPE / LW_CAST 로 두 번 들어 있다.
const std::vector<LumiWord> &lumiWords();
