#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QByteArray>
#include <QHash>

// 채널: 터미널/REPL 탭마다 자기 프로세스를 하나씩 들고 산다 (하나로 합치면 셸 명령
// 한 번에 REPL 세션 상태가 날아간다). 채널 번호는 탭을 열 때 OutputPanel 이 붙여 주고,
// F5 런이 갈 탭은 "지금 고른 탭"이라 고정 번호가 없다.

class RunSession : public QObject {
    Q_OBJECT
public:
    explicit RunSession(QObject *parent = nullptr);

    bool isBusy() const;                  // 아무 채널이나 돌고 있으면 true
    bool isBusy(int chan) const;
    bool alive(int chan) const;
    bool starting(int chan) const;        // 셸이 아직 첫 프롬프트를 안 냈다

    // 상주 터미널 셸 (어느 셸인지는 hostenv 의 hostShellProgram 이 정한다)
    void ensureShell(int chan, const QString &cwd);
    void cdShell(int chan, const QString &cwd);
    // REPL 한 블록 실행 (프로세스가 없으면 먼저 띄운다).
    void ensureRepl(int chan, const QString &baseDir);
    void replRun(int chan, const QString &code);

    void stopChan(int chan);   // 한 채널만 (탭을 닫을 때 / Stop)
    void stopAll();            // 앱을 닫을 때
    void resetRepl(int chan);  // REPL 프로세스를 죽여 변수/함수 기억을 비운다

    // 자식 stdin 입력 (input() 응답)
    void writeStdin(int chan, const QByteArray &data);

signals:
    void output(int chan, int cls, const QString &text);   // cls = OutClass
    void finished(int chan);
    void awaitingInput(int chan);
    void shellInitialized(int chan, const QString &cleanPrompt);

private:
    struct Channel {
        QProcess *proc = nullptr;
        bool busy = false;
        bool initializing = false; // 셸 초기 구동 텍스트 필터링 중
        QString pending;      // 마커가 읽기 경계에서 잘렸을 때 보관
        QByteArray rawPending; // UTF-8 글자가 읽기 경계에서 잘렸을 때 보관
        QString cwd;          // 프로세스 작업 디렉토리
    };
    QHash<int, Channel> m_ch;   // 채널 번호 → 프로세스

    void spawn(int chan, const QString &program, const QStringList &args, const QString &cwd);
    void onData(int chan);
    void handleMarkers(int chan, const QString &text);
    static QString decode(const QByteArray &b);
};
