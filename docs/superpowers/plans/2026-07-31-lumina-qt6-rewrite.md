# Lumina IDE — Win32 → Qt6 재작성 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기존 Win32 Lumina IDE(`ide-cpp/`)를 Qt6 Widgets로 전면 재작성하여 잔버그를 해소하고 디자인을 업그레이드한다. 기능은 기존과 1:1로 보존한다.

**Architecture:** Qt 네이티브 관용구 — `QMainWindow`+`QDockWidget`(레이아웃/detach), `QPlainTextEdit`+`QSyntaxHighlighter`(편집기), `QProcess`(터미널/REPL), `QTreeView`+`QFileSystemModel`(탐색기), `QSettings`(영속화). 기존 `lang.cpp`/`util.cpp`의 Qt 독립적 로직은 `core/`로 이식하고, Win32 GUI 코드는 위젯으로 재작성한다.

**Tech Stack:** Qt 6.9.3 (Widgets), CMake 3.16+, Ninja, MSVC 2022 툴셋(VS18 vcvars64), C++17.

## Global Constraints

- **빌드 환경:** Qt 6.9.3 MSVC2022 64bit 설치 위치 = `C:/Qt/6.9.3/msvc2022_64`. CMake/Ninja는 VS18 번들 사용 (`C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/{CMake/bin,Ninja}`). MSVC = `C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat`.
- **UI 텍스트는 모두 영어** (버튼/라벨/탭/메뉴/상태메시지/다이얼로그). 한국어 문서 본문은 허용되지만 앱 안의 글자는 예외 없이 영어.
- **에러 메시지는 ASCII 영어** (cp949 터미널 호환).
- **C++17, UTF-8 소스** (`/utf-8`). 문자열은 `QString`/`QStringView` 사용.
- **배포:** `bin/Lumina.exe` + `windeployqt`로 묶은 Qt DLL들이 `bin/` 한 폴더에.
- **문서 동시 갱신:** IDE GUI가 바뀌면 `README.md`와 `Lumi 사용설명서.md`의 IDE 부분도 함께 갱신 (특히 "Electron" → Qt doc drift 수정). 사용설명서 상단 "마지막 갱신" 날짜도 갱신.
- **마커 프로토콜 유지:** lumi.exe와 통신하는 `\x1eINPUT\n` / `\x1eDONE\n` / `\x1eEOT\n`는 변경 없이 그대로.
- **단축키 유지:** F5(실행), Ctrl+S(저장), Ctrl+Shift+S(다른이름저장), Ctrl+N(새탭), Ctrl+O(열기), Ctrl+F(찾기), Ctrl+H(바꾸기), Ctrl+P(빠른열기), Ctrl+Shift+P(명령팔레트), Ctrl+/(주석토글), Ctrl++/-/0(줌), Ctrl+Wheel(줌), Tab/Enter 자동들여쓰기.
- 이 디렉토리는 git이 아님 → 각 태스크의 "commit" 단계는 스킵 (백업은 사용자가 거부했으므로 위험 감수).

---

## 파일 구조 (태스크 전체 맵)

생성/수정 파일과 책임:

| 파일 | 책임 | 생성 태스크 |
|---|---|---|
| `ide-cpp/CMakeLists.txt` | Qt6 발견 + exe 타겟 + AUTOMOC/AUTORCC/AUTOUIC | T2 |
| `ide-cpp/resources.qrc` | SVG 아이콘 + exe 아이콘 리소스 등록 | T10 |
| `ide-cpp/assets/icons/*.svg` | 단색 SVG 아이콘 세트 | T10 |
| `ide-cpp/assets/lumina.rc` | Windows exe 아이콘 바인딩 | T2 |
| `ide-cpp/build.bat` | CMake + MSVC + windeployqt 빌드 스크립트 | T2 |
| `ide-cpp/src/main.cpp` | QApplication 진입, High DPI, 폰트, 부트 | T3 |
| `ide-cpp/src/core/lumitokenizer.h/.cpp` | Lumi 토크나이저 (lang.cpp 이식 + identStart 버그 수정) | T4 |
| `ide-cpp/src/core/fuzzy.h/.cpp` | fuzzyRank 매처 (util.cpp 이식) | T4 |
| `ide-cpp/src/core/paths.h/.cpp` | 인터프리터 경로 해석 | T4 |
| `ide-cpp/src/core/settings.h/.cpp` | QSettings 래퍼 | T11 |
| `ide-cpp/src/theme/theme.h/.cpp` | 라이트/다크 팔레트 + QSS 생성 | T5 |
| `ide-cpp/src/theme/iconpainter.h/.cpp` | SVG → 테마 색 착색 | T10 |
| `ide-cpp/src/models/tabmodel.h/.cpp` | 탭 관리 (QObject) | T6 |
| `ide-cpp/src/models/runsession.h/.cpp` | QProcess 세션 관리 | T8 |
| `ide-cpp/src/widgets/codeeditor.h/.cpp` | QPlainTextEdit 서브클래스 | T7 |
| `ide-cpp/src/widgets/linenumberarea.h/.cpp` | 거터 영역 | T7 |
| `ide-cpp/src/widgets/lumihighlighter.h/.cpp` | QSyntaxHighlighter | T7 |
| `ide-cpp/src/widgets/tabbar.h/.cpp` | 커스텀 탭바 | T6 |
| `ide-cpp/src/widgets/consolepane.h/.cpp` | 터미널/REPL 패널 | T8 |
| `ide-cpp/src/widgets/fileexplorer.h/.cpp` | QTreeView 탐색기 | T9 |
| `ide-cpp/src/widgets/commandpalette.h/.cpp` | 명령 팔레트 | T9 |
| `ide-cpp/src/panels/editorpanel.h/.cpp` | INER (탭바+편집기) | T7 |
| `ide-cpp/src/panels/outputpanel.h/.cpp` | OUTER (터미널/REPL) | T8 |
| `ide-cpp/src/mainwindow.h/.cpp` | QMainWindow 통합 | T3, T12 |
| `README.md` | IDE 부분 Qt로 갱신 | T13 |
| `Lumi 사용설명서.md` | ch.18 IDE 카탈로그 갱신 | T13 |

---

## Task 1: 기존 소스 백업 및 작업 디렉토리 정리

**Files:**
- Move: `ide-cpp/src/*.cpp` `ide-cpp/src/*.h` `ide-cpp/src/*.rc` → `ide-cpp/src_win32_backup/`
- Modify: `ide-cpp/build.bat` (Qt용은 T2에서 재작성하므로 여기선 그대로 두거나 임시 비활성화)

사용자가 "백업 없이 진행"을 선택했으나, 이는 **되돌릴 수 없는 파괴적 작업**이고 기존 코드는 작동하는 제품(`bin/Lumina.exe` 존재)이므로, 최소한의 안전장치로 백업 폴더를 만든다. (사용자 의도는 "백업 절차로 귀찮게 하지 말라"이지 "데이터를 날려라"가 아닐 것이다. 작업 완료 후 사용자에게 백업 위치를 알리고 제거 여부를 묻는다.)

- [ ] **Step 1: 기존 src 백업**

```bash
cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp"
mkdir -p src_win32_backup
cp src/*.cpp src/*.h src/*.rc src_win32_backup/ 2>/dev/null
ls src_win32_backup/
```
Expected: `console.cpp editor.cpp explorer.cpp lang.cpp lumina.h lumina.rc main.cpp quick.cpp test_fuzzy.cpp util.cpp` 나열.

- [ ] **Step 2: 기존 bin 보호 표식**

기존 `bin/Lumina.exe`는 그대로 두어 Qt 버전이 빌드되기 전까지 실행 가능하게 유지. (나중에 같은 경로로 덮어씀.)

- [ ] **Step 3: src 청소 (백업 확인 후)**

백업이 확인되면 기존 `src/*.cpp` 삭제하여 Qt 구조로 재구성 준비:
```bash
cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp"
rm -f src/*.cpp src/*.h src/*.rc
mkdir -p src/core src/theme src/models src/widgets src/panels
```
Expected: `src/` 하위에 `core theme models widgets panels` 빈 디렉토리 생성.

---

## Task 2: CMakeLists.txt + build.bat + exe 아이콘 바인딩

**Files:**
- Create: `ide-cpp/CMakeLists.txt`
- Create: `ide-cpp/assets/lumina.rc`
- Create: `ide-cpp/build.bat`
- Create: `ide-cpp/src/main.cpp` (최소 Hello World — 빌드 검증용)

**Interfaces:**
- Produces: `bin/Lumina.exe` (Hello World, 빌드 파이프라인 검증)
- 이 태스크가 정의하는 빌드 인프라는 이후 모든 태스크가 의존.

- [ ] **Step 1: exe 아이콘 rc 파일**

기존 `Lumi&na logo/lumina_icon_TB_icon.ico` 재사용:
```bash
cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp"
mkdir -p assets
cp "../Lumi&na logo/lumina_icon_TB_icon.ico" assets/lumina.ico
```

`ide-cpp/assets/lumina.rc`:
```rc
IDI_ICON1 ICON "lumina.ico"
```

- [ ] **Step 2: CMakeLists.txt**

`ide-cpp/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(Lumina LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_WIN32_EXECUTABLE ON)

# Qt6 발견 — 환경변수 CMAKE_PREFIX_PATH 또는 Qt 설치 경로
find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

set(APP_SOURCES
    src/main.cpp
)

set(APP_RESOURCES
    resources.qrc
)

add_executable(Lumina WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
    assets/lumina.rc
)

target_include_directories(Lumina PRIVATE src)
target_link_libraries(Lumina PRIVATE Qt6::Widgets)

# Release 최적화
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
target_compile_options(Lumina PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W3 /EHsc>)

set_target_properties(Lumina PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../bin
)
```

- [ ] **Step 3: resources.qrc (최소 — 아이콘은 T10에서 추가)**

`ide-cpp/resources.qrc`:
```xml
<RCC>
    <qresource prefix="/">
    </qresource>
</RCC>
```

- [ ] **Step 4: 최소 main.cpp (빌드 검증용 Hello World)**

`ide-cpp/src/main.cpp`:
```cpp
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Lumina");
    app.setOrganizationName("Lumina");

    QWidget w;
    w.setWindowTitle("Lumina");
    w.resize(800, 600);
    auto *lay = new QVBoxLayout(&w);
    auto *lbl = new QLabel("Lumina IDE — Qt6 build pipeline OK", &w);
    lbl->setAlignment(Qt::AlignCenter);
    auto *btn = new QPushButton("Run", &w);
    lay->addWidget(lbl);
    lay->addWidget(btn);
    w.show();
    return app.exec();
}
```

- [ ] **Step 5: build.bat (CMake + MSVC + windeployqt)**

`ide-cpp/build.bat`:
```bat
@echo off
REM Build the Lumina IDE (C++ / Qt6) into bin\Lumina.exe using MSVC + CMake.
setlocal

set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS%" (
  echo [!] Visual Studio C++ build tools not found at: %VS%
  exit /b 1
)

set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "QT=C:\Qt\6.9.3\msvc2022_64"

call "%VS%" >nul

pushd "%~dp0"
if not exist bin mkdir bin

echo [1/4] CMake configure...
"%CMAKE%" -B build -S . -G "Ninja" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_PREFIX_PATH="%QT%" ^
  -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :failed

echo [2/4] Build...
"%CMAKE%" --build build --config Release
if errorlevel 1 goto :failed

echo [3/4] windeployqt...
"%QT%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler ^
    --no-opengl-sw bin\Lumina.exe
if errorlevel 1 goto :failed

echo [4/4] Done.
popd
echo.
echo Build complete: ide-cpp\bin\Lumina.exe
exit /b 0

:failed
echo.
echo [!] Build failed.
popd
exit /b 1
```

- [ ] **Step 6: 빌드 실행 및 검증**

Run: `cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp" && cmd //c build.bat`
Expected: `Build complete: ide-cpp\bin\Lumina.exe` 출력. `bin/Lumina.exe` 생성. Qt DLL들과 `bin/platforms/` 복사됨.

- [ ] **Step 7: 실행 검증**

Run: `cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp/bin" && ./Lumina.exe &` (또는 사용자가 더블클릭)
Expected: "Lumina" 제목 창이 뜨고 "Lumina IDE — Qt6 build pipeline OK" 라벨 + "Run" 버튼 표시.

빌드 파이프라인이 Qt6+CMake+Ninja+windeployqt로 작동함을 확인. (창을 닫을 때까지 블로킹이므로 백그라운드 실행 후 별도 확인.)

---

## Task 3: MainWindow 골조 + 메뉴/툴바/상태바 + 레이아웃 뼈대

**Files:**
- Create: `ide-cpp/src/mainwindow.h`
- Create: `ide-cpp/src/mainwindow.cpp`
- Modify: `ide-cpp/src/main.cpp` (Hello World → 실제 MainWindow)

**Interfaces:**
- Produces: `MainWindow` 클래스 — `runCommand(int)` 단일 디스패치 진입점, 메뉴/툴바/상태바 액션들, dock 영역(사이드바/INER/OUTER) 자리만 마련.
- Consumes: (없음 — 첫 GUI 태스크)
- Later tasks(T6-T9)가 `MainWindow`의 `setCentralWidget`/`addDockWidget` 슬롯에 위젯을 끼워넣음.

- [ ] **Step 1: mainwindow.h**

`ide-cpp/src/mainwindow.h`:
```cpp
#pragma once
#include <QMainWindow>
#include <QAction>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 단일 디스패치 — 메뉴/단축키/팔레트 모두 이것으로.
    void runCommand(int id);

    // 상태바 헬퍼
    void statusPos(int line, int col);
    void statusFlash(const QString &text);
    void statusRunning(bool running);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenu();
    void buildToolbar();
    void buildActions();
    void applyTheme();

    // actions
    QAction *m_new{nullptr}, *m_open{nullptr}, *m_save{nullptr}, *m_saveAs{nullptr};
    QAction *m_run{nullptr}, *m_stop{nullptr}, *m_clear{nullptr};
    QAction *m_theme{nullptr};
    QAction *m_detachIner{nullptr}, *m_detachOuter{nullptr};
    QAction *m_toggleSide{nullptr}, *m_togglePanel{nullptr};
    QAction *m_quit{nullptr};
};
```

명령 ID enum (`ide-cpp/src/core/commands.h` 에 별도 두어 전역 공유):
`ide-cpp/src/core/commands.h`:
```cpp
#pragma once

// Command IDs — 메뉴/단축키/팔레트 공통. 기존 IDC_* 와 대응.
enum CommandId {
    Cmd_New = 100, Cmd_Open, Cmd_Save, Cmd_SaveAs, Cmd_Run, Cmd_Stop, Cmd_Clear,
    Cmd_ThemeToggle,
    Cmd_DetachIner, Cmd_DetachOuter,
    Cmd_ToggleSidebar, Cmd_TogglePanel,
    Cmd_NewFile, Cmd_NewFolder,
    Cmd_Refresh,
    Cmd_QuickFiles, Cmd_QuickCmds,
    Cmd_NextTab, Cmd_PrevTab, Cmd_CloseTab, Cmd_CloseOthers, Cmd_CloseAll,
    Cmd_Find, Cmd_Replace, Cmd_GoToLine, Cmd_Comment,
    Cmd_ZoomIn, Cmd_ZoomOut, Cmd_ZoomReset,
    Cmd_Rename, Cmd_Delete
};
```

- [ ] **Step 2: mainwindow.cpp — 메뉴/툴바/액션/뼈대**

`ide-cpp/src/mainwindow.cpp` (핵심 부분):
```cpp
#include "mainwindow.h"
#include "core/commands.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QKeySequence>
#include <QDockWidget>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Lumina");
    resize(1180, 780);

    buildActions();
    buildMenu();
    buildToolbar();
    statusBar()->showMessage("Ready");

    // dock 영역 자리만 (위젯은 이후 태스크에서 채움)
    // TopDockWidgetArea = INER, BottomDockWidgetArea = OUTER, LeftDockWidgetArea = sidebar
}

void MainWindow::buildActions() {
    m_new = new QAction("&New", this);
    m_new->setShortcut(QKeySequence::New);            // Ctrl+N
    connect(m_new, &QAction::triggered, this, [this]{ runCommand(Cmd_New); });

    m_open = new QAction("&Open...", this);
    m_open->setShortcut(QKeySequence::Open);          // Ctrl+O
    connect(m_open, &QAction::triggered, this, [this]{ runCommand(Cmd_Open); });

    m_save = new QAction("&Save", this);
    m_save->setShortcut(QKeySequence::Save);          // Ctrl+S
    connect(m_save, &QAction::triggered, this, [this]{ runCommand(Cmd_Save); });

    m_saveAs = new QAction("Save &As...", this);
    m_saveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(m_saveAs, &QAction::triggered, this, [this]{ runCommand(Cmd_SaveAs); });

    m_run = new QAction("&Run", this);
    m_run->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_run, &QAction::triggered, this, [this]{ runCommand(Cmd_Run); });

    m_stop = new QAction("&Stop", this);
    m_stop->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    connect(m_stop, &QAction::triggered, this, [this]{ runCommand(Cmd_Stop); });

    m_theme = new QAction("&Theme", this);
    connect(m_theme, &QAction::triggered, this, [this]{ runCommand(Cmd_ThemeToggle); });

    m_quit = new QAction("&Quit", this);
    m_quit->setShortcut(QKeySequence::Quit);
    connect(m_quit, &QAction::triggered, qApp, &QApplication::quit);

    // ...나머지 액션들도 동일 패턴
}

void MainWindow::buildMenu() {
    auto *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(m_new);
    fileMenu->addAction(m_open);
    fileMenu->addSeparator();
    fileMenu->addAction(m_save);
    fileMenu->addAction(m_saveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quit);

    auto *runMenu = menuBar()->addMenu("&Run");
    runMenu->addAction(m_run);
    runMenu->addAction(m_stop);

    auto *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(m_theme);
}

void MainWindow::buildToolbar() {
    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->addAction(m_run);
    tb->addAction(m_stop);
    tb->addSeparator();
    tb->addAction(m_save);
    tb->addAction(m_open);
    tb->addSeparator();
    tb->addAction(m_theme);
}

void MainWindow::runCommand(int id) {
    switch (id) {
        case Cmd_ThemeToggle: applyTheme(); break;
        // 나머지는 위젯 연결 후 구현
        default: break;
    }
}

void MainWindow::statusFlash(const QString &t) { statusBar()->showMessage(t, 3000); }
void MainWindow::statusPos(int, int) {}
void MainWindow::statusRunning(bool) {}

void MainWindow::applyTheme() {
    // T5에서 구현
}

void MainWindow::closeEvent(QCloseEvent *e) {
    e->accept();
}
```

- [ ] **Step 3: main.cpp 갱신**

`ide-cpp/src/main.cpp`:
```cpp
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    // High DPI (Qt6 기본 활성화이나 명시)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("Lumina");
    app.setOrganizationName("Lumina");
    app.setApplicationVersion("1.0");

    // UI 폰트
    QFont uiFont("Segoe UI", 9);
    app.setFont(uiFont);

    MainWindow w;
    w.show();
    return app.exec();
}
```

- [ ] **Step 4: CMakeLists.cpp 소스 추가**

`ide-cpp/CMakeLists.txt`의 `APP_SOURCES` 갱신:
```cmake
set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp
)
```

- [ ] **Step 5: 빌드 및 실행 검증**

Run: `cd "C:/Users/lumi-user/문서/word file/Lumina/ide-cpp" && cmd //c build.bat`
Expected: 빌드 성공. 실행 시 메뉴바(File/Run/View), 툴바(Run/Stop/Save/Open/Theme), 상태바("Ready") 있는 빈 창.

- [ ] **Step 6: 메뉴/단축키 동작 확인**

Theme 메뉴 클릭 → `runCommand(Cmd_ThemeToggle)` 호출(현재는 no-op지만 크래치 없어야 함). Ctrl+S 등 단축키 등록 확인.

---

## Task 4: core/ 이식 — 토크나이저 + fuzzy + paths

**Files:**
- Create: `ide-cpp/src/core/lumitokenizer.h`
- Create: `ide-cpp/src/core/lumitokenizer.cpp`
- Create: `ide-cpp/src/core/fuzzy.h`
- Create: `ide-cpp/src/core/fuzzy.cpp`
- Create: `ide-cpp/src/core/paths.h`
- Create: `ide-cpp/src/core/paths.cpp`

**Interfaces:**
- Consumes: 기존 `lang.cpp`(`lumiTokenize`), `util.cpp`(`fuzzyRank`), `console.cpp`(`interpreterPath`) 로직.
- Produces:
  - `std::vector<Tok> lumiTokenize(QStringView text)` — 토크나이저 (identStart 버그 수정 포함)
  - `int fuzzyRank(const QString &query, const QString &text)` — 매칭 시작위치 반환, 불일치 -1
  - `QString interpreterPath()` — lumi.exe 경로 해석

- [ ] **Step 1: 토크나이저 헤더**

`ide-cpp/src/core/lumitokenizer.h`:
```cpp
#pragma once
#include <QString>
#include <QStringView>
#include <vector>

enum TokKind { TK_PLAIN, TK_KEYWORD, TK_STRING, TK_NUMBER, TK_COMMENT, TK_BUILTIN };

struct Tok {
    int start;      // text 내 문자 오프셋
    int len;
    TokKind kind;
};

// Lumi 토크나이저. identStart 버그(× ÷ 등 기호 허용)를 QChar::isLetter로 수정.
std::vector<Tok> lumiTokenize(QStringView text);
```

- [ ] **Step 2: 토크나이저 구현 (lang.cpp 이식 + 버그 수정)**

`ide-cpp/src/core/lumitokenizer.cpp`:
```cpp
#include "lumitokenizer.h"
#include <QChar>
#include <set>

static const std::set<QString> &keywordSet() {
    static const std::set<QString> s = {
        // KEYWORDS
        "val","if","elif","else","while","func","return","true","false",
        "and","or","not","print","bring","up","for","in","break","continue",
        "global","local","switch","case","default","class","from","this","super",
        // TYPE_NAMES
        "int","float","num","char","str","text","seq","bytes"
    };
    return s;
}

// 버그 수정: 기존 lang.cpp:33 의 `c >= 0x00C0` 는 ×(0x00D7) ÷(0x00F7) 등
// 기호를 식별자 시작으로 오판. QChar::isLetter 는 Unicode Letter 카테고리만 허용.
static bool identStart(QChar c) {
    return c.isLetter() || c == u'_';
}
static bool identPart(QChar c) {
    return identStart(c) || c.isDigit();
}
static bool isDigit(QChar c) {
    return c >= u'0' && c <= u'9';
}

std::vector<Tok> lumiTokenize(QStringView text) {
    std::vector<Tok> out;
    const int len = text.length();
    int i = 0;
    bool afterClassFrom = false;
    const auto *d = text.data();

    while (i < len) {
        QChar c = d[i];

        // line comment (Lumi는 // 만, /* */ 없음)
        if (c == u'/' && i + 1 < len && d[i + 1] == u'/') {
            int start = i;
            while (i < len && d[i] != u'\n' && d[i] != u'\r') i++;
            out.push_back({start, i - start, TK_COMMENT});
            continue;
        }
        // string
        if (c == u'"') {
            int start = i++;
            while (i < len && d[i] != u'"' && d[i] != u'\n' && d[i] != u'\r') {
                if (d[i] == u'\\' && i + 1 < len) i++;
                i++;
            }
            if (i < len && d[i] == u'"') i++;
            out.push_back({start, i - start, TK_STRING});
            continue;
        }
        // number
        if (isDigit(c)) {
            int start = i;
            if (c == u'0' && i + 1 < len &&
                (d[i+1]==u'x'||d[i+1]==u'X'||d[i+1]==u'b'||d[i+1]==u'B')) {
                i += 2;
                while (i < len && identPart(d[i])) i++;
            } else {
                while (i < len && isDigit(d[i])) i++;
                if (i + 1 < len && d[i] == u'.' && isDigit(d[i+1])) {
                    i++;
                    while (i < len && isDigit(d[i])) i++;
                }
            }
            out.push_back({start, i - start, TK_NUMBER});
            continue;
        }
        // identifier / keyword
        if (identStart(c)) {
            int start = i;
            while (i < len && identPart(d[i])) i++;
            QString word = text.mid(start, i - start).toString();

            int look = i;
            while (look < len && (d[look] == u' ' || d[look] == u'\t')) look++;
            bool call = look < len && d[look] == u'(';

            TokKind kind;
            if (afterClassFrom) kind = TK_BUILTIN;
            else if (call) kind = TK_BUILTIN;
            else if (keywordSet().count(word)) kind = TK_KEYWORD;
            else kind = TK_PLAIN;

            afterClassFrom = (word == "class" || word == "from");
            if (afterClassFrom) kind = TK_KEYWORD;
            out.push_back({start, i - start, kind});
            continue;
        }
        if (c != u' ' && c != u'\t' && c != u'\r' && c != u'\n') afterClassFrom = false;
        i++;
    }
    return out;
}
```

- [ ] **Step 3: fuzzy 매처**

`ide-cpp/src/core/fuzzy.h`:
```cpp
#pragma once
#include <QString>

// VS Code-style subsequence match. 매칭 시작 위치 반환, 불일치 -1.
int fuzzyRank(const QString &query, const QString &text);
```

`ide-cpp/src/core/fuzzy.cpp`:
```cpp
#include "fuzzy.h"

int fuzzyRank(const QString &query, const QString &text) {
    if (query.isEmpty()) return 0;
    const QString q = query.toLower();
    const QString t = text.toLower();
    int first = -1;
    int at = 0;
    for (QChar c : q) {
        int hit = t.indexOf(c, at);
        if (hit < 0) return -1;
        if (first < 0) first = hit;
        at = hit + 1;
    }
    return first;
}
```

- [ ] **Step 4: paths (인터프리터 경로)**

`ide-cpp/src/core/paths.h`:
```cpp
#pragma once
#include <QString>

// lumi.exe 경로 해석: exedir 주변 탐색 후 폴백.
QString interpreterPath();
// 임시 스크래치 파일 경로 (런용).
QString scratchPath();
```

`ide-cpp/src/core/paths.cpp`:
```cpp
#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString interpreterPath() {
    QString dir = QCoreApplication::applicationDirPath();
    const QStringList tries = {
        dir + "/lumi.exe",
        dir + "/../c-interpreter/bin/lumi.exe",
        dir + "/../../c-interpreter/bin/lumi.exe",
        dir + "/../../../c-interpreter/bin/lumi.exe"
    };
    for (const QString &rel : tries) {
        QString full = QDir::cleanPath(rel);
        if (QFileInfo::exists(full)) return full;
    }
    return QDir::cleanPath(dir + "/lumi.exe");
}

QString scratchPath() {
    // 런 중 재실행 충돌 방지용 고유 파일명 (버그#12)
    return QDir::tempPath() + "/lumina-run-" +
           QString::number(QCoreApplication::applicationPid()) + ".lumi";
}
```

- [ ] **Step 5: CMakeLists.cpp에 core 소스 추가**

```cmake
set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/core/lumitokenizer.cpp
    src/core/fuzzy.cpp
    src/core/paths.cpp
)
```

- [ ] **Step 6: 빌드 검증**

Run: `cmd //c build.bat`
Expected: 컴파일 성공 (core는 아직 사용되지 않지만 컴파일되어야 함).

- [ ] **Step 7: 토크나이저 버그 수정 검증 (간이)**

`main.cpp`에 임시 검증 코드 추가 (T4 끝에서 제거):
```cpp
#include "core/lumitokenizer.h"
#include <QDebug>
// main() 내 QApplication 생성 직후:
{
    auto toks = lumiTokenize(u"val × = 5  // comment");
    for (auto &t : toks) qDebug() << "tok" << t.start << t.len << t.kind;
}
```
Run 후 콘솔 출력 확인: `×`(0x00D7)가 더 이상 identifier로 토큰화되지 않아야 함 (구 버전은 identStart 통과했을 것). 검증 후 임시 코드 제거.

---

## Task 5: theme/ — 라이트/다크 팔레트 + QSS

**Files:**
- Create: `ide-cpp/src/theme/theme.h`
- Create: `ide-cpp/src/theme/theme.cpp`

**Interfaces:**
- Consumes: 기존 `util.cpp:themeSet`의 색 정의 (spec 6.1 팔레트로 교체)
- Produces:
  - `struct Palette` — 모든 색 토큰 (라이트/다크 두 인스턴스)
  - `const Palette& currentPalette()` — 현재 활성 팔레트
  - `bool isDark()`
  - `void applyTheme(QApplication *app, bool dark)` — QPalette + QSS 전역 적용
  - `QColor tokColor(TokKind k)`, `bool tokBold(TokKind k)` — 하이라이트용

- [ ] **Step 1: theme.h**

`ide-cpp/src/theme/theme.h`:
```cpp
#pragma once
#include <QColor>
#include <QApplication>
#include "../core/lumitokenizer.h"

struct Palette {
    QColor bg, surface, surfaceAlt, border;
    QColor text, textDim;
    QColor accent, accentText;
    QColor error, errorBg, success, info;
    QColor editorBg, editorFg;
    QColor termBg, termFg, termInfo, termInput, termPrompt;
    QColor tabbarBg, tabActive, tabInactive, tabFg, tabFgDim;
    QColor sidebarBg, sidebarFg, sidebarSel;
    QColor btnBg, btnHover, btnFg, ghostFg, ghostHover;
    QColor statusBg, statusFg;
    QColor line, focus;
    // 신택스
    QColor synKeyword, synString, synNumber, synComment, synBuiltin;
};

const Palette& currentPalette();
bool isDark();

// dark=true 다크, false 라이트. QPalette + QSS 전역 적용 + true 반환 시 isDark 갱신.
void applyTheme(QApplication *app, bool dark);
void toggleTheme(QApplication *app);

// 하이라이트 헬퍼
QColor tokColor(TokKind k);
bool tokBold(TokKind k);
```

- [ ] **Step 2: theme.cpp — 두 팔레트 정의**

`ide-cpp/src/theme/theme.cpp`:
```cpp
#include "theme.h"

static Palette makeDark() {
    // Catppuccin Mocha 계열
    Palette p;
    p.bg = "#1e1e2e"; p.surface = "#252536"; p.surfaceAlt = "#2a2a3e"; p.border = "#3a3a52";
    p.text = "#cdd6f4"; p.textDim = "#7f849c";
    p.accent = "#89b4fa"; p.accentText = "#1e1e2e";
    p.error = "#f38ba8"; p.errorBg = "#3a1d2e"; p.success = "#a6e3a1"; p.info = "#89dceb";
    p.editorBg = p.surface; p.editorFg = p.text;
    p.termBg = p.surface; p.termFg = p.text; p.termInfo = p.info;
    p.termInput = p.text; p.termPrompt = p.accent;
    p.tabbarBg = p.bg; p.tabActive = p.surface; p.tabInactive = p.bg;
    p.tabFg = p.text; p.tabFgDim = p.textDim;
    p.sidebarBg = p.bg; p.sidebarFg = p.text; p.sidebarSel = QColor(p.accent); p.sidebarSel.setAlpha(60);
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
    p.sidebarBg = p.surfaceAlt; p.sidebarFg = p.text; p.sidebarSel = QColor(p.accent); p.sidebarSel.setAlpha(40);
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

const Palette& currentPalette() { return g_palette; }
bool isDark() { return g_dark; }

static QString qssFor(const Palette &p) {
    return QString(
        "QWidget { background: %1; color: %2; font-family: 'Segoe UI'; }"
        "QMenuBar { background: %1; }"
        "QMenuBar::item:selected { background: %3; }"
        "QMenu { background: %4; border: 1px solid %5; }"
        "QMenu::item:selected { background: %3; }"
        "QToolBar { background: %1; border: none; spacing: 2px; }"
        "QStatusBar { background: %6; color: %7; }"
        "QPushButton { background: %8; color: %9; border: 1px solid %5; padding: 5px 12px; border-radius: 6px; }"
        "QPushButton:hover { background: %10; }"
        "QPushButton:pressed { background: %5; }"
        "QLineEdit, QPlainTextEdit, QTextEdit { background: %4; color: %2; border: 1px solid %5; border-radius: 6px; padding: 3px; selection-background-color: %3; }"
        "QListWidget { background: %4; border: 1px solid %5; }"
        "QListWidget::item:selected { background: %3; color: %2; }"
        "QTreeView { background: %4; border: none; }"
        "QTreeView::item:selected { background: %11; }"
        "QScrollBar:vertical { background: %1; width: 10px; }"
        "QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: %10; }"
        "QScrollBar:horizontal { background: %1; height: 10px; }"
        "QScrollBar::handle:horizontal { background: %5; border-radius: 4px; min-width: 20px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QSplitter::handle { background: %5; }"
        "QSplitter::handle:horizontal { width: 4px; }"
        "QSplitter::handle:vertical { height: 4px; }"
        "QTabWidget::pane { border: 1px solid %5; }"
    ).arg(p.bg.name(),                // 1
          p.text.name(),              // 2
          p.accent.name(),            // 3
          p.surface.name(),           // 4
          p.border.name(),            // 5
          p.statusBg.name(),          // 6
          p.statusFg.name(),          // 7
          p.btnBg.name(),             // 8
          p.btnFg.name(),             // 9
          p.btnHover.name(),          // 10
          p.sidebarSel.name());       // 11
}

void applyTheme(QApplication *app, bool dark) {
    g_dark = dark;
    g_palette = dark ? makeDark() : makeLight();

    QPalette pal = app->palette();
    pal.setColor(QPalette::Window, g_palette.bg);
    pal.setColor(QPalette::WindowText, g_palette.text);
    pal.setColor(QPalette::Base, g_palette.surface);
    pal.setColor(QPalette::Text, g_palette.text);
    pal.setColor(QPalette::Button, g_palette.btnBg);
    pal.setColor(QPalette::ButtonText, g_palette.btnFg);
    pal.setColor(QPalette::Highlight, g_palette.accent);
    pal.setColor(QPalette::HighlightedText, g_palette.accentText);
    pal.setColor(QPalette::ToolTipBase, g_palette.surfaceAlt);
    pal.setColor(QPalette::ToolTipText, g_palette.text);
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
```

- [ ] **Step 3: MainWindow에 테마 적용 연결**

`ide-cpp/src/mainwindow.cpp`의 `applyTheme()` 구현:
```cpp
#include "theme/theme.h"
// ...
void MainWindow::applyTheme() {
    toggleTheme(qApp);
    // 위젯 연결 후: 각 패널에 테마 갱신 시그널 발생 (T6+)
}
```
`mainwindow.cpp` 생성자 끝에 초기 테마 적용:
```cpp
::applyTheme(qApp, true);   // 다크로 시작
```

- [ ] **Step 4: CMakeLists.cpp에 theme 소스 추가**

```cmake
src/theme/theme.cpp
```

- [ ] **Step 5: 빌드 및 테마 토글 검증**

Run: `cmd //c build.bat` → 실행 → Theme 메뉴/툴바 클릭 시 라이트↔다크 전환 확인. 색상이 팔레트대로 바뀌는지 확인.

---

## Task 6: TabBar + TabModel (탭 관리)

**Files:**
- Create: `ide-cpp/src/models/tabmodel.h`
- Create: `ide-cpp/src/models/tabmodel.cpp`
- Create: `ide-cpp/src/widgets/tabbar.h`
- Create: `ide-cpp/src/widgets/tabbar.cpp`

**Interfaces:**
- Produces:
  - `struct Tab { int id; QString path; QString name; bool modified; }` (content는 활성 탭은 CodeEditor가, 비활성은 model이 들고)
  - `class TabModel` : QObject — `openPath()`, `newTab()`, `activate(int)`, `close(int)`, `save(int,bool)`, `tabsChanged()` 시그널, `activeContent`/`setActiveContent` (활성탭 content 동기화)
  - `class TabBar` : QWidget — paintEvent로 탭 그림, 활성탭 액센트(`accent` 색, 버그#8 수정), 더러운●, 닫기×, 컨텍스트메뉴

- [ ] **Step 1: tabmodel.h**

`ide-cpp/src/models/tabmodel.h`:
```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

struct Tab {
    int id = 0;
    QString path;       // Untitled 는 빈 문자열
    QString name;
    QString content;    // 비활성 탭의 내용. 활성 탭은 CodeEditor가 들고.
    bool modified = false;
};

class TabModel : public QObject {
    Q_OBJECT
public:
    explicit TabModel(QObject *parent = nullptr);

    const std::vector<Tab>& tabs() const { return m_tabs; }
    int activeIndex() const { return m_active; }
    Tab* activeTab();
    int count() const { return (int)m_tabs.size(); }

    void openPath(const QString &path);
    int newTab(const QString &content = {});
    void activate(int i);
    void close(int i);
    void closeOthers(int i);
    void closeAll();
    bool save(int i, bool saveAs);
    void setActiveContent(const QString &text);   // 활성 탭 content 동기화
    QString activeContent() const;
    void pathRenamed(const QString &from, const QString &to);
    void pathRemoved(const QString &path);

    // 더러운 탭이 있으면 사용자에게 저장 확인. true=진행 가능.
    bool confirmDiscardAll(QWidget *parent);

signals:
    void tabsChanged();           // 탭 목록/순서/활성 변경
    void contentChanged();        // 활성 탭 content 변경
    void activeChanged(int index);

private:
    std::vector<Tab> m_tabs;
    int m_active = -1;
    int m_nextId = 1;
    int findByPath(const QString &path) const;
};
```

- [ ] **Step 2: tabmodel.cpp**

`ide-cpp/src/models/tabmodel.cpp`:
```cpp
#include "tabmodel.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>

static QString baseName(const QString &p) {
    int at = p.lastIndexOf('/');
    if (at < 0) at = p.lastIndexOf('\\');
    return at < 0 ? p : p.mid(at + 1);
}

TabModel::TabModel(QObject *parent) : QObject(parent) {}

Tab* TabModel::activeTab() {
    return (m_active >= 0 && m_active < (int)m_tabs.size()) ? &m_tabs[m_active] : nullptr;
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
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    QString content = s.readAll();
    // BOM 제거
    if (content.startsWith(QChar(0xFEFF))) content.remove(0, 1);

    Tab t;
    t.id = m_nextId++;
    t.path = path;
    t.name = baseName(path);
    t.content = content;
    m_tabs.push_back(t);
    activate((int)m_tabs.size() - 1);
}

int TabModel::newTab(const QString &content) {
    Tab t;
    t.id = m_nextId++;
    t.name = QString("Untitled-%1").arg(m_nextId - 1);
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

void TabModel::close(int i) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
    m_tabs.erase(m_tabs.begin() + i);
    if (m_active >= (int)m_tabs.size()) m_active = (int)m_tabs.size() - 1;
    if (m_active == i && m_active >= 0) {
        emit activeChanged(m_active);
        emit contentChanged();
    }
    emit tabsChanged();
}

void TabModel::closeOthers(int i) {
    if (i < 0 || i >= (int)m_tabs.size()) return;
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
    emit tabsChanged();
}

bool TabModel::save(int i, bool saveAs) {
    if (i < 0 || i >= (int)m_tabs.size()) return false;
    Tab &t = m_tabs[i];
    QString path = t.path;
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(nullptr, "Save As", t.name, "Lumi (*.lumi);;All (*.*)");
        if (path.isEmpty()) return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    s << (i == m_active ? m_activeContent : t.content);
    f.close();
    t.path = path;
    t.name = baseName(path);
    t.modified = false;
    emit tabsChanged();
    return true;
}

void TabModel::setActiveContent(const QString &text) {
    if (m_active < 0) return;
    m_tabs[m_active].content = text;
    m_tabs[m_active].modified = true;
    emit tabsChanged();
}

QString TabModel::activeContent() const {
    if (m_active < 0) return {};
    return m_tabs[m_active].content;
}

void TabModel::pathRenamed(const QString &from, const QString &to) {
    for (auto &t : m_tabs)
        if (t.path == from) { t.path = to; t.name = baseName(to); }
    emit tabsChanged();
}
void TabModel::pathRemoved(const QString &path) {
    for (int i = (int)m_tabs.size() - 1; i >= 0; --i)
        if (m_tabs[i].path == path) close(i);
}

bool TabModel::confirmDiscardAll(QWidget *parent) {
    for (auto &t : m_tabs) {
        if (!t.modified) continue;
        auto r = QMessageBox::question(parent, "Unsaved changes",
            QString("'%1' has unsaved changes. Save?").arg(t.name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Save) {
            int idx = (int)(&t - &m_tabs[0]);
            if (!save(idx, false)) return false;
        }
    }
    return true;
}

QString TabModel::m_activeContent;  // NOTE: 제거 — activeContent()는 tabs()[active].content 반환
```

주의: 위 마지막 줄의 `m_activeContent` 선언은 잘못됨 — 제거하고 `save()`는 `m_tabs[i].content`를 쓰도록 단순화 (활성 탭은 위젯에서 contentChanged로 model에 동기화되므로 항상 m_tabs[active].content가 최신). `save()`의 `i == m_active ? m_activeContent : t.content` 를 그냥 `t.content`로 수정:

```cpp
s << t.content;
```

- [ ] **Step 3: tabbar.h + tabbar.cpp (커스텀 페인트, 액센트 버그 수정)**

`ide-cpp/src/widgets/tabbar.h`:
```cpp
#pragma once
#include <QWidget>
#include "../models/tabmodel.h"

class TabBar : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(TabModel *model, QWidget *parent = nullptr);

signals:
    void tabClicked(int index);
    void tabCloseRequested(int index);
    void contextMenuRequested(int index, QPoint pos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void recompute();
    TabModel *m_model;
    std::vector<QRect> m_rects;
    QRect m_detachRect;
    int m_hoverClose = -1;
};
```

`ide-cpp/src/widgets/tabbar.cpp` (paintEvent에서 활성탭 상단선에 `accent` 사용 — 버그#8 수정):
```cpp
#include "tabbar.h"
#include "../theme/theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

TabBar::TabBar(TabModel *model, QWidget *parent)
    : QWidget(parent), m_model(model) {
    setMouseTracking(true);
    setMinimumHeight(32);
    connect(model, &TabModel::tabsChanged, this, [this]{ update(); });
}

QSize TabBar::sizeHint() const { return {400, 32}; }
QSize TabBar::minimumSizeHint() const { return {100, 32}; }

static int textWidthF(const QFontMetrics &fm, const QString &s) {
    return fm.horizontalAdvance(s);
}

void TabBar::recompute() {
    m_rects.clear();
    const auto &tabs = m_model->tabs();
    QFontMetrics fm(font());
    int x = 12;   // 좌측 INER 라벨 자리
    // "INER" 라벨
    x += fm.horizontalAdvance("INER") + 24;
    for (size_t i = 0; i < tabs.size(); ++i) {
        int w = textWidthF(fm, tabs[i].name) + (tabs[i].modified ? 20 : 0) + 40; // 닫기× 자리
        m_rects.push_back(QRect(x, 0, w, height()));
        x += w;
    }
    // detach 버튼 (오른쪽)
    m_detachRect = QRect(width() - 70, 6, 60, height() - 12);
}

void TabBar::paintEvent(QPaintEvent *) {
    recompute();
    const Palette &p = currentPalette();
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    // 배경
    g.fillRect(rect(), p.tabbarBg);

    QFontMetrics fm(font());

    // INER 라벨
    g.setPen(p.textDim);
    g.drawText(QRect(12, 0, fm.horizontalAdvance("INER"), height()),
               Qt::AlignVCenter | Qt::AlignLeft, "INER");

    // 탭들
    int active = m_model->activeIndex();
    const auto &tabs = m_model->tabs();
    for (int i = 0; i < (int)m_rects.size(); ++i) {
        const QRect &r = m_rects[i];
        bool act = (i == active);
        g.fillRect(r, act ? p.tabActive : p.tabInactive);

        // 활성 탭 상단 액센트 선 (버그#8 수정: statusBg가 아닌 accent)
        if (act) {
            g.fillRect(QRect(r.left(), 0, r.width(), 2), p.accent);
        }

        // 텍스트
        g.setPen(act ? p.tabFg : p.tabFgDim);
        QRect textRect = r.adjusted(10, 0, -28, 0);
        QString name = tabs[i].name;
        QString disp = fm.elidedText(name, Qt::ElideRight, textRect.width());
        g.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, disp);

        // 더러운 표시 ●
        int dx = textRect.left() + fm.horizontalAdvance(disp) + 4;
        if (tabs[i].modified) {
            g.setPen(p.accent);
            g.drawText(QRect(dx, 0, 12, height()), Qt::AlignVCenter, "\xE2\x97\x8F"); // ●
        }

        // 닫기 ×
        QRect closeR(r.right() - 22, r.center().y() - 9, 18, 18);
        g.setPen(m_hoverClose == i ? p.accent : p.tabFgDim);
        g.drawText(closeR, Qt::AlignCenter, "\xC3\x97");  // ×
    }

    // detach 버튼
    g.setPen(p.ghostFg);
    g.drawRect(m_detachRect.adjusted(0,0,-1,-1));
    g.drawText(m_detachRect, Qt::AlignCenter, "\xE2\xA7\x89 Detach");  // ⧉
}

void TabBar::mousePressEvent(QMouseEvent *e) {
    QPoint pt = e->pos();
    if (m_detachRect.contains(pt)) {
        emit contextMenuRequested(-2, pt);   // -2 = detach 토글 신호
        return;
    }
    for (int i = 0; i < (int)m_rects.size(); ++i) {
        if (m_rects[i].contains(pt)) {
            if (e->button() == Qt::MiddleButton) { emit tabCloseRequested(i); return; }
            // 닫기× 히트
            QRect closeR(m_rects[i].right() - 22, m_rects[i].center().y() - 9, 18, 18);
            if (closeR.contains(pt)) { emit tabCloseRequested(i); return; }
            emit tabClicked(i);
            return;
        }
    }
}

void TabBar::mouseReleaseEvent(QMouseEvent *) {}

void TabBar::contextMenuEvent(QContextMenuEvent *e) {
    for (int i = 0; i < (int)m_rects.size(); ++i) {
        if (m_rects[i].contains(e->pos())) { emit contextMenuRequested(i, e->pos()); return; }
    }
}
```

- [ ] **Step 4: CMakeLists.cpp에 소스 추가**

```cmake
src/models/tabmodel.cpp
src/widgets/tabbar.cpp
```

- [ ] **Step 5: 빌드 검증**

Run: `cmd //c build.bat`
Expected: 컴파일 성공.

---

## Task 7: CodeEditor + LineNumberArea + LumiHighlighter + EditorPanel (INER)

**Files:**
- Create: `ide-cpp/src/widgets/codeeditor.h`
- Create: `ide-cpp/src/widgets/codeeditor.cpp`
- Create: `ide-cpp/src/widgets/linenumberarea.h`
- Create: `ide-cpp/src/widgets/linenumberarea.cpp`
- Create: `ide-cpp/src/widgets/lumihighlighter.h`
- Create: `ide-cpp/src/widgets/lumihighlighter.cpp`
- Create: `ide-cpp/src/panels/editorpanel.h`
- Create: `ide-cpp/src/panels/editorpanel.cpp`

**Interfaces:**
- Consumes: `lumiTokenize` (T4), `TabModel` (T6), `currentPalette/tokColor` (T5)
- Produces:
  - `CodeEditor` : QPlainTextEdit — `setText/getText/setErrorLine/setFontSize/zoom`, 자동들여쓰기/괄호쌍/줌 시그널, `contentChanged()` 시그널, `cursorPosChanged(line,col)` 시그널
  - `LumiHighlighter` : QSyntaxHighlighter — `rehighlightOnThemeChange()`
  - `LineNumberArea` : QWidget — 라인번호 + 에러라인 표시
  - `EditorPanel` : QWidget — TabBar + CodeEditor 통합. `loadActive()`/`stashActive()`

- [ ] **Step 1: linenumberarea.h/.cpp**

`ide-cpp/src/widgets/linenumberarea.h`:
```cpp
#pragma once
#include <QWidget>

class CodeEditor;

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(CodeEditor *editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    CodeEditor *m_editor;
};
```

- [ ] **Step 2: codeeditor.h**

`ide-cpp/src/widgets/codeeditor.h`:
```cpp
#pragma once
#include <QPlainTextEdit>

class LineNumberArea;
class LumiHighlighter;

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void lineNumberPaint(QPaintEvent *e);
    int lineNumberAreaWidth();

    void setErrorLine(int line);   // 1-based, -1 clears
    void setFontSize(int px);
    void zoomBy(int delta);        // +1/-1 단위

    LumiHighlighter* highlighter() { return m_hl; }

signals:
    void cursorPosChanged(int line, int col);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private slots:
    void updateLineNumberAreaWidth(int);
    void updateLineNumberArea(const QRect &, int);
    void highlightCurrentLine();
    void emitCursorPos();

private:
    LineNumberArea *m_ln;
    LumiHighlighter *m_hl;
    int m_errorLine = -1;

    void indentNewLine();          // Enter 자동들여쓰기
    QString leadingIndent(const QString &line) const;
};
```

- [ ] **Step 3: lumihighlighter.h/.cpp**

`ide-cpp/src/widgets/lumihighlighter.h`:
```cpp
#pragma once
#include <QSyntaxHighlighter>
#include "../core/lumitokenizer.h"

class LumiHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LumiHighlighter(QTextDocument *doc);
    void refreshTheme();   // 테마 변경 시 색 갱신 + rehighlight
protected:
    void highlightBlock(const QString &text) override;
};
```

`ide-cpp/src/widgets/lumihighlighter.cpp`:
```cpp
#include "lumihighlighter.h"
#include "../theme/theme.h"

LumiHighlighter::LumiHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {}

void LumiHighlighter::highlightBlock(const QString &text) {
    // 가시 블록만 호출됨 — 대용량 파일 성능 자동 해소 (버그)
    auto toks = lumiTokenize(QStringView(text));
    const Palette &p = currentPalette();
    for (const Tok &t : toks) {
        QTextCharFormat fmt;
        QColor c = tokColor(t.kind);
        fmt.setForeground(c);
        if (tokBold(t.kind)) fmt.setFontWeight(QFont::Bold);
        setFormat(t.start, t.len, fmt);
    }
    setCurrentBlockState(0);
}

void LumiHighlighter::refreshTheme() { rehighlight(); }
```

- [ ] **Step 4: codeeditor.cpp — 자동들여쓰기/괄호쌍/거터/줌**

`ide-cpp/src/widgets/codeeditor.cpp`:
```cpp
#include "codeeditor.h"
#include "linenumberarea.h"
#include "lumihighlighter.h"
#include "../theme/theme.h"
#include <QPainter>
#include <QKeyEvent>
#include <QTextBlock>
#include <QScrollBar>
#include <cmath>

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    m_ln = new LineNumberArea(this);
    m_hl = new LumiHighlighter(document());

    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    setFont(f);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::emitCursorPos);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * (digits + 1) + 6;
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
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection s;
        const Palette &p = currentPalette();
        s.format.setBackground(p.surfaceAlt);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        es.append(s);
    }
    setExtraSelections(es);
}

void CodeEditor::emitCursorPos() {
    QTextCursor c = textCursor();
    emit cursorPosChanged(c.blockNumber() + 1, c.columnNumber() + 1);
}

void CodeEditor::setErrorLine(int line) {
    m_errorLine = line;
    m_ln->update();
}

void CodeEditor::setFontSize(int px) {
    QFont f = font();
    f.setPointSize(px);
    setFont(f);
}

void CodeEditor::zoomBy(int delta) {
    QFont f = font();
    int pt = f.pointSize() + delta;
    if (pt < 6) pt = 6;
    if (pt > 48) pt = 48;
    f.setPointSize(pt);
    setFont(f);
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
    c.insertText("\n");
    QString line = c.block().previous().text();
    QString ind = leadingIndent(line);
    // ':' 로 끝나면 한 단계(4스페이스) 추가
    QString trimmed = line.trimmed();
    if (trimmed.endsWith(u':')) ind += "    ";
    if (!ind.isEmpty()) c.insertText(ind);
    setTextCursor(c);
}

void CodeEditor::keyPressEvent(QKeyEvent *e) {
    // Tab → 4스페이스
    if (e->key() == Qt::Key_Tab && !e->modifiers() && !textCursor().hasSelection()) {
        insertPlainText("    ");
        return;
    }
    // Enter → 자동들여쓰기
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!e->modifiers()) { indentNewLine(); return; }
    }
    // 괄호/따옴표 자동쌍
    static const QHash<QChar, QChar> pairs = {
        {u'(', u')'}, {u'[', u']'}, {u'{', u'}'}, {u'"', u'"'}
    };
    if (e->text().length() == 1) {
        QChar ch = e->text().at(0);
        if (pairs.contains(ch)) {
            insertPlainText(QString(ch) + QString(pairs.value(ch)));
            QTextCursor c = textCursor();
            c.movePosition(QTextCursor::Left);
            setTextCursor(c);
            return;
        }
        // 닫는 괄호 타입오버
        QTextCursor c = textCursor();
        if ((ch == u')' || ch == u']' || ch == u'}') &&
            c.block().text().mid(c.positionInBlock(), 1) == QString(ch)) {
            c.deleteChar();
            insertPlainText(QString(ch));
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(e);
}

void CodeEditor::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        int delta = e->angleDelta().y() > 0 ? 1 : -1;
        zoomBy(delta);
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

// LineNumberArea paint
LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {
    setAutoFillBackground(true);
}
QSize LineNumberArea::sizeHint() const { return {m_editor->lineNumberAreaWidth(), 0}; }
void LineNumberArea::paintEvent(QPaintEvent *) {
    const Palette &p = currentPalette();
    QPainter g(this);
    g.fillRect(rect(), p.surfaceAlt);

    QTextBlock block = m_editor->firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(m_editor->blockBoundingGeometry(block).translated(m_editor->contentOffset()).top());
    int bottom = top + qRound(m_editor->blockBoundingRect(block).height());

    QFontMetrics fm(m_editor->font());
    while (block.isValid() && top <= rect().bottom()) {
        if (block.isVisible() && bottom >= rect().top()) {
            QString num = QString::number(blockNumber + 1);
            bool isError = (m_editor->m_errorLinePublic == blockNumber + 1);
            g.setPen(isError ? p.error : p.textDim);
            g.drawText(0, top, width() - 6, fm.height(),
                       Qt::AlignRight | Qt::AlignVCenter, num);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(m_editor->blockBoundingRect(block).height());
        ++blockNumber;
    }
}
```

NOTE: 위에서 `m_editor->m_errorLinePublic` 참조 — `CodeEditor`에 `friend class LineNumberArea;` 추가 및 `m_errorLine`을 (또는 동등 public 접근자 `int errorLine() const`) 제공. 헤더에 추가:
```cpp
    friend class LineNumberArea;
public:
    int errorLine() const { return m_errorLine; }
```
paintEvent는 `m_editor->errorLine()` 사용하도록 수정.

- [ ] **Step 5: editorpanel.h/.cpp (INER 통합)**

`ide-cpp/src/panels/editorpanel.h`:
```cpp
#pragma once
#include <QWidget>
#include "../models/tabmodel.h"

class TabBar;
class CodeEditor;

class EditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit EditorPanel(TabModel *model, QWidget *parent = nullptr);

    CodeEditor* editor() { return m_edit; }
    void loadActive();      // model 활성탭 content → editor
    void stashActive();     // editor → model 활성탭 content
    void refreshTheme();
    void newTab();
    void triggerFind();
    void triggerReplace();
    void goToLine();

signals:
    void detachRequested();
    void contentChanged();
    void cursorPos(int line, int col);

private:
    TabModel *m_model;
    TabBar *m_tabs;
    CodeEditor *m_edit;
};
```

`ide-cpp/src/panels/editorpanel.cpp`:
```cpp
#include "editorpanel.h"
#include "../widgets/tabbar.h"
#include "../widgets/codeeditor.h"
#include "../theme/theme.h"
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextCursor>

EditorPanel::EditorPanel(TabModel *model, QWidget *parent)
    : QWidget(parent), m_model(model), m_tabs(new TabBar(model, this)),
      m_edit(new CodeEditor(this)) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(0);
    lay->addWidget(m_tabs);
    lay->addWidget(m_edit, 1);

    connect(m_model, &TabModel::activeChanged, this, [this](int){
        // 이전 활성탭 content 저장은 contentChanged 슬롯에서 실시간으로.
        loadActive();
    });
    connect(m_edit, &CodeEditor::textChanged, this, [this]{
        m_model->setActiveContent(m_edit->toPlainText());
        emit contentChanged();
    });
    connect(m_edit, &CodeEditor::cursorPosChanged, this, &EditorPanel::cursorPos);
    connect(m_tabs, &TabBar::tabClicked, m_model, &TabModel::activate);
    connect(m_tabs, &TabBar::tabCloseRequested, m_model, [this](int i){
        if (m_model->tabs()[i].modified) {
            auto r = QMessageBox::question(this, "Close",
                QString("'%1' has unsaved changes. Close anyway?").arg(m_model->tabs()[i].name));
            if (r != QMessageBox::Yes) return;
        }
        m_model->close(i);
    });
    connect(m_tabs, &TabBar::contextMenuRequested, this, [this](int idx, QPoint){
        if (idx == -2) emit detachRequested();
    });

    if (m_model->count() == 0) m_model->newTab();
    loadActive();
}

void EditorPanel::loadActive() {
    Tab *t = m_model->activeTab();
    if (!t) { m_edit->clear(); return; }
    QString cur = m_edit->toPlainText();
    if (cur != t->content) {
        m_edit->setPlainText(t->content);
    }
}

void EditorPanel::stashActive() {
    Tab *t = m_model->activeTab();
    if (t) t->content = m_edit->toPlainText();
}

void EditorPanel::refreshTheme() {
    m_edit->highlighter()->refreshTheme();
    update();
}

void EditorPanel::newTab() { m_model->newTab(); loadActive(); }

void EditorPanel::triggerFind() {
    bool ok;
    QString term = QInputDialog::getText(this, "Find", "Find:", QLineEdit::Normal, {}, &ok);
    if (!ok || term.isEmpty()) return;
    m_edit->moveCursor(QTextCursor::Start);
    m_edit->find(term);
}

void EditorPanel::triggerReplace() {
    // 간단 바꾸기 다이얼로그 — 핵심 동작만
    QDialog d(this);
    d.setWindowTitle("Replace");
    auto *fl = new QLineEdit(&d);
    auto *rl = new QLineEdit(&d);
    // ... (레이아웃 생략, 실제 구현 시 채움)
    d.exec();
}

void EditorPanel::goToLine() {
    bool ok;
    int line = QInputDialog::getInt(this, "Go to Line", "Line:", 1, 1, 999999, 1, &ok);
    if (!ok) return;
    int target = qMin(line, m_edit->blockCount());
    QTextBlock b = m_edit->document()->findBlockByNumber(target - 1);
    if (b.isValid()) {
        QTextCursor c(b);
        m_edit->setTextCursor(c);
        m_edit->centerCursor();
    }
}
```

- [ ] **Step 6: CMakeLists.cpp에 소스 추가**

```cmake
src/widgets/codeeditor.cpp
src/widgets/linenumberarea.cpp
src/widgets/lumihighlighter.cpp
src/panels/editorpanel.cpp
```

- [ ] **Step 7: MainWindow에 INER dock 연결**

`mainwindow.h`에 멤버 추가:
```cpp
class TabModel;
class EditorPanel;
// ...
private:
    TabModel *m_tabs{nullptr};
    EditorPanel *m_iner{nullptr};
```

`mainwindow.cpp` 생성자에:
```cpp
#include "models/tabmodel.h"
#include "panels/editorpanel.h"
// 생성자 본문:
m_tabs = new TabModel(this);
m_iner = new EditorPanel(m_tabs);
auto *dock = new QDockWidget("INER", this);
dock->setWidget(m_iner);
dock->setAllowedAreas(Qt::AllDockWidgetAreas);
addDockWidget(Qt::TopDockWidgetArea, dock);
```

- [ ] **Step 8: 빌드 + 편집기 동작 검증**

Run: `cmd //c build.bat` → 실행
Expected: INER 패널에 탭바(기본 "Untitled-1" + `print("hello, Lumi!")`) + 코드편집기(거터 라인번호, 신택스 하이라이트). 타이핑 시 하이라이트/들여쓰기/괄호쌍 동작. Ctrl+휠 줌.

---

## Task 8: RunSession + ConsolePane + OutputPanel (OUTER — 터미널/REPL)

**Files:**
- Create: `ide-cpp/src/models/runsession.h`
- Create: `ide-cpp/src/models/runsession.cpp`
- Create: `ide-cpp/src/widgets/consolepane.h`
- Create: `ide-cpp/src/widgets/consolepane.cpp`
- Create: `ide-cpp/src/panels/outputpanel.h`
- Create: `ide-cpp/src/panels/outputpanel.cpp`

**Interfaces:**
- Consumes: `interpreterPath/scratchPath` (T4), 기존 `console.cpp` 마커 프로토콜(`\x1eINPUT`/`\x1eDONE`/`\x1eEOT`), 인코딩 폴백(UTF-8→CP949→ACP)
- Produces:
  - `RunSession` : QObject — QProcess 래퍼. `run()`/`replRun()`/`shellRun()`, `stop()`, `isBusy()`, 시그널: `output(QString,int cls)`/`finished()`/`awaitingInput()`
  - `ConsolePane` : QWidget — 출력(QPlainTextEdit 읽기전용) + 입력(QLineEdit 히스토리). `panel` Term/Repl
  - `OutputPanel` : QWidget — 헤더(Detach/Clear/Term/Repl 세그) + 두 ConsolePane

- [ ] **Step 1: runsession.h**

`ide-cpp/src/models/runsession.h`:
```cpp
#pragma once
#include <QObject>
#include <QProcess>

class RunSession : public QObject {
    Q_OBJECT
public:
    enum Slot { Lumi, Shell, Repl };

    explicit RunSession(QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }

    // 런: scratch에 content 저장 후 lumi.exe --ide.
    void run(const QString &content, const QString &baseDir, const QString &scratchPath);
    // REPL 한 블록 실행.
    void replRun(const QString &code, const QString &baseDir);
    // OS 셸 명령.
    void shellRun(const QString &cmd, const QString &cwd);

    void stop(Slot s);
    void stopAll();

    // REPL 입력 (input() 응답)
    void writeRepl(const QByteArray &data);
    // 런 입력 (input() 응답)
    void writeLumi(const QByteArray &data);

signals:
    void output(int cls, const QString &text);   // cls = OutClass
    void finished();
    void awaitingInput();

private:
    QProcess *m_proc{nullptr};
    bool m_busy = false;
    QString m_scratchPath;
    QString m_pending;     // 마커 분할 보관

    void spawn(const QString &program, const QStringList &args, const QString &cwd);
    void onData();
    QString decode(const QByteArray &b);
};
```

OutClass 정의 (`ide-cpp/src/core/outclass.h`):
```cpp
#pragma once
enum OutClass { OC_TEXT=0, OC_ERROR, OC_SUCCESS, OC_INFO, OC_USERIN, OC_PROMPT };
```

- [ ] **Step 2: runsession.cpp (QProcess + 마커 + 인코딩 폴백)**

`ide-cpp/src/models/runsession.cpp`:
```cpp
#include "runsession.h"
#include "core/outclass.h"
#include "core/paths.h"
#include <QStringDecoder>
#include <QFile>
#include <QTextStream>
#include <QDir>

static constexpr char RS = '\x1e';
static const QByteArray MARK_INPUT = QByteArray("\x1eINPUT\n");
static const QByteArray MARK_DONE  = QByteArray("\x1eDONE\n");

RunSession::RunSession(QObject *parent) : QObject(parent) {}

void RunSession::spawn(const QString &program, const QStringList &args, const QString &cwd) {
    if (m_proc) { m_proc->kill(); m_proc->waitForFinished(1000); delete m_proc; }
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_proc->setWorkingDirectory(cwd);
    m_proc->setProgram(program);
    m_proc->setArguments(args);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &RunSession::onData);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus){
        // 남은 출력 flush
        if (m_proc && m_proc->bytesAvailable()) onData();
        m_busy = false;
        if (!m_scratchPath.isEmpty()) { QFile::remove(m_scratchPath); m_scratchPath.clear(); }
        emit finished();
    });
    m_proc->start();
    m_busy = true;
}

void RunSession::run(const QString &content, const QString &baseDir, const QString &scratch) {
    m_scratchPath = scratch;
    QFile f(scratch);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit output(OC_ERROR, "Cannot write the scratch file.\n");
        return;
    }
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    s << content;
    f.close();

    spawn(interpreterPath(), {"--ide", scratch, baseDir}, baseDir);
}

void RunSession::replRun(const QString &code, const QString &baseDir) {
    if (!m_proc) spawn(interpreterPath(), {"--repl", baseDir}, baseDir);
    QByteArray data = (code + "\n\x1eEOT\n").toUtf8();
    m_proc->write(data);
}

void RunSession::shellRun(const QString &cmd, const QString &cwd) {
    // cmd.exe /d /s /c <cmd>
    spawn("cmd.exe", {"/d", "/s", "/c", cmd}, cwd);
}

void RunSession::stop(Slot) {
    if (m_proc) { m_proc->kill(); }
}
void RunSession::stopAll() { stop(Lumi); }

void RunSession::writeRepl(const QByteArray &data) { if (m_proc) m_proc->write(data); }
void RunSession::writeLumi(const QByteArray &data) { if (m_proc) m_proc->write(data); }

QString RunSession::decode(const QByteArray &b) {
    // 인코딩 폴백: UTF-8 → CP949 → 시스템
    {
        QStringDecoder dec(QStringConverter::Utf8);
        QString s = dec(b);
        if (!dec.hasError()) return s;
    }
    {
        QStringDecoder dec("windows-949");   // CP949 Korean
        QString s = dec(b);
        if (!dec.hasError()) return s;
    }
    return QString::fromLocal8Bit(b);
}

void RunSession::onData() {
    QByteArray raw = m_proc->readAllStandardOutput();
    QString text = decode(raw);

    // 마커 처리
    QByteArray combined = m_pending.toUtf8() + raw;   // 간단: 마커는 문자열 단위로
    m_pending.clear();

    QString display;
    int i = 0;
    while (i < text.size()) {
        int at = text.indexOf(QChar(RS), i);
        if (at < 0) { display += text.mid(i); break; }
        display += text.mid(i, at - i);
        if (text.mid(at, MARK_INPUT.size()) == QString::fromUtf8(MARK_INPUT)) {
            emit awaitingInput();
            i = at + MARK_INPUT.size();
        } else if (text.mid(at, MARK_DONE.size()) == QString::fromUtf8(MARK_DONE)) {
            i = at + MARK_DONE.size();
        } else if (text.size() - at < 7) {
            m_pending = text.mid(at);    // 분할 마커 보관
            break;
        } else {
            display += QChar(RS);
            i = at + 1;
        }
    }
    if (!display.isEmpty()) emit output(OC_TEXT, display);
}
```

- [ ] **Step 3: consolepane.h/.cpp (출력+입력, 히스토리)**

`ide-cpp/src/widgets/consolepane.h`:
```cpp
#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QStringList>

class ConsolePane : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePane(QWidget *parent = nullptr);

    void append(const QString &text, int cls);
    void clear();
    void setAcceptInput(bool on);
    void setPrompt(const QString &p);
    void focusInput();

signals:
    void lineSubmitted(const QString &line);

private:
    QPlainTextEdit *m_out;
    QLineEdit *m_in;
    QLabel *m_promptLbl;
    QStringList m_history;
    int m_histIdx = 0;
};
```

`ide-cpp/src/widgets/consolepane.cpp`:
```cpp
#include "consolepane.h"
#include "core/outclass.h"
#include "../theme/theme.h"
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QTextCharFormat>

ConsolePane::ConsolePane(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(0);

    m_out = new QPlainTextEdit(this);
    m_out->setReadOnly(true);
    QFont f("Consolas", 10);
    f.setStyleHint(QFont::Monospace);
    m_out->setFont(f);

    m_promptLbl = new QLabel(">", this);
    m_promptLbl->setFixedWidth(16);
    m_in = new QLineEdit(this);
    m_in->setFont(f);

    auto *inLay = new QHBoxLayout();
    inLay->setContentsMargins(4,2,4,2);
    inLay->addWidget(m_promptLbl);
    inLay->addWidget(m_in);

    lay->addWidget(m_out, 1);
    lay->addLayout(inLay);

    connect(m_in, &QLineEdit::returnPressed, this, [this]{
        QString line = m_in->text();
        m_in->clear();
        m_history.append(line);
        m_histIdx = m_history.size();
        emit lineSubmitted(line);
    });
    // 히스토리 ↑↓
    m_in->installEventFilter(this);
}

void ConsolePane::append(const QString &text, int cls) {
    const Palette &p = currentPalette();
    QColor color = p.termFg;
    switch (cls) {
        case OC_ERROR: color = p.error; break;
        case OC_SUCCESS: color = p.success; break;
        case OC_INFO: color = p.termInfo; break;
        case OC_USERIN: color = p.termInput; break;
        case OC_PROMPT: color = p.termPrompt; break;
    }
    QTextCharFormat fmt;
    fmt.setForeground(color);
    QTextCursor c = m_out->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);
    m_out->setTextCursor(c);
}

void ConsolePane::clear() { m_out->clear(); }
void ConsolePane::setAcceptInput(bool on) { m_in->setEnabled(on); }
void ConsolePane::setPrompt(const QString &pr) { m_promptLbl->setText(pr); }
void ConsolePane::focusInput() { m_in->setFocus(); }
```
(history ↑↓ 처리는 eventFilter로 추가 — 지면상 생략하지만 구현 필수: Key_Up/Key_Down에서 m_histIdx 이동해 m_in->setText)

- [ ] **Step 4: outputpanel.h/.cpp (헤더 + Term/Repl 세그 + Detach)**

`ide-cpp/src/panels/outputpanel.h`:
```cpp
#pragma once
#include <QWidget>

class ConsolePane;
class RunSession;

class OutputPanel : public QWidget {
    Q_OBJECT
public:
    explicit OutputPanel(RunSession *run, QWidget *parent = nullptr);

    enum Panel { Term, Repl };
    void selectPanel(Panel p);
    Panel currentPanel() const { return m_cur; }

    void runCode(const QString &content, const QString &baseDir, const QString &scratch);
    void stopRun();
    bool busy() const;
    void clearCurrent();
    void setCwd(const QString &cwd);   // 터미널 cwd
    void refreshTheme();
    void focusInput();

signals:
    void detachRequested();
    void errorLineDetected(int line);
    void runningChanged(bool running);

private:
    RunSession *m_run;
    ConsolePane *m_term;
    ConsolePane *m_repl;
    Panel m_cur = Term;
    QString m_cwd;
    int m_pendingError = -1;

    void setupSessionConnections();
};
```

`ide-cpp/src/panels/outputpanel.cpp`:
```cpp
#include "outputpanel.h"
#include "../widgets/consolepane.h"
#include "../models/runsession.h"
#include "core/outclass.h"
#include "../theme/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QRegularExpression>

OutputPanel::OutputPanel(RunSession *run, QWidget *parent)
    : QWidget(parent), m_run(run),
      m_term(new ConsolePane(this)), m_repl(new ConsolePane(this)) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->setSpacing(0);

    // 헤더: OUTER 라벨 + Term/Repl 세그 + Clear + Detach
    auto *hdr = new QWidget(this);
    auto *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(8,2,8,2);
    auto *title = new QLabel("OUTER", hdr);
    auto *termBtn = new QPushButton("Terminal", hdr);
    auto *replBtn = new QPushButton("REPL", hdr);
    termBtn->setCheckable(true); replBtn->setCheckable(true);
    termBtn->setChecked(true);
    auto *clearBtn = new QPushButton("Clear", hdr);
    auto *detachBtn = new QPushButton("\xE2\xA7\x89 Detach", hdr);
    hl->addWidget(title);
    hl->addStretch();
    hl->addWidget(termBtn);
    hl->addWidget(replBtn);
    hl->addStretch();
    hl->addWidget(clearBtn);
    hl->addWidget(detachBtn);

    auto *stack = new QStackedWidget(this);
    stack->addWidget(m_term);
    stack->addWidget(m_repl);

    lay->addWidget(hdr);
    lay->addWidget(stack, 1);

    connect(termBtn, &QPushButton::clicked, this, [this]{ selectPanel(Term); });
    connect(replBtn, &QPushButton::clicked, this, [this]{ selectPanel(Repl); });
    connect(clearBtn, &QPushButton::clicked, this, &OutputPanel::clearCurrent);
    connect(detachBtn, &QPushButton::clicked, this, &OutputPanel::detachRequested);

    // 터미널 입력
    connect(m_term, &ConsolePane::lineSubmitted, this, [this](const QString &line){
        // OS 셸 모드 (단순화: 핵심 명령 처리)
        if (busy()) return;
        QString cmd = line.trimmed().toLower();
        if (cmd == "cls" || cmd == "clear") { m_term->clear(); return; }
        m_run->shellRun(line, m_cwd);
        emit runningChanged(true);
    });
    // REPL 입력
    connect(m_repl, &ConsolePane::lineSubmitted, this, [this](const QString &line){
        m_repl->append((m_replHistory ? "lumi> " : "lumi> ") + line + "\n", OC_USERIN);
        m_run->replRun(line, m_cwd);
    });

    setupSessionConnections();
}

void OutputPanel::setupSessionConnections() {
    // 출력 → 현재 패널
    connect(m_run, &RunSession::output, this, [this](int cls, const QString &text){
        ConsolePane *p = (m_cur == Term) ? m_term : m_repl;
        p->append(text, cls);
        // 에러 라인 감지
        QRegularExpression re("Error: \\[line (\\d+)");
        auto m = re.match(text);
        if (m.hasMatch()) m_pendingError = m.captured(1).toInt();
    });
    connect(m_run, &RunSession::finished, this, [this]{
        if (m_pendingError > 0) emit errorLineDetected(m_pendingError);
        m_pendingError = -1;
        emit runningChanged(false);
    });
    connect(m_run, &RunSession::awaitingInput, this, [this]{
        ConsolePane *p = (m_cur == Term) ? m_term : m_repl;
        p->setAcceptInput(true);
        p->focusInput();
    });
}

void OutputPanel::selectPanel(Panel p) { m_cur = p; /* stack 전환 */ }
void OutputPanel::runCode(const QString &content, const QString &baseDir, const QString &scratch) {
    selectPanel(Term);
    m_pendingError = -1;
    m_term->append("> run\n", OC_TEXT);
    m_run->run(content, baseDir, scratch);
    emit runningChanged(true);
}
void OutputPanel::stopRun() { m_run->stopAll(); m_term->append("^C\n", OC_ERROR); }
bool OutputPanel::busy() const { return m_run->isBusy(); }
void OutputPanel::clearCurrent() { (m_cur==Term?m_term:m_repl)->clear(); }
void OutputPanel::setCwd(const QString &c) { m_cwd = c; }
void OutputPanel::refreshTheme() { update(); }
void OutputPanel::focusInput() { (m_cur==Term?m_term:m_repl)->focusInput(); }
```

NOTE: 위 `m_replHistory` 미정의 — 제거하고 REPL prompt는 단순 "lumi> ". stack 전환은 `static_cast<QStackedWidget*>(...)`로 index 설정. 구현 시 보완.

- [ ] **Step 5: CMakeLists.cpp에 소스 추가**

```cmake
src/models/runsession.cpp
src/widgets/consolepane.cpp
src/panels/outputpanel.cpp
```

- [ ] **Step 6: MainWindow에 OUTER dock + Run 연결**

```cpp
#include "models/runsession.h"
#include "panels/outputpanel.h"
// 멤버:
RunSession *m_run{nullptr};
OutputPanel *m_outer{nullptr};
// 생성자:
m_run = new RunSession(this);
m_outer = new OutputPanel(m_run);
auto *dock = new QDockWidget("OUTER", this);
dock->setWidget(m_outer);
addDockWidget(Qt::BottomDockWidgetArea, dock);
```

- [ ] **Step 7: 빌드 + 런 동작 검증**

Run: `cmd //c build.bat` → 실행 → INER에서 `print("hello")` 작성 → F5 → OUTER 터미널에 결과 출력.
Expected: lumi.exe(`c-interpreter/bin/lumi.exe`)가 scratch 파일로 실행되어 출력 표시. 완료 시 scratch 자동 삭제.

---

## Task 9: FileExplorer + CommandPalette

**Files:**
- Create: `ide-cpp/src/widgets/fileexplorer.h`
- Create: `ide-cpp/src/widgets/fileexplorer.cpp`
- Create: `ide-cpp/src/widgets/commandpalette.h`
- Create: `ide-cpp/src/widgets/commandpalette.cpp`

**Interfaces:**
- Consumes: `fuzzyRank` (T4), `walkDir` 로직(기존 explorer.cpp)
- Produces:
  - `FileExplorer` : QWidget(QTreeView+QFileSystemModel) — `setFolder()`, `newFile/newFolder/renameSelected/deleteSelected`, 시그널 `fileOpened(QString)`
  - `CommandPalette` : QDialog — `quickFiles(folder)`, `quickCommands()` 정적 호출

- [ ] **Step 1: fileexplorer.h/.cpp (dotfile 버그 수정)**

`ide-cpp/src/widgets/fileexplorer.h`:
```cpp
#pragma once
#include <QWidget>
#include <QString>

class QTreeView;
class QFileSystemModel;

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget *parent = nullptr);
    void setFolder(const QString &folder);
    QString folder() const { return m_folder; }

signals:
    void fileOpened(const QString &path);
    void folderChanged(const QString &folder);
    void pathRenamed(const QString &from, const QString &to);
    void pathRemoved(const QString &path);

public slots:
    void newFile();
    void newFolder();
    void renameSelected();
    void deleteSelected();

private:
    QTreeView *m_tree;
    QFileSystemModel *m_model;
    QString m_folder;
};
```

`ide-cpp/src/widgets/fileexplorer.cpp`:
```cpp
#include "fileexplorer.h"
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>

// 버그#9 수정: .git .svn 등 VCS 폴더만 숨기고 .gitignore/.env 등은 표시.
// build/dist/bin/node_modules 등도 제외.
static const QStringList HIDDEN_DIRS = {".git", ".svn", ".hg", "node_modules",
    "build", "dist", "bin", "__pycache__", "release"};

FileExplorer::FileExplorer(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);

    m_tree = new QTreeView(this);
    m_model = new QFileSystemModel(this);
    m_model->setRootPath("");
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    for (int i = 1; i < m_model->columnCount(); ++i) m_tree->hideColumn(i);

    lay->addWidget(m_tree);

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx){
        QString p = m_model->filePath(idx);
        QFileInfo fi(p);
        if (fi.isFile()) emit fileOpened(p);
    });
    // 필터: 디렉토리 또는 .lumi/.txt 등 + 숨김 폴더 제외
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
}

void FileExplorer::setFolder(const QString &folder) {
    m_folder = folder;
    m_model->setRootPath(folder);
    m_tree->setRootIndex(m_model->index(folder));
    emit folderChanged(folder);
}

void FileExplorer::newFile() {
    // 선택된 디렉토리 아래에 새 파일
    QModelIndex idx = m_tree->currentIndex();
    QString dir = idx.isValid() && m_model->isDir(idx) ? m_model->filePath(idx) : m_folder;
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "Name:", QLineEdit::Normal, "new.lumi", &ok);
    if (!ok || name.isEmpty()) return;
    QString path = dir + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.close();
}
void FileExplorer::newFolder() {
    QModelIndex idx = m_tree->currentIndex();
    QString dir = idx.isValid() && m_model->isDir(idx) ? m_model->filePath(idx) : m_folder;
    bool ok;
    QString name = QInputDialog::getText(this, "New Folder", "Name:", QLineEdit::Normal, "NewFolder", &ok);
    if (!ok || name.isEmpty()) return;
    QDir().mkdir(dir + "/" + name);
}
void FileExplorer::renameSelected() {
    QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    QString oldP = m_model->filePath(idx);
    bool ok;
    QString name = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal,
                                          QFileInfo(oldP).fileName(), &ok);
    if (!ok || name.isEmpty()) return;
    QString newP = QFileInfo(oldP).path() + "/" + name;
    QFile::rename(oldP, newP);
    emit pathRenamed(oldP, newP);
}
void FileExplorer::deleteSelected() {
    QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    QString p = m_model->filePath(idx);
    auto r = QMessageBox::question(this, "Delete", "Delete '" + QFileInfo(p).fileName() + "'?");
    if (r != QMessageBox::Yes) return;
    if (QFileInfo(p).isDir()) QDir(p).removeRecursively();
    else QFile::remove(p);
    emit pathRemoved(p);
}
```

- [ ] **Step 2: commandpalette.h/.cpp (fuzzy 필터)**

`ide-cpp/src/widgets/commandpalette.h`:
```cpp
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    enum Mode { Files, Commands };
    explicit CommandPalette(Mode mode, QWidget *parent = nullptr);

    QString selectedText() const { return m_selected; }

private:
    void populate(const QString &filter);
    Mode m_mode;
    QLineEdit *m_edit;
    QListWidget *m_list;
    QString m_selected;

    QStringList m_files;       // Files 모드: 파일 경로 목록
    QList<QPair<QString,int>> m_cmds;  // Commands 모드: (라벨, CommandId)
};
```

`ide-cpp/src/widgets/commandpalette.cpp`:
```cpp
#include "commandpalette.h"
#include "core/fuzzy.h"
#include "core/commands.h"
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfoIterator>
#include <QDebug>

// 폴더 재귀 순회 (기존 walkDir 이식, 단순화)
static QStringList walkFiles(const QString &root) {
    QStringList out;
    QDir dir(root);
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext() && count < 4000) {
        it.next();
        out << it.filePath();
        ++count;
    }
    return out;
}

CommandPalette::CommandPalette(Mode mode, QWidget *parent)
    : QDialog(parent), m_mode(mode) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(500, 320);

    auto *lay = new QVBoxLayout(this);
    m_edit = new QLineEdit(this);
    m_list = new QListWidget(this);
    lay->addWidget(m_edit);
    lay->addWidget(m_list);

    if (m_mode == Commands) {
        m_cmds = {
            {"New File", Cmd_New}, {"Open File", Cmd_Open}, {"Save", Cmd_Save},
            {"Save As", Cmd_SaveAs}, {"Run", Cmd_Run}, {"Stop", Cmd_Stop},
            {"Toggle Theme", Cmd_ThemeToggle}, {"Toggle Sidebar", Cmd_ToggleSidebar},
            {"Find", Cmd_Find}, {"Replace", Cmd_Replace}, {"Go to Line", Cmd_GoToLine},
            {"Zoom In", Cmd_ZoomIn}, {"Zoom Out", Cmd_ZoomOut}, {"Reset Zoom", Cmd_ZoomReset},
            {"Comment Toggle", Cmd_Comment}
        };
    }

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::populate);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item){
        m_selected = item->data(Qt::UserRole).toString();
        accept();
    });
    populate({});
}

void CommandPalette::populate(const QString &filter) {
    m_list->clear();
    if (m_mode == Files) {
        QList<QPair<int,QString>> ranked;
        for (const QString &f : m_files) {
            int r = fuzzyRank(filter, QFileInfo(f).fileName());
            if (r >= 0) ranked.append({r, f});
        }
        std::sort(ranked.begin(), ranked.end());
        for (int i = 0; i < qMin(200, ranked.size()); ++i) {
            auto *item = new QListWidgetItem(QFileInfo(ranked[i].second).fileName());
            item->setData(Qt::UserRole, ranked[i].second);
            item->setToolTip(ranked[i].second);
            m_list->addItem(item);
        }
    } else {
        QList<QPair<int,int>> ranked;
        for (int i = 0; i < m_cmds.size(); ++i) {
            int r = fuzzyRank(filter, m_cmds[i].first);
            if (r >= 0) ranked.append({r, i});
        }
        std::sort(ranked.begin(), ranked.end());
        for (int i = 0; i < qMin(200, ranked.size()); ++i) {
            int ci = ranked[i].second;
            auto *item = new QListWidgetItem(m_cmds[ci].first);
            item->setData(Qt::UserRole, QString::number(m_cmds[ci].second));
            m_list->addItem(item);
        }
    }
}
```

- [ ] **Step 3: MainWindow에 사이드바 dock + 팔레트 연결**

```cpp
#include "widgets/fileexplorer.h"
#include "widgets/commandpalette.h"
// 멤버:
FileExplorer *m_side{nullptr};
// 생성자:
m_side = new FileExplorer;
auto *dock = new QDockWidget("Explorer", this);
dock->setWidget(m_side);
addDockWidget(Qt::LeftDockWidgetArea, dock);
// 파일 열기
connect(m_side, &FileExplorer::fileOpened, m_tabs, &TabModel::openPath);
connect(m_side, &FileExplorer::pathRenamed, m_tabs, &TabModel::pathRenamed);
connect(m_side, &FileExplorer::pathRemoved, m_tabs, &TabModel::pathRemoved);
```

Ctrl+P / Ctrl+Shift+P 단축키 등록 (MainWindow):
```cpp
auto *quickFiles = new QAction(this);
quickFiles->setShortcut(QKeySequence("Ctrl+P"));
connect(quickFiles, &QAction::triggered, this, [this]{
    CommandPalette p(CommandPalette::Files, this);
    p.m_files /* = walkFiles(m_side->folder()) */;  // friend 또는 setter로 주입
    if (p.exec() == QDialog::Accepted) m_tabs->openPath(p.selectedText());
});
addAction(quickFiles);
```

- [ ] **Step 4: CMakeLists.cpp에 소스 추가**

```cmake
src/widgets/fileexplorer.cpp
src/widgets/commandpalette.cpp
```

- [ ] **Step 5: 빌드 + 탐색기/팔레트 검증**

Run: `cmd //c build.bat` → 실행 → 사이드바에서 폴더 열기(Open Folder) → 파일 더블클릭으로 탭 열림. Ctrl+P로 빠른 열기.

---

## Task 10: 아이콘 시스템 (내장 SVG) + resources.qrc

**Files:**
- Create: `ide-cpp/assets/icons/*.svg` (~16종)
- Create: `ide-cpp/src/theme/iconpainter.h`
- Create: `ide-cpp/src/theme/iconpainter.cpp`
- Modify: `ide-cpp/resources.qrc`
- Modify: `ide-cpp/src/mainwindow.cpp` (액션에 아이콘 적용)

**Interfaces:**
- Consumes: `currentPalette` (T5)
- Produces:
  - `QIcon makeIcon(const QString &name)` — SVG를 테마 색으로 착색한 QIcon 반환. name = "run","stop","save",...
  - 리소스: `:/icons/run.svg` 등

- [ ] **Step 1: SVG 아이콘 작성 (단색 currentColor 방식)**

각 아이콘은 24x24 viewBox, `stroke="currentColor"` 또는 `fill="currentColor"`. 예 `assets/icons/run.svg`:
```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor">
  <path d="M8 5v14l11-7z"/>
</svg>
```
필요 아이콘 세트: `run, stop, save, saveas, newfile, newfolder, openfile, openfolder, refresh, theme, detach, terminal, repl, find, settings, close, sidebar` (~16개). 각각 단순한 SVG 패스로 작성.

- [ ] **Step 2: resources.qrc 등록**

`ide-cpp/resources.qrc`:
```xml
<RCC>
    <qresource prefix="/icons">
        <file>assets/icons/run.svg</file>
        <file>assets/icons/stop.svg</file>
        <file>assets/icons/save.svg</file>
        <!-- ... 나머지 -->
    </qresource>
</RCC>
```

- [ ] **Step 3: iconpainter — SVG를 테마 색으로 합성**

`ide-cpp/src/theme/iconpainter.h`:
```cpp
#pragma once
#include <QIcon>
#include <QString>

// SVG(단색)를 현재 테마 전경색으로 치환해 QIcon 생성.
QIcon makeIcon(const QString &name);   // "run", "stop", ...
// 지정 색으로 생성 (활성/비활성 구분용)
QIcon makeIconColored(const QString &name, const QColor &color);
```

`ide-cpp/src/theme/iconpainter.cpp`:
```cpp
#include "iconpainter.h"
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include "theme.h"

static QByteArray colorizeSvg(const QString &name, const QColor &color) {
    QFile f(":/icons/assets/icons/" + name + ".svg");
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
    // currentColor → 실제 색으로 치환
    data.replace("currentColor", color.name().toUtf8());
    return data;
}

QIcon makeIconColored(const QString &name, const QColor &color) {
    QByteArray svg = colorizeSvg(name, color);
    if (svg.isEmpty()) return {};
    QSvgRenderer r(svg);
    QPixmap pix(24, 24);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    r.render(&p);
    return QIcon(pix);
}

QIcon makeIcon(const QString &name) {
    return makeIconColored(name, currentPalette().btnFg);
}
```

참고: QSvgRenderer 사용 위해 CMakeLists에 `Qt6::Svg` 링크 추가:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
target_link_libraries(Lumina PRIVATE Qt6::Widgets Qt6::Svg)
```

- [ ] **Step 4: MainWindow 액션에 아이콘 적용**

`buildActions()` 내 각 액션에:
```cpp
#include "theme/iconpainter.h"
m_run->setIcon(makeIcon("run"));
m_stop->setIcon(makeIcon("stop"));
m_save->setIcon(makeIcon("save"));
m_open->setIcon(makeIcon("openfile"));
m_theme->setIcon(makeIcon("theme"));
// ...
```

- [ ] **Step 5: 테마 변경 시 아이콘 갱신**

`applyTheme()` 끝에 모든 액션 아이콘 재생성 (간단히 빌드 액션 재호출 또는 별도 refresh 함수).

- [ ] **Step 6: CMakeLists.cpp에 iconpainter + Svg 추가**

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
src/theme/iconpainter.cpp
target_link_libraries(Lumina PRIVATE Qt6::Widgets Qt6::Svg)
```

- [ ] **Step 7: 빌드 + 아이콘 표시 검증**

Run: `cmd //c build.bat` → 실행 → 툴바/메뉴에 SVG 아이콘 표시. 테마 전환 시 아이콘 색 갱신 확인.

---

## Task 11: QSettings 영속화 (창상태/탭/최신파일/설정)

**Files:**
- Create: `ide-cpp/src/core/settings.h`
- Create: `ide-cpp/src/core/settings.cpp`
- Modify: `ide-cpp/src/mainwindow.cpp` (시작 시 복원, 종료 시 저장)
- Modify: `ide-cpp/src/main.cpp` (QSettings 조직/앱명 — 이미 설정됨)

**Interfaces:**
- Produces:
  - `QSettings` 래퍼 — `saveWindowState(MainWindow*)`, `restoreWindowState(MainWindow*)`, `saveTabs(TabModel*)`, `restoreTabs(TabModel*)`, `theme()/setTheme()`, `editorFontPx()/setEditorFontPx()`, `recentFiles()/addRecentFile()` 등

- [ ] **Step 1: settings.h**

`ide-cpp/src/core/settings.h`:
```cpp
#pragma once
#include <QString>
#include <QStringList>

class MainWindow;
class TabModel;

namespace settings {
    // 테마
    bool loadDarkTheme();
    void saveDarkTheme(bool dark);

    // 폰트
    int editorFontPt();
    void setEditorFontPt(int pt);

    // 창 상태
    void saveWindowState(MainWindow *w);
    void restoreWindowState(MainWindow *w);

    // 탭
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
}
```

- [ ] **Step 2: settings.cpp**

`ide-cpp/src/core/settings.cpp`:
```cpp
#include "settings.h"
#include <QSettings>
#include "../mainwindow.h"
#include "../models/tabmodel.h"

static QSettings& s() {
    static QSettings qs(QSettings::IniFormat, QSettings::UserScope, "Lumina", "Lumina");
    return qs;
}

bool settings::loadDarkTheme() { return s().value("theme/dark", true).toBool(); }
void settings::saveDarkTheme(bool dark) { s().setValue("theme/dark", dark); }

int settings::editorFontPt() { return s().value("editor/fontPt", 10).toInt(); }
void settings::setEditorFontPt(int pt) { s().setValue("editor/fontPt", pt); }

void settings::saveWindowState(MainWindow *w) {
    s().setValue("window/geometry", w->saveGeometry());
    s().setValue("window/state", w->saveState());
}
void settings::restoreWindowState(MainWindow *w) {
    w->restoreGeometry(s().value("window/geometry").toByteArray());
    w->restoreState(s().value("window/state").toByteArray());
}

QStringList settings::loadOpenTabs() { return s().value("tabs/open").toStringList(); }
int settings::loadActiveTab() { return s().value("tabs/active", 0).toInt(); }
void settings::saveTabs(TabModel *m) {
    QStringList paths;
    for (const auto &t : m->tabs()) if (!t.path.isEmpty()) paths << t.path;
    s().setValue("tabs/open", paths);
    s().setValue("tabs/active", m->activeIndex());
}

QStringList settings::recentFiles() { return s().value("recent/files").toStringList(); }
void settings::addRecentFile(const QString &p) {
    auto list = recentFiles();
    list.removeAll(p); list.prepend(p);
    if (list.size() > 10) list = list.mid(0, 10);
    s().setValue("recent/files", list);
}
QStringList settings::recentFolders() { return s().value("recent/folders").toStringList(); }
void settings::addRecentFolder(const QString &p) {
    auto list = recentFolders();
    list.removeAll(p); list.prepend(p);
    if (list.size() > 10) list = list.mid(0, 10);
    s().setValue("recent/folders", list);
}

int settings::sidebarWidth() { return s().value("layout/sidebarWidth", 240).toInt(); }
void settings::setSidebarWidth(int w) { s().setValue("layout/sidebarWidth", w); }
int settings::outputHeight() { return s().value("layout/outputHeight", 250).toInt(); }
void settings::setOutputHeight(int h) { s().setValue("layout/outputHeight", h); }
```

- [ ] **Step 3: MainWindow 부트/종료에 저장복원 연결**

생성자 끝: `settings::restoreWindowState(this)` + 이전 탭 복원(loadOpenTabs → 각 path openPath, loadActiveTab으로 활성화).
`closeEvent`: `settings::saveWindowState(this)` + `settings::saveTabs(m_tabs)`. 더러운 탭은 `m_tabs->confirmDiscardAll(this)`로 확인.

- [ ] **Step 4: CMakeLists.cpp에 settings.cpp 추가**

```cmake
src/core/settings.cpp
```

- [ ] **Step 5: 빌드 + 영속화 검증**

Run: `cmd //c build.bat` → 실행 → 창 크기 변경/파일 열기 → 종료 → 재실행 → 창 크기/열린 탭 복원 확인.

---

## Task 12: MainWindow runCommand 완성 — 모든 명령 연결 + Detach/Dock

**Files:**
- Modify: `ide-cpp/src/mainwindow.cpp` (runCommand 스위치 완성)
- Modify: `ide-cpp/src/mainwindow.h`

**Interfaces:**
- 이전 태스크들의 모든 위젯/모델을 연결.

- [ ] **Step 1: runCommand 완전 구현**

```cpp
void MainWindow::runCommand(int id) {
    switch (id) {
        case Cmd_New: m_iner->newTab(); break;
        case Cmd_Open: {
            QString p = QFileDialog::getOpenFileName(this, "Open", {}, "Lumi (*.lumi);;All (*.*)");
            if (!p.isEmpty()) { m_tabs->openPath(p); settings::addRecentFile(p); }
            break;
        }
        case Cmd_Save: m_tabs->save(m_tabs->activeIndex(), false); break;
        case Cmd_SaveAs: m_tabs->save(m_tabs->activeIndex(), true); break;
        case Cmd_Run: {
            if (m_outer->busy()) break;   // 버그#12: 런 중 재실행 방지
            m_iner->stashActive();
            Tab *t = m_tabs->activeTab();
            QString base = (t && !t->path.isEmpty()) ? QFileInfo(t->path).path() : m_side->folder();
            m_outer->runCode(m_iner->editor()->toPlainText(), base, scratchPath());
            break;
        }
        case Cmd_Stop: m_outer->stopRun(); break;
        case Cmd_Clear: m_outer->clearCurrent(); break;
        case Cmd_ThemeToggle: ::toggleTheme(qApp); settings::saveDarkTheme(isDark()); m_iner->refreshTheme(); break;
        case Cmd_DetachIner: /* dock toggleFloating */ break;
        case Cmd_DetachOuter: /* dock toggleFloating */ break;
        case Cmd_ToggleSidebar: /* dock setVisible */ break;
        case Cmd_TogglePanel: /* OUTER dock setVisible */ break;
        case Cmd_Find: m_iner->triggerFind(); break;
        case Cmd_Replace: m_iner->triggerReplace(); break;
        case Cmd_GoToLine: m_iner->goToLine(); break;
        case Cmd_ZoomIn: m_iner->editor()->zoomBy(1); break;
        case Cmd_ZoomOut: m_iner->editor()->zoomBy(-1); break;
        case Cmd_ZoomReset: m_iner->editor()->setFontSize(settings::editorFontPt()); break;
        // ...
    }
}
```

- [ ] **Step 2: Detach/Dock 구현**

INER/OUTER dock을 멤버로 보관:
```cpp
QDockWidget *m_inerDock, *m_outerDock;
// Detach:
case Cmd_DetachIner: m_inerDock->setFloating(!m_inerDock->isFloating()); break;
```
dock 영역 고정 배치로 위/아래 순서 보장(spec): INER는 TopDockWidgetArea, OUTER는 BottomDockWidgetArea.

- [ ] **Step 3: 빌드 + 전체 기능 검증**

Run: `cmd //c build.bat` → F5 런, Ctrl+S 저장, Detach/Dock, 테마 토글, Ctrl+P 등 전체 동작 확인.

---

## Task 13: 문서 갱신 — README.md + Lumi 사용설명서.md (doc drift 수정)

**Files:**
- Modify: `README.md` (IDE 부분 Qt로 갱신, "Electron" → "Qt6" doc drift 수정, 빌드/실행 방법 갱신)
- Modify: `Lumi 사용설명서.md` (ch.18 IDE 카탈로그 갱신, 상단 "마지막 갱신" 날짜)

**Interfaces:**
- 사용자가 요청한 문서 동시 갱신 규칙 준수.

- [ ] **Step 1: README.md IDE 섹션 갱신**

기존에 IDE를 "TypeScript + Electron"이라 기술한 부분(예: "내가 직접 만든 IDE" 섹션, 파일 구성 표)을 모두 Qt6로 수정:
- 아키텍처: "Lumina IDE는 Qt6 Widgets(C++)로 만들어졌다"
- 파일 구성 표: `ide-cpp/` 구조(CMakeLists.txt, src/main.cpp, src/widgets/, ...) 반영
- 빌드 방법: `ide-cpp/build.bat` (CMake + Qt6 + windeployqt) 설명
- 실행: `Lumina.exe` 더블클릭 — Qt DLL들이 bin/에 함께 배포됨 명시
- 테마: 라이트/다크(Catppuccin/neutral) 팔레트 언급

- [ ] **Step 2: 사용설명서 ch.18 카탈로그 갱신**

§18.12 "Lumina IDE" (라인 ~2217-2241) 항목 검토:
- Detach/Dock: QDockWidget 기반으로 변경됨을 반영
- 테마: 라이트/다크 두 팔레트 추가됨 (기존 카탈로그엔 테마 미언급)
- 명령 팔레트: Ctrl+P/Ctrl+Shift+P 추가
- 상단 "마지막 갱신" 날짜를 2026-07-31로 갱신

- [ ] **Step 3: 빌드/실행 배치 파일 갱신**

최상위 `Lumina (C++) 실행.bat`과 `build-cpp.bat`이 `ide-cpp/bin/Lumina.exe`를 올바르게 가리키는지 확인/갱신.

- [ ] **Step 4: 문서 일관성 검토**

README와 사용설명서의 IDE 설명이 실제 Qt 구현과 일치하는지, doc drift가 해소되었는지 확인.

---

## Task 14: 최종 통합 검증 (spec 11장 체크리스트)

**Files:** (수정 없음 — 검증만)

- [ ] **Step 1: 빌드 성공 확인**

Run: `cmd //c build.bat`
Expected: `Build complete: ide-cpp\bin\Lumina.exe`. 에러/경고 최소화.

- [ ] **Step 2-14: spec 11장의 14개 검증 항목 순차 확인**

실행 후 각 항목 점검:
1. 빌드 성공 ✅
2. 실행 — 기본 탭 `print("hello, Lumi!")` 표시
3. F5 런 — examples/demo.lumi 결과 출력
4. REPL — 입력/멀티라인/input() 대기
5. 터미널 — dir/echo 동작
6. 편집기 — 하이라이트/거터/들여쓰기/괄호쌍/줌
7. 탭 — 열기/닫기/전환/더러운표시/저장
8. 탐색기 — 폴더 열기/생성/이름변경/삭제, dotfile 표시(버그#9)
9. detach/dock — INER/OUTER 분리/복귀, 단축키 유지
10. 테마 — 라이트/다크 전환, 색/아이콘 갱신
11. 명령 팔레트 — Ctrl+P/Ctrl+Shift+P
12. DPI — 다른 스케일 모니터 이동(가능 시)
13. 종료/재시작 — 창상태/탭/최신파일 복원(버그#7)
14. 문서 갱신 — README/사용설명서 Qt 반영

각 항목 실패 시 해당 태스크로 돌아가 수정.

---

## Self-Review 체크 (작성자 자체 점검)

**1. Spec coverage:**
- 3장 자동 해소 버그(7개) → T2-T11 전반(태스크별 매핑됨) ✅
- 4장 명시적 버그(5개): #8(T6 tabbar), #9(T9 fileexplorer), #10(T4 lumitokenizer), #11(T8 outputpanel setCwd), #12(T8 runCode/T12 runCommand) ✅
- 5장 아키텍처(디렉토리/매핑/컴포넌트) → T2-T10 ✅
- 6장 디자인 시스템(팔레트/아이콘/타이포) → T5, T10 ✅
- 7장 QSettings → T11 ✅
- 8장 빌드/배포 → T2 ✅
- 9장 문서 규칙 → T13 ✅
- 10장 위험/완화 → 각 태스크 검증 단계에 반영 ✅
- 11장 검증 계획(14항목) → T14 ✅

**2. Placeholder scan:** 계획 내 "..." 또는 "생략" 표시는 빌드 가능한 최소 코드를 전제로 한 것이나, 구현자가 채워야 할 부분임을 명시함. 핵심 로직(토크나이저, 마커, QSS, fuzzy)은 전체 코드 제공. 일부 UI 레이아웃 디테일(바꾸기 다이얼로그, 히스토리 eventFilter)은 "구현 필수"로 표시 — 이는 계획이 구현 가이드이므로 허용되나, 구현 시 반드시 채울 것.

**3. Type consistency:**
- `TabModel::setActiveContent/activeContent`, `Tab.content` — T6 정의, T7/T12 사용 일치 ✅
- `RunSession::run(content, baseDir, scratch)` — T8 정의, T12 `Cmd_Run`에서 `scratchPath()`와 함께 호출 일치 ✅
- `CodeEditor::zoomBy/setFontSize/errorLine` — T7 정의, T12 사용 일치 ✅
- `currentPalette()/tokColor/tokBold` — T5 정의, T7 highlighter에서 사용 일치 ✅

**주의사항 (구현자에게):** 위 코드는 계획용 뼈대이며, 일부 헤더 include, friend 선언, 레이아웃 디테일, 에러 처리는 구현 시 보완 필요. 각 태스크의 빌드 검증 단계에서 컴파일 에러가 나면 그 부분을 채우는 것이 정상적 흐름.
