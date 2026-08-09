#pragma once
#include <QString>
#include <QByteArray>

// 자식 프로세스가 stdout 에 섞어 보내는 신호. 읽기(readyRead)는 아무 데서나 끊기므로
// 이 두 함수가 "조각난 것"과 "진짜 아닌 것"을 갈라 준다. 순수 함수라 테스트가 쉽다
// (tests/streammarkers_test.cpp).
//
// MSVC: "\x1eD" 는 hex 이스케이프가 D 까지 먹어서 범위 초과 — 문자열을 끊어 붙인다.
inline const QString &markInput() {
    static const QString s = QString::fromUtf8("\x1e" "INPUT\n");   // 한 줄 입력해 달라
    return s;
}
inline const QString &markDone() {
    static const QString s = QString::fromUtf8("\x1e" "DONE\n");    // 실행이 끝났다
    return s;
}
inline const QString &markEot() {
    static const QString s = QString::fromUtf8("\x1e" "EOT\n");     // REPL 블록 경계
    return s;
}

// s[at] 부터 마커가 있는가.
//   > 0 : 그 길이만큼 완전히 맞았다
//    -1 : 마커의 앞부분이긴 한데 뒤가 잘렸다 — 다음 조각을 기다려야 한다
//     0 : 마커가 아니다 (그냥 \x1e 글자)
//
// 예전엔 "남은 길이가 7 미만이면 잘린 것" 으로 뭉뚱그렸는데, DONE 은 6자 EOT 는 5자라
// 읽기 끝에 딱 떨어지면 영영 보관만 하고 처리되지 않았다 (실행이 안 끝난 것처럼 보임).
inline int matchMarker(const QString &s, int at) {
    const QString *marks[] = {&markInput(), &markDone(), &markEot()};
    bool partial = false;
    const int avail = s.size() - at;
    for (const QString *m : marks) {
        if (avail >= m->size()) {
            if (QStringView(s).mid(at, m->size()) == QStringView(*m)) return m->size();
        } else if (QStringView(*m).first(avail) == QStringView(s).mid(at)) {
            partial = true;
        }
    }
    return partial ? -1 : 0;
}

// UTF-8 글자 하나가 읽기 경계에서 잘리면 한글이 깨진다 — 잘린 꼬리를 b 에서 떼어 돌려주고
// (호출한 쪽이 보관했다가 다음 조각 앞에 붙인다), 온전하면 빈 배열을 준다. 최대 3바이트.
inline QByteArray takeIncompleteUtf8(QByteArray *b) {
    for (int back = 1; back <= 3 && back <= b->size(); ++back) {
        const unsigned char ch = static_cast<unsigned char>(b->at(b->size() - back));
        if ((ch & 0xC0) == 0x80) continue;              // 이어지는 바이트 — 더 앞을 본다
        const int need = (ch & 0xE0) == 0xC0 ? 2
                       : (ch & 0xF0) == 0xE0 ? 3
                       : (ch & 0xF8) == 0xF0 ? 4 : 0;
        if (need > back) { QByteArray tail = b->right(back); b->chop(back); return tail; }
        break;                                          // 완성된 글자에서 끝났다
    }
    return {};
}
