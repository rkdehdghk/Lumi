#include "theme.h"
#include <QPalette>

static Palette makeDark() {
    // 깔끔한 무채색 검회색 (Neutral Dark Gray)
    Palette p;
    p.bg = "#121212"; p.surface = "#1e1e1e"; p.surfaceAlt = "#252526"; p.border = "#333333";
    p.text = "#cccccc"; p.textDim = "#858585";
    p.accent = "#007acc"; p.accentText = "#ffffff";
    p.error = "#f48771"; p.errorBg = "#3d2222"; p.success = "#89d185"; p.info = "#6796e6";
    p.editorBg = p.surface; p.editorFg = p.text;
    p.termBg = p.surface; p.termFg = p.text; p.termInfo = p.info;
    p.termInput = p.text; p.termPrompt = p.accent;
    p.tabbarBg = p.bg; p.tabActive = p.surface; p.tabInactive = p.bg;
    p.tabFg = p.text; p.tabFgDim = p.textDim;
    p.sidebarBg = p.bg; p.sidebarFg = p.text;
    p.sidebarSel = QColor(p.accent); p.sidebarSel.setAlpha(60);
    p.btnBg = p.surfaceAlt; p.btnHover = p.border; p.btnFg = p.text;
    p.ghostFg = p.textDim; p.ghostHover = p.surfaceAlt;
    p.statusBg = p.bg; p.statusFg = p.textDim;
    p.line = p.border; p.focus = p.accent;
    p.synKeyword = "#cba6f7"; p.synString = "#a6e3a1"; p.synNumber = "#fab387";
    p.synComment = "#6c7086"; p.synBuiltin = "#f9e2af";
    return p;
}

static Palette makeLight() {
    // 깔끔한 neutral
    Palette p;
    p.bg = "#fafafa"; p.surface = "#ffffff"; p.surfaceAlt = "#f4f4f5"; p.border = "#e4e4e7";
    p.text = "#18181b"; p.textDim = "#71717a";
    p.accent = "#2563eb"; p.accentText = "#ffffff";
    p.error = "#dc2626"; p.errorBg = "#fee2e2"; p.success = "#16a34a"; p.info = "#0891b2";
    p.editorBg = p.surface; p.editorFg = p.text;
    p.termBg = p.surface; p.termFg = p.text; p.termInfo = p.info;
    p.termInput = p.text; p.termPrompt = p.accent;
    p.tabbarBg = p.surfaceAlt; p.tabActive = p.surface; p.tabInactive = p.surfaceAlt;
    p.tabFg = p.text; p.tabFgDim = p.textDim;
    p.sidebarBg = p.surfaceAlt; p.sidebarFg = p.text;
    p.sidebarSel = QColor(p.accent); p.sidebarSel.setAlpha(40);
    p.btnBg = p.surfaceAlt; p.btnHover = p.border; p.btnFg = p.text;
    p.ghostFg = p.textDim; p.ghostHover = p.surfaceAlt;
    p.statusBg = p.surfaceAlt; p.statusFg = p.textDim;
    p.line = p.border; p.focus = p.accent;
    p.synKeyword = "#7c3aed"; p.synString = "#16a34a"; p.synNumber = "#ea580c";
    p.synComment = "#a1a1aa"; p.synBuiltin = "#ca8a04";
    return p;
}

static bool g_dark = true;
static Palette g_palette = makeDark();

const Palette &currentPalette() { return g_palette; }
bool isDark() { return g_dark; }

static QString qssFor(const Palette &p) {
    return QString(
        "QWidget { background: %1; color: %2; }"
        "QMenuBar { background: %1; }"
        "QMenuBar::item { padding: 4px 8px; }"
        "QMenuBar::item:selected { background: %3; color: %4; }"
        "QMenu { background: %5; border: 1px solid %6; }"
        "QMenu::item { padding: 5px 24px; }"
        "QMenu::item:selected { background: %3; color: %4; }"
        "QToolBar { background: %1; border: none; border-bottom: 1px solid %6; spacing: 2px; padding: 2px; }"
        "QToolBar QToolButton { padding: 5px 8px; border-radius: 6px; }"
        "QToolBar QToolButton:hover { background: %7; }"
        "QStatusBar { background: %8; color: %9; }"
        "QStatusBar::item { border: none; }"
        // 상태줄 라벨은 여백 없이 창 맨 오른쪽 끝에 딱 붙어 잘린 것처럼 보였다.
        "QStatusBar QLabel { padding: 0px 5px; }"
        "QPushButton { background: %10; color: %11; border: 1px solid %6; padding: 5px 12px; border-radius: 6px; }"
        "QPushButton:hover { background: %7; }"
        "QPushButton:pressed { background: %6; }"
        "QPushButton:checked { background: %3; color: %4; border-color: %3; }"
        "QLineEdit, QPlainTextEdit, QTextEdit { background: %5; color: %2; border: 1px solid %6; border-radius: 6px; padding: 3px; selection-background-color: %3; selection-color: %4; }"
        // QListView 까지 잡아야 자동완성 팝업(QCompleter)도 같은 옷을 입는다.
        "QListWidget, QListView { background: %5; border: 1px solid %6; border-radius: 6px; }"
        "QListWidget::item, QListView::item { padding: 4px 8px; }"
        "QListWidget::item:selected, QListView::item:selected { background: %3; color: %4; }"
        "QTreeView { background: %12; border: none; outline: none; }"
        "QTreeView::item { padding: 2px; color: %2; background: transparent; }"
        "QTreeView::item:hover { background: %7; }"
        "QTreeView::item:selected { background: rgba(%13,%14,%15,%16); color: %2; }"
        "QHeaderView::section { background: %8; border: none; padding: 3px; }"
        "QScrollBar:vertical { background: %1; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %6; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: %7; }"
        "QScrollBar:horizontal { background: %1; height: 10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %6; border-radius: 4px; min-width: 20px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }"
        "QSplitter::handle { background: %6; }"
        "QSplitter::handle:horizontal { width: 1px; }"
        "QSplitter::handle:vertical { height: 1px; }"
        // 분리된 창의 타이틀바는 그 창을 옮기고 닫는 유일한 손잡이다. 예전엔 아이콘을
        // none 으로 지워 버려서 닫기/도킹 버튼이 안 보이는 채로 자리만 차지했다.
        "QDockWidget { color: %2; }"
        "QDockWidget::title { background: %8; padding: 5px 8px; border-bottom: 1px solid %6; text-align: left; }"
        "QDockWidget::close-button, QDockWidget::float-button { border: none; border-radius: 4px; padding: 0px; }"
        "QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: %7; }"
        "QLabel { background: transparent; }"
        "QLabel#PanelTitle { background: transparent; color: %9; font-weight: bold; }"
        "QFrame#HeaderFrame { background: %8; border-bottom: 1px solid %6; }"
        // OUTER 탭은 INER 탭바(TabBar::paintEvent)와 같은 규칙으로 칠한다:
        // 비활성 tabInactive / 활성 tabActive + 상단 2px accent, 호버 surfaceAlt, × 는 빨강.
        "QWidget#OuterTab { background: %18; border-top: 2px solid transparent; }"
        "QWidget#OuterTab[active=\"true\"] { background: %19; border-top: 2px solid %3; }"
        "QPushButton#OuterTabName { background: transparent; color: %17; border: none; border-radius: 0px; padding: 0px 6px 0px 10px; font-weight: normal; text-align: left; }"
        "QPushButton#OuterTabName:hover { background: %20; }"
        "QPushButton#OuterTabName:checked { background: transparent; color: %2; font-weight: normal; }"
        "QPushButton#OuterTabClose { background: transparent; color: %17; border: none; border-radius: 4px; padding: 0px; margin: 0px 6px 0px 0px; min-width: 18px; max-width: 18px; }"
        "QPushButton#OuterTabClose:hover { background: #e53935; color: #ffffff; }"
        "QPushButton#HeaderToolBtn { background: transparent; color: %17; border: none; border-radius: 4px; padding: 3px 8px; font-size: 12px; font-weight: normal; }"
        "QPushButton#HeaderToolBtn:hover { background: %7; color: %3; }"
        // 접기 화살표만 크게 (헤더 높이 30px 은 고정이라 줄이 두꺼워지지는 않는다)
        "QPushButton#HeaderChevronBtn { background: transparent; color: %17; border: none; border-radius: 4px; padding: 0px 6px; font-size: 20px; font-weight: bold; }"
        "QPushButton#HeaderChevronBtn:hover { background: %7; color: %3; }"
        "QLabel#NoFileTitle { color: %9; font-size: 16px; font-weight: bold; background: transparent; }"
        "QPushButton#NewTabBtn { background: %10; color: %11; border: 1px solid %6; border-radius: 4px; padding: 6px 16px; font-size: 13px; font-weight: 500; }"
        "QPushButton#NewTabBtn:hover { background: %7; border-color: %3; color: %2; }"
    ).arg(p.bg.name(),                  // 1
          p.text.name(),                // 2
          p.accent.name(),              // 3
          p.accentText.name(),          // 4
          p.surface.name(),             // 5
          p.border.name(),              // 6
          p.btnHover.name(),            // 7
          p.statusBg.name(),            // 8
          p.statusFg.name(),            // 9
          p.btnBg.name(),               // 10
          p.btnFg.name(),               // 11
          p.sidebarBg.name(),           // 12
          // .name() 은 알파를 버린다 — 선택색은 rgba() 로 넘겨야 반투명이 산다.
          QString::number(p.sidebarSel.red()),    // 13
          QString::number(p.sidebarSel.green()),  // 14
          QString::number(p.sidebarSel.blue()),   // 15
          QString::number(p.sidebarSel.alpha()),  // 16
          p.tabFgDim.name(),            // 17
          p.tabInactive.name(),         // 18
          p.tabActive.name(),           // 19
          p.surfaceAlt.name());         // 20
}

void applyTheme(QApplication *app, bool dark) {
    g_dark = dark;
    g_palette = dark ? makeDark() : makeLight();

    QPalette pal = app->palette();
    pal.setColor(QPalette::Window, g_palette.bg);
    pal.setColor(QPalette::WindowText, g_palette.text);
    pal.setColor(QPalette::Base, g_palette.surface);
    pal.setColor(QPalette::AlternateBase, g_palette.surfaceAlt);
    pal.setColor(QPalette::Text, g_palette.text);
    pal.setColor(QPalette::Button, g_palette.btnBg);
    pal.setColor(QPalette::ButtonText, g_palette.btnFg);
    pal.setColor(QPalette::Highlight, g_palette.accent);
    pal.setColor(QPalette::HighlightedText, g_palette.accentText);
    pal.setColor(QPalette::ToolTipBase, g_palette.surfaceAlt);
    pal.setColor(QPalette::ToolTipText, g_palette.text);
    pal.setColor(QPalette::PlaceholderText, g_palette.textDim);
    app->setPalette(pal);
    app->setStyleSheet(qssFor(g_palette));
}

void toggleTheme(QApplication *app) { applyTheme(app, !g_dark); }

QColor tokColor(TokKind k) {
    const Palette &p = currentPalette();
    switch (k) {
        case TK_KEYWORD: return p.synKeyword;
        case TK_STRING:  return p.synString;
        case TK_NUMBER:  return p.synNumber;
        case TK_COMMENT: return p.synComment;
        case TK_BUILTIN: return p.synBuiltin;
        default:         return p.editorFg;
    }
}
bool tokBold(TokKind k) { return k == TK_KEYWORD || k == TK_BUILTIN; }
