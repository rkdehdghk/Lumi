#pragma once
#include <QWidget>
#include <QStringList>
#include <QVector>

class ConsolePane;
class RunSession;
class QLabel;
class QPushButton;
class QStackedWidget;
class QFrame;
class QHBoxLayout;
class CollapsedBar;
class ElideLabel;
class ShrinkBox;

class OutputPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutputPanel(RunSession *run, QWidget *parent = nullptr);

    void runCode(const QString &fileName, const QString &baseDir);
    void stopRun();
    bool busy() const;
    void clearCurrent();
    void setCwd(const QString &cwd);   // 터미널 cwd
    QString cwd() const { return m_cwd; }
    void refreshTheme();
    void focusInput();
    void setDetached(bool on);   // 버튼 글자 Detach ↔ Dock
    void setCollapsed(bool on, bool horizontal);  // 본문만 접고 헤더는 남긴다

signals:
    void detachRequested();
    void collapseRequested();
    void errorLineDetected(int line);
    void runningChanged(bool running);

private:
    // 탭 하나 = 콘솔 하나 = 프로세스 채널 하나. (VS Code 터미널 탭과 같은 개념)
    struct Sess {
        ConsolePane *pane = nullptr;
        int chan = 0;
        bool repl = false;
        QString name;
        QWidget *tab = nullptr;          // [이름][×] 묶음
        QPushButton *nameBtn = nullptr;
        QPushButton *closeBtn = nullptr;
        QStringList replBuffer;          // `:` 블록을 빈 줄까지 모은다
        bool awaitingInput = false;      // 실행 중인 프로그램이 input() 으로 대기 중
        QString pendingCmd, pendingDir;  // 셸이 뜨기 전에 누른 F5 — 첫 프롬프트 뒤에 보낸다
    };

    Sess *addSession(bool repl);
    void closeSession(Sess *s);
    void select(Sess *s);
    Sess *byChan(int chan) const;
    void updateTabButtons();
    void syncPrompt(Sess *s);
    void submitted(Sess *s, const QString &line);
    void runShellCommand(Sess *s, const QString &raw);
    void replSubmit(Sess *s, const QString &raw);
    void sendRun(Sess *s, const QString &cmdLine, const QString &baseDir);

    RunSession *m_run;
    QVector<Sess*> m_sessions;
    Sess *m_cur = nullptr;
    Sess *m_runSess = nullptr;   // F5 가 마지막으로 돌린 탭 (에러줄·Stop 이 갈 곳)
    int m_nextChan = 1;
    int m_termNo = 0, m_replNo = 0;

    QFrame *m_hdr;
    ShrinkBox *m_leftBox;      // 제목 + 터미널 탭 + [+] — 좁아지면 양보하는 묶음
    QHBoxLayout *m_tabRow;
    QLabel *m_titleLbl;
    ElideLabel *m_cwdLbl;
    QPushButton *m_addBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_detachBtn;
    QPushButton *m_collapseBtn;
    QStackedWidget *m_stack;
    CollapsedBar *m_bar;

    QString m_cwd;
    int m_pendingError = -1;
};
