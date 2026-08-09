#include "consolepane.h"
#include "lumihighlighter.h"
#include "../core/outclass.h"
#include "../theme/theme.h"
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>

ConsolePane::ConsolePane(Role role, QWidget *parent)
    : CodeEditor(parent), m_role(role) {
    setShowLineNumbers(false);
    setObjectName("Mono");                 // 테마 QSS 의 고정폭 규칙 대상
    setUndoRedoEnabled(false);             // Ctrl+Z 로 찍힌 출력을 되돌리면 상태가 깨진다
    setMaximumBlockCount(1000);            // 스크롤백 1000줄 고정 (GUI 마비 방지)
    setLineWrapMode(CodeEditor::WidgetWidth);
    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    setFont(f);

    if (m_role == Shell) {
        highlighter()->setDocument(nullptr);
    }

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(20);         // 20ms = 50 FPS 배치 렌더링
    connect(m_flushTimer, &QTimer::timeout, this, &ConsolePane::flushPending);

    m_prompt = (role == Repl) ? "lumi> " : "> ";
    showPrompt(QString());
}

void ConsolePane::setFontPointSize(int pt) {
    if (pt < 6) pt = 6;
    if (pt > 48) pt = 48;
    QFont f = font();
    f.setPointSize(pt);
    setFont(f);
}

int ConsolePane::fontPointSize() const {
    return font().pointSize() <= 0 ? 10 : font().pointSize();
}

void ConsolePane::zoomBy(int delta) {
    setFontPointSize(fontPointSize() + delta);
}

void ConsolePane::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->angleDelta().y() != 0) {
            zoomBy(e->angleDelta().y() > 0 ? 1 : -1);
        }
        return;
    }
    CodeEditor::wheelEvent(e);
}

QColor ConsolePane::colorFor(int cls) const {
    const Palette &p = currentPalette();
    switch (cls) {
        case OC_ERROR:   return p.error;
        case OC_SUCCESS: return p.success;
        case OC_INFO:    return p.termInfo;
        // 셸·REPL 프롬프트와 다 친 입력(Enter 뒤): 셸은 초록, REPL 은 파랑.
        case OC_USERIN:
        case OC_PROMPT:  return (m_role == Shell) ? p.success : p.accent;
        default:         return p.termFg;
    }
}

void ConsolePane::writeChunk(const QString &text, int cls) {
    if (text.isEmpty()) return;
    QTextCharFormat fmt;
    fmt.setForeground(colorFor(cls));
    if (cls == OC_PROMPT) fmt.setFontWeight(QFont::Bold);

    // 맨 아래를 보고 있을 때만 따라 내려간다. 늘 따라가면 실행 중에 위로
    // 올라가 볼 수도, 글자를 골라 복사할 수도 없었다.
    QScrollBar *sb = verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;

    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);          // setTextCursor 를 쓰면 사용자의 선택이 날아간다

    if (atBottom) sb->setValue(sb->maximum());
}

void ConsolePane::commit(const QString &text, int cls) {
    if (text.isEmpty()) return;
    m_chunks.append({cls, text});
    if (m_chunks.size() > 500) m_chunks.remove(0, m_chunks.size() - 500);
    writeChunk(text, cls);
}

int ConsolePane::inputStart() const { return m_promptStart.position() + m_prompt.size(); }

QString ConsolePane::currentInput() const {
    QTextCursor c(document());
    c.setPosition(qMin(inputStart(), document()->characterCount() - 1));
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return c.selectedText();
}

void ConsolePane::removeTail() {
    QTextCursor c(document());
    c.setPosition(qMin(m_promptStart.position(), document()->characterCount() - 1));
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
}

void ConsolePane::showPrompt(const QString &keep) {
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    m_promptStart = c;
    // 뒤에서 타이핑해도 앵커는 프롬프트 앞에 머물러야 한다.
    m_promptStart.setKeepPositionOnInsert(true);
    writeChunk(m_prompt, OC_PROMPT);
    writeChunk(keep, OC_TEXT);

    QTextCharFormat fmt;
    fmt.setForeground(colorFor(OC_TEXT));
    fmt.setFontWeight(QFont::Normal);
    c.movePosition(QTextCursor::End);
    c.setCharFormat(fmt);
    setTextCursor(c);
    setCurrentCharFormat(fmt);

    ensureCursorVisible();
}

void ConsolePane::setInput(const QString &s) {
    QTextCursor c(document());
    c.setPosition(inputStart());
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setForeground(colorFor(OC_TEXT));
    fmt.setFontWeight(QFont::Normal);
    c.insertText(s, fmt);
    setTextCursor(c);
    setCurrentCharFormat(fmt);
    ensureCursorVisible();
}

void ConsolePane::append(const QString &text, int cls) {
    if (text.isEmpty()) return;
    if (!m_pendingBuf.isEmpty() && m_pendingCls != cls) {
        flushPending();
    }
    m_pendingBuf += text;
    if (m_pendingBuf.size() > 8000) {
        m_pendingBuf = m_pendingBuf.right(6000);
    }
    m_pendingCls = cls;
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start();
    }
}

void ConsolePane::flushPending() {
    if (m_pendingBuf.isEmpty()) {
        m_flushTimer->stop();
        return;
    }

    QString textToFlush = m_pendingBuf;
    const int clsToFlush = m_pendingCls;
    m_pendingBuf.clear();

    if (textToFlush.contains("\x1b[2J") || textToFlush.contains("\f")) {
        m_chunks.clear();
        CodeEditor::clear();
        textToFlush.remove("\x1b[2J");
        textToFlush.remove("\x1b[H");
        textToFlush.remove("\x1b[m");
        textToFlush.remove("\f");
    }

    const QString keep = currentInput();
    const bool isDirect = m_prompt.isEmpty() && keep.isEmpty();

    if (!isDirect) removeTail();

    commit(textToFlush, clsToFlush);

    if (!isDirect) {
        showPrompt(keep);
    } else {
        QTextCursor c(document());
        c.movePosition(QTextCursor::End);
        m_promptStart = c;
        m_promptStart.setKeepPositionOnInsert(true);
    }

    if (m_pendingBuf.isEmpty()) {
        m_flushTimer->stop();
    }
}

void ConsolePane::setPrompt(const QString &pr) {
    if (!m_pendingBuf.isEmpty()) {
        flushPending();
    }
    if (pr == m_prompt) return;
    const QString keep = currentInput();
    removeTail();
    m_prompt = pr;
    showPrompt(keep);
}

void ConsolePane::refreshTheme() {
    // 이미 찍힌 글자는 색이 박혀 있다 — 테마가 바뀌면 통째로 다시 칠한다.
    const QString keep = currentInput();
    CodeEditor::clear();
    for (const auto &ch : m_chunks) writeChunk(ch.second, ch.first);
    showPrompt(keep);
}

void ConsolePane::clear() {
    m_pendingBuf.clear();
    const QString keep = currentInput();
    CodeEditor::clear();
    m_chunks.clear();
    showPrompt(keep);
}

void ConsolePane::focusInput() {
    setFocus();
    if (textCursor().position() < inputStart()) moveCursor(QTextCursor::End);
    ensureCursorVisible();
}

void ConsolePane::submit() {
    const QString line = currentInput();
    removeTail();
    commit(m_prompt, OC_PROMPT);              // 프롬프트도 본문에 남는다 — 그 줄도 그대로 복사한다
    commit(line + "\n", OC_USERIN);
    if (!line.trimmed().isEmpty() && (m_history.isEmpty() || m_history.last() != line))
        m_history.append(line);
    m_histIdx = m_history.size();
    // REPL 블록: 다음 줄 들여쓰기를 편집칸과 같은 규칙으로 미리 넣어 준다.
    // 공백뿐인 줄은 블록의 끝이므로 다시 0칸으로 돌아간다.
    QString nextIndent;
    if (m_role == Repl && !line.trimmed().isEmpty()) {
        nextIndent = leadingIndent(line);
        if (line.trimmed().endsWith(u':')) nextIndent += "    ";
    }
    showPrompt(nextIndent);
    emit lineSubmitted(line);
}

void ConsolePane::insertFromMimeData(const QMimeData *src) {
    if (!src || !src->hasText()) return;
    if (textCursor().selectionStart() < inputStart()) moveCursor(QTextCursor::End);
    // 여러 줄을 붙이면 터미널처럼 줄마다 실행한다. 마지막 조각은 입력줄에 남긴다.
    const QStringList lines = src->text().split(QRegularExpression("\r\n|\n|\r"));
    for (int i = 0; i < lines.size(); ++i) {
        QTextCursor c = textCursor();
        QTextCharFormat fmt;
        fmt.setForeground(colorFor(OC_TEXT));
        fmt.setFontWeight(QFont::Normal);
        c.insertText(lines[i], fmt);
        if (i + 1 < lines.size()) submit();
    }
    QTextCharFormat fmt;
    fmt.setForeground(colorFor(OC_TEXT));
    fmt.setFontWeight(QFont::Normal);
    setCurrentCharFormat(fmt);
    ensureCursorVisible();
}

void ConsolePane::keyPressEvent(QKeyEvent *e) {
    const int start = inputStart();
    const QTextCursor cur = textCursor();
    const bool ctrl = e->modifiers() & Qt::ControlModifier;

    // Ctrl+plus/minus/0 줌
    if (ctrl) {
        if (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus) { zoomBy(1); return; }
        if (e->key() == Qt::Key_Minus) { zoomBy(-1); return; }
        if (e->key() == Qt::Key_0) { setFontPointSize(10); return; }
    }

    // Ctrl+C: copy selection or interrupt
    if (ctrl && e->key() == Qt::Key_C && !(e->modifiers() & Qt::ShiftModifier)) {
        if (cur.hasSelection()) copy(); else emit interruptRequested();
        return;
    }
    if (e->matches(QKeySequence::Copy) || e->matches(QKeySequence::SelectAll)
        || e->matches(QKeySequence::Find)) {
        CodeEditor::keyPressEvent(e);
        return;
    }

    switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            submit();
            return;
        case Qt::Key_Up:
            if (m_histIdx > 0) { --m_histIdx; setInput(m_history.value(m_histIdx)); }
            return;
        case Qt::Key_Down:
            if (m_histIdx < m_history.size() - 1) { ++m_histIdx; setInput(m_history.value(m_histIdx)); }
            else { m_histIdx = m_history.size(); setInput(QString()); }
            return;
        case Qt::Key_Home: {
            QTextCursor c = cur;
            c.setPosition(start, (e->modifiers() & Qt::ShiftModifier)
                                     ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
            setTextCursor(c);
            return;
        }
        case Qt::Key_Left:
            // prevent moving into the prompt
            if (!cur.hasSelection() && cur.position() <= start) return;
            break;
        case Qt::Key_Backspace: {
            if (!cur.hasSelection() && cur.position() <= start) return;
            // 들여쓰기 안이면 4칸을 한 번에 지운다. CodeEditor 의 같은 처리는
            // 줄 앞의 프롬프트까지 세어 버려서 콘솔에선 쓸 수 없다.
            const int col = cur.position() - start;
            const QString head = currentInput().left(col);
            if (!ctrl && !cur.hasSelection() && col > 0 && col % 4 == 0
                && head.trimmed().isEmpty() && !head.contains(u'\t')) {
                QTextCursor c = cur;
                c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
                c.removeSelectedText();
                setTextCursor(c);
                return;
            }
            break;
        }
        default:
            break;
    }

    const bool edits = !e->text().isEmpty() || e->key() == Qt::Key_Delete;
    if (edits && cur.selectionStart() < start) moveCursor(QTextCursor::End);

    if (!e->text().isEmpty()) {
        QTextCharFormat fmt;
        fmt.setForeground(colorFor(OC_TEXT));
        fmt.setFontWeight(QFont::Normal);
        setCurrentCharFormat(fmt);
    }

    if (m_role == Shell) {
        QPlainTextEdit::keyPressEvent(e);
    } else {
        CodeEditor::keyPressEvent(e);
    }
}
