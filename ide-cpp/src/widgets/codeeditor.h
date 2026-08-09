#pragma once
#include <QPlainTextEdit>

class LineNumberArea;
class LumiHighlighter;
class QCompleter;
class QStandardItemModel;
class QModelIndex;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    int lineNumberAreaWidth();

    void setErrorLine(int line);   // 1-based, -1 clears
    int errorLine() const { return m_errorLine; }
    void setFontPointSize(int pt);
    int fontPointSize() const;
    QFont lineNumberFont() const;   // 코드 글자의 5/6 크기
    void zoomBy(int delta);        // +1/-1 단위

    void setShowLineNumbers(bool show);

    LumiHighlighter *highlighter() { return m_hl; }

public slots:
    void highlightCurrentLine();   // 테마 바뀌면 현재줄 강조색도 다시 칠해야 한다
    void updateLineNumberAreaWidth(int = 0);

signals:
    void cursorPosChanged(int line, int col);
    void fontSizeChanged(int pt);
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *src) const override;
    QString leadingIndent(const QString &line) const;   // REPL 입력줄도 같은 규칙을 쓴다

private slots:
    void updateLineNumberArea(const QRect &, int);
    void emitCursorPos();

private:
    friend class LineNumberArea;
    LineNumberArea *m_ln;
    LumiHighlighter *m_hl;
    int m_errorLine = -1;

    QCompleter *m_completer;
    QStandardItemModel *m_words;
    QString wordPrefix() const;
    void insertCompletion(const QModelIndex &index);
    void refreshCompletion(bool force);

    void indentNewLine();
    void indentSelection(bool forward);

    bool m_showLineNumbers = true;
};
