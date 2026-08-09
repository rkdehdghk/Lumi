# Lumina IDE — Win32 → Qt6 재작성 설계

**날짜:** 2026-07-31
**상태:** 설계 (사용자 승인 대기)
**대상:** `ide-cpp/` 내부 코드를 Qt6 Widgets로 전면 재작성 + 잔버그 수정 + 디자인 업그레이드

---

## 1. 배경과 목표

현재 Lumina IDE(`ide-cpp/src/`)는 "외부 의존성 없는 단일 exe"를 철학으로 **순수 Win32 API**(`user32`/`gdi32`/`comctl32`)로 작성되었다. 사용자 요청에 따라 이를 **Qt6 Widgets로 전면 재작성**하여:

1. **잔버그 일괄 해소** — Win32 수동 관리로 누적된 DPI 미대응, GDI/폰트 누수, 종료 시 스레드 경쟁 등을 Qt의 자동 관리(RAII, High DPI, QProcess 시그널)로 근본 제거.
2. **디자인 업그레이드** — 라이트/다크 팔레트 정교화, 내장 SVG 아이콘 시스템, QSS 폴리시 통일로 모던한 외관.
3. **기능 1:1 보존** — 탭, 코드 편집기+거터+신택스 하이라이트, 터미널/REPL, 파일 탐색기, 명령 팔레트, Detach/Dock, 단축키, 라이트/다크 테마를 모두 유지.

### 비목표 (이번 작업에서 안 함)
- Lumi 언어 자체의 기능 추가/변경 (언어 인터프리터는 건드리지 않음).
- 새 에디터 기능(미니맵, 멀티커서 등) 추가 — 요청 범위 아님.
- 단일 exe 배포(static Qt) — 폴더 배포로 결정.

---

## 2. 기존 아키텍처 요약 (참고용)

| 파일 | 책임 | 줄 수(대략) |
|---|---|---|
| `lumina.h` | 공유 헤더, `Theme` 구조체, 글로벌 HWND, `IDC_*` 명령 enum | 203 |
| `main.cpp` | 메인 프레임, 레이아웃/사시, detach/dock, `runCommand` 디스패치, 메시지 루프, DPI/테마 부트 | 943 |
| `editor.cpp` | INER: 탭바, 거터(`LuminaGutter`), RichEdit(`MSFTEDIT_CLASS`) 편집기, 신택스 하이라이트, 자동들여쓰기, 괄호쌍, 찾기/바꾸기 | 776 |
| `console.cpp` | OUTER: 터미널/REPL 탭, 자식 프로세스 스폰/파이프, 마커 프로토콜(`\x1eINPUT`/`\x1eDONE`/`\x1eEOT`), 스크래치 파일 런, OS 셸 패스스루 | 839 |
| `explorer.cpp` | 사이드바 `WC_TREEVIEWW`, lazy fill, 파일 생성/이름변경/삭제, `walkDir` | 385 |
| `quick.cpp` | 모달 팝업: `askText`, `popupMenu`, `quickFiles`(Ctrl+P), `quickCommands`(Ctrl+Shift+P) | 442 |
| `lang.cpp` | Lumi 토크나이저(키워드/문자열/숫자/주석). Qt 독립적 순수 C++ | 116 |
| `util.cpp` | UTF-8↔wstring 변환, 파일 I/O, 경로 헬퍼, fuzzy 매처, INI 설정, 테마 색 | 250 |

**통신 방식:** 대부분 글로벌(`g_tabs`, `g_active`, `g_iner`, `g_outer` 등)을 통한 함수 호출. detach는 `SetParent`로 같은 자식 HWND를 다른 프레임으로 옮기는 트릭(동기화 상태 없음 — 깔끔한 부분).

---

## 3. 재작성으로 자동 해소되는 버그

다음은 Qt 위젯/QProcess/QSettings로 옮기면 **자동으로** 사라지는 기존 Win32 약점들:

| # | 기존 버그 | 위치 | Qt로 해소 방식 |
|---|---|---|---|
| 1 | DPI 변경 미대응 (`WM_DPICHANCED` 핸들러 없음, `g_dpi` 부트 시 1회 샘플) | `main.cpp:836-842` | Qt High DPI 자동 스케일링 |
| 2 | 폰트/GDI 객체 누수 + 매 `WM_CTLCOLOR`마다 브러시 재생성 | `main.cpp:816-829`, `console.cpp:671`, `quick.cpp:87,317` | `QFont`/`QBrush` RAII 자동 관리 |
| 3 | 종료 시 리더스레드 경쟁 → 메모리 누수 가능 | `console.cpp:70-76` | `QProcess::finished` 시그널로 안전 종료 |
| 4 | 스크래치 파일 누수(빠른 재실행 시) | `console.cpp:373` | 임시파일 RAII + `finished`에서 정리 |
| 5 | 스크롤백 트림 O(n²) (`chunks.erase(begin())` 루프) | `console.cpp:215` | `QPlainTextEdit` 내부 관리 |
| 6 | 접근성 전무 (커스텀 바/탭/거터에 UI Automation 없음) | 전역 | Qt 위젯 기본 UI Automation |
| 7 | 창상태/최신파일/탭 미저장 (INI엔 테마+폰트사이즈 2개만) | `util.cpp:156-187` | `QSettings`로 전체 저장 |

---

## 4. 명시적으로 고칠 로직 버그

재작성 과정에서 명시적으로 수정하는 로직 버그:

| # | 버그 | 기존 위치 | 수정 내용 |
|---|---|---|---|
| 8 | 활성 탭 상단 액센트 색이 `statusBg` 사용(복붙 버그, 액센트여야 함) | `editor.cpp:490` | `accent` 색 사용 |
| 9 | dotfile(`.` 시작) 강제 숨김 → `.gitignore`/`.env` 안 보임 | `explorer.cpp:198` | `.git`, `.svn` 등만 숨기고 나머지 dotfile은 표시. (필요시 뷰 토글) |
| 10 | `identStart`가 `c >= 0x00C0` 조건으로 `×`(0x00D7), `÷`(0x00F7) 같은 기호를 식별자 시작으로 오판 | `lang.cpp:33` | Unicode 카테고리(`QChar::isLetter`) 기반으로 정확히 판정 |
| 11 | `conSetCwd`가 실행 중이면 cwd 갱신 묵시적 거부 → 새 폴더 열어도 터미널 cwd stale | `console.cpp:781-785` | 런/셸이 실행 중일 때 폴더를 바꾸려 하면 사용자에게 "Stop the running program first" 경고로 안내(가장 단순·안전). 유휴 상태에서는 즉시 적용. |
| 12 | 런 중 빠른 재실행 시 스크래시 충돌 | `console.cpp:373` | 이전 런 stop→wait 후 새 런 시작(스크래치 파일명 충돌 방지) |

**토크나이저는 현행 유지:** Lumi는 `//` 라인 주석만 지원(`/* */` 없음 — `lang.cpp:47` 및 문법 확인). 따라서 블록 주석 추가는 안 함.

---

## 5. 새 아키텍처 (Qt6)

### 5.1 디렉토리 구조

```
ide-cpp/                       # 기존 위치 그대로, 내부 교체
├── CMakeLists.txt             # Qt6 Widgets 찾기 + rc 리소스 + exe 타겟
├── resources.qrc              # SVG 아이콘 + exe 아이콘 Qt 리소스 묶음
├── assets/
│   ├── icons/                 # SVG 아이콘 (run, stop, save, folder, ...)
│   └── lumina.rc              # Windows exe 아이콘 바인딩 (기존 .ico 재사용)
├── src/
│   ├── main.cpp               # QApplication 진입, High DPI, 폰트, 부트
│   ├── core/                  # Qt 독립적 순수 로직 (기존 lang/util 이식)
│   │   ├── lumitokenizer.h/.cpp   # ← lang.cpp 거의 그대로 이식 (identStart 수정)
│   │   ├── fuzzy.h/.cpp           # ← util.cpp의 fuzzyRank 이식
│   │   ├── settings.h/.cpp        # QSettings 래퍼
│   │   └── paths.h/.cpp           # 인터프리터 경로 해석
│   ├── models/
│   │   ├── tabmodel.h/.cpp        # 탭 관리 (QObject 기반)
│   │   └── runsession.h/.cpp      # QProcess 세션 관리
│   ├── widgets/
│   │   ├── codeeditor.h/.cpp      # QPlainTextEdit 서브클래스 (거터+하이라이트 호스트)
│   │   ├── linenumberarea.h/.cpp  # 거터 영역
│   │   ├── lumihighlighter.h/.cpp # QSyntaxHighlighter (lumiTokenize 기반)
│   │   ├── tabbar.h/.cpp          # 커스텀 탭바
│   │   ├── consolepane.h/.cpp     # 터미널/REPL 출력+입력
│   │   ├── fileexplorer.h/.cpp    # QTreeView + QFileSystemModel
│   │   └── commandpalette.h/.cpp  # Ctrl+P / Ctrl+Shift+P
│   ├── panels/
│   │   ├── editorpanel.h/.cpp     # INER (탭바 + 코드편집) — dock 가능
│   │   └── outputpanel.h/.cpp     # OUTER (터미널/REPL) — dock 가능
│   ├── theme/
│   │   ├── theme.h/.cpp           # 라이트/다크 팔레트 정의 + QSS 생성
│   │   └── iconpainter.h/.cpp     # SVG → 테마 색 자동 착색
│   └── mainwindow.h/.cpp         # QMainWindow: 메뉴/툴바/상태바 + dock 레이아웃
├── build.bat                   # CMake + Qt + MSVC + windeployqt
└── bin/                        # 빌드 산출물 + Qt DLL들
```

**백업 정책:** 사용자가 "백업 없이 진행"을 선택했으므로 기존 `ide-cpp/src/*.cpp`는 덮어쓴다. (git이 아니므로 별도 복구 불가 — 사용자 책임 하에 진행.)

### 5.2 위젯 매핑 (Win32 → Qt)

| 기존 Win32 | 새 Qt 위젯 | 비고 |
|---|---|---|
| `LuminaMain` 오버랩 윈도우 | `QMainWindow` | 메뉴/툴바/상태바/중앙위젯 |
| `LuminaBar` (툴바/상태바/헤더 커스텀 페인트) | `QToolBar` + `QStatusBar` + 패널 헤더 `QWidget` | QSS 동일 룩, 접근성 자동 |
| `g_iner` (탭바+RichEdit+거터) | `EditorPanel`: `QWidget` { `TabBar` + `CodeEditor` + `LineNumberArea` } | `QDockWidget`으로 dock 가능 |
| `MSFTEDIT_CLASS` RichEdit | `CodeEditor`: `QPlainTextEdit` 서브클래스 | 코드용 단일폭 적합 |
| `LuminaGutter` | `LineNumberArea`: `QWidget` | `blockBoundingRect`로 정렬, 에러라인 표시 |
| 하이라이트(`CHARFORMAT2W`) | `LumiHighlighter`: `QSyntaxHighlighter` | `lumiTokenize` 재사용, 가시블록만 자동 재하이라이트 (성능 개선) |
| `WC_TREEVIEWW` 탐색기 | `QTreeView` + `QFileSystemModel` | lazy fill 자동, dotfile/build 필터 제어 (버그#9 수정) |
| RichEdit 출력 `g_out[P_TERM/P_REPL]` | `QPlainTextEdit`(읽기전용) | 색상 처리 |
| `EDIT` 입력 `g_cmd` | `QLineEdit` | 히스토리 ↑↓ |
| `CreateProcessW`+파이프+리더스레드 | `QProcess` | stdout/stderr 시그널, `finished`로 종료 (버그#3,4) |
| Detach/Dock (`SetParent`) | `QDockWidget::setFloating` | 상태 동기화 불필요. INER 위/OUTER 아래는 dock area 위치 |
| 사시 드래그 | `QSplitter` | 자동 리사이즈 커서/드래그/DPI (버그#1) |
| `quickFiles/quickCommands` | `CommandPalette`: `QDialog` { `QLineEdit` + `QListWidget` } | fuzzy 필터 재사용 |
| `LuminaTabs` 탭바 | `TabBar`: `QWidget` paintEvent | 더러운표시●, 닫기×, 컨텍스트메뉴, 액센트(버그#8 수정) |
| DPI `S()`/`g_dpi` | Qt High DPI (`AA_EnableHighDpiScaling`) | 버그#1 근본 제거 |
| `lumina.ini` (2개만) | `QSettings` | 창상태/사이즈/사이드바폭/패널높이/탭/최신파일/테마/폰트 저장 (버그#7) |

### 5.3 핵심 컴포넌트 설계

**MainWindow (`mainwindow.h/.cpp`)**
- `QMainWindow` 서브클래스. 메뉴바(File/Edit/View/Run/Help), `QToolBar`(Run/Stop/Save/Open/Theme/Detach), `QStatusBar`(위치/들여쓰기/언어/상태메시지).
- 중앙: `QSplitter`(수평) = [`QDockWidget`(사이드바 탐색기)] | [`QSplitter`(수직) = [`QDockWidget`(INER)] / [`QDockWidget`(OUTER)]].
- detach/dock: 각 패널을 `QDockWidget`으로, `toggleFloating()`로 분리/복귀. 두 패널 모두 분리 가능. INER가 `Qt::TopDockWidgetArea`, OUTER가 `Qt::BottomDockWidgetArea`에 고정 배치되어 분리→복귀 시 위/아래 순서가 항상 보존됨(기존 동작과 일치). 분리 시 각 도크의 지오메트리는 `QSettings`에 저장/복원.
- `runCommand(int id)` 단일 디스패치 진입점 유지 (메뉴/단축키/팔레트 공통).
- 종료 시 `QSettings`에 지오메트리/상태 저장.

**CodeEditor (`widgets/codeeditor.h/.cpp`)**
- `QPlainTextEdit` 서브클래스.
- `LineNumberArea`를 왼쪽 자식 위젯으로. `updateLineNumberAreaWidth`/`updateLineNumberArea`/`highlightCurrentLine`은 Qt 공식 예제 패턴 준용.
- 단축키: Tab→4스페이스, Enter→자동들여쓰기(`:` 후 +1레벨), 괄호쌍 `()[]{}`/`""` 자동완성, Ctrl+휠 줌, 닫는 괄호 타입오버.
- 폰트 줌(`edZoom`), 찾기/바꾸기(`QPlainTextEdit` + 커스텀 바 또는 `QInputDialog`), 줄번호 이동.
- 에러 표식: 해당 블록 배경색 + 밑줄. `setPendingErrorLine(int)`.

**LumiHighlighter (`widgets/lumihighlighter.h/.cpp`)**
- `QSyntaxHighlighter` 서브클래스. `highlightBlock(QString)`에서 `lumiTokenize` 호출 → `QTextCharFormat` 적용.
- 가시 블록만 자동 재하이라이트 → 대용량 파일 성능 버그(기존 `editor.cpp:108` 전체 재토크나이즈) 해소.
- 테마 변경 시 `setFormat` 색 갱신 + `rehighlight()`.

**ConsolePane / RunSession (`widgets/consolepane.h/.cpp`, `models/runsession.h/.cpp`)**
- 세 슬롯: 런(`CH_LUMI`), 터미널(`CH_SHELL`), REPL(`CH_REPL`).
- `RunSession`이 각각의 `QProcess`를 소유. stdout/stderr → `readyReadStandardOutput/Error` 시그널 → `outAppend`로 출력 위젯에 추가.
- 마커 프로토콜 유지: `\x1eINPUT\n` / `\x1eDONE\n` / `\x1eEOT\n`. `handleMarkers()`로 파싱.
- `input()` 대기 감지 → 입력창 활성화. 런 완료 → `QProcess::finished` → 스크래치 정리 + 에러라인 적용.
- REPL: `lumi.exe --repl <base>`. 멀티라인 블록 종료 `\n\x1eEOT\n`.
- 터미널(셸): `cmd.exe /d /s /c <cmd>`. `cls`/`exit`/`cd`/드라이브전환 수동 처리 유지.
- 인코딩: UTF-8 우선, 실패 시 `QStringDecoder`로 CP949/시스템코드페이지 폴백 (기존 3-try 로직을 Qt Decoder로 대체).
- 스크래치 파일: `%TEMP%/lumina-run-<pid>.lumi`. `finished`에서 삭제. 런 중 재실행 시 이전 stop→wait(버그#12).
- 입력 히스토리: 패널별 `QStringList`, ↑↓ 탐색.

**FileExplorer (`widgets/fileexplorer.h/.cpp`)**
- `QTreeView` + `QFileSystemModel`. `setNameFilters`로 build/dist/bin/node_modules 등 제외.
- dotfile 정책 수정(버그#9): `.git`, `.svn`만 숨기고 `.gitignore`/`.env` 등은 표시. 필요시 `setOption(QFileDialog::ShowDirsOnly)` 식 뷰 토글.
- 파일 생성/이름변경/삭제: `QFile`/`QDir` + 모델 `mkdir`/`remove`. 탭 경로 동기화(`tabsPathRenamed`/`tabsPathRemoved`).
- 빠른 열기를 위한 `walkDir` 재귀 수집 유지(`quickFiles`용).

**CommandPalette (`widgets/commandpalette.h/.cpp`)**
- `QDialog` { `QLineEdit`(필터) + `QListWidget`(결과) }. fuzzy 매칭(`fuzzyRank` 재사용), 200개 캡.
- Ctrl+P = 파일, Ctrl+Shift+P = 명령.

**TabBar (`widgets/tabbar.h/.cpp`)**
- `QWidget` 서브클래스, paintEvent로 직접 그림(기존 `LuminaTabs` 룩 유지).
- 활성 탭 상단 액센트 선(버그#8 수정: `accent` 색). 더러운 표시 `●`. 닫기 `×`. 컨텍스트메뉴(Close/CloseOthers/CloseAll). 좌클릭 선택, 중클릭 닫기.

**Theme (`theme/theme.h/.cpp`)**
- `Palette` 구조체(~40 토큰, 기존 `Theme` 대응). `paletteFor(bool dark)`로 두 세트 반환.
- `applyTheme(QApplication*, bool dark)`: `QPalette` + `qApp->setStyleSheet(QSS)`로 전역 적용.
- QSS: 버튼/툴바/탭/스크롤바/스플리터 핸들 등 폴리시 (둥근 모서리 6px, 호버, 평평한 스크롤바).
- 아이콘 착색: SVG를 테마 전경색으로 `QPainter` 합성.

---

## 6. 디자인 시스템

### 6.1 색 팔레트

**다크 (Catppuccin Mocha 계열):**
- `bg #1e1e2e`, `surface #252536`, `surfaceAlt #2a2a3e`, `border #3a3a52`
- `text #cdd6f4`, `textDim #7f849c`, `accent #89b4fa`, `accentText #1e1e2e`
- `error #f38ba8`, `success #a6e3a1`, `info #89dceb`
- 신택스: keyword `#cba6f7`(보라), string `#a6e3a1`(연두), number `#fab387`(살구), comment `#6c7086`(회), builtin `#f9e2af`(밝은노랑)

**라이트 (깔끔한 neutral):**
- `bg #fafafa`, `surface #ffffff`, `surfaceAlt #f4f4f5`, `border #e4e4e7`
- `text #18181b`, `textDim #71717a`, `accent #2563eb`, `accentText #ffffff`
- `error #dc2626`, `success #16a34a`, `info #0891b2`
- 신택스: keyword `#7c3aed`, string `#16a34a`, number `#ea580c`, comment `#a1a1aa`, builtin `#ca8a04`

**대비:** WCAG AA(본문 4.5:1, 큰글자 3:1) 준수 목표.

### 6.2 아이콘 시스템

내장 SVG(단색 `currentColor` 방식). 필요 아이콘 ~16종:
- Run(▶), Stop(■), Save(💾), SaveAs, NewFile, NewFolder, OpenFile, OpenFolder, Refresh, Theme(🌓), Detach/Dock(⊒), Terminal(□>_), REPL(>_), Find(🔍), Settings(⚙), Close(×), Sidebar(☰)
- 모두 코드로 SVG 패스 작성, 라이선스 무. `assets/icons/*.svg`로 저장, `resources.qrc`에 등록.
- 테마 색에 맞춰 `QPainter`로 자동 합성(라이트=어두운 아이콘, 다크=밝은 아이콘).

### 6.3 타이포그래피
- UI: `Segoe UI` (윈도우 기본). 한글은 Qt 폰트 매칭 폴백.
- 코드: `Consolas` 기본, 설정에서 변경 가능(폰트 다이얼로그).

---

## 7. 설정 & 영속화 (QSettings)

`%APPDATA%/Lumina/Lumina.ini` (`QSettings` IniFormat). 저장 항목:
- `theme` (light/dark), `editorFontSize`, `termFontSize` (기존 호환)
- `window/geometry`, `window/state` (QMainWindow saveGeometry/saveState)
- `layout/sidebarWidth`, `layout/outputHeight`, `layout/sidebarVisible`, `layout/outputVisible`
- `layout/inerDetached`, `layout/outerDetached`, `layout/inerGeometry`, `layout/outerGeometry`
- `tabs/open` (열린 파일 경로 목록), `tabs/active`, `tabs/cursors` (탭별 커서/스크롤)
- `recent/files`, `recent/folders`
- `editor/font`, `editor/wrap`, `editor/autoIndent`

---

## 8. 빌드 & 배포

### 8.1 Qt6 설치
`aqtinstall`(Python pip)로 MSVC용 Qt6 Widgets 설치. 설치 위치는 환경변수 또는 고정 경로.
```
pip install aqtinstall
aqt install-qt windows desktop 6.7.x win64_msvc2022_64 -m qtwidgets
```
(정확한 버전은 설치 시점 사용 가능 버전으로 결정.)

### 8.2 빌드 (`ide-cpp/build.bat`)
```
1. vcvars64.bat (MSVC 환경)
2. Qt 경로 자동 탐색 (QTDIR 환경변수 또는 aqt 설치 위치)
3. cmake -B build -S . -G Ninja
4. cmake --build build --config Release
5. windeployqt --release bin/Lumina.exe   # Qt DLL + platforms 플러그인 복사
```

### 8.3 배포 형태
`bin/Lumina.exe` + Qt DLL들 + `platforms/` 플러그인이 `bin/` 한 폴더에. 더블클릭 실행. 최상위 `Lumina (C++) 실행.bat`, `build-cpp.bat`은 `ide-cpp/bin/Lumina.exe`를 가리키도록 유지/갱신.

---

## 9. 문서화 규칙 준수

프로젝트 기존 규칙을 그대로 준수:
1. **UI는 모두 영어** — 버튼/라벨/탭/메뉴/상태메시지/다이얼로그 전부 영어(기존 규칙, `README.md:11-13`).
2. **문서 동시 갱신** — `README.md`, `Lumi 사용설명서.md`(갱신날짜 포함)의 IDE 부분을 Qt 버전으로 갱신. 특히 현재 문서는 여전히 IDE를 "TypeScript + Electron"이라고 기술하는 doc drift가 있음 → 이것도 함께 수정.
3. **Feature Catalog** — 새 GUI 동작은 사용설명서 ch.18 카탈로그에 영어 이름 + 한국어 설명으로 등록.

---

## 10. 위험과 완화

| 위험 | 완화 |
|---|---|
| Qt 설치 실패(네트워크/디스크) | 설치 단계를 구현 전에 먼저 수행해 검증. 실패 시 원인 명확히 보고. |
| 마커 프로토콜(`\x1e...`) 이식 실수 | REPL/런 동작을 실제 `lumi.exe`로 단계별 검증. |
| detach/dock 동작 차이 | `QDockWidget` 동작을 INER 위/OUTER 아래 순서 보장하도록 restoreState로 검증. |
| 기존 단축키 호환 | F5 실행, Ctrl+S 저장 등 핵심 단축키는 `QShortcut`/`QKeySequence`로 1:1 매핑 후 검증. |
| 인코딩 폴백 회귀 | 한글/CP949 출력을 실제로 띄워 검증. |
| 빌드 복잡도 증가 | build.bat을 단계별로, 실패 시 명확한 에러 메시지. |

---

## 11. 검증 계획 (완료 기준)

1. **빌드 성공** — `ide-cpp/build.bat`이 깨끗이 `bin/Lumina.exe` 산출.
2. **실행** — 더블클릭/배치로 창이 뜨고 기본 탭(`print("hello, Lumi!")`) 표시.
3. **런** — F5로 `examples/demo.lumi` 실행, 결과가 OUTER에 출력.
4. **REPL** — 입력 라인 동작, 멀티라인 블록, `input()` 대기.
5. **터미널** — OS 명령(`dir`, `echo`) 동작.
6. **편집기** — 신택스 하이라이트, 거터 라인번호, 자동들여쓰기, 괄호쌍, 폰트 줌.
7. **탭** — 열기/닫기/전환/더러운표시, 저장.
8. **탐색기** — 폴더 열기, 파일 생성/이름변경/삭제, dotfile 표시(버그#9).
9. **detach/dock** — INER/OUTER 각각 분리/복귀, 단축키 유지.
10. **테마** — 라이트/다크 전환, 색/아이콘 갱신.
11. **명령 팔레트** — Ctrl+P(파일), Ctrl+Shift+P(명령).
12. **DPI** — 다른 스케일 모니터로 이동해 깨지지 않음(버그#1).
13. **종료/재시작** — 창상태/탭/최신파일 복원(버그#7).
14. **문서 갱신** — README/사용설명서 IDE 부분 Qt 버전 반영, doc drift 수정.

---

## 12. 향후(이번 작업 외)

- 미니맵, 멀티커서, 설정 다이얼로그 GUI, 플러그인 시스템 등은 별도 후속 작업.
- 언어 기능(문자열 함수, OOP 확장 등)은 언어 인터프리터 쪽 별도 작업.
