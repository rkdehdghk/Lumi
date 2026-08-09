#pragma once
#include <QString>
#include <QStringList>

class QMainWindow;
class QWidget;
class TabModel;

namespace settings {
    // 테마
    bool loadDarkTheme();
    void saveDarkTheme(bool dark);

    // 폰트
    int editorFontPt();
    void setEditorFontPt(int pt);

    QString defaultTerminalDir();
    void setDefaultTerminalDir(const QString &dir);

    // 창 상태
    void saveWindowState(QMainWindow *w);
    void restoreWindowState(QMainWindow *w);
    // 창(본창이든 분리된 패널이든)이 화면 밖으로 나가면 안으로 되돌린다.
    void clampToScreen(QWidget *w);

    // 탭 및 활성 폴더
    QString loadActiveFolder();
    void saveActiveFolder(const QString &folder);
    QStringList loadOpenTabs();
    int loadActiveTab();
    void saveTabs(TabModel *m);

    // 최근
    QStringList recentFiles();
    void addRecentFile(const QString &path);
    QStringList recentFolders();
    void addRecentFolder(const QString &path);

    // 레이아웃
    int sidebarWidth();
    void setSidebarWidth(int w);
    int outputHeight();
    void setOutputHeight(int h);
    bool loadOuterVisible();
    void saveOuterVisible(bool visible);

    // 패널 접기 상태 (name = "tree" / "iner" / "outer").
    // 접어 둔 채로 껐다 켜면 도크가 34px 로 되살아나는데 프로그램은 펼친 줄 알았다 —
    // 그래서 접힘 여부·펼쳤을 때 크기·접힌 방향을 같이 저장한다.
    bool collapsed(const QString &name);
    int collapsedSize(const QString &name);
    bool collapsedHoriz(const QString &name);
    void setCollapse(const QString &name, bool on, int size, bool horiz);
}
