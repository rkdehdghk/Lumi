#include "mainwindow.h"
#include "core/commands.h"
#include "core/paths.h"
#include "core/hostenv.h"
#include "core/settings.h"
#include "models/tabmodel.h"
#include "models/runsession.h"
#include "panels/editorpanel.h"
#include "panels/outputpanel.h"
#include "widgets/fileexplorer.h"
#include "widgets/codeeditor.h"
#include "widgets/commandpalette.h"
#include "theme/theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QIcon>
#include <QTimer>
#include <QHBoxLayout>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Lumina");
    resize(1180, 760);

    // 중앙 위젯 앵커 설정: 모든 도크 패널이 분리/이동되더라도 QMainWindowLayout 의 레이아웃 트리가
    // 파손되지 않아 파란색 드롭 프리뷰 박스(Rubberband)가 유지되고 튕김 현상을 완전히 방지한다.
    auto *dummyCentral = new QWidget(this);
    dummyCentral->setMaximumSize(0, 0);
    dummyCentral->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setCentralWidget(dummyCentral);

    m_tabs = new TabModel(this);
    m_tabs->setOwner(this);       // 저장 확인/Save As 대화상자의 부모
    m_run = new RunSession(this);
    m_iner = new EditorPanel(m_tabs, this);
    m_outer = new OutputPanel(m_run, this);
    m_tree = new FileExplorer(this);

    buildDocks();
    buildMenus();
    buildStatusBar();

    connect(m_iner, &EditorPanel::cursorPos, this, [this](int l, int c) {
        m_stPos->setText(QString("Ln %1, Col %2").arg(l).arg(c));
    });
    connect(m_iner, &EditorPanel::detachRequested, this, [this] { toggleFloat(m_dockIner); });
    connect(m_iner, &EditorPanel::collapseRequested, this, [this] { toggleCollapse(m_dockIner); });
    connect(m_iner->editor(), &CodeEditor::filesDropped, this, [this](const QStringList &ps) {
        for (const QString &p : ps) {
            if (QFileInfo(p).isDir()) { openFolder(p); continue; }
            m_tabs->openPath(p);
            settings::addRecentFile(p);
        }
    });
    connect(m_outer, &OutputPanel::detachRequested, this, [this] { toggleFloat(m_dockOuter); });
    connect(m_outer, &OutputPanel::collapseRequested, this, [this] { toggleCollapse(m_dockOuter); });
    connect(m_outer, &OutputPanel::errorLineDetected, this, [this](int line) {
        m_iner->editor()->setErrorLine(line);
        setStatus(QString("Error on line %1").arg(line));
    });
    connect(m_outer, &OutputPanel::runningChanged, this, [this](bool running) {
        m_actRun->setEnabled(!running);
        m_actStop->setEnabled(running);
        setStatus(running ? "Running..." : "Ready");
    });
    connect(m_tabs, &TabModel::tabsChanged, this, &MainWindow::updateTitle);
    connect(m_tabs, &TabModel::status, this, &MainWindow::setStatus);
    connect(m_iner, &EditorPanel::status, this, &MainWindow::setStatus);
    connect(m_tree, &FileExplorer::fileOpened, this, [this](const QString &p) {
        m_tabs->openPath(p);
        settings::addRecentFile(p);
    });
    // 탐색기에서 파일 이름을 바꾸거나 지우면 열려 있는 탭도 따라가야 한다. 안 그러면
    // 지운 파일의 탭이 그대로 남아 Ctrl+S 한 번에 그 파일이 되살아난다.
    connect(m_tree, &FileExplorer::pathRenamed, m_tabs, &TabModel::pathRenamed);
    connect(m_tree, &FileExplorer::pathRemoved, m_tabs, &TabModel::pathRemoved);
    connect(m_tree, &FileExplorer::detachRequested, this, [this] { toggleFloat(m_dockTree); });
    connect(m_tree, &FileExplorer::collapseRequested, this, [this] { toggleCollapse(m_dockTree); });
    connect(m_tree, &FileExplorer::openFolderRequested, this, [this] { runCommand(Cmd_OpenFolder); });

    restoreSession();
    processCommandLineArgs();
    updateTitle();
    m_actStop->setEnabled(false);
}

// ------------------------------------------------------------------ 레이아웃

void MainWindow::buildDocks() {
    // 중앙 위젯 없이 도크만 쓴다: 세 패널 모두 setFloating 으로 분리되며,
    // 어디든(왼쪽/오른쪽/위/아래, 가로/세로 중첩 분할, 탭 결합) 자유롭게 드래그앤드롭 도킹할 수 있다.
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);

    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto mkDock = [this](const QString &title, QWidget *w) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(title);            // saveState/restoreState 는 이름으로 매칭
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                       | QDockWidget::DockWidgetClosable);   // 분리된 창의 × 도 먹히게
        d->setAllowedAreas(Qt::AllDockWidgetAreas);   // 어디든(가로/세로/상하좌우/탭) 자유롭게 분할 및 도킹 가능

        // 창이 분리(Floating)되면 Qt 의 도크 타이틀바(nullptr)를 써야 창을 옮기고 닫을 수
        // 있고, 도킹된 동안은 패널마다 제 헤더가 있으니 타이틀바를 숨긴다.
        // 감출 위젯은 도크마다 하나만 만들어 돌려 쓴다 — 예전엔 떼었다 붙일 때마다 새로
        // 만들어서, 붙였다 뗀 횟수만큼 빈 위젯이 도크에 쌓였다.
        auto *blank = new QWidget(d);
        auto updateTitleBar = [d, blank](bool floating) {
            d->setTitleBarWidget(floating ? nullptr : blank);
            blank->setVisible(!floating);
        };
        updateTitleBar(d->isFloating());
        d->installEventFilter(this);   // 분리된 창의 × → 닫지 말고 제자리로

        // 드래그 도중에 타이틀바를 갈아 끼우면 Qt 가 잡고 있던 위젯이 사라진다 — 한 박자 늦춘다.
        connect(d, &QDockWidget::topLevelChanged, d, [d, updateTitleBar](bool floating) {
            QMetaObject::invokeMethod(d, [updateTitleBar, floating]() {
                updateTitleBar(floating);
            }, Qt::QueuedConnection);
        });
        return d;
    };

    m_dockTree = mkDock("Explorer", m_tree);
    m_dockIner = mkDock("INER", m_iner);
    m_dockOuter = mkDock("OUTER", m_outer);

    // 세 패널을 서로 다른 도크 영역(왼쪽/위/아래)에 두면, 영역과 영역 사이의 경계선은
    // "중앙 위젯"하고만 폭을 주고받는다. 이 창은 중앙 위젯이 없어서 내어줄 상대가 없었고,
    // 그래서 탐색기 경계선을 끌어도 폭이 그대로였다 (사이드바를 껐다 켜야 반영).
    // 셋을 한 영역 안에 나눠 넣으면 경계선이 이웃 패널과 직접 크기를 주고받는다.
    addDockWidget(Qt::LeftDockWidgetArea, m_dockTree);
    splitDockWidget(m_dockTree, m_dockIner, Qt::Horizontal);
    splitDockWidget(m_dockIner, m_dockOuter, Qt::Vertical);

    // 접기 상태는 여기 한 곳에만 적어 둔다 (헤더에 있는 Panel 표).
    m_panels[0] = {m_dockTree,  "tree",
                   [this](bool on, bool h) { m_tree->setCollapsed(on, h); },  0, false, true};
    m_panels[1] = {m_dockIner,  "iner",
                   [this](bool on, bool h) { m_iner->setCollapsed(on, h); },  0, false, false};
    m_panels[2] = {m_dockOuter, "outer",
                   [this](bool on, bool h) { m_outer->setCollapsed(on, h); }, 0, false, false};

    connect(m_dockIner, &QDockWidget::topLevelChanged, this,
            [this](bool f) { m_iner->setDetached(f); });
    connect(m_dockOuter, &QDockWidget::topLevelChanged, this,
            [this](bool f) { m_outer->setDetached(f); });
    connect(m_dockTree, &QDockWidget::topLevelChanged, this,
            [this](bool f) { m_tree->setDetached(f); });

    // 드래그로 떼어냈을 때도(버튼을 안 눌러도) 접힘이 풀리고 화면 안에 들어오게 한다.
    for (Panel &p : m_panels) {
        QDockWidget *d = p.dock;
        connect(d, &QDockWidget::topLevelChanged, this, [this, d](bool floating) {
            if (!floating) return;
            expandDock(d);
            settings::clampToScreen(d);
        });
    }

    resizeDocks({m_dockTree}, {settings::sidebarWidth()}, Qt::Horizontal);
    resizeDocks({m_dockIner, m_dockOuter},
                {760 - settings::outputHeight(), settings::outputHeight()}, Qt::Vertical);
}

// 접기는 숨김이 아니라 "접힘"이다: 도크는 그대로 두고 헤더 줄 높이(가로 접기면 버튼
// 하나 폭)로 눌러 둔다. 다시 누르면 접기 전 크기로 되돌린다.
//
// 되돌릴 때 resizeDocks 만 쓰면 요청한 크기를 그대로 주지 않는다(옆 도크 사정에 따라
// 조금 깎인다). 그 깎인 값을 다음 접기 때 또 저장하니 접었다 펼 때마다 패널이
// 야금야금 작아졌다. 그래서 펼 때 잠깐 "고정 크기"로 못박아 정확히 되돌린 뒤,
// 다음 이벤트 루프에서 제약만 풀어 준다 (크기는 그대로 남고 다시 드래그로 조절 가능).
// 이웃 패널이 좌우에 붙어 있으면 가로 배치 → 가로로(폭을) 접고, 위아래에 붙어 있으면
// 세로 배치 → 세로로(높이를) 접는다. 맞닿은 변이 더 긴 쪽을 배치 방향으로 본다.
// 이웃이 없으면(혼자거나 분리된 창) 긴 변 쪽을 접는다.
bool MainWindow::collapsesHorizontally(QDockWidget *d) const {
    const QRect r = d->geometry();
    int side = 0, stack = 0;
    for (QDockWidget *o : {m_dockTree, m_dockIner, m_dockOuter}) {
        if (o == d || !o->isVisible() || o->isFloating() || d->isFloating()) continue;
        const QRect g = o->geometry();
        const int yOverlap = qMin(r.bottom(), g.bottom()) - qMax(r.top(), g.top());
        const int xOverlap = qMin(r.right(), g.right()) - qMax(r.left(), g.left());
        if (yOverlap > 0 && xOverlap <= 0) side = qMax(side, yOverlap);
        if (xOverlap > 0 && yOverlap <= 0) stack = qMax(stack, xOverlap);
    }
    if (side != stack) return side > stack;
    return r.height() > r.width();
}

// 접기가 걸어 둔 고정 크기를 푼다. 접은 방향과 편 방향이 다를 수 있으니 늘 양쪽 다 푼다 —
// 예전엔 접은 축만 풀어서, 방향이 바뀐 뒤 펴면 반대 축에 고정 크기가 남아 도크가 굳었다.
static void freeDockSize(QDockWidget *d) {
    d->setMinimumSize(0, 0);
    d->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
}

MainWindow::Panel *MainWindow::panelFor(QDockWidget *d) {
    for (Panel &p : m_panels) if (p.dock == d) return &p;
    return nullptr;
}

void MainWindow::toggleCollapse(QDockWidget *d) {
    Panel *p = panelFor(d);
    if (!p) return;
    const int kBar = 34;

    p->collapsed = !p->collapsed;
    if (p->collapsed) {
        p->horiz = collapsesHorizontally(d);
        // 접기 전 크기는 접힌 상태에서 다시 읽지 않는다 — 한 번만 기록.
        const int cur = p->horiz ? d->width() : d->height();
        if (cur > kBar) p->saved = cur;
        if (p->horiz) d->setFixedWidth(kBar);
        else          d->setFixedHeight(kBar);
    } else {
        // 되돌릴 때 resizeDocks 만 쓰면 요청한 크기를 그대로 주지 않는다(옆 도크 사정에
        // 따라 조금 깎인다). 그 깎인 값을 다음 접기 때 또 저장하니 접었다 펼 때마다
        // 패널이 야금야금 작아졌다. 그래서 잠깐 고정 크기로 못박아 정확히 되돌린 뒤,
        // 다음 이벤트 루프에서 제약만 풀어 준다.
        const int target = qMax(p->saved, 160);
        if (p->horiz) d->setFixedWidth(target);
        else          d->setFixedHeight(target);
        QTimer::singleShot(0, d, [d] { freeDockSize(d); });
    }
    p->apply(p->collapsed, p->horiz);
}

// 접혀 있으면 편다. 분리하기 전 / 레이아웃 초기화 / 종료 저장 전에 모두 이 길로 지나간다.
// (이벤트 루프를 기다리지 않고 고정 크기를 바로 풀기 때문에 종료 직전에도 안전하다.)
void MainWindow::expandDock(QDockWidget *d) {
    Panel *p = panelFor(d);
    if (!p || !p->collapsed) return;
    p->collapsed = false;
    freeDockSize(d);
    p->apply(false, p->horiz);
    if (!d->isFloating())
        resizeDocks({d}, {qMax(p->saved, 160)}, p->horiz ? Qt::Horizontal : Qt::Vertical);
}

// 분리해 둔 패널의 × 를 누르면 도크가 통째로 숨어 버려서, INER 을 떼어 놓고 닫으면
// 편집기가 사라지고 View 메뉴를 뒤져야 되찾을 수 있었다. × 는 "제자리로 되돌리기"로 쓴다.
bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::Close) {
        auto *d = qobject_cast<QDockWidget *>(o);
        if (d && d->isFloating() && panelFor(d)) {
            d->setFloating(false);
            e->ignore();
            return true;
        }
    }
    return QMainWindow::eventFilter(o, e);
}

// 숨겼다 다시 켤 때: 분리된 창이면 화면 밖으로 나가 있을 수 있으니 안으로 되돌리고
// 앞으로 끌어 온다 (안 그러면 "켰는데 아무 일도 없다"로 보인다).
void MainWindow::toggleDock(QDockWidget *d) {
    const bool show = !d->isVisible();
    d->setVisible(show);
    if (show && d->isFloating()) {
        settings::clampToScreen(d);
        d->raise();
    }
}

void MainWindow::toggleFloat(QDockWidget *d) {
    const bool wantFloat = !d->isFloating();
    // 접힌 채로 떼면 34px 짜리 창이 떠서 손댈 데가 없었다 — 떼기 전에 먼저 편다.
    if (wantFloat) expandDock(d);
    // 숨겨 둔 패널을 떼면 보이지 않는 창이 떠 다닌다.
    if (!d->isVisible()) d->show();

    d->setFloating(wantFloat);
    if (!wantFloat) return;

    d->resize(qMin(900, qMax(480, width() - 120)), qMin(600, qMax(320, height() - 120)));
    // 도킹돼 있던 자리에 그대로 뜨면 본창에 가려 안 보인다 — 본창 가운데로 옮기고,
    // 화면 밖으로 나가면 안으로 되돌린다.
    d->move(frameGeometry().center() - QPoint(d->width() / 2, d->height() / 2));
    settings::clampToScreen(d);
    d->raise();
    d->activateWindow();
}

// -------------------------------------------------------------------- 메뉴

QAction *MainWindow::addCmd(QMenu *menu, const QString &text, int id, const QKeySequence &key) {
    QAction *a = menu->addAction(text);
    if (!key.isEmpty()) {
        a->setShortcut(key);
        a->setShortcutContext(Qt::ApplicationShortcut);   // 분리된 패널에서도 동작
    }
    connect(a, &QAction::triggered, this, [this, id] { runCommand(id); });
    addAction(a);
    return a;
}

void MainWindow::buildMenus() {
    QMenu *file = menuBar()->addMenu("&File");
    addCmd(file, "New Tab", Cmd_New, QKeySequence("Ctrl+N"));
    addCmd(file, "New File...", Cmd_NewFile);
    addCmd(file, "New Folder...", Cmd_NewFolder);
    file->addSeparator();
    addCmd(file, "Open File...", Cmd_Open, QKeySequence("Ctrl+O"));
    addCmd(file, "Open Folder...", Cmd_OpenFolder, QKeySequence("Ctrl+K, Ctrl+O"));
    addCmd(file, "Close Folder", Cmd_CloseFolder, QKeySequence("Ctrl+K, F"));
    // 최근 목록을 저장만 하고 아무 데서도 못 열었다.
    m_recentMenu = file->addMenu("Open Recent");
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addSeparator();
    addCmd(file, "Save", Cmd_Save, QKeySequence("Ctrl+S"));
    addCmd(file, "Save As...", Cmd_SaveAs, QKeySequence("Ctrl+Shift+S"));
    file->addSeparator();
    addCmd(file, "Close Tab", Cmd_CloseTab, QKeySequence("Ctrl+W"));
    addCmd(file, "Close Others", Cmd_CloseOthers);
    addCmd(file, "Close All", Cmd_CloseAll);
    file->addSeparator();
    addCmd(file, "Exit", Cmd_Quit, QKeySequence("Alt+F4"));

    QMenu *edit = menuBar()->addMenu("&Edit");
    addCmd(edit, "Find...", Cmd_Find, QKeySequence("Ctrl+F"));
    addCmd(edit, "Find Next", Cmd_FindNext, QKeySequence("F3"));
    addCmd(edit, "Find Previous", Cmd_FindPrev, QKeySequence("Shift+F3"));
    addCmd(edit, "Replace...", Cmd_Replace, QKeySequence("Ctrl+H"));
    addCmd(edit, "Go to Line...", Cmd_GoToLine, QKeySequence("Ctrl+G"));
    addCmd(edit, "Toggle Comment", Cmd_Comment, QKeySequence("Ctrl+/"));

    QMenu *view = menuBar()->addMenu("&View");
    addCmd(view, "Toggle Theme", Cmd_ThemeToggle);
    addCmd(view, "Explore", Cmd_ToggleSidebar, QKeySequence("Ctrl+B"));
    addCmd(view, "Toggle INER", Cmd_ToggleIner);
    addCmd(view, "Toggle OUTER", Cmd_TogglePanel, QKeySequence("Ctrl+`"));
    view->addSeparator();
    addCmd(view, "Detach / Dock INER", Cmd_DetachIner);
    addCmd(view, "Detach / Dock OUTER", Cmd_DetachOuter);
    addCmd(view, "Detach / Dock Explorer", Cmd_DetachTree);
    addCmd(view, "Reset Window Layout", Cmd_ResetLayout);
    view->addSeparator();
    // Ctrl++ 는 Shift 를 눌러야 나오는 자판이 많다 — Ctrl+= 도 같이 받는다.
    addCmd(view, "Zoom In", Cmd_ZoomIn, QKeySequence("Ctrl++"))
        ->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    addCmd(view, "Zoom Out", Cmd_ZoomOut, QKeySequence("Ctrl+-"));
    addCmd(view, "Reset Zoom", Cmd_ZoomReset, QKeySequence("Ctrl+0"));
    view->addSeparator();
    addCmd(view, "Quick Open...", Cmd_QuickFiles, QKeySequence("Ctrl+P"));
    addCmd(view, "Command Palette...", Cmd_QuickCmds, QKeySequence("Ctrl+Shift+P"));
    addCmd(view, "Next Tab", Cmd_NextTab, QKeySequence("Ctrl+Tab"));
    addCmd(view, "Previous Tab", Cmd_PrevTab, QKeySequence("Ctrl+Shift+Tab"));

    QMenu *outerMenu = menuBar()->addMenu("&OUTER");
    addCmd(outerMenu, "Set Default Terminal Path...", Cmd_SetDefaultTerminalDir);

    QAction *actRun = menuBar()->addAction(QString(QChar(0x25B7)) + " RUN (F5)");
    actRun->setShortcut(QKeySequence("F5"));
    actRun->setShortcutContext(Qt::ApplicationShortcut);
    connect(actRun, &QAction::triggered, this, [this] { runCommand(Cmd_Run); });
    m_actRun = actRun;

    QAction *actStop = menuBar()->addAction(QString(QChar(0x25A1)) + " STOP (Shift+F5)");
    actStop->setShortcut(QKeySequence("Shift+F5"));
    actStop->setShortcutContext(Qt::ApplicationShortcut);
    connect(actStop, &QAction::triggered, this, [this] { runCommand(Cmd_Stop); });
    m_actStop = actStop;

    QMenu *help = menuBar()->addMenu("&Help");
    QAction *about = help->addAction("About Lumina");
    connect(about, &QAction::triggered, this, [this] {
        // 상태줄 한 줄만 바뀌면 눌러도 아무 일 없는 것처럼 보인다.
        QMessageBox::about(this, "About Lumina",
            "<b>Lumina</b> - the Lumi language workbench.<br>Qt6 build.<br><br>"
            "Interpreter: " + QDir::toNativeSeparators(interpreterPath()));
    });
}

void MainWindow::buildStatusBar() {
    m_stMsg = new QLabel("Ready", this);
    m_stPos = new QLabel("Ln 1, Col 1", this);
    m_stIndent = new QLabel("Spaces: 4", this);
    m_stLang = new QLabel("Lumi", this);
    m_stTheme = new QLabel(isDark() ? "Dark" : "Light", this);
    statusBar()->addWidget(m_stMsg, 1);
    statusBar()->addPermanentWidget(m_stTheme);
    statusBar()->addPermanentWidget(m_stPos);
    statusBar()->addPermanentWidget(m_stIndent);
    statusBar()->addPermanentWidget(m_stLang);
}

void MainWindow::setStatus(const QString &msg) { m_stMsg->setText(msg); }

void MainWindow::rebuildRecentMenu() {
    m_recentMenu->clear();
    const QStringList folders = settings::recentFolders();
    const QStringList files = settings::recentFiles();
    if (folders.isEmpty() && files.isEmpty()) {
        m_recentMenu->addAction("(nothing yet)")->setEnabled(false);
        return;
    }
    for (const QString &f : files) {
        if (!QFileInfo::exists(f)) continue;
        connect(m_recentMenu->addAction(QDir::toNativeSeparators(f)), &QAction::triggered,
                this, [this, f] { m_tabs->openPath(f); });
    }
    if (!folders.isEmpty()) {
        m_recentMenu->addSeparator();
        for (const QString &d : folders) {
            if (!QFileInfo(d).isDir()) continue;
            connect(m_recentMenu->addAction("Folder: " + QDir::toNativeSeparators(d)),
                    &QAction::triggered, this, [this, d] {
                if (m_outer->busy()) { setStatus("Stop the running program first."); return; }
                openFolder(d);
            });
        }
    }
}

void MainWindow::updateTitle() {
    Tab *t = m_tabs->activeTab();
    QString name = t ? t->name : QString("Lumina");
    QString folder = m_folder.isEmpty() ? QString() : " [" + QDir(m_folder).dirName() + "]";
    setWindowTitle((t && t->modified ? "* " : "") + name + folder + " - Lumina");
}

// ------------------------------------------------------------------- 커맨드

void MainWindow::runCommand(int id) {
    switch (id) {
        // New Tab 은 디스크에 파일을 만들지 않는다 — 빈 Untitled 탭이다 (폴더가 없어도
        // 된다). 실행할 때 Cmd_Run 이 Save As 로 저장을 받아낸다.
        case Cmd_New: m_tabs->newTab(); break;

        case Cmd_NewFile:
        case Cmd_NewFolder: {
            if (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()) {
                auto res = QMessageBox::question(this, "Open Folder Required",
                                                 "No folder is currently open. You must open a folder first before creating a file.\nWould you like to open a folder now?",
                                                 QMessageBox::Yes | QMessageBox::No);
                if (res == QMessageBox::Yes) {
                    runCommand(Cmd_OpenFolder);
                }
                break;
            }
            if (id == Cmd_NewFile) m_tree->newFile();
            else m_tree->newFolder();
            break;
        }
        case Cmd_Rename: m_tree->renameSelected(); break;
        case Cmd_Delete: m_tree->deleteSelected(); break;

        case Cmd_Open: {
            QString p = QFileDialog::getOpenFileName(this, "Open File", m_folder,
                                                     "Lumi (*.lumi);;All Files (*.*)");
            if (!p.isEmpty()) { m_tabs->openPath(p); settings::addRecentFile(p); }
            break;
        }
        case Cmd_OpenFolder: {
            // 버그#11: 실행 중에 cwd 를 바꾸면 터미널이 stale 해진다 — 조용히 무시하지 않고 알린다.
            if (m_outer->busy()) { setStatus("Stop the running program first."); break; }
            QString d = QFileDialog::getExistingDirectory(this, "Open Folder", m_folder);
            if (!d.isEmpty()) openFolder(d);
            break;
        }
        case Cmd_SetDefaultTerminalDir: {
            QString dir = QFileDialog::getExistingDirectory(this, "Select Default Terminal Path", settings::defaultTerminalDir());
            if (!dir.isEmpty()) {
                settings::setDefaultTerminalDir(dir);
                if (m_folder.isEmpty()) m_outer->setCwd(dir);
            }
            break;
        }
        case Cmd_CloseFolder: {
            if (m_outer->busy()) { setStatus("Stop the running program first."); break; }
            closeFolder();
            break;
        }
        case Cmd_Save:
            if (m_tabs->save(m_tabs->activeIndex(), false, this)) {
                setStatus("Saved.");
                Tab *t = m_tabs->activeTab();
                if (t && !t->path.isEmpty()) settings::addRecentFile(t->path);
            }
            break;
        case Cmd_SaveAs:
            if (m_tabs->save(m_tabs->activeIndex(), true, this)) setStatus("Saved.");
            break;

        case Cmd_Run: {
            Tab *t = m_tabs->activeTab();
            if (!t) {
                QMessageBox::information(this, "No File", "No active file to run. Please open or create a file first.");
                break;
            }
            // 디스크에 저장된 실체 파일이 없는 경우 저장을 유도 (Save As)
            if (t->path.isEmpty() || !QFileInfo::exists(t->path)) {
                QMessageBox::information(this, "Save Required", "You must save and create a file on disk before running it.");
                if (!m_tabs->save(m_tabs->activeIndex(), true, this)) {
                    setStatus("Run cancelled: File must be created on disk to execute.");
                    break;
                }
            }
            // 수정된 사항이 있으면 저장
            if (t->modified) {
                if (!m_tabs->save(m_tabs->activeIndex(), false, this)) {
                    setStatus("Run cancelled: Could not save file changes.");
                    break;
                }
            }
            if (t->path.isEmpty() || !QFileInfo::exists(t->path)) break;
            m_iner->editor()->setErrorLine(-1);
            QString base = QFileInfo(t->path).absolutePath();
            QString fileName = QFileInfo(t->path).fileName();
            m_outer->runCode(fileName, base);
            break;
        }
        case Cmd_Stop: m_outer->stopRun(); break;
        case Cmd_Clear: m_outer->clearCurrent(); break;
        case Cmd_Refresh: m_tree->setFolder(m_folder); break;

        case Cmd_ThemeToggle: applyThemeAll(!isDark()); break;
        case Cmd_ToggleSidebar: toggleDock(m_dockTree); break;
        case Cmd_TogglePanel: toggleDock(m_dockOuter); break;
        case Cmd_ToggleIner: toggleDock(m_dockIner); break;
        case Cmd_DetachIner: toggleFloat(m_dockIner); break;
        case Cmd_DetachOuter: toggleFloat(m_dockOuter); break;
        case Cmd_DetachTree: toggleFloat(m_dockTree); break;
        case Cmd_ResetLayout: resetToDefaultLayout(); break;

        case Cmd_CloseTab: m_tabs->close(m_tabs->activeIndex()); break;
        case Cmd_CloseOthers: m_tabs->closeOthers(m_tabs->activeIndex()); break;
        case Cmd_CloseAll:
            if (m_tabs->confirmDiscardAll(this)) { m_tabs->closeAll(); }
            break;
        case Cmd_NextTab:
            if (m_tabs->count() > 0)
                m_tabs->activate((m_tabs->activeIndex() + 1) % m_tabs->count());
            break;
        case Cmd_PrevTab:
            if (m_tabs->count() > 0)
                m_tabs->activate((m_tabs->activeIndex() + m_tabs->count() - 1) % m_tabs->count());
            break;

        case Cmd_Find: m_iner->triggerFind(); break;
        case Cmd_FindNext: m_iner->findNext(false); break;
        case Cmd_FindPrev: m_iner->findNext(true); break;
        case Cmd_Replace: m_iner->triggerReplace(); break;
        case Cmd_GoToLine: m_iner->goToLine(); break;
        case Cmd_Comment: m_iner->commentToggle(); break;

        case Cmd_ZoomIn: m_iner->editor()->zoomBy(1); break;
        case Cmd_ZoomOut: m_iner->editor()->zoomBy(-1); break;
        case Cmd_ZoomReset: m_iner->editor()->setFontPointSize(10); break;

        case Cmd_QuickFiles: {
            CommandPalette pal(CommandPalette::Files, this);
            pal.setFileList(m_tree->collectFiles(), m_folder);
            if (pal.exec() == QDialog::Accepted && !pal.selectedText().isEmpty()) {
                m_tabs->openPath(pal.selectedText());
                settings::addRecentFile(pal.selectedText());
            }
            break;
        }
        case Cmd_QuickCmds: {
            CommandPalette pal(CommandPalette::Commands, this);
            if (pal.exec() == QDialog::Accepted && pal.selectedCommand())
                runCommand(pal.selectedCommand());
            break;
        }
        case Cmd_Quit: close(); break;
        default: break;
    }
}

void MainWindow::applyThemeAll(bool dark) {
    applyTheme(qApp, dark);
    m_iner->refreshTheme();
    m_outer->refreshTheme();
    m_stTheme->setText(dark ? "Dark" : "Light");
    settings::saveDarkTheme(dark);
}

void MainWindow::openFolder(const QString &folder) {
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) return;
    m_folder = QDir(folder).absolutePath();
    m_tree->setFolder(m_folder);
    m_outer->setCwd(m_folder);
    m_tabs->setDefaultDir(m_folder);   // Save As 가 이 폴더에서 열리도록
    settings::addRecentFolder(m_folder);
    setStatus("Folder: " + QDir::toNativeSeparators(m_folder));
    updateTitle();
}

void MainWindow::closeFolder() {
    m_folder.clear();
    m_tree->setFolder(QString());
    m_outer->setCwd(settings::defaultTerminalDir());
    m_tabs->setDefaultDir(QString());
    setStatus("Folder closed.");
    updateTitle();
}

// ------------------------------------------------------------------- 세션

// lumi 실행 파일이 Lumina 옆에 있으면 "패키지 배포" 상태 — 그때는 exe 폴더가 아니라
// 그 한 단계 위(프로젝트 폴더)를 연다. (기존 Win32 IDE 와 같은 규칙)
static QString defaultFolder() {
    QDir dir(QCoreApplication::applicationDirPath());
    if (QFileInfo::exists(dir.absoluteFilePath(hostExeName("lumi")))) {
        QDir up = dir;
        if (up.cdUp()) return up.absolutePath();
    }
    return dir.absolutePath();
}

void MainWindow::resetToDefaultLayout() {
    // 접힌 채로 초기화하면 고정 크기가 남는다 — 전부 펴 두고 시작한다.
    for (Panel &p : m_panels) { expandDock(p.dock); freeDockSize(p.dock); }

    m_dockTree->setFloating(false);
    m_dockIner->setFloating(false);
    m_dockOuter->setFloating(false);

    m_dockTree->show();
    m_dockIner->show();
    if (settings::loadOuterVisible()) {
        m_dockOuter->show();
    } else {
        m_dockOuter->hide();
    }

    addDockWidget(Qt::LeftDockWidgetArea, m_dockTree);
    splitDockWidget(m_dockTree, m_dockIner, Qt::Horizontal);
    splitDockWidget(m_dockIner, m_dockOuter, Qt::Vertical);

    resizeDocks({m_dockTree}, {240}, Qt::Horizontal);
    resizeDocks({m_dockIner, m_dockOuter}, {520, 240}, Qt::Vertical);

    // 기본 레이아웃 = 최대화 (시작할 때와 같은 모습). setWindowState 라서 아직 안 뜬
    // 창을 여기서 먼저 보여 버리지 않는다.
    setWindowState(windowState() | Qt::WindowMaximized);
}

void MainWindow::restoreSession() {
    QString folder = settings::loadActiveFolder();
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
        openFolder(folder);
    } else {
        closeFolder();
    }

    const QStringList open = settings::loadOpenTabs();
    for (const QString &p : open)
        if (QFileInfo::exists(p)) m_tabs->openPath(p);
    int act = settings::loadActiveTab();
    if (act >= 0 && act < m_tabs->count()) m_tabs->activate(act);

    m_iner->editor()->setFontPointSize(settings::editorFontPt());
    connect(m_iner->editor(), &CodeEditor::fontSizeChanged, this, &settings::setEditorFontPt);

    settings::restoreWindowState(this);

    // 접어 둔 채로 껐다면 도크는 34px 로 복원된다 — 프로그램도 "접혀 있다"고 알고 있어야
    // ▾/띠를 눌러 원래 크기로 되돌릴 수 있다. (예전엔 펼친 줄 알고 헤더를 다 그려서
    // 납작한 패널 안에서 내용이 잘려 보였다.)
    for (Panel &p : m_panels) {
        p.saved = settings::collapsedSize(p.key);
        if (!settings::collapsed(p.key)) continue;
        p.collapsed = true;
        p.horiz = settings::collapsedHoriz(p.key);
        if (p.horiz) p.dock->setFixedWidth(34);
        else         p.dock->setFixedHeight(34);
        p.apply(true, p.horiz);
    }
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!m_tabs->confirmDiscardAll(this)) { e->ignore(); return; }
    settings::saveActiveFolder(m_folder);
    settings::saveOuterVisible(m_dockOuter->isVisible() && !m_dockOuter->isHidden());
    settings::saveTabs(m_tabs);
    // 접힘 여부와 펼쳤을 때 크기를 같이 저장한다 — 접힌 34px 만 저장하면 다음에 켤 때
    // 되돌릴 크기를 잃는다.
    for (Panel &p : m_panels) settings::setCollapse(p.key, p.collapsed, p.saved, p.horiz);
    settings::saveWindowState(this);
    // 접어 둔 패널의 width()/height() 는 34 다 — 그대로 저장하면 다음에 켤 때
    // 사이드바/출력창이 납작하게 살아난다.
    if (m_dockTree->isVisible() && m_dockTree->width() > 40)
        settings::setSidebarWidth(m_dockTree->width());
    if (m_dockOuter->isVisible() && m_dockOuter->height() > 40)
        settings::setOutputHeight(m_dockOuter->height());
    settings::saveDarkTheme(isDark());
    m_run->stopAll();
    e->accept();
}

void MainWindow::processCommandLineArgs() {
    const QStringList args = QCoreApplication::arguments();
    if (args.size() <= 1) return;

    QString folderToOpen;
    QStringList filesToOpen;

    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg.startsWith("-")) continue;
        QFileInfo fi(arg);
        if (fi.isFile()) {
            if (folderToOpen.isEmpty()) folderToOpen = fi.absolutePath();
            filesToOpen << fi.absoluteFilePath();
        } else if (fi.isDir()) {
            // 폴더를 줬으면 그 폴더를 연다 (absolutePath() 는 그 위 폴더다).
            if (folderToOpen.isEmpty()) folderToOpen = fi.absoluteFilePath();
        }
    }

    if (!folderToOpen.isEmpty()) {
        openFolder(folderToOpen);
    }
    for (const QString &filePath : filesToOpen) {
        m_tabs->openPath(filePath);
        settings::addRecentFile(filePath);
    }
}
