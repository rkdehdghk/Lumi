// 자식 프로세스 출력 조각 처리 자체 점검. 빌드 & 실행: tests\run.bat
#include "../src/models/streammarkers.h"
#include <cassert>
#include <cstdio>

static void markers() {
    const QString in = markInput(), done = markDone(), eot = markEot();

    // 완전히 들어온 마커
    assert(matchMarker(in, 0) == in.size());
    assert(matchMarker(done, 0) == done.size());
    assert(matchMarker(eot, 0) == eot.size());
    assert(matchMarker("hi" + done, 2) == done.size());

    // 읽기 경계에서 잘린 마커 → -1 (기다린다)
    for (int cut = 1; cut < done.size(); ++cut) assert(matchMarker(done.first(cut), 0) == -1);
    for (int cut = 1; cut < eot.size(); ++cut) assert(matchMarker(eot.first(cut), 0) == -1);
    for (int cut = 1; cut < in.size(); ++cut) assert(matchMarker(in.first(cut), 0) == -1);

    // 진짜 버그였던 자리: DONE(6자) / EOT(5자) 가 조각 맨 끝에 딱 떨어질 때.
    // 예전 규칙("남은 길이 < 7 이면 잘린 것")은 여기서 -1 을 줘서 실행이 안 끝났다.
    assert(matchMarker(done, 0) == 6);
    assert(matchMarker(eot, 0) == 5);

    // 마커가 아닌 \x1e 는 그냥 글자
    assert(matchMarker(QString::fromUtf8("\x1e" "NOPE\n"), 0) == 0);
    assert(matchMarker(QString::fromUtf8("\x1e"), 0) == -1);   // 어느 마커의 시작이든 될 수 있다
}

static void utf8() {
    // "가" = ea b0 80 (3바이트)
    const QByteArray ga = QString::fromUtf8("가").toUtf8();
    assert(ga.size() == 3);

    // 온전하면 떼어 갈 게 없다
    QByteArray whole = "ok" + ga;
    assert(takeIncompleteUtf8(&whole).isEmpty());
    assert(whole == "ok" + ga);

    // 1바이트/2바이트만 왔으면 그만큼 떼어 둔다
    for (int keep = 1; keep <= 2; ++keep) {
        QByteArray cut = "ok" + ga.first(keep);
        QByteArray tail = takeIncompleteUtf8(&cut);
        assert(cut == "ok");
        assert(tail == ga.first(keep));
        // 떼어 둔 꼬리에 나머지를 붙이면 원래 글자가 돌아온다
        assert(QString::fromUtf8(tail + ga.mid(keep)) == QString::fromUtf8("가"));
    }

    // 순수 ASCII 와 빈 입력은 건드리지 않는다
    QByteArray ascii = "plain text\n";
    assert(takeIncompleteUtf8(&ascii).isEmpty());
    QByteArray empty;
    assert(takeIncompleteUtf8(&empty).isEmpty());
}

int main() {
    markers();
    utf8();
    std::printf("streammarkers: all checks passed\n");
    return 0;
}
