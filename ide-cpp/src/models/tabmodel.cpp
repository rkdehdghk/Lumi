#include "tabmodel.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>

static QString baseName(const QString &p) {
    int at = p.lastIndexOf('/');
    if (at < 0) at = p.lastIndexOf('\\');
    return at < 0 ? p : p.mid(at + 1);
}

TabModel::TabModel(QObject *parent) : QObject(parent) {}

Tab *TabModel::activeTab() { return tabAt(m_active); }

Tab *TabModel::tabAt(int i) {
    return (i >= 0 && i < (int)m_tabs.size()) ? &m_tabs[i] : nullptr;
}

int TabModel::findByPath(const QString &path) const {
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        if (m_tabs[i].path == path) return i;
    return -1;
}

void TabModel::openPath(const QString &path) {
    int idx = findByPath(path);
    if (idx >= 0) { activate(idx); return; }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 조용히 실패하면 "왜 안 열리지" 로 끝난다 — 이유를 보여준다.
        emit status("Cannot open " + baseName(path) + ": " + f.errorString());
        return;
    }
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    QString content = s.readAll();
    if (content.startsWith(QChar(0xFEFF))) content.remove(0, 1);  // BOM

    Tab t;
    t.id = m_nextId++;
    t.path = path;
    t.name = baseName(path);
    t.content = content;
    m_tabs.push_back(t);
    activate((int)m_tabs.size() - 1);
}

QString TabModel::nextUntitledName() const {
    int num = 1;
    while (true) {
        QString candidate = QString("Untitled-%1").arg(num);
        bool exists = false;
        for (const auto &t : m_tabs) {
            if (t.path.isEmpty() && t.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) return candidate;
        ++num;
    }
}

int TabModel::newTab(const QString &content) {
    Tab t;
    t.id = m_nextId++;
    t.name = nextUntitledName();
    t.content = content.isEmpty() ? QString("print(\"hello, Lumi!\")\n") : content;
    m_tabs.push_back(t);
    activate((int)m_tabs.size() - 1);
    return (int)m_tabs.size() - 1;
}

void TabModel::activate(int i) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
    if (m_active == i) return;
    m_active = i;
    emit activeChanged(i);
    emit contentChanged();
    emit tabsChanged();
}

// 안 저장한 탭을 말없이 버리지 않는다. 닫는 길이 여러 개(×, Ctrl+W, 메뉴,
// 팔레트)라 확인은 여기 한 군데에만 둔다.
bool TabModel::confirmClose(int i) {
    Tab *t = tabAt(i);
    if (!t || !t->modified) return true;
    auto r = QMessageBox::question(m_owner, "Unsaved Changes",
        QString("'%1' has unsaved changes. Save before closing?").arg(t->name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save(i, false, m_owner);
    return true;
}

void TabModel::close(int i, bool confirm) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
    if (confirm && !confirmClose(i)) return;
    m_tabs.erase(m_tabs.begin() + i);
    // 앞쪽 탭이 사라지면 뒤 탭들이 한 칸씩 당겨진다 — 활성 번호도 같이 당겨야
    // 엉뚱한 탭이 활성으로 보이지 않는다.
    if (i < m_active) --m_active;
    if (m_active >= (int)m_tabs.size()) m_active = (int)m_tabs.size() - 1;
    emit activeChanged(m_active);
    emit contentChanged();
    emit tabsChanged();
}

void TabModel::closeOthers(int i) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
    for (int k = (int)m_tabs.size() - 1; k >= 0; --k) {
        if (k == i) continue;
        if (!confirmClose(k)) return;
    }
    Tab keep = m_tabs[i];
    m_tabs.clear();
    m_tabs.push_back(keep);
    m_active = 0;
    emit activeChanged(0);
    emit contentChanged();
    emit tabsChanged();
}

void TabModel::closeAll() {
    m_tabs.clear();
    m_active = -1;
    // activeChanged 를 안 보내면 편집칸이 마지막 파일 글자를 그대로 들고 남는다
    // (게다가 거기 친 글자는 m_active < 0 이라 어디에도 안 담긴다).
    emit activeChanged(-1);
    emit contentChanged();
    emit tabsChanged();
}

bool TabModel::save(int i, bool saveAs, QWidget *parent) {
    if (i < 0 || i >= (int)m_tabs.size()) return false;
    Tab &t = m_tabs[i];
    QString path = t.path;
    if (path.isEmpty() || saveAs) {
        // 기본 폴더가 없으면 대화상자가 프로세스 cwd 에서 열려 엉뚱한 데 저장된다.
        QString start = !t.path.isEmpty() ? t.path
                      : (m_defaultDir.isEmpty() ? t.name : QDir(m_defaultDir).filePath(t.name));
        path = QFileDialog::getSaveFileName(parent ? parent : m_owner, "Save As", start,
                                             "Lumi (*.lumi);;All Files (*.*)");
        if (path.isEmpty()) return false;
        if (!QFileInfo(path).fileName().contains('.')) path += ".lumi";
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent ? parent : m_owner, "Save Failed",
                             "Cannot write " + path + "\n" + f.errorString());
        return false;
    }
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    s << t.content;     // 활성 탭은 textChanged 로 항상 최신 동기화됨
    f.close();
    t.path = path;
    t.name = baseName(path);
    t.modified = false;
    emit tabsChanged();
    return true;
}

void TabModel::setActiveContent(const QString &text) {
    if (m_active < 0) return;
    if (m_tabs[m_active].content == text) return;
    m_tabs[m_active].content = text;
    m_tabs[m_active].modified = true;
    emit tabsChanged();
}

void TabModel::setModified(int i, bool m) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
    m_tabs[i].modified = m;
    emit tabsChanged();
}

void TabModel::pathRenamed(const QString &from, const QString &to) {
    for (auto &t : m_tabs)
        if (t.path == from) { t.path = to; t.name = baseName(to); }
    emit tabsChanged();
}

void TabModel::pathRemoved(const QString &path) {
    // 파일이 이미 지워졌으니 "저장할까요" 를 물어봐야 소용이 없다.
    for (int i = (int)m_tabs.size() - 1; i >= 0; --i)
        if (m_tabs[i].path == path) close(i, false);
}

bool TabModel::confirmDiscardAll(QWidget *parent) {
    for (size_t k = 0; k < m_tabs.size(); ++k) {
        if (!m_tabs[k].modified) continue;
        auto r = QMessageBox::question(parent, "Unsaved Changes",
            QString("'%1' has unsaved changes. Save?").arg(m_tabs[k].name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Save) {
            if (!save((int)k, false, parent)) return false;
        }
    }
    return true;
}
