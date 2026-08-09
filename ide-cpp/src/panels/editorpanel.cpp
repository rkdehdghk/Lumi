#include "editorpanel.h"
#include "../widgets/tabbar.h"
#include "../widgets/collapsedbar.h"
#include "../widgets/codeeditor.h"
#include "../widgets/lumihighlighter.h"
#include "../theme/theme.h"
#include "../core/commands.h"
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTextCursor>
#include <QTextBlock>
#include <QRegularExpression>
#include <QMenu>
#include <QStackedWidget>
#include <QClipboard>
#include <QGuiApplication>

EditorPanel::EditorPanel(TabModel *model, QWidget *parent)
    : QWidget(parent), m_model(model), m_tabs(new TabBar(model, this)),
      m_edit(new CodeEditor(this)) {
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_edit);

    m_emptyOverlay = new QWidget(this);
    m_emptyOverlay->setObjectName("EmptyOverlay");
    auto *olay = new QVBoxLayout(m_emptyOverlay);
    olay->setContentsMargins(0, 0, 0, 0);
    olay->setAlignment(Qt::AlignCenter);

    auto *noFileLbl = new QLabel("No file", m_emptyOverlay);
    noFileLbl->setObjectName("NoFileTitle");
    noFileLbl->setAlignment(Qt::AlignCenter);

    olay->addWidget(noFileLbl);

    m_stack->addWidget(m_emptyOverlay);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_tabs);
    lay->addWidget(m_stack, 1);

    // 세로 띠로 접혔을 때의 모습은 세 패널이 공유한다.
    m_bar = new CollapsedBar("INER", this);
    m_bar->hide();
    lay->addWidget(m_bar, 1);
    connect(m_bar, &CollapsedBar::clicked, this, &EditorPanel::collapseRequested);

    connect(m_model, &TabModel::activeChanged, this, [this](int) { loadActive(); });
    connect(m_edit, &CodeEditor::textChanged, this, [this] {
        if (m_loading) return;
        m_model->setActiveContent(m_edit->toPlainText());
        emit contentChanged();
    });
    connect(m_edit, &CodeEditor::cursorPosChanged, this, &EditorPanel::cursorPos);

    connect(m_tabs, &TabBar::tabClicked, m_model, &TabModel::activate);
    // 저장 확인은 TabModel::close 안에서 한 번만 한다 (Ctrl+W / 메뉴 / × 모두 같은 길).
    connect(m_tabs, &TabBar::tabCloseRequested, m_model, [this](int i) { m_model->close(i); });
    connect(m_tabs, &TabBar::newTabRequested, this, [this] { m_model->newTab(); });
    connect(m_tabs, &TabBar::contextMenuRequested, this, [this](int idx, QPoint pos) {
        QMenu menu(this);
        auto *close = menu.addAction("Close");
        auto *closeOthers = menu.addAction("Close Others");
        menu.addSeparator();
        auto *closeAll = menu.addAction("Close All");
        auto *copyPath = menu.addAction("Copy Path");
        QAction *sel = menu.exec(pos);
        if (sel == close) m_model->close(idx);
        else if (sel == closeOthers) m_model->closeOthers(idx);
        else if (sel == closeAll) {
            if (m_model->confirmDiscardAll(this)) { m_model->closeAll(); }
        } else if (sel == copyPath) {
            if (Tab *t = m_model->tabAt(idx))
                QGuiApplication::clipboard()->setText(t->path.isEmpty() ? t->name : t->path);
        }
    });
    connect(m_tabs, &TabBar::detachClicked, this, &EditorPanel::detachRequested);
    connect(m_tabs, &TabBar::panelCollapseClicked, this, &EditorPanel::collapseRequested);

    loadActive();
}

void EditorPanel::setDetached(bool on) { m_tabs->setDetached(on); }

void EditorPanel::setCollapsed(bool on, bool horizontal) {
    const bool strip = on && horizontal;
    m_stack->setVisible(!on);
    m_tabs->setVisible(!strip);
    m_bar->setVisible(strip);
    m_tabs->setCollapsed(on);
}

void EditorPanel::loadActive() {
    const int idx = m_model->activeIndex();
    // 탭을 옮기기 전에 있던 자리를 기억해 둔다 (돌아오면 그 자리로).
    if (m_lastIndex != idx)
        if (Tab *prev = m_model->tabAt(m_lastIndex))
            prev->cursor = m_edit->textCursor().position();

    Tab *t = m_model->activeTab();
    // 탭이 하나도 없을 때 편집칸이 살아 있으면 타이핑이 어디에도 안 남는다.
    m_edit->setReadOnly(t == nullptr);
    if (!t) {
        m_loading = true;
        m_edit->clear();
        m_loading = false;
        m_lastIndex = -1;
        m_edit->updateLineNumberAreaWidth(0);
        m_stack->setCurrentWidget(m_emptyOverlay);
        return;
    }

    m_stack->setCurrentWidget(m_edit);

    m_loading = true;
    if (m_edit->toPlainText() != t->content) m_edit->setPlainText(t->content);
    QTextCursor c = m_edit->textCursor();
    c.setPosition(qBound(0, t->cursor, m_edit->document()->characterCount() - 1));
    m_edit->setTextCursor(c);
    m_loading = false;
    m_edit->updateLineNumberAreaWidth(0);
    m_edit->ensureCursorVisible();
    m_edit->setFocus();
    m_lastIndex = idx;
}

void EditorPanel::refreshTheme() {
    m_edit->highlighter()->refreshTheme();
    m_edit->highlightCurrentLine();
    update();
}

void EditorPanel::newTab() { m_model->newTab(); loadActive(); }

// 커서 자리부터 찾고, 끝에 닿으면 처음으로 돌아가서 한 번 더 찾는다.
bool EditorPanel::findFrom(const QString &term, bool backward) {
    if (term.isEmpty()) return false;
    QTextDocument::FindFlags fl;
    if (backward) fl |= QTextDocument::FindBackward;
    if (m_edit->find(term, fl)) return true;
    QTextCursor keep = m_edit->textCursor();
    m_edit->moveCursor(backward ? QTextCursor::End : QTextCursor::Start);
    if (m_edit->find(term, fl)) return true;
    m_edit->setTextCursor(keep);   // 한 군데도 없으면 있던 자리를 지킨다
    return false;
}

void EditorPanel::triggerFind() {
    // 고른 글자가 있으면 그걸 기본값으로 (VS Code 와 같은 습관).
    QString preset = m_edit->textCursor().selectedText();
    if (preset.contains(QChar(0x2029))) preset.clear();   // 여러 줄 선택은 무시
    if (preset.isEmpty()) preset = m_lastFind;
    bool ok;
    QString term = QInputDialog::getText(this, "Find", "Find (F3 = next):",
                                         QLineEdit::Normal, preset, &ok);
    if (!ok || term.isEmpty()) return;
    m_lastFind = term;
    if (!findFrom(term, false)) emit status("Not found: " + term);
}

void EditorPanel::findNext(bool backward) {
    if (m_lastFind.isEmpty()) { triggerFind(); return; }
    if (!findFrom(m_lastFind, backward)) emit status("Not found: " + m_lastFind);
}

void EditorPanel::triggerReplace() {
    QDialog d(this);
    d.setWindowTitle("Replace");
    auto *l = new QVBoxLayout(&d);
    auto *findL = new QLineEdit(&d);
    auto *replL = new QLineEdit(&d);
    findL->setPlaceholderText("Find");
    findL->setText(m_lastFind);
    replL->setPlaceholderText("Replace with");
    auto *btnRow = new QHBoxLayout;
    auto *replaceOne = new QPushButton("Replace", &d);
    auto *replaceAll = new QPushButton("Replace All", &d);
    auto *cancel = new QPushButton("Close", &d);
    btnRow->addWidget(replaceOne);
    btnRow->addWidget(replaceAll);
    btnRow->addStretch();
    btnRow->addWidget(cancel);
    l->addWidget(findL);
    l->addWidget(replL);
    l->addLayout(btnRow);

    QObject::connect(replaceOne, &QPushButton::clicked, &d, [&] {
        const QString term = findL->text();
        if (term.isEmpty()) return;
        m_lastFind = term;
        QTextCursor c = m_edit->textCursor();
        if (c.hasSelection() && c.selectedText() == term) c.insertText(replL->text());
        findFrom(term, false);
    });
    QObject::connect(replaceAll, &QPushButton::clicked, &d, [&] {
        const QString term = findL->text();
        // 빈 문자열을 바꾸면 글자 사이마다 끼워 넣어 문서를 망가뜨린다.
        if (term.isEmpty()) return;
        m_lastFind = term;
        // setPlainText 는 되돌리기/커서/스크롤을 통째로 날린다 — 커서로 고친다.
        QTextCursor c(m_edit->document());
        c.beginEditBlock();
        int n = 0;
        c.movePosition(QTextCursor::Start);
        while (true) {
            QTextCursor hit = m_edit->document()->find(term, c);
            if (hit.isNull()) break;
            hit.insertText(replL->text());
            c = hit;
            ++n;
        }
        c.endEditBlock();
        emit status(QString("Replaced %1 occurrence(s).").arg(n));
    });
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::accept);
    d.exec();
}

void EditorPanel::goToLine() {
    bool ok;
    const int total = m_edit->blockCount();
    int line = QInputDialog::getInt(this, "Go to Line",
                                    QString("Line number (1-%1):").arg(total),
                                    m_edit->textCursor().blockNumber() + 1, 1, total, 1, &ok);
    if (!ok) return;
    int target = qMin(line, m_edit->blockCount());
    QTextBlock b = m_edit->document()->findBlockByNumber(target - 1);
    if (b.isValid()) {
        QTextCursor c(b);
        m_edit->setTextCursor(c);
        m_edit->centerCursor();
        m_edit->setFocus();
    }
}

// Lumi 주석은 `//` 하나뿐. 고른 줄이 전부 주석이면 벗기고, 아니면 전부 붙인다.
void EditorPanel::commentToggle() {
    QTextCursor c = m_edit->textCursor();
    QTextDocument *doc = m_edit->document();
    int endPos = c.selectionEnd();
    QTextBlock first = doc->findBlock(c.selectionStart());
    QTextBlock last = doc->findBlock(endPos);
    if (last != first && endPos == last.position()) last = last.previous();
    const int firstNo = first.blockNumber(), lastNo = last.blockNumber();

    // 빈 줄은 세지 않는다 — 빈 줄 하나 때문에 "주석 아님"이 되면 안 된다.
    bool allCommented = true, anyText = false;
    for (int n = firstNo; n <= lastNo; ++n) {
        const QString t = doc->findBlockByNumber(n).text().trimmed();
        if (t.isEmpty()) continue;
        anyText = true;
        if (!t.startsWith("//")) { allCommented = false; break; }
    }
    if (!anyText) allCommented = false;

    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (int n = firstNo; n <= lastNo; ++n) {
        QTextBlock b = doc->findBlockByNumber(n);
        if (!b.isValid()) continue;
        const QString line = b.text();
        if (allCommented) {
            int at = line.indexOf("//");
            if (at < 0) continue;
            int len = (line.mid(at + 2, 1) == " ") ? 3 : 2;   // 붙일 때 넣은 한 칸도 같이
            QTextCursor lc(b);
            lc.setPosition(b.position() + at);
            lc.setPosition(b.position() + at + len, QTextCursor::KeepAnchor);
            lc.removeSelectedText();
        } else {
            if (line.trimmed().isEmpty() && firstNo != lastNo) continue;
            QTextCursor lc(b);
            lc.movePosition(QTextCursor::StartOfBlock);
            lc.insertText("// ");
        }
    }
    edit.endEditBlock();
}
