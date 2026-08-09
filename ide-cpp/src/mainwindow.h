#pragma once
#include <QMainWindow>
#include <QString>
#include <functional>

class TabModel;
class RunSession;
class EditorPanel;
class OutputPanel;
class FileExplorer;
class QDockWidget;
class QLabel;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 메뉴/툴바/단축키/팔레트가 모두 통과하는 단일 디스패치 (기존 runCommand 유지).
    void runCommand(int id);

protected:
    void closeEvent(QCloseEvent *e) override;
    // 분리된 패널의 × 를 "닫기"가 아니라 "제자리로 되돌리기"로 쓴다.
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    TabModel *m_tabs;
    RunSession *m_run;
    EditorPanel *m_iner;
    OutputPanel *m_outer;
    FileExplorer *m_tree;
    QDockWidget *m_dockIner;
    QDockWidget *m_dockOuter;
    QDockWidget *m_dockTree;

    QLabel *m_stPos;
    QLabel *m_stMsg;
    QLabel *m_stTheme;
    QLabel *m_stIndent;
    QLabel *m_stLang;
    QAction *m_actRun;
    QAction *m_actStop;
    class QMenu *m_recentMenu = nullptr;

    QString m_folder;

    void buildDocks();
    void buildMenus();
    void buildStatusBar();
    QAction *addCmd(class QMenu *menu, const QString &text, int id,
                    const QKeySequence &key = {});
    void rebuildRecentMenu();
    void applyThemeAll(bool dark);
    void openFolder(const QString &folder);
    void closeFolder();
    void setStatus(const QString &msg);
    void updateTitle();
    void toggleFloat(QDockWidget *d);
    void toggleDock(QDockWidget *d);   // 켜고 끄기 (분리된 창이면 화면 안으로 되돌린다)

    // 세 패널(Explorer / INER / OUTER)의 접기 상태를 한 곳에 모아 둔다. 예전엔 saved /
    // collapsed / horiz 가 패널마다 따로 있어서, 분리·레이아웃 초기화·종료 저장 중
    // 한 곳이라도 빠지면 고정 크기가 그대로 남아 패널이 납작해졌다.
    struct Panel {
        QDockWidget *dock = nullptr;
        QString key;                              // 설정에 저장할 이름
        std::function<void(bool, bool)> apply;    // 패널 위젯에 접힌 모습 알리기
        int saved = 0;                            // 펼쳤을 때 크기
        bool collapsed = false;
        bool horiz = false;                       // 가로(폭)로 접혔나
    };
    Panel m_panels[3];
    Panel *panelFor(QDockWidget *d);

    // 패널 접기: 도크를 없애지 않고 헤더 줄만 남긴다.
    // 접히는 방향은 지금 이웃과 어떻게 놓여 있는지(가로 배치 ↔ 세로 배치)를 보고 정한다.
    void toggleCollapse(QDockWidget *d);
    void expandDock(QDockWidget *d);      // 접혀 있으면 편다 (분리/초기화/종료 전 공통)
    bool collapsesHorizontally(QDockWidget *d) const;
    void resetToDefaultLayout();
    void restoreSession();
    void processCommandLineArgs();
};
