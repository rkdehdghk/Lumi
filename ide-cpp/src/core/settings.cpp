#include "settings.h"
#include "hostenv.h"
#include "models/tabmodel.h"
#include <QSettings>
#include <QMainWindow>
#include <QGuiApplication>
#include <QScreen>
#include <QMargins>

// %APPDATA%/Lumina/Lumina.ini — main.cpp 에서 IniFormat 을 기본으로 지정.

namespace {
    QStringList pushRecent(QStringList list, const QString &path, int cap = 12) {
        list.removeAll(path);
        list.prepend(path);
        while (list.size() > cap) list.removeLast();
        return list;
    }
}

namespace settings {

bool loadDarkTheme() { return QSettings().value("theme", "dark").toString() != "light"; }
void saveDarkTheme(bool dark) { QSettings().setValue("theme", dark ? "dark" : "light"); }

int editorFontPt() { return QSettings().value("editorFontSize", 10).toInt(); }
void setEditorFontPt(int pt) { QSettings().setValue("editorFontSize", pt); }

void saveWindowState(QMainWindow *w) {
    if (!w) return;
    QSettings st;
    st.setValue("window/geometry", w->saveGeometry());
    st.setValue("window/state", w->saveState());
}

// 저장해 둔 크기가 지금 화면보다 크면(모니터를 바꿨거나 배율이 달라졌을 때) 창의
// 오른쪽/아래가 화면 밖으로 나가 헤더 버튼과 상태줄이 잘려 보인다. 화면 안으로 되돌린다.
// 최대화/전체화면은 이미 화면에 맞춰져 있으므로 건드리지 않는다.
void clampToScreen(QWidget *w) {
    if (!w || w->isMaximized() || w->isFullScreen()) return;
    QScreen *scr = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    if (!scr) return;
    const QRect fr = w->frameGeometry(), cr = w->geometry();
    // 창틀 두께만큼 좁힌 자리가 내용이 실제로 놓일 수 있는 범위다.
    const QRect avail = scr->availableGeometry().marginsRemoved(
        QMargins(cr.left() - fr.left(), cr.top() - fr.top(),
                 fr.right() - cr.right(), fr.bottom() - cr.bottom()));
    if (avail.width() <= 0 || avail.height() <= 0) return;

    QRect g = cr;
    g.setSize(g.size().boundedTo(avail.size()));
    if (g.right()  > avail.right())  g.moveRight(avail.right());
    if (g.bottom() > avail.bottom()) g.moveBottom(avail.bottom());
    if (g.left()   < avail.left())   g.moveLeft(avail.left());
    if (g.top()    < avail.top())    g.moveTop(avail.top());
    if (g != cr) w->setGeometry(g);
}

void restoreWindowState(QMainWindow *w) {
    if (!w) return;
    QSettings st;
    // 저장된 크기가 없으면(첫 실행) 최대화로 시작한다. 있으면 지난번 크기를 존중하되
    // 화면 밖으로 나간 크기는 여기서 잘라 낸다.
    if (st.contains("window/geometry")
        && w->restoreGeometry(st.value("window/geometry").toByteArray()))
        clampToScreen(w);
    else
        w->setWindowState(w->windowState() | Qt::WindowMaximized);
    if (st.contains("window/state")) w->restoreState(st.value("window/state").toByteArray());
}

QString defaultTerminalDir() {
    return QSettings().value("term/defaultDir", hostDefaultTermDir()).toString();
}

void setDefaultTerminalDir(const QString &dir) {
    QSettings().setValue("term/defaultDir", dir);
}

QString loadActiveFolder() { return QSettings().value("session/activeFolder").toString(); }
void saveActiveFolder(const QString &folder) { QSettings().setValue("session/activeFolder", folder); }

QStringList loadOpenTabs() { return QSettings().value("tabs/open").toStringList(); }
int loadActiveTab() { return QSettings().value("tabs/active", 0).toInt(); }

void saveTabs(TabModel *m) {
    if (!m) {
        QSettings st;
        st.remove("tabs/open");
        st.remove("tabs/active");
        return;
    }
    QStringList paths;
    int active = 0;
    for (int i = 0; i < m->count(); ++i) {
        const Tab &t = m->tabs()[i];
        if (t.path.isEmpty()) continue;
        if (i == m->activeIndex()) active = paths.size();
        paths << t.path;
    }
    QSettings st;
    st.setValue("tabs/open", paths);
    st.setValue("tabs/active", active);
}

QStringList recentFiles() { return QSettings().value("recent/files").toStringList(); }
void addRecentFile(const QString &path) {
    QSettings st;
    st.setValue("recent/files", pushRecent(st.value("recent/files").toStringList(), path));
}
QStringList recentFolders() { return QSettings().value("recent/folders").toStringList(); }
void addRecentFolder(const QString &path) {
    QSettings st;
    st.setValue("recent/folders", pushRecent(st.value("recent/folders").toStringList(), path));
}

int sidebarWidth() { return QSettings().value("layout/sidebarWidth", 240).toInt(); }
void setSidebarWidth(int w) { QSettings().setValue("layout/sidebarWidth", w); }
int outputHeight() { return QSettings().value("layout/outputHeight", 240).toInt(); }
void setOutputHeight(int h) { QSettings().setValue("layout/outputHeight", h); }

bool loadOuterVisible() { return QSettings().value("layout/outerVisible", true).toBool(); }
void saveOuterVisible(bool visible) { QSettings().setValue("layout/outerVisible", visible); }

static QString collapseKey(const QString &name, const char *what) {
    return "layout/collapse/" + name + "/" + what;
}
bool collapsed(const QString &name) {
    return QSettings().value(collapseKey(name, "on"), false).toBool();
}
int collapsedSize(const QString &name) {
    return QSettings().value(collapseKey(name, "size"), 0).toInt();
}
bool collapsedHoriz(const QString &name) {
    return QSettings().value(collapseKey(name, "horiz"), false).toBool();
}
void setCollapse(const QString &name, bool on, int size, bool horiz) {
    QSettings st;
    st.setValue(collapseKey(name, "on"), on);
    st.setValue(collapseKey(name, "size"), size);
    st.setValue(collapseKey(name, "horiz"), horiz);
}

}   // namespace settings
