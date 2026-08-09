#include "codeeditor.h"
#include "linenumberarea.h"
#include "lumihighlighter.h"
#include "../core/lumitokenizer.h"
#include "../theme/theme.h"
#include <QPainter>
#include <QKeyEvent>
#include <QTextBlock>
#include <QScrollBar>
#include <QFontMetrics>
#include <QMimeData>
#include <QUrl>
#include <QCompleter>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <QScopeGuard>
#include <QSet>
#include <cmath>

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    m_ln = new LineNumberArea(this);
    m_hl = new LumiHighlighter(document());

    setObjectName("Mono");                          // 테마 QSS 의 고정폭 규칙 대상
    setLineWrapMode(QPlainTextEdit::WidgetWidth);   // 사용설명서: 긴 줄은 접어서 보여준다
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(false);
    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    setFont(f);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::emitCursorPos);

    m_words = new QStandardItemModel(this);
    m_completer = new QCompleter(m_words, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    // 목록에 보이는 글자에는 종류표(keyword/type/func)가 붙어 있으므로,
    // 찾고 넣는 데 쓰는 진짜 이름은 UserRole 에 따로 담는다.
    m_completer->setCompletionRole(Qt::UserRole);
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &CodeEditor::insertCompletion);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

// 줄번호는 코드 글자의 5/6 크기 — 휠 줌을 하면 같이 커지고 작아진다.
QFont CodeEditor::lineNumberFont() const {
    QFont f = font();
    f.setPointSize(qMax(1, fontPointSize() * 5 / 6));
    return f;
}

void CodeEditor::setShowLineNumbers(bool show) {
    m_showLineNumbers = show;
    m_ln->setVisible(show);
    updateLineNumberAreaWidth(0);
}

int CodeEditor::lineNumberAreaWidth() {
    if (!m_showLineNumbers) return 0;
    if (isReadOnly() && document()->isEmpty()) return 0;
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    const QFontMetrics fm(lineNumberFont());
    int space = 6 + fm.horizontalAdvance(QLatin1Char('9')) * (digits + 1);
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) m_ln->scroll(0, dy);
    else m_ln->update(0, rect.y(), m_ln->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_ln->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> es;
    const Palette &p = currentPalette();
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(p.surfaceAlt);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        es.append(s);
    }
    // 오류 줄: 줄번호 색만 바뀌면 눈에 안 띈다 — 줄 전체에 배경을 깐다.
    if (m_errorLine > 0 && m_errorLine <= blockCount()) {
        QTextBlock b = document()->findBlockByNumber(m_errorLine - 1);
        if (b.isValid()) {
            QTextEdit::ExtraSelection s;
            s.format.setBackground(p.errorBg);
            s.format.setProperty(QTextFormat::FullWidthSelection, true);
            s.cursor = QTextCursor(b);
            es.append(s);
        }
    }
    setExtraSelections(es);
}

void CodeEditor::emitCursorPos() {
    QTextCursor c = textCursor();
    emit cursorPosChanged(c.blockNumber() + 1, c.columnNumber() + 1);
}

void CodeEditor::setErrorLine(int line) {
    m_errorLine = line;
    if (line > 0 && line <= blockCount()) {
        // 오류 줄로 데려간다 (커서는 옮기지 않고 화면만 맞춘다).
        QTextBlock b = document()->findBlockByNumber(line - 1);
        if (b.isValid()) {
            QTextCursor c(b);
            setTextCursor(c);
            centerCursor();
        }
    }
    highlightCurrentLine();
    m_ln->update();
}

void CodeEditor::setFontPointSize(int pt) {
    if (pt < 6) pt = 6;
    if (pt > 48) pt = 48;
    QFont f = font();
    f.setPointSize(pt);
    setFont(f);
    updateLineNumberAreaWidth(0);   // 줄번호 칸도 새 글자 크기에 맞춘다
    m_ln->update();
    emit fontSizeChanged(pt);
}

int CodeEditor::fontPointSize() const {
    return font().pointSize() <= 0 ? 10 : font().pointSize();
}

void CodeEditor::zoomBy(int delta) {
    setFontPointSize(fontPointSize() + delta);
}

QString CodeEditor::leadingIndent(const QString &line) const {
    QString ind;
    for (QChar c : line) {
        if (c == u' ' || c == u'\t') ind += c;
        else break;
    }
    return ind;
}

void CodeEditor::indentNewLine() {
    QTextCursor c = textCursor();
    // 한 덩이로 묶지 않으면 Ctrl+Z 한 번에 들여쓰기만 지워지고 줄바꿈이 남는다.
    c.beginEditBlock();
    c.insertText("\n");
    QString line = c.block().previous().text();
    QString ind = leadingIndent(line);
    QString trimmed = line.trimmed();
    if (trimmed.endsWith(u':')) ind += "    ";
    if (!ind.isEmpty()) c.insertText(ind);
    c.endEditBlock();
    setTextCursor(c);
}

// 파일을 편집칸에 끌어다 놓으면 예전엔 "file:///..." 글자가 코드에 박혔다.
void CodeEditor::dropEvent(QDropEvent *e) {
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (!paths.isEmpty()) {
            e->acceptProposedAction();
            emit filesDropped(paths);
            return;
        }
    }
    QPlainTextEdit::dropEvent(e);
}

bool CodeEditor::canInsertFromMimeData(const QMimeData *src) const {
    return src->hasUrls() || QPlainTextEdit::canInsertFromMimeData(src);
}

// 선택된 줄 전체를 4칸 밀거나 당긴다. 여러 번 연속으로 누를 수 있도록
// 선택 범위(선택이 없으면 커서 자리)를 다시 잡아 준다.
void CodeEditor::indentSelection(bool forward) {
    QTextCursor c = textCursor();
    const bool hadSel = c.hasSelection();
    const int startPos = c.selectionStart(), endPos = c.selectionEnd();
    const int curCol = c.positionInBlock();

    QTextBlock first = document()->findBlock(startPos);
    QTextBlock last = document()->findBlock(endPos);
    // 선택이 줄 첫칸에서 끝났으면 그 줄은 건드리지 않는다 (에디터 공통 동작).
    if (last != first && endPos == last.position()) last = last.previous();
    const int firstNo = first.blockNumber(), lastNo = last.blockNumber();

    int firstDelta = 0;   // 첫 줄에서 실제로 늘거나 준 칸 수
    QTextCursor edit(document());
    edit.beginEditBlock();
    for (int n = firstNo; n <= lastNo; ++n) {
        QTextBlock it = document()->findBlockByNumber(n);
        if (!it.isValid()) break;
        QTextCursor lc(it);
        int delta = 0;
        if (forward) {
            if (it.length() > 1 || firstNo == lastNo) { lc.insertText("    "); delta = 4; }
        } else {
            // 앞쪽 공백을 최대 4칸까지 지운다 (탭 하나도 한 단계로 친다).
            const QString line = it.text();
            int k = 0;
            while (k < 4 && k < line.size() && (line[k] == u' ' || line[k] == u'\t')) {
                ++k;
                if (line[k - 1] == u'\t') break;
            }
            if (k > 0) {
                lc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, k);
                lc.removeSelectedText();
                delta = -k;
            }
        }
        if (n == firstNo) firstDelta = delta;
    }
    edit.endEditBlock();

    QTextBlock f = document()->findBlockByNumber(firstNo);
    QTextBlock l = document()->findBlockByNumber(lastNo);
    QTextCursor sel(document());
    if (!hadSel) {
        sel.setPosition(f.position() + qBound(0, curCol + firstDelta, f.length() - 1));
    } else {
        sel.setPosition(f.position());
        sel.setPosition(l.position() + l.length() - 1, QTextCursor::KeepAnchor);
    }
    setTextCursor(sel);
}

// ---- 자동완성 ----

static constexpr int RoleCallable = Qt::UserRole + 1;   // 이 이름은 함수인가

// 언어가 쥐고 있는 이름들 — 파일에서 긁어온 이름과 겹치지 않게 걸러 낼 때 쓴다.
static const QSet<QString> &languageWords() {
    static const QSet<QString> s = [] {
        QSet<QString> k;
        for (const LumiWord &w : lumiWords()) k.insert(w.name);
        return k;
    }();
    return s;
}

// 커서 바로 앞에 있는, 지금 치고 있는 이름.
QString CodeEditor::wordPrefix() const {
    const QString line = textCursor().block().text();
    int col = textCursor().positionInBlock(), i = col;
    while (i > 0 && (line[i - 1].isLetterOrNumber() || line[i - 1] == u'_')) --i;
    return line.mid(i, col - i);
}

void CodeEditor::insertCompletion(const QModelIndex &index) {
    const QString word = index.data(Qt::UserRole).toString();
    QTextCursor c = textCursor();
    // 함수면 괄호까지 넣고 그 안에 커서를 둔다. 이미 '(' 가 있으면 덧붙이지 않는다.
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    const bool call = index.data(RoleCallable).toBool()
                      && !(col < line.size() && line[col] == u'(');

    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor,
                   m_completer->completionPrefix().length());
    c.insertText(call ? word + "()" : word);
    if (call) c.movePosition(QTextCursor::Left);
    setTextCursor(c);
}

void CodeEditor::refreshCompletion(bool force) {
    QAbstractItemView *popup = m_completer->popup();
    const QString prefix = wordPrefix();
    // 두 글자부터 (Ctrl+Space 면 바로). 선택 중일 땐 방해하지 않는다.
    if (textCursor().hasSelection() || (!force && prefix.length() < 2)) {
        popup->hide();
        return;
    }

    // 지금 파일에 이미 쓰인 이름 (문자열/주석은 토크나이저가 걸러 준다).
    // 토크나이저가 '(' 앞의 이름을 TK_BUILTIN 으로 주므로, 내가 만든 함수·클래스도
    // 부르는 이름인지 알 수 있다 → 자동완성 때 괄호까지 붙여 준다.
    // ponytail: 팝업을 띄울 때마다 문서 전체를 훑는다. 수천 줄에서 굼뜨면 그때 캐시.
    const QString doc = toPlainText();
    QSet<QString> mine, mineCalls;
    for (const Tok &t : lumiTokenize(QStringView(doc))) {
        if (t.kind != TK_PLAIN && t.kind != TK_BUILTIN) continue;
        const QString w = doc.mid(t.start, t.len);
        if (languageWords().contains(w)) continue;
        mine.insert(w);
        if (t.kind == TK_BUILTIN) mineCalls.insert(w);
    }
    QStringList mineSorted(mine.begin(), mine.end());
    mineSorted.sort(Qt::CaseInsensitive);

    // 종류별로 색과 종류표를 달아 준다 (편집칸 문법 강조색과 같은 색).
    const Palette &pal = currentPalette();
    QFont strong = font();
    strong.setBold(true);
    m_words->clear();
    // 한 줄: 이름 | 종류표 | 쓰는 형식. 팝업이 고정폭 글꼴이라 칸이 맞아떨어진다.
    auto add = [&](const QString &w, const QString &tag, const QColor &fg,
                   bool call, const QString &hint) {
        // 함수는 괄호까지가 완성형이므로 목록에도 그렇게 보여 준다.
        QString label = call ? w + "()" : w;
        if (!tag.isEmpty()) {
            label += QString(qMax(1, 13 - label.size()), u' ') + tag;
            if (!hint.isEmpty()) label += QString(qMax(1, 8 - tag.size()), u' ') + hint;
        }
        auto *item = new QStandardItem(label);
        item->setData(w, Qt::UserRole);          // 찾기·넣기에 쓰는 진짜 이름
        item->setData(call, RoleCallable);       // 넣을 때 괄호까지 붙일지
        item->setForeground(fg);
        if (!tag.isEmpty()) item->setFont(strong);
        item->setEditable(false);
        m_words->appendRow(item);
    };
    // 자료형은 두 몫을 하므로(선언 `int x = 1` / 형변환 `int("42")`) 표에 두 번 들어 있다.
    for (const LumiWord &w : lumiWords()) {
        switch (w.kind) {
            case LW_KEYWORD: add(w.name, "keyword", pal.synKeyword, false, w.hint); break;
            case LW_TYPE:    add(w.name, "type",    pal.synKeyword, false, w.hint); break;
            case LW_CAST:    add(w.name, "cast",    pal.synBuiltin, true,  w.hint); break;
            case LW_FUNC:    add(w.name, "func",    pal.synBuiltin, true,  w.hint); break;
        }
    }
    for (const QString &w : mineSorted)
        add(w, QString(), pal.editorFg, mineCalls.contains(w), QString());

    m_completer->setCompletionPrefix(prefix);
    const int n = m_completer->completionCount();
    const QModelIndex first = m_completer->completionModel()->index(0, 0);
    // 이름을 다 쳤고 더 보탤 게 없으면 닫는다. 단 함수는 괄호까지가 완성이라,
    // 이름만 다 쳤을 땐 아직 덜 친 것으로 보고 목록을 열어 둔다.
    const bool nothingLeft = n == 1 && m_completer->currentCompletion() == prefix
                             && !first.data(RoleCallable).toBool();
    if (n == 0 || nothingLeft) {
        popup->hide();
        return;
    }
    popup->setCurrentIndex(first);
    popup->setFont(font());   // 코드니까 고정폭으로 (줌하면 같이 커진다)

    QRect r = cursorRect().translated(viewport()->pos());   // 줄번호칸만큼 밀어 준다
    r.setWidth(popup->sizeHintForColumn(0)
               + popup->verticalScrollBar()->sizeHint().width() + 8);
    m_completer->complete(r);
}

void CodeEditor::keyPressEvent(QKeyEvent *e) {
    // 팝업이 떠 있으면 위/아래/엔터/탭/Esc 는 팝업 몫이다.
    if (m_completer->popup()->isVisible()) {
        switch (e->key()) {
            case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Tab:
            case Qt::Key_Escape: case Qt::Key_Up: case Qt::Key_Down:
                e->ignore();
                return;
            default: break;
        }
    }
    // 어느 길로 빠져나가든 팝업 상태를 다시 맞춘다 (아래에 return 이 많다).
    const bool force = (e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Space;
    auto sync = qScopeGuard([this, force] { refreshCompletion(force); });

    // Ctrl+plus/minus/0 줌, Ctrl+Space 자동완성
    if (e->modifiers() & Qt::ControlModifier) {
        if (force) return;
        if (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus) { zoomBy(1); return; }
        if (e->key() == Qt::Key_Minus) { zoomBy(-1); return; }
        if (e->key() == Qt::Key_0) { setFontPointSize(10); return; }
    }
    // Tab: 선택이 있으면 블록 들여쓰기, 없으면 4스페이스
    if (e->key() == Qt::Key_Tab && !(e->modifiers() & ~Qt::KeypadModifier)) {
        if (textCursor().hasSelection()) indentSelection(true);
        else insertPlainText("    ");
        return;
    }
    // Shift+Tab: 역들여쓰기 (선택 없으면 커서 줄 하나만)
    if (e->key() == Qt::Key_Backtab) {
        indentSelection(false);
        return;
    }
    // Backspace: 들여쓰기 안이면 4칸을 한 번에 지운다
    if (e->key() == Qt::Key_Backspace && !e->modifiers() && !textCursor().hasSelection()) {
        QTextCursor c = textCursor();
        int col = c.positionInBlock();
        const QString head = c.block().text().left(col);
        if (col > 0 && col % 4 == 0 && head.trimmed().isEmpty() && !head.contains(u'\t')) {
            c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
            c.removeSelectedText();
            return;
        }
        // 자동쌍 안에서 지우면 짝도 같이 지운다: (|) → 빈 줄
        static const QHash<QChar, QChar> pairs = {
            {u'(', u')'}, {u'[', u']'}, {u'{', u'}'}, {u'"', u'"'}, {u'\'', u'\''}
        };
        const QString line = c.block().text();
        if (col > 0 && col < line.size() && pairs.value(line[col - 1]) == line[col]) {
            c.deleteChar();
            c.deletePreviousChar();
            return;
        }
    }
    // Enter → 자동들여쓰기
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!e->modifiers()) { indentNewLine(); return; }
    }
    // 괄호/따옴표 자동쌍
    if (e->text().length() == 1) {
        QChar ch = e->text().at(0);
        static const QHash<QChar, QChar> pairs = {
            {u'(', u')'}, {u'[', u']'}, {u'{', u'}'}, {u'"', u'"'}, {u'\'', u'\''}
        };
        QTextCursor c = textCursor();
        const QString line = c.block().text();
        const int col = c.positionInBlock();
        const QChar next = col < line.size() ? line[col] : QChar(u'\n');

        // '/' 뒤에 '.' 를 치면 여는 주석 → 닫는 './' 까지 붙여 주고 사이에 커서를 둔다
        // (앞이 '//' 면 한 줄 주석이거나 "http://..." 같은 글자다 — 건드리지 않는다)
        if (ch == u'.' && col > 0 && line[col - 1] == u'/' && !c.hasSelection()
            && !(col > 1 && line[col - 2] == u'/')) {
            insertPlainText(". ./");
            QTextCursor c2 = textCursor();
            c2.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 3);
            setTextCursor(c2);
            return;
        }
        // 닫는 괄호/따옴표 타입오버: 이미 있는 짝 위에 덧치지 않는다
        if ((ch == u')' || ch == u']' || ch == u'}' || ch == u'"' || ch == u'\'') && next == ch) {
            c.movePosition(QTextCursor::Right);
            setTextCursor(c);
            return;
        }
        // 선택이 있으면 감싸기, 없으면 뒤에 글자가 안 붙어 있을 때만 자동쌍
        if (pairs.contains(ch)) {
            if (c.hasSelection()) {
                QString sel = c.selectedText();
                c.insertText(QString(ch) + sel + pairs.value(ch));
                return;
            }
            bool nextIsWord = next.isLetterOrNumber() || next == u'_' || next == u'"'
                           || next == u'\'';
            if (!nextIsWord) {
                insertPlainText(QString(ch) + QString(pairs.value(ch)));
                QTextCursor c2 = textCursor();
                c2.movePosition(QTextCursor::Left);
                setTextCursor(c2);
                return;
            }
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

void CodeEditor::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->angleDelta().y() != 0) {
            zoomBy(e->angleDelta().y() > 0 ? 1 : -1);
        }
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

void CodeEditor::paintEvent(QPaintEvent *e) {
    QPlainTextEdit::paintEvent(e);
    if (isReadOnly() && document()->isEmpty()) {
        const Palette &p = currentPalette();
        QPainter g(viewport());
        g.setRenderHint(QPainter::Antialiasing, true);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.5);
        f.setBold(true);
        g.setFont(f);
        g.setPen(p.textDim);
        g.drawText(viewport()->rect(), Qt::AlignCenter, "No file");
    }
}

// ---- LineNumberArea ----

LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}
QSize LineNumberArea::sizeHint() const { return {m_editor->lineNumberAreaWidth(), 0}; }

void LineNumberArea::paintEvent(QPaintEvent *) {
    if (m_editor->isReadOnly() && m_editor->document()->isEmpty()) return;
    const Palette &p = currentPalette();
    QPainter g(this);
    g.fillRect(rect(), p.surfaceAlt);

    QTextBlock block = m_editor->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(m_editor->blockBoundingGeometry(block)
                         .translated(m_editor->contentOffset()).top());
    int bottom = top + qRound(m_editor->blockBoundingRect(block).height());

    // 번호 글자는 코드보다 한 단계 작게(5/6) — 그래서 여기서 폰트를 직접 건다.
    // (안 걸면 위젯 기본 UI 폰트로 그려져서 줌을 해도 번호만 그대로였다.)
    g.setFont(m_editor->lineNumberFont());
    while (block.isValid() && top <= rect().bottom()) {
        if (block.isVisible() && bottom >= rect().top()) {
            QString num = QString::number(blockNumber + 1);
            bool isError = (m_editor->errorLine() == blockNumber + 1);
            g.setPen(isError ? p.error : p.textDim);
            g.drawText(0, top, width() - 6, bottom - top,
                       Qt::AlignRight | Qt::AlignVCenter, num);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(m_editor->blockBoundingRect(block).height());
        ++blockNumber;
    }
}
