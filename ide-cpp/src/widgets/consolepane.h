#pragma once
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QStringList>
#include <QList>
#include <QPair>

#include "codeeditor.h"

// 진짜 터미널처럼: 프롬프트가 출력칸 *안*에 찍히고 그 뒤에서 바로 타이핑한다.
// 그래서 "C:\...\Lumina>" 같은 프롬프트도 드래그해서 복사할 수 있다.
class ConsolePane : public CodeEditor {
    Q_OBJECT
public:
    enum Role { Shell, Repl };
    explicit ConsolePane(Role role, QWidget *parent = nullptr);

    void append(const QString &text, int cls);
    void clear();
    // 입력줄은 늘 살아 있다: 실행 중에도 Ctrl+C(중단)와 미리 치기가 되어야 한다.
    // 그 줄을 어디로 보낼지는 OutputPanel 이 정한다.
    void setPrompt(const QString &p);
    void focusInput();
    void refreshTheme();                // 찍힌 글자까지 새 테마 색으로 다시 칠한다
    void flushPending();
    void setFontPointSize(int pt);
    int fontPointSize() const;
    void zoomBy(int delta);

signals:
    void lineSubmitted(const QString &line);
    void interruptRequested();          // 입력줄에서 Ctrl+C

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void insertFromMimeData(const QMimeData *src) override;

private:
    QColor colorFor(int cls) const;
    void writeChunk(const QString &text, int cls);   // 문서 끝에 그리기만
    void commit(const QString &text, int cls);       // 그리고 원본도 보관
    int  inputStart() const;
    QString currentInput() const;
    void setInput(const QString &s);
    void removeTail();                  // 프롬프트+입력을 문서에서 걷어낸다
    void showPrompt(const QString &keep);
    void submit();

    Role m_role;
    QString m_prompt;
    QTextCursor m_promptStart;             // 문서가 바뀌어도 Qt 가 따라 옮겨준다
    QStringList m_history;
    QList<QPair<int, QString>> m_chunks;   // (OutClass, 글자) 원본
    int m_histIdx = 0;

    QString m_pendingBuf;
    int m_pendingCls = 0;
    QTimer *m_flushTimer = nullptr;
};
