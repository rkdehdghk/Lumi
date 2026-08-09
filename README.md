# Lumi 언어 & Lumina IDE

내가 직접 만든 프로그래밍 언어 **Lumi(루미)** 와, 그 언어를 작성·실행하는 **나만의 IDE** 입니다.
Lumi는 파이썬처럼 **세미콜론 없이 들여쓰기로 블록을 구분**합니다.

> 📖 **전체 사용법은 [Lumi 사용설명서.md](Lumi%20사용설명서.md) 를 보세요.** (문법·자료형·함수 총정리)

> ✍️ **문서 유지 규칙:** 언어에 기능을 추가하거나 바꿀 때는 **코드뿐 아니라 사용 설명도 항상 함께 갱신**합니다.
> 대상: 이 `README.md`, [`Lumi 사용설명서.md`](Lumi%20사용설명서.md)(+ 상단의 갱신 날짜), `examples/*.lumi` 데모.

> 🌐 **UI 언어 규칙:** Lumina IDE의 **모든 UI 텍스트는 영어로 작성**합니다.
> 대상: 버튼·라벨·탭·메뉴, 패널 이름, 알림/경고/확인 다이얼로그(messagebox), 상태 메시지, 툴팁 등 **사용자에게 보이는 모든 문자열**.
> (이 문서 같은 설명 문서의 본문은 한국어로 써도 되지만, **앱 안에서 보이는 UI 글자**는 예외 없이 영어입니다. 예: `Detach`/`Dock`, `Terminal`, `REPL`.)

## 실행 방법

가장 쉬운 방법: **`Lumina\Lumina.exe`** 를 더블클릭하세요. (`Lumina (C++) 실행.bat` 도 같은 일을 합니다.)
설치할 것도, 따로 깔아야 할 프로그램도 없습니다. (Qt 런타임은 폴더 안에 함께 들어 있습니다.)

IDE가 열리면 코드를 작성하고 **F5**(또는 ▶ Run 버튼)를 누르면 됩니다.

터미널에서 파이썬처럼 명령어로 Lumi 코드를 실행할 수 있습니다 (`lumi` / `lumi.bat`):

```bash
lumi                                       # 1. 대화형 셸 실행 (REPL 내에서 run 파일명 으로 실행)
lumi examples\demo.lumi                    # 2. Lumi 파일 직접 실행
lumi -c "print('Hello World')"             # 3. 한 줄 코드 직접 실행
lumi app.lumi --port 8080                  # 4. 인자 넘기기 (프로그램에서 args() 로 읽습니다)
```

### 리눅스 · 맥에서 쓰기

인터프리터(`lumi`)는 윈도우·리눅스·맥에서 모두 빌드됩니다. 운영체제마다 다른 부분은 `src/platform.c` 한 곳에 모아 두었습니다.

```bash
cd c-interpreter
./build.sh          # -> c-interpreter/bin/lumi
./bin/lumi examples/hello.lumi
```

필요한 것은 **C 컴파일러(gcc 또는 clang)** 뿐입니다. cmake 가 있으면 쓰고, 없으면 `build.sh` 가 `cc` 를 직접 부르는 길로 넘어갑니다. CMake 를 직접 쓰고 싶으면:

```bash
cmake -S c-interpreter -B c-interpreter/build -DCMAKE_BUILD_TYPE=Release
cmake --build c-interpreter/build -j
```

- 파일 이름은 어느 운영체제에서나 **UTF-8** 입니다. 한글 이름도 그대로 됩니다.
- `cp949` / `euc-kr` 변환은 리눅스·맥에서 `iconv` 를 씁니다(리눅스 glibc 는 기본 포함, 맥은 `libiconv` 를 연결합니다).
#### IDE(Lumina)도 리눅스·맥에서 빌드됩니다 — 다만 아직 아무도 띄워 보지 않았습니다

```bash
cd ide-cpp
./build.sh          # -> ide-cpp/bin/Lumina   (인터프리터가 없으면 그것부터 빌드합니다)
```

필요한 것은 **cmake · C++17 컴파일러 · Qt6(Widgets, Svg)** 입니다. 꾸러미 이름은 `ide-cpp/build.sh` 맨 위에 적어 두었습니다.

운영체제마다 다른 부분은 **`ide-cpp/src/core/hostenv.cpp` 한 곳**에 모여 있습니다 (인터프리터의 `platform.c` 와 같은 방식). 갈라지는 것은 여섯 가지뿐입니다.

| 갈라지는 것 | 윈도우 | 리눅스·맥 |
|---|---|---|
| 실행 파일 이름 | `lumi.exe` | `lumi` |
| 아래 터미널이 쓰는 셸 | `cmd.exe` | `/bin/sh -i` |
| 줄 끝 | CRLF | LF |
| 폴더 옮기기 | `cd /d` | `cd` |
| Stop (트리째 죽이기) | `taskkill /T /F` | 무리(process group)에 `SIGKILL` |
| 터미널 첫 폴더 | `C:\` | 홈 폴더 |

아래 터미널은 일부러 `$SHELL` 이 아니라 **`/bin/sh`** 를 씁니다. 윈도우가 `cmd.exe` 를 쓰는 것과 같은 이유이고, 무엇보다 프롬프트를 못박을 수 있어서입니다 — 대화형 `bash` 는 `~/.bashrc` 를 읽어 우리가 넣어 준 `PS1` 을 도로 덮어씁니다.

> ⚠️ **정직하게**: IDE 의 리눅스·맥 쪽은 **코드는 있지만 실제로 띄워 본 적이 없습니다** (이 개발 PC 에 리눅스용 Qt6 이 없습니다). 처음 돌려 보실 분은 아래 터미널의 프롬프트·`cd`·Stop·F5 네 가지를 먼저 확인해 주세요.

> **검증 상태 (2026-08-10)**: 인터프리터 POSIX 분기를 gcc 15.2 로 실제 빌드해 돌렸습니다 — **언어 단위시험 109/109 통과**, 따로 도는 일감(`start`/`wait`)이 pthread 로 실제로 겹쳐 돌고, 디버거(DAP) 대화가 **윈도우 골든과 바이트 단위로 같습니다**. 그 과정에서 이식성 구멍 둘을 잡았습니다 — 빌드에 **pthread 를 안 이어 주고 있었고**, `dap.c` 가 오류 자리를 실마다 따로(TLS) 표시 없이 다시 선언하고 있었습니다(MSVC 는 넘어가고 GCC 는 오류). 다만 검증에 쓴 환경이 MSYS2(POSIX 호환 계층)라 **진짜 리눅스(glibc)에서의 확인은 아직**입니다.

> 💡 **VS Code 방식 터미널 & 실행 시스템:** Lumina IDE는 VS Code 시스템과 동일하게 동작합니다.
> 1. **폴더 선택 필수**: 열린 작업 폴더가 없으면 새 파일/폴더를 생성할 수 없으며, 먼저 작업 폴더를 선택하도록 안내합니다.
> 2. **실체 파일 실행**: 디스크에 저장된 실체 파일(`.lumi`)을 생성해야만 실행(F5)할 수 있습니다.
> 3. **상주 터미널 및 자동 실행**: 터미널(OUTER Terminal)은 파일/작업 디렉토리의 **실제 대화형 상주 셸**(윈도우 `cmd.exe`, 리눅스·맥 `/bin/sh`)로 동작합니다. **F5(Run)**를 누르면 파일 저장 후 상주 터미널로 `lumi "파일명.확장자"` 명령이 전송되어 상단 `>run 파일명.확장자` 및 **줄바꿈 2번(`\n\n`)** 후 실행되며, 완료 후에도 **하단 줄바꿈 2번(`\n\n`)** 후 셸 프롬프트로 복귀합니다.
> 4. **연결 프로그램 지원 (Open With)**: 탐색기에서 파일을 `연결 프로그램` → `Lumina.exe`로 열면, 파일 위치 폴더가 작업 폴더로 설정되고 편집기에 해당 파일이 열립니다.

### 외부 패키지 매니저 (`lumipm` / `lumi pkg`)

외부 라이브러리 및 저장소를 설치하고 관리할 수 있습니다:

```bash
lumipm init                           # lumi.json 생성
lumipm install https://github.com/... # Git 패키지 설치
lumipm install ../my_lib              # 로컬 패키지 복사 설치
lumipm install                        # lumi.json 의존성 전체 설치
lumipm list                           # 설치된 패키지 목록
lumipm remove 패키지명                 # 패키지 삭제
```
*(`lumi pkg <명령어>` 도 같은 기능이며, `-g` 옵션으로 전역 `~/.lumi/packages/` 관리가 가능합니다.)*


#### 어제와 똑같이 받기 — `lumi.lock`

git URL 만 적어 두면 `install` 을 돌릴 때마다 그 저장소의 **지금 맨 끝**이 들어옵니다. 그러면 어제 되던 것이 오늘 안 될 수 있습니다. 그래서 Lumi 는 실제로 받아 온 **커밋 번호**를 `lumi.lock` 에 적어 둡니다.

```bash
lumi pkg install github:someone/util   # 받고 나서 lumi.lock 을 갱신합니다
lumi pkg lock                          # 지금 깔린 것들의 커밋을 다시 적기
lumi pkg install                       # 이름 없이 = lumi.lock 대로 맞추기
```
```json
{
  "packages": {
    "util": { "source": "https://github.com/someone/util.git", "commit": "f73d2a8c..." }
  }
}
```
- **`lumi.lock` 은 저장소에 함께 넣어 두세요.** 그래야 팀원과 CI 가 같은 코드를 받습니다.
- `lumi.json` 은 "무엇을 쓸지", `lumi.lock` 은 "정확히 어느 것을 받았는지"를 적습니다.

## IDE 화면 구성 (INER / OUTER)

Lumina IDE 화면은 크게 두 영역으로 나뉩니다.

| 영역 | 이름 | 내용 |
|------|------|------|
| 위쪽 | **INER** | 탭 바(열린 파일들) + 코드 편집 칸(입력칸). 탭 바 맨 왼쪽에 `INER` 표시가 있습니다. |
| 아래쪽 | **OUTER** | 실행 결과가 나오는 곳. `Terminal`(OS 명령) / `REPL`(Lumi 대화형 셸) 두 패널을 선택해 씁니다. |
| 왼쪽 | **Explorer** | 폴더 나무. 위아래 전체 높이를 차지합니다. |

**각 패널의 자리는 고정입니다.** Explorer는 맨 왼쪽, INER는 오른쪽 위, OUTER는 오른쪽 아래로 정해져 있어 서로 자리를 바꾸거나 다른 쪽에 붙일 수 없습니다. 대신 경계선을 끌어 **크기는 자유롭게 조절**할 수 있고, 분리(Detach)도 그대로 됩니다.

### 창 분리 (Detach) / 도킹 (Dock)

세 패널 모두 **독립된 창으로 떼어낼 수 있습니다.**

- **INER**: 탭 바 오른쪽 끝의 **`⧉ Detach`** 버튼 → 탭+편집 칸이 별도 창으로 분리됩니다.
- **OUTER**: 결과 패널 헤더의 `Clear` 옆 **`⧉ Detach`** 버튼 → 결과 영역이 별도 창으로 분리됩니다.
- **Explorer**: `EXPLORER` 헤더 오른쪽의 **`⧉ Detach`** 버튼 → 파일 나무가 별도 창으로 분리됩니다.
- 분리된 창의 같은 버튼은 **`⧉ Dock`** 으로 바뀌며, 이 버튼을 누르면 **원래 자리로 되돌아옵니다.** (떠 있는 창의 제목줄을 끌어다 붙일 수도 있지만, 붙는 곳은 언제나 자기 원래 자리뿐입니다.)
- 동시에 분리할 수 있고, 되돌릴 때는 Explorer가 왼쪽, INER가 위, OUTER가 아래로 원래 자리에 다시 붙습니다.
- 분리해도 열려 있던 탭·편집 내용·결과 출력은 그대로 유지됩니다.

> ⌨️ 분리된 창에서도 `F5`(실행)·`Ctrl+S`(저장) 같은 단축키가 그대로 동작합니다. 어느 창에서 눌러도 같은 결과입니다.

## 파일 구성

| 파일 | 역할 |
|------|------|
| `Lumina/Lumina.exe` | 빌드된 실행 파일 (+ 옆의 Qt DLL·`platforms/` 폴더가 함께 있어야 합니다) |
| `Lumina (C++) 실행.bat` | 더블클릭으로 IDE 켜기 |
| `build-cpp.bat` | 인터프리터와 IDE를 한 번에 다시 빌드 → `Lumina\` 폴더 |
| `lumipm.bat` | 패키지 매니저 CLI 실행 래퍼 스크립트 |
| `c-interpreter/` | **언어의 두뇌 (C).** 렉서(들여쓰기 토큰 포함) → 파서 → AST 트리워크 인터프리터 & 패키지 매니저 |
| `c-interpreter/src/` | 소스: `lexer.c` `parser.c` `interp.c` `pkg.c` `builtins.inc` `value.c` `unicode.c` `main.c` |
| `c-interpreter/src/platform.c` | **운영체제마다 다른 것들을 모아 둔 곳** (파일·경로·터미널·코드페이지). 나머지 소스는 여기만 거칩니다 |
| `c-interpreter/src_vm_backup/` | 걷어낸 미완성 바이트코드 VM (아래 "바이트코드 VM 을 걷어낸 이유" 참고) |
| `c-interpreter/build.bat` | 윈도우(MSVC) 빌드 (→ `c-interpreter/bin/lumi.exe`) |
| `c-interpreter/build.sh` | 리눅스·맥 빌드 (→ `c-interpreter/bin/lumi`) |
| `c-interpreter/CMakeLists.txt` | 세 운영체제 공통 빌드 정의 |
| `tests/run.bat` | **예제 회귀 테스트.** 언어를 고쳤으면 반드시 돌리세요 |
| `tests/record.bat` | 골든 출력 다시 적기 (출력이 *일부러* 달라졌을 때만) |
| `ide-cpp/` | **Lumina IDE (C++ / Qt6 Widgets).** |
| `ide-cpp/src/core/` | Qt 독립 로직: Lumi 토크나이저, fuzzy 매칭, 경로 해석, 설정 |
| `ide-cpp/src/models/` | `TabModel`(탭 상태), `RunSession`(자식 프로세스 = 런/셸 + REPL 2채널) |
| `ide-cpp/src/widgets/` | 편집기·거터·하이라이터·탭바·콘솔·탐색기·명령 팔레트 |
| `ide-cpp/src/panels/` | `EditorPanel`(INER), `OutputPanel`(OUTER) |
| `ide-cpp/src/theme/` | 라이트/다크 팔레트 + 전역 QSS |
| `ide-cpp/src/mainwindow.cpp` | 메뉴·툴바·상태바·도크 배치, 모든 명령이 지나가는 `runCommand()` |
| `libraries/` | 기본 제공 라이브러리 (`math.lumi`, `csv.lumi`, `http.lumi`, `sqlite.lumi`) |
| `c-interpreter/src/ffi.c` | 남의 C 라이브러리 불러 쓰기 (`ccall`) — 가장 위험한 곳 |
| `examples/` | 예제 프로그램 (`.lumi` 파일). `examples/project/` 는 폴더로 나눈 프로그램 예 |
| `tests/lang/` | Lumi 언어 자체의 단위 시험 (`lumi test` 로 돕니다) |
| `editors/vscode-lumi/` | VS Code 확장 (`lumi lsp` 를 켜서 이어 줍니다) |
| `c-interpreter/src/lumiwords.h` | **낱말 표 한 곳.** LSP·IDE·VS Code 색칠이 모두 이걸 봅니다 |
| `c-interpreter/src/lsp.c` | 언어 서버 (`lumi lsp`) |
| `c-interpreter/src/regex.c` | 무늬 찾기 엔진 |
| `c-interpreter/src/lint.c` · `fmt.c` | `lumi lint` · `lumi fmt` |
| `Lumi 사용설명서.md` | 전체 사용설명서 (문법·함수 총정리) — **배울 때는 이쪽** |
| `docs/Lumi_Language_Spec.md` | **언어 명세** — EBNF 문법·시맨틱·버전 정책. 다시 구현할 수 있을 만큼 정확하게 |
| `docs/Lumi_Changes.md` | **바뀐 것들** — 깨는 변경은 반드시 여기에 |
| `tests/lang/spec_test.lumi` | 명세가 구현과 맞는지 검사하는 시험 40 개 |
| `Lumi&na logo/` | 로고·아이콘 이미지 (`.ico` 포함) |

---

## 배포하기 — 실행 파일 하나로 (`lumi build`)

Lumi 를 깔지 않은 사람에게도 그냥 건네줄 수 있는 **단독 실행 파일**을 만듭니다.

```bash
lumi build app.lumi                 # -> app.exe (윈도우) / app (리눅스·맥)
lumi build app.lumi -o 나눔이.exe    # 이름 골라서
```
```
lumi build: app.lumi  ->  app.exe
  담음 app.lumi (136 글자)
  담음 models/user.lumi (105 글자)
lumi build: 파일 2 개, 글자 241 개를 담았습니다.
lumi build: 이제 'app.exe' 만 있으면 Lumi 없이 돌아갑니다.
```

- **시작 파일이 있는 폴더 아래의 모든 `.lumi`** 를 함께 담습니다. 그래서 `bring "models/user"` 같은 폴더 구조가 그대로 살아 있습니다.
- 만들어진 파일은 lumi 실행 파일 **뒤에** 프로그램을 붙인 것입니다. 켜질 때 자기 뒤를 보고, 있으면 그 프로그램을 돌립니다.
- 넘긴 인자는 그대로 프로그램에게 갑니다 — `app.exe --port 8080` 이면 `args()` 가 `["...app.exe", "--port", "8080"]`.
- 담기는 것은 `.lumi` 뿐입니다. 프로그램이 읽는 **자료 파일(csv, txt)은 따로 챙겨 주세요.**
- 만든 실행 파일은 **만든 그 운영체제에서만** 돕니다 (윈도우에서 만들면 윈도우용).

---

## 다른 편집기에서 쓰기 (`lumi lsp`)

Lumina IDE 말고 **VS Code · Neovim · Helix · JetBrains** 에서도 Lumi 를 쓸 수 있습니다.
`lumi lsp` 가 표준 LSP(Language Server Protocol) 서버라, LSP 를 아는 편집기면 어디든 붙습니다.

| 해 주는 일 | 설명 |
|-----------|------|
| **문법 오류 표시** | 타이핑하는 대로 렉서·파서를 돌려 틀린 줄에 밑줄 |
| **자동완성** | 내장 낱말 160개 + 그 파일이 만든 `func`/`class`/`val` |
| **설명 보기** | 이름 위에 마우스를 올리면 쓰는 형식 |
| **파일 안 목록** | `func`/`class` 개요 (VS Code 는 Ctrl+Shift+O) |

**VS Code**: `editors/vscode-lumi/` 폴더를 확장 폴더에 통째로 복사하고 VS Code 를 다시 켜면 끝입니다. `npm install` 이 필요 없습니다 (받아 오는 꾸러미가 하나도 없습니다). 자세한 것은 `editors/vscode-lumi/README.md`.

**그 밖의 편집기**: 명령은 `lumi lsp`, 파일 확장자는 `.lumi` 로 등록하면 됩니다.
```lua
-- Neovim
vim.lsp.start({ name = "lumi", cmd = { "lumi", "lsp" } })
```

> 낱말 표는 `c-interpreter/src/lumiwords.h` **한 곳에만** 있습니다. LSP·Lumina IDE·VS Code 색칠이 모두 이 표를 봅니다. 내장 함수를 새로 만들면 여기에 한 줄 넣고, 색칠 파일은 `editors/vscode-lumi/make-grammar.py` 를 한 번 돌려 다시 만드세요.

---

## 디버거 (`lumi dap`)

**중단점을 걸고 한 줄씩 따라가며 변수를 들여다봅니다.** `lumi dap` 이 표준 DAP(Debug Adapter Protocol) 서버라, DAP 를 아는 편집기면 어디든 붙습니다.

| 되는 것 | 설명 |
|---|---|
| **중단점** | 그 줄에 닿으면 멈춥니다 (편집기에서 줄 왼쪽을 눌러 겁니다) |
| **한 줄씩** | 넘어가기(step over) · 들어가기(step in) · 나가기(step out) · 이어 하기 |
| **호출 자취** | 어느 함수를 거쳐 여기까지 왔는지, 겹마다 그 줄 |
| **변수** | 그 겹의 지역값과 전역값. 리스트·딕셔너리·객체는 **펼쳐서** 봅니다 |
| **식 값 보기** | 멈춘 자리에서 아무 식이나 셈해 봅니다 (마우스를 올려도 뜹니다) |

**VS Code**: `editors/vscode-lumi/` 를 넣어 두었으면 `.lumi` 파일을 열고 **F5** 를 누르면 됩니다 (`launch.json` 없이도 지금 파일을 돌립니다).

**그 밖의 편집기**: 어댑터 명령은 `lumi dap`, 시작 설정은 `{ "type": "lumi", "request": "launch", "program": "경로.lumi" }` 입니다. `stopOnEntry: true` 를 주면 첫 줄에서 멈춥니다.

**한계 두 가지** (일부러 이렇게 두었습니다)
- **돌고 있는 도중에는 '멈춤'(pause)이 안 됩니다.** 디버거와 프로그램이 실 하나를 나눠 쓰기 때문에, 멈춰 있을 때만 편집기의 말을 듣습니다. 무한 반복에 빠졌다면 중단점을 걸고 다시 돌리세요.
- **`input()` 은 EOF 입니다.** 표준입력이 편집기와 이야기하는 통로라서 프로그램이 나눠 쓸 수 없습니다. 입력이 필요한 프로그램은 터미널이나 Lumina IDE 에서 돌리세요.

---

## 내 코드 시험하기 (`test` · `lumi test`)

`test "이름":` 블록에 확인할 것을 적어 두고 `lumi test` 로 한꺼번에 돌립니다.

```
func 더하기(a, b):
    return a + b

test "더하기가 맞는가":
    assertsame(7, 더하기(3, 4))
    assert(더하기(0, 0) == 0)

test "빈 리스트는 길이가 0":
    assertsame(0, len([]))
```
```bash
lumi test                    # 지금 폴더 아래의 .lumi 를 모두
lumi test tests              # 폴더 하나만
lumi test tests\core.lumi    # 파일 하나만
```
```
tests\core.lumi
  ok   더하기가 맞는가
  ok   빈 리스트는 길이가 0

----------------------------------------
  2 test(s), 2 passed, 0 failed
----------------------------------------
```

| 무엇 | 하는 일 |
|------|---------|
| `test "이름":` | 시험 하나. **`lumi test` 로 돌릴 때만 실행**됩니다 |
| `assert(조건)` / `assert(조건, 설명)` | 참이 아니면 그 시험을 실패로 |
| `assertsame(기대한 것, 나온 것)` | 다르면 실패 — **둘 다 보여 줍니다** |

- 그냥 `lumi 파일.lumi` 로 돌리면 `test` 블록은 **조용히 건너뜁니다.** 시험이 평소 실행을 느리게 하거나 화면을 어지럽히지 않도록 한 것입니다.
- 시험 안에서 난 오류(잡히지 않은 것 포함)는 **그 시험만** 실패로 적고 다음 시험으로 넘어갑니다.
- 실패한 시험에는 오류 메시지와 호출 자취가 함께 나옵니다.
- 하나라도 실패하면 종료 코드가 1 입니다 (CI 에 그대로 걸 수 있습니다).
- `assert` 가 내는 오류의 종류는 `AssertError` 입니다.

---

## 데이터베이스 (`bring sqlite`)

```
bring sqlite

val db = sqlite.open("가게.db")            // ":memory:" 면 기억 장치에만
sqlite.run("create table 손님(이름 text, 나이 int)", db)
sqlite.run("insert into 손님 values(?, ?)", db, ["루미", 3])

for 줄 in sqlite.rows("select * from 손님 where 나이 > ?", db, [1]):
    print(줄["이름"], 줄["나이"])

sqlite.close(db)
```

| 함수 | 하는 일 |
|---|---|
| `open(경로)` / `close(db)` | 열고 닫기 |
| `run(sql, db, [값들])` | 줄을 안 돌려주는 것 — 바뀐 줄 수를 줍니다 |
| `rows(sql, db, [값들])` | 딕셔너리 리스트 |
| `one` / `value` | 첫 줄 / 첫 칸 (없으면 `none`) |
| `tx(함수, db)` | **한 묶음으로** — 터지면 하나도 안 한 것으로 되돌립니다 |
| `lastid(db)` · `tables(db)` · `hastable(이름, db)` · `version()` | |

**값은 반드시 `?` 자리로 넘기세요.** 글자를 이어 붙이면 SQL 주입이 열립니다.

```
sqlite.run("... where 이름 = '" + 입력 + "'", db)    // 절대 이렇게 하지 마세요
sqlite.run("... where 이름 = ?", db, [입력])          // 이렇게
```

자료형은 그대로 오갑니다: `none↔NULL` · `int↔INTEGER` · `float↔REAL` · `str↔TEXT` · `bytes↔BLOB`.

**어디서 SQLite 를 가져오나** — 저장소에 25만 줄짜리 `sqlite3.c` 를 넣지 않았습니다. 운영체제에 이미 있는 것을 빌려 씁니다 (윈도우 10 이상 `winsqlite3.dll`, 맥 `libsqlite3.dylib`, 리눅스 `libsqlite3.so`). 없으면 `DatabaseError` 로 알려 줍니다. 예제는 [`examples/database.lumi`](examples/database.lumi).

---

## 남의 C 라이브러리 불러 쓰기 (`ccall`)

> ⚠️ **여기가 Lumi 에서 가장 위험한 곳입니다.** 서명을 한 글자만 틀려도 프로그램이 그냥 죽습니다 (Lumi 오류가 아니라 운영체제가 내리는 죽음). 이것은 FFI 의 본성이고 어떤 언어에서도 마찬가지입니다.
>
> **그래서 쓰는 방식이 정해져 있습니다: `ccall` 은 배관이고, 물건은 라이브러리입니다.** `libraries/sqlite.lumi` 처럼 감싸개를 Lumi 로 적어 두고, 보통은 그것만 `bring` 해서 씁니다.

```
val lib = cload("sqlite3")                          // 윈도우면 "winsqlite3"
print(ccall(lib, "sqlite3_libversion", ">s", []))   // 3.51.1
cclose(lib)
```

**서명 적기** — `"인자들>돌려주는것"`

| 글자 | 뜻 | | 글자 | 뜻 |
|---|---|---|---|---|
| `i` | 32비트 정수 | | `s` | 글자 (UTF-8) |
| `l` | 64비트 정수 | | `b` | 바이트 |
| `p` | 주소 (Lumi 에서는 정수) | | `d` | 실수 |
| | | | `v` | 없음 (돌려주는 자리에만) |

| 함수 | 하는 일 |
|---|---|
| `cload(이름)` / `cclose(손잡이)` / `chas(손잡이, 함수이름)` | 라이브러리 |
| `ccall(손잡이, 함수이름, 서명, [인자들])` | 부르기 |
| `cbuf(바이트수)` / `cfree(주소)` | 자리 잡고 놓아주기 (0 으로 채워 줍니다) |
| `cget(종류, 주소)` / `cput(값, 종류, 주소)` | 값 하나 읽고 쓰기 (`c h i l p f d`) |
| `ctext(주소)` / `cbytes(길이, 주소)` | 글자·바이트 읽기 |

**제약 두 가지.** 인자는 여덟 개까지입니다. **실수는 마지막 인자일 때만** 넘길 수 있습니다 — x86-64 는 실수를 정수와 다른 레지스터로 넘기고 윈도우 ABI 는 그 자리를 몇 번째 인자인지로 정해서, 자리마다 골라 맞추려면 경우의 수가 2ⁿ 으로 늘어납니다. 실제로 필요한 모양(`f(..., 실수)`)만 열어 두었습니다.

값싸게 막을 수 있는 것은 막아 두었습니다: `cfree` 는 **`cbuf` 가 준 주소만** 받고, 두 번 놓아주면 오류입니다. 서명의 인자 개수가 안 맞거나 없는 함수 이름이면 부르기 전에 알려 줍니다.

---

## 따로 도는 일감 (`start` · `wait`)

기다리는 일이 여럿일 때 한 줄로 세워 놓지 않습니다. 내장 함수 **둘뿐**이고 새 문법은 없습니다.

```
func 느린일(이름, 초):
    sleep(초)
    return 이름

val 일감들 = [start(느린일, "가", 0.5), start(느린일, "나", 0.5), start(느린일, "다", 0.5)]
for 하나 in 일감들:
    print(wait(하나))
```

하나씩 했으면 1.5초, 이렇게 하면 **0.5초쯤**입니다.

| 무엇 | 하는 일 |
|---|---|
| `start(부를것, 값...)` | 다른 실에 태우고 곧바로 **일감**(`task`)을 돌려줍니다 |
| `wait(일감)` | 끝나기를 기다렸다가 돌려준 값을 줍니다 (**여러 번 기다려도** 됩니다) |

- 일감 안에서 난 오류는 **`wait` 을 부른 자리에서 다시 납니다** — 종류와 말이 그대로라 `catch ValueError e:` 로 잡힙니다.
- `start` 는 부를 수 있는 값만 받습니다 (내 함수·이름 없는 함수·클래스). 내장 함수는 `func s: upper(s)` 처럼 감싸세요.

### 한 번에 하나만 돕니다 (그리고 그게 좋은 까닭)

**큰 자물쇠 하나**가 Lumi 코드를 돌리는 실을 언제나 하나로 지킵니다. 자리를 내주는 곳은 **`sleep` 과 `shell` 둘뿐**입니다 (`bring http` 는 `shell` 로 curl 을 부르니 여기 들어옵니다).

| 그래서 | 뜻 |
|---|---|
| 기다리는 일은 **겹칩니다** | 내려받기·셸 명령이 같이 흘러가 빨라집니다 |
| 셈만 하는 일은 **안 겹칩니다** | 계산을 나눠 돌린다고 빨라지지 않습니다 |
| `sleep`·`shell` 사이는 **안 끊깁니다** | 자물쇠(mutex)·잠금 장치가 **따로 필요 없습니다** |
| 전역 변수는 **함께** 씁니다 | 지역 자리와 오류 자리는 일감마다 따로입니다 |

참조 세기와 순환 수집기가 안전하려면 한 줄기여야 합니다. 자물쇠를 잘게 쪼개면 빨라질 수는 있어도 **조용히 깨지는 버그**가 들어올 자리가 생기고, Lumi 가 실제로 쓰이는 곳(자동화·CLI)은 거의 다 기다리는 일이라 이 정도로 충분합니다.

> `gc()` 는 일감이 도는 동안 부르지 마세요. 예제: [`examples/concurrency.lumi`](examples/concurrency.lumi)

---

## 돌리기 전에 훑어보기 (`lumi lint`)

문법은 맞는데 **뜻이 어긋난 것 같은 곳**을 알려 줍니다. 실행하지 않으니 시간이 안 걸립니다.

```bash
lumi lint                    # 지금 폴더 아래의 .lumi 를 모두
lumi lint app.lumi           # 파일 하나만
lumi lint src                # 폴더 하나만
```
```
app.lumi:12: this function is called 'sort', which is also a built-in - yours will never be called. Pick another name.
app.lumi:31: 'catch FileNotFund' names an error kind nothing here ever raises - if that is a typo this catch will never run. ...

----------------------------------------
  1 file(s), 2 thing(s) to look at
----------------------------------------
```

찾아 주는 것은 일곱 가지입니다. 전부 **이 저장소에서 실제로 겪은 사고**를 본떠 골랐습니다.

| 무엇 | 왜 위험한가 |
|------|------------|
| 내가 만든 함수·클래스가 **내장 이름을 가림** | 내장이 먼저라 내 것은 **영영 안 불립니다.** `func run()` 이 이렇게 조용히 깨진 적이 있습니다 |
| `catch` 의 **오류 종류 이름이 낯설다** | 오타면 그 `catch` 는 **한 번도 안 돕니다.** 오류가 그냥 위로 새어 나갑니다 |
| **`return`/`break`/`continue` 뒤의 줄** | 절대 닿지 않습니다 |
| **매개변수 이름이 겹침** (`func f(a, b, a)`) | 뒤엣것만 남아 앞의 인자가 사라집니다 |
| 딕셔너리에 **같은 키를 두 번** | 뒤엣것만 남습니다 |
| **가져와 놓고 안 쓰는 `bring`** | 지워도 되는 줄입니다 |
| **적어 둔 자료형과 어긋나는 값** (`func f(int a)` 를 `f(true)` 로) | 돌려 봐야 알던 것을 돌리기 전에 알려 줍니다 |

- 하나라도 찾으면 **종료 코드가 1** 입니다 — CI 에 그대로 걸 수 있습니다.
- **편집기에서도 그대로 뜹니다.** `lumi lsp` 가 같은 검사를 돌려 노란 밑줄로 보여 줍니다 (문법 오류는 빨간 줄).
- 내장 이름 가리기는 그 파일이 **그 이름을 맨이름으로 부를 때만** 말합니다. 라이브러리의 `func pow` 는 `math.pow(2,3)` 로만 불려 가려질 일이 없으니 조용합니다.
- 없는 규칙: 안 쓰는 변수 같은 것은 일부러 뺐습니다. 거짓 경고가 잦으면 사람들이 lint 를 통째로 꺼 버립니다.

---

## 모양 다듬기 (`lumi fmt`)

```bash
lumi fmt                     # 지금 폴더 아래의 .lumi 를 모두 고쳐 씁니다
lumi fmt app.lumi            # 파일 하나만
lumi fmt --check src         # 고치지 않고 '달라질 파일'만 알려 줍니다 (CI 용)
```

**하는 일** — 줄 안의 빈칸과 줄의 들여쓰기만 손봅니다.

| 전 | 후 |
|---|---|
| `val a=1` | `val a = 1` |
| `print( a+b )` | `print(a + b)` |
| `{ "가":1,"나" : 2 }` | `{"가": 1, "나": 2}` |
| `목록[ 1 ]` / `목록[0 : 2]` | `목록[1]` / `목록[0:2]` |
| `func 더하기(x,y=2):` | `func 더하기(x, y = 2):` |
| `func x:x*2` | `func x: x * 2` |
| 줄 끝 빈칸, 빈 줄 셋 이상 | 없앰 / 둘로 |

**하지 않는 일 — 줄을 붙이거나 자르지 않습니다.** 긴 줄을 접어 주지 않고, 짧은 줄을 합치지도 않습니다. 줄을 나누는 자리는 사람이 뜻을 담아 고른 것이라, 기계가 다시 고르면 대개 더 나빠집니다. 그래서 이런 것들은 **건드리지 않고 그대로 둡니다**:

- 여러 줄에 걸쳐 쓴 딕셔너리·리스트·인자 목록 (줄바꿈도, 그 줄의 칸 수도 그대로)
- 세로로 맞춰 놓은 **줄 끝 주석** (`print(a)      // 설명` 의 그 자리를 지킵니다)
- 혼자 한 줄을 쓰는 주석의 들여쓰기
- 파일이 CRLF 로 적혀 있으면 CRLF 로 그대로 (줄 끝을 통째로 바꾸면 한 줄만 고쳐도 파일 전체가 바뀐 것으로 보입니다)

한 가지 지우는 것: 파일 맨 앞의 **UTF-8 BOM** 은 없앱니다.

**이 포매터가 옳다는 것을 어떻게 아나** — `examples/` 와 `libraries/` 전체에 돌렸을 때 **한 글자도 안 바뀌어야** 합니다. `tests\run.bat` 이 그것을 지킵니다. 규칙을 새로 넣었는데 멀쩡한 코드가 바뀌기 시작하면 거기서 바로 잡힙니다. 반대쪽 절반은 `tests/fmt/messy.src` 로, 일부러 지저분하게 적어 두고 다듬은 결과를 골든 파일과 견줍니다.

---

## Lumi 자체의 테스트 (`tests/`)

언어(`c-interpreter/src/`)를 고쳤으면 **빌드한 뒤 반드시** 돌리세요.

```
tests\run.bat        # 윈도우
./tests/run.sh       # 리눅스 · 맥
```

GitHub Actions 가 밀어 넣을 때마다 **우분투 · 맥 · 윈도우 셋 다** 돌립니다 (`.github/workflows/ci.yml`).

일곱 가지를 이어서 돌립니다. (리눅스·맥은 `tests/run.sh` 가 같은 것을 같은 차례로 합니다)
1. **예제 회귀** — `examples/` 아래의 모든 `.lumi` (아래 표 참고)
2. **언어 단위 시험** — `tests/lang/*.lumi` 를 `lumi test` 로
3. **lint** — `examples/` 와 `libraries/` 는 **깨끗해야** 하고, 일부러 일곱 규칙을 다 어겨 둔 `tests/lint/bad.lumi` 는 골든 파일과 같아야 합니다 (lint 가 멀쩡한 코드에 짖기 시작하면 여기서 잡힙니다)
4. **fmt** — `examples/` 와 `libraries/` 는 `fmt --check` 로 **하나도 안 바뀌어야** 하고 (포매터가 멀쩡한 코드를 다시 흐트러뜨리면 여기서 잡힙니다), 일부러 지저분한 `tests/fmt/messy.src` 는 다듬은 결과가 골든 파일과 같아야 합니다
5. **디버거** — 미리 적어 둔 DAP 대화(`tests/dap/session.txt`)를 `lumi dap` 에 흘려 넣고 답을 통째로 골든과 견줍니다
6. **CRLF 소스** — 일부러 CRLF 로 저장한 `tests/crlf/blankline.lumi` 가 LF 파일과 똑같이 파싱돼야 합니다
7. **HTTP 서버** — `tests/http/{server,client}.lumi` 두 프로세스로

`examples/` 아래의 모든 `.lumi` 를 (하위 폴더까지) 실행해서 검사합니다.

파일마다 **KEY** 가 붙습니다 — `examples\` 아래 경로에서 `\` 를 `_` 로 바꾸고 `.lumi` 를 뗀 것입니다.
`examples\hello.lumi` → `hello`, `examples\graph\menu.lumi` → `graph_menu`.

| 무엇 | 어떻게 |
|------|--------|
| **어느 경우든** | 출력에 `Error:` 로 시작하는 줄이 있으면 **FAIL** (예제가 오류로 끝나면 안 됩니다) |
| `tests/expected/<KEY>.txt` 가 있으면 | 출력이 한 바이트라도 달라지면 **FAIL** (골든 테스트) |
| 없으면 | 에러 없이 끝나기만 하면 통과 (스모크 테스트) |
| `tests/stdin/<KEY>.txt` 가 있으면 | 그 내용을 `input()` 에 넣어 줍니다 |
| `tests/nondeterministic.txt` 에 적힌 KEY | 골든을 만들지 않습니다 (시간·무작위라 매번 다름) |

오류 검사를 **골든이 있을 때도** 하는 것이 중요합니다. 그러지 않으면 한 번 잘못 기록된 골든이 오류를 그대로 굳혀 놓고 계속 초록불이 됩니다.

전부 통과하면 종료 코드 0, 하나라도 실패하면 1 입니다.

출력이 **일부러** 달라졌을 때만 (기능을 새로 넣었거나 메시지를 고쳤을 때):

```
tests\record.bat
```

⚠️ `record.bat` 을 먼저 돌리면 안 됩니다. **항상 `run.bat` 으로 무엇이 달라졌는지 눈으로 확인한 뒤** 기록하세요.
예제를 새로 만들었다면 `record.bat` 을 한 번 돌려 골든을 만들어 두면 됩니다.

### 바이트코드 VM 을 걷어낸 이유

예전에는 `compiler.c` + `vm.c` 가 AST 를 바이트코드로 옮겨 돌렸습니다. 그런데 그 VM 은 **미완성**이었습니다.

* `compile_ast_to_chunk` 는 늘 성공을 돌려주고, 처리 못 하는 노드는 `OP_EVAL_AST` 로 트리워커에 되돌렸습니다 — 즉 트리워커 위에 씌운 얇은 껍데기였습니다.
* 그래서 **에러 핸들러 스택이 둘로 갈라졌습니다.** VM 은 자기 `try_stack` 을 쌓는데, 실제 런타임 오류는 `lumi_error()` 를 직접 불러 트리워커 쪽 `top_handler` 만 보고 `longjmp` 했습니다.
* 결과: **`try` / `catch` / `safe` / `always` 가 하나도 작동하지 않았습니다.** `examples/trycatch.lumi` 조차 0으로 나누기에서 그대로 죽었습니다.

걷어내고 트리워크 하나로 통일했더니 예외 처리가 되살아났고, 속도는 그대로였습니다 (100만 번 루프 0.20초 → 0.20초). 껍데기 VM 은 속도를 준 적이 없었습니다.

소스는 지우지 않고 `c-interpreter/src_vm_backup/` 에 두었습니다. 나중에 진짜 VM 을 만들 거라면 **모든 노드를 컴파일하고(`OP_EVAL_AST` 제거), 오류를 `OP_THROW` 한 길로 모으는 것**부터 해야 합니다.

---

## exe 파일 빌드 / 업데이트

`c-interpreter` 나 `ide-cpp` 의 코드를 고친 뒤 **`Lumina.exe` 를 새로 만드는 방법**입니다.
처음 하는 사람도 아래 순서를 **그대로** 따라 하면 오류 없이 다시 빌드할 수 있습니다.

> 🔑 **핵심 2가지**
> 1. 빌드는 **Windows에서** 합니다. (Windows에서 빌드해야 `.exe` 가 나옵니다.)
> 2. 두 조각을 만듭니다 — **언어(C)** 와 **IDE(C++/Qt6)**. `build-cpp.bat` 이 둘 다 해 줍니다.

### 0단계. 준비물 (처음 한 번만)

1. **Visual Studio C++ 빌드 도구** — 언어(C)와 IDE(C++)를 컴파일합니다.
   [Visual Studio](https://visualstudio.microsoft.com/) 설치 화면에서
   **"C++를 사용한 데스크톱 개발"** 을 체크하세요. (CMake·Ninja가 함께 깔립니다.)

2. **Qt6** — IDE의 GUI 라이브러리입니다. `C:\Qt\6.9.3\msvc2022_64` 에 설치되어 있어야 합니다.
   [Qt 온라인 설치 프로그램](https://www.qt.io/download-qt-installer) 또는:
   ```bash
   pip install aqtinstall
   aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 -O C:\Qt
   ```
   다른 경로에 깔았다면 `ide-cpp\build.bat` 의 `set "QT=..."` 줄을 고치세요.
   (Node.js는 더 이상 필요 없습니다 — Electron 버전은 Qt로 대체됐습니다.)

### 1단계. 실행 중인 Lumina 끄기 ⚠️

`Lumina.exe` 가 켜져 있으면 파일을 덮어쓸 수 없어 **오류**(`Access is denied`)가 납니다.
빌드 전에 **Lumina 프로그램을 완전히 종료**하세요.

### 2단계. 빌드

**`build-cpp.bat`** 을 더블클릭하면 끝입니다. 순서대로 이렇게 합니다:

1. `c-interpreter\build.bat` → `c-interpreter\bin\lumi.exe`
2. `ide-cpp\build.bat` → CMake 구성 → 빌드 → `windeployqt` (Qt DLL·플러그인 복사) → `ide-cpp\bin\Lumina.exe`
3. `ide-cpp\bin\` 전체와 `lumi.exe` 를 맨 위 `Lumina\` 폴더로 복사

빌드는 보통 1~2분입니다. 마지막에 **`Build complete.`** 가 보이면 성공입니다.

> ⚠️ **`Lumina.exe` 만 따로 복사하면 실행되지 않습니다.** Qt DLL과 `platforms\` 폴더가
> 같은 폴더에 있어야 합니다. 폴더째로 옮기세요.

### 끝! ✅

`Lumina.exe` 를 더블클릭해서 잘 켜지는지, 바뀐 내용이 반영됐는지 확인하세요.

---

### 한 조각만 다시 빌드하기

- **언어(C)만** 고쳤을 때:
  ```bash
  c-interpreter\build.bat
  ```
  → `c-interpreter\bin\lumi.exe` 가 새로 만들어집니다. 이걸로 바로 `.lumi` 파일을 실행해 볼 수 있어요.
  (IDE 안에 들어가려면 2단계까지 다시 해야 합니다.)

- **IDE만** 고쳤을 때 (개발 중에는 이게 제일 빠릅니다):
  ```bash
  ide-cpp\build.bat
  ```
  → `ide-cpp\bin\Lumina.exe` 가 새로 만들어집니다. 그 자리에서 바로 실행해 볼 수 있고,
  인터프리터는 `c-interpreter\bin\lumi.exe` 를 알아서 찾아 씁니다.

### 배포 폴더에 무엇이 들어가나요

`build-cpp.bat` 이 만드는 `Lumina\` 폴더 구성입니다.

| 항목 | 뜻 |
|---|---|
| `Lumina.exe` | IDE 본체 (Qt6 Widgets) |
| `Qt6Core/Gui/Widgets/Svg.dll` | Qt 런타임. `windeployqt` 가 넣습니다 |
| `platforms/`, `styles/`, `imageformats/` 등 | Qt 플러그인. **없으면 창이 아예 안 뜹니다** |
| `lumi.exe` | Lumi 인터프리터. `Lumina.exe` 옆에 있으면 "패키지 모드"로 인식합니다 |

- 설정은 exe 안이 아니라 `%APPDATA%\Lumina\Lumina.ini` 에 저장됩니다
  (테마·폰트 크기·창 위치·도크 배치·열린 탭·최근 파일/폴더).
- 라이브러리(`.lumi`)를 찾는 곳은 ① 실행 중인 파일과 같은 폴더 ② `lumi.exe` 옆의 `libraries/` 입니다.

### 아이콘을 바꿨을 때

exe 아이콘은 `Lumi&na logo/lumina_icon_TB_icon.ico` 에서 옵니다. 파일을 바꾼 뒤 다시 빌드하면 반영됩니다.

- **속성 창엔 새 아이콘이 나오는데 탐색기 목록엔 옛날 아이콘/깨진 아이콘이 보이면**, 그건 exe 문제가 아니라 **Windows 아이콘 캐시** 때문입니다. 순서대로 시도:
  1. 탐색기에서 `F5` (새로고침)
  2. 아이콘 다시 읽기:
     ```bash
     ie4uinit.exe -show
     ```
  3. 그래도 안 되면 캐시 초기화 + 탐색기 재시작 (작업표시줄이 잠깐 사라졌다 돌아옵니다):
     ```bash
     taskkill /f /im explorer.exe & del /a /q "%LocalAppData%\IconCache.db" & del /a /f /q "%LocalAppData%\Microsoft\Windows\Explorer\iconcache*" & start explorer.exe
     ```
- `.ico` 파일은 **여러 크기(16·24·32·48·64·128·256px)를 한 파일에 담아야** 작은 아이콘 보기에서도 선명합니다.

### 자주 나는 오류 & 해결

| 증상 | 원인 / 해결 |
|---|---|
| `cl.exe ... not found` / `Visual Studio C++ build tools not found` | C++ 빌드 도구 미설치 → 0단계 1번 참고. 설치 경로가 다르면 `c-interpreter\build.bat` 안의 `VS=` 줄을 고치세요 |
| `Could not find a package configuration file provided by "Qt6"` | Qt6 미설치 또는 경로 다름 → 0단계 2번 참고, `ide-cpp\build.bat` 의 `QT=` 줄 확인 |
| 실행하면 `This application failed to start because no Qt platform plugin could be initialized` | `platforms\` 폴더가 빠진 것 → exe만 복사하지 말고 폴더째 쓰거나 `build-cpp.bat` 을 다시 돌리세요 |
| `PermissionError` / `Access is denied` (Lumina.exe) | `Lumina.exe` 가 실행 중 → 프로그램을 끄고 다시 빌드 (1단계) |
| 백신이 exe를 삭제/차단 | 서명 없는 exe의 흔한 **오탐**입니다. 예외로 등록하거나 잠시 실시간 검사를 끄고 빌드 |
| exe 아이콘이 안 바뀜 | 위 **"아이콘을 바꿨을 때"** 참고 (캐시 문제) |
| IDE는 켜지는데 F5가 안 됨 | `lumi.exe` 를 못 찾은 것 → `c-interpreter\build.bat` 을 먼저 실행했는지 확인 |

## Lumi 언어 문법

세미콜론(`;`)이 없고, 블록은 `:` 과 **들여쓰기**로 만듭니다.

### 출력
```
print("안녕하세요")
print("합:", 1 + 2)
```
`print` 는 끝에 자동으로 줄을 바꿉니다. 마지막 자리에 `keep(끝말)` 을 넣으면 줄바꿈 대신 그 끝말이 붙습니다. (`keep` 은 `print` 의 마지막 인자로만 쓸 수 있어요.)
```
print("a", keep(""))
print("b")                    // ab
print("x", "y", keep(" -> "))
print("z")                    // x y -> z
```

### 문자열 — 따옴표 두 가지
```
"안녕\n다음 줄"          // 큰따옴표: \ 뒤의 글자를 특수문자로 읽습니다
'C:\notes\temp.txt'      // 작은따옴표: \ 를 그대로 둡니다 (있는 그대로)
```

**큰따옴표 안의 특수문자**

| 쓰기 | 뜻 | 쓰기 | 뜻 |
|------|----|------|----|
| `\n` | 줄바꿈 | `\r` | 줄 맨 앞으로 |
| `\t` | 탭 | `\e` | 이스케이프 (터미널 색) |
| `\"` `\'` | 따옴표 | `\a` `\b` `\f` `\v` | 삑·뒤로·다음 쪽·세로 탭 |
| `\\` | 역슬래시 | `\xNN` `\uNNNN` `\UNNNNNNNN` | 글자 번호 (`"\uac00"` → `가`) |

표에 **없는** 글자 앞의 `\` 는 적은 그대로 남습니다 (`"C:\사진"` → `C:\사진`).

> **규칙: 파일 주소(경로)는 작은따옴표로 적습니다.** 글자·문장은 큰따옴표, 폴더와 파일 주소는 작은따옴표 — 이렇게 나눠 쓰세요.

```
read('C:\notes\a.txt')          // 주소 -> 작은따옴표
print("파일을 읽었습니다")        // 글자 -> 큰따옴표
```

- 작은따옴표는 **윈도우 경로를 손대지 않고 붙여넣으라고** 있습니다. `"C:\notes\temp.txt"` 는 `\n`·`\t` 가 줄바꿈과 탭이 되지만, `'...'` 는 적은 그대로입니다. 탐색기에서 복사한 주소를 `' '` 사이에 그냥 넣으면 됩니다.
- 다만 이 규칙은 **적는 사람의 약속**이지 언어가 막아 주는 것은 아닙니다 (아래처럼 나온 값이 똑같아서, 변수를 한 번 거치면 어느 따옴표로 썼는지 알 수 없습니다). 그래도 `\` 가 든 주소를 큰따옴표로 적으면 대부분 오류로 걸리니 습관을 들여 두면 편합니다.
- **나온 값은 똑같은 문자열입니다.** `'abc' == "abc"` 는 참이고 `type`·`upper`·`+`·`len` 이 구별하지 않습니다. 따옴표가 하는 일은 코드를 읽는 순간에 끝납니다.
- 작은따옴표 안에는 `'` 를 못 넣습니다(이스케이프가 없으니까요). 그럴 땐 `"don't"` 처럼 큰따옴표를 쓰세요. 반대로 `'그가 "안녕" 이라 했다'` 도 됩니다.

### 변수
처음 만들 때 `val`, 나중에 바꿀 때는 이름만 씁니다.
```
val x = 10
x = x + 5
```

`val` 대신 **자료형**으로 선언하면 값이 그 형에 맞는지 검사합니다 (재할당 때도 검사).
```
int   a = 5          // 정수형
float b = 3.14       // 실수형
num   c = 10         // 숫자형 (정수·실수 모두)
char  d = "A"        // 문자형 (한 글자)
str   e = "hello"    // 문자열
text  f = "Z"        // 텍스트형 (1글자=문자, 2글자↑=문자열)

print(type(a))     // "int"   (type: 자료형 이름 확인)
```

한 줄에 여러 개도 선언할 수 있습니다.
```
int a, b, c              // 기본값 -> 0, 0, 0
int d = 0, e = 1, f = 2  // 각각 값 주기
int g, h, i = 10, 20, 30 // 이름·값 순서대로 짝짓기
```
자세한 규칙은 [사용설명서](Lumi%20사용설명서.md#3-변수-variables)를 참고하세요.

### 자료형
- 숫자: `10`, `3.14`  (2진수 `0b1011`, 16진수 `0xff` 로도 적을 수 있어요 → [진수](#진수-2진수--10진수--16진수))
- 문자열: `"글자"`
- 참/거짓: `true`, `false`
- 없음: `none` (값이 없음, 함수 반환값 기본값)
- 바이트: `encode("hi")` → `bytes[68 69]` (글자를 숫자로 담은 것 → [글자와 바이트](#글자와-바이트-encode--decode))

#### 정수가 담을 수 있는 크기
정수는 **-9223372036854775808 ~ 9223372036854775807** 까지 담습니다. 넘어가면 **조용히 이상한 값이 되지 않고 오류(`MathError`)** 로 알려 줍니다.
```
val 큰수 = 9223372036854775807
print(큰수 + 1)     // MathError: this whole number is too big for Lumi
```
`+ - * **` 는 물론 `sum`, 벡터 산술(`add`·`mul` …)에서도 똑같이 검사합니다. 그렇게 큰 수를 정확히 셀 필요가 없으면 소수로 바꿔 쓰세요 (`1.0 * 큰수`).

### 연산자
- 사칙연산: `+  -  *  /  %  **`(거듭제곱)
- 비트연산: `&  |  ^  ~  <<  >>` (정수 전용)
- 비교: `<  >  <=  >=  ==  !=`  (크고 작음은 **같은 갈래끼리만** — 숫자·글자·리스트·튜플·바이트. `==`/`!=` 는 아무 값끼리나)
- 논리: `and`  `or`  `not`  (`&&`/`||` 는 논리, `&`/`|` 는 비트!)
- 멤버십: `값 in 시퀀스` / `값 not in 시퀀스` → 포함되어 있으면 `true` (아래 [시퀀스](#시퀀스-자료형-seq) 참고)
- 시퀀스 잇기 `+` / 반복 `*`: `"안녕" + "하세요"`, `"ab" * 3` → `ababab` (아래 [시퀀스](#시퀀스-자료형-seq) 참고)
- 안전하게 꺼내기 `?.` / 없을 때 기본값 `??` → 바로 아래

### 없을 때를 짧게 (`?.` · `??`)
`none` 일지도 모르는 값을 다룰 때 `if` 로 감싸지 않고 짧게 씁니다.

```
class 사람:
    func init(이름, 윗사람):
        this.이름 = 이름
        this.윗사람 = 윗사람

val 사장 = 사람("한사장", none)
val 대리 = 사람("김대리", 사장)

print(대리?.윗사람?.이름)          // 한사장
print(사장?.윗사람?.이름)          // none   ('.' 하나였다면 오류)
print(사장?.윗사람?.이름 ?? "없음") // 없음
```

- `?.` 는 **앞의 것이 `none` 이면 `none`**. 부르기(`누구?.인사()`)에도 됩니다.
- `?.` 는 **마디마다** 붙입니다 — `a?.b.c` 는 `b` 가 `none` 이면 오류입니다. 읽기 전용이라 `a?.b = 1` 은 안 됩니다.
- `??` 는 왼쪽이 **`none` 일 때만** 오른쪽. `or` 와 다릅니다: `0 or 99` 는 `99` 지만 `0 ?? 99` 는 `0` 입니다.
- 세기는 `or` 보다 붙고 `and` 보다 헐겁습니다. 예제: [`examples/sugar.lumi`](examples/sugar.lumi)

### 연산자 축약 (복합 대입)
`변수 += 값` 은 `변수 = 변수 + 값` 을 짧게 쓴 것입니다. 대입 계열 연산자는 다음과 같습니다.

| 축약 | 뜻 |
|---|---|
| `+=` `-=` `*=` `/=` `%=` `**=` | 사칙연산·나머지·거듭제곱 |
| `&=` `\|=` `^=` `<<=` `>>=` | 비트연산 (정수 전용) |

```
val x = 10
x += 5          // x = x + 5  -> 15
x *= 2          // 30
x **= 2         // 900

val s = "Lu"
s += "mi"       // "Lumi"  (문자열도 += 로 이어붙이기)

val nums = [10, 20, 30]
nums[0] += 100  // 리스트 원소에도 됩니다 -> [110, 20, 30]

for val i = 0, i < 3, i += 1:   // C 스타일 for 의 증감식에도 씁니다
    print(i)
```

### 증감 연산자 (1씩 늘리고 줄이기)
`++` 는 1을 더하고, `--` 는 1을 뺍니다. `i++` 는 `i = i + 1` 과 똑같습니다.
앞에 붙이든(`++i`) 뒤에 붙이든(`i++`) 뜻은 같습니다 — Lumi 에서는 둘 다 **한 줄짜리 문장**이라
식 안에서 값으로는 쓰지 않습니다 (예: `val a = i++` 는 안 됩니다).

```
val i = 5
i++             // 6
i--             // 5
++i             // 6  (앞에 붙여도 똑같음)

val nums = [10, 20, 30]
nums[0]++       // 리스트 원소에도 됩니다 -> [11, 20, 30]

for val k = 0, k < 3, k++:   // C 스타일 for 의 증감식에 딱 좋습니다
    print(k)
```

이미 있는 변수(또는 리스트·딕셔너리 원소)에만 쓸 수 있습니다. 없는 변수에 `x++` 를 쓰면 오류입니다.

### 튜플 (못 바꾸는 리스트)
소괄호로 만드는 읽기 전용 리스트입니다.
```
val t = (1, 2, 3)
print(t[0])            // 1
print((1,2) + (3,4))   // (1, 2, 3, 4)
// t[0] = 9  <- 오류 (튜플은 못 바꿈)
```

### 지역변수와 전역변수
함수 밖에서 만들면 전역, 함수 안 `val`/`local` 은 지역입니다. 함수 안에서 전역을 만들려면 `global`.
```
val g = 1
func f():
    local x = 5        // 지역변수
    global h = 42      // 전역변수 만들기
f()
print(g, h)            // 1 42
```

### 조건문
```
if score >= 90:
    print("A")
elif score >= 80:
    print("B")
else:
    print("C")
```

### switch — 값에 따라 갈라지기
하나의 값을 여러 후보와 `==` 로 비교해 실행합니다. 기본 메커니즘으로 아래로 흘러내리는 **폴스루(fall-through)** 방식을 사용하며, 빠져나가려면 `break` 문을 사용합니다.
```
switch fruit:
    case "apple":
        print("빨강")
        break
    case "banana", "lemon":    // 쉼표로 여러 값
        print("노랑")
        break
    default:                   // 아무것도 안 맞을 때 (생략 가능)
        print("몰라요")
```
- 기본 메커니즘으로 아래로 흘러내리며(fall-through), `break` 문으로 `switch`를 빠져나올 수 있습니다.
- `default` 는 하나만, 생략 가능합니다.

### 반복문
```
val i = 0
while i < 5:
    print(i)
    i = i + 1
```

**for 반복문** 은 두 가지 방식으로 쓸 수 있습니다.

방식 1 — 리스트(또는 문자열)를 순회하는 Python 스타일:
```
for x in [10, 20, 30]:
    print(x)

for ch in "ABC":
    print(ch)

for 키 in {"a": 1, "b": 2}:    // 딕셔너리는 키 하나씩
    print(키)
```

돌릴 수 있는 것: **리스트 · 튜플 · 문자열 · 바이트 · 딕셔너리(키)**, 그리고 아래의 **내가 만든 클래스**.

**내가 만든 클래스도 돌릴 수 있습니다.** 둘 중 하나를 두면 됩니다.

| 메서드 | 하는 일 |
|--------|---------|
| `iter()` (또는 `items()`) | 돌릴 것들을 **리스트/튜플로 한꺼번에** |
| `next()` | 하나씩 주다가, 더 없으면 **`none`** |

```
class 범위:
    func init(끝):
        this.끝 = 끝
    func iter():
        val out = []
        for val i = 0, i < this.끝, i++:
            push(out, i)
        return out

for x in 범위(4):
    print(x)                    // 0 1 2 3
print([x * 2 for x in 범위(4)])  // [0, 2, 4, 6]   컴프리헨션도 똑같이
```
- `iter()` 가 리스트·튜플이 아닌 것을 돌려주면 오류로 알려 줍니다 (조용히 0번 돌지 않습니다).

방식 2 — C 언어 스타일. 초기화, 조건, 증감 세 부분을 **쉼표**로 구분합니다 (세미콜론 X, 괄호 X).
```
for val i = 0, i < 5, i = i + 1:
    print(i)
```
- 초기화는 `val i = 0` (새 변수 선언) 또는 `i = 0` (기존 변수 재할당) 둘 다 가능합니다.
- 조건이 거짓이면 한 번도 실행되지 않습니다.

### forr() — 범위 만들기 (Python의 range() 역할)
`forr(변수선언, 조건, 증감)` 은 정수 범위를 **리스트**로 만들어 돌려줍니다. 변수는 반드시 `val` 으로 정수를 선언해야 합니다.
```
print(forr(val i = 0, i < 5, i = i + 1))      // [0, 1, 2, 3, 4]
print(forr(val i = 3, i > 0, i = i - 1))      // [3, 2, 1]

// for-in 과 짝으로 자주 씁니다
for n in forr(val k = 1, k <= 5, k = k + 1):
    print(n)
```

### break — 반복문 빠져나오기
`while`, `for`, `for-in` 안에서 `break` 를 쓰면 그 즉시 반복문을 빠져나옵니다.
```
for x in [1, 2, 3, 4, 5]:
    if x == 4:
        break
    print(x)        // 1, 2, 3

val n = 0
while true:         // 무한루프도 break 로 멈출 수 있습니다
    n = n + 1
    if n >= 3:
        break
```

### continue — 이번 차례만 건너뛰기
`for`, `while` 안에서 `continue` 를 만나면 아래 코드를 건너뛰고 다음 순서로 넘어갑니다.

### 예외 처리 — try · catch · safe · always · error
런타임 에러가 발생해도 프로그램이 튕기지 않고 대처할 수 있는 Lumi 스타일 예외 처리 구문입니다.
```
try:
    val a = 10
    val b = 0
    print(a / b)
safe:
    print("계산 성공!")
catch e:
    print("에러 포획:", e)
always:
    print("항상 실행되는 마무리 작업")
```
- `try:` — 예외가 일어날 수 있는 코드를 일단 시도합니다.
- `catch e:` — 에러가 나면 잡아서 **'난 오류' 값**을 `e` 에 담고 처리합니다 (`catch FileNotFound e:` 처럼 종류를 골라 잡을 수도 있습니다).
- `safe:` — 에러 없이 성공적으로 끝났을 때만 실행됩니다.
- `always:` — 에러 유무에 상관없이 **항상** 마지막에 실행됩니다.
- `error("메시지")` — `error("메시지")` 또는 `error("종류", "메시지")` 로 내가 직접 에러를 냅니다.

#### 잡은 에러에서 꺼내 보기
`e` 는 글자가 아니라 **값**이라, 안에 담긴 것들을 따로 꺼내 쓸 수 있습니다.

| 꺼내는 것 | 무엇 |
|---|---|
| `e.type` | 오류 종류 이름 (`"IndexError"`, 내가 지은 이름 등) |
| `e.message` | 설명만 (줄 번호 없이) |
| `e.line` | 난 줄 번호 |
| `e.file` | 난 파일 이름 |
| `print(e)` | `[line 4] [IndexError] Index out of range` 처럼 한 줄로 |
| `type(e)` | 종류 이름 (`e.type` 과 같습니다) |

#### 종류에는 갈래가 있습니다
위엣것으로 잡으면 그 아래 것도 함께 잡힙니다.
```
Error                                  <- 맨 위. catch Error e 는 전부 잡습니다
  ├ LookupError ─ IndexError / KeyError
  ├ FileError   ─ FileNotFound
  ├ TypeError / ValueError / NameError / MathError / ArgumentError
  └ (내가 error("이름", ...) 로 지은 종류들)
```
```
try:
    print([1, 2][9])
catch LookupError e:
    print(e.type, "를 LookupError 로 잡았습니다")   // IndexError 를 ...
```
- **메시지 글자로 잡지 않습니다.** 종류는 값 안에 따로 담기므로, 메시지에 우연히 `FileNotFound` 라는 글자가 있어도 `catch FileNotFound` 에 걸리지 않습니다.
- 표에 없는 이름(내가 지은 종류)은 부모가 `Error` 입니다.
- **한글 종류 이름**을 쓸 때는 `catch 잔액부족 e:` 처럼 종류와 담을 이름을 **둘 다** 적어 주세요. 하나만 적으면 대문자로 시작할 때만 종류로 봅니다.
> 전체 예시는 `examples/trycatch.lumi` 를 참고하세요.

```
for x in [1, 2, 3, 4, 5]:
    if x == 3:
        continue        // 3만 빼고
    print(x)            // 1, 2, 4, 5
```

### 리스트(배열)
여러 값을 한 줄로 담는 자료형입니다. 대괄호 `[ ]` 로 만듭니다.
```
val nums = [10, 20, 30]
print(nums)            // [10, 20, 30]
print(nums[0])         // 10   (0부터 시작)
print(nums[-1])        // 30   (음수는 뒤에서부터)
print(len(nums))       // 3    (길이)
```
- 원소를 바꾸려면 `리스트[번호] = 값` 처럼 씁니다 (음수 번호도 가능):
```
val nums = [10, 20, 30]
nums[0] = 99
nums[-1] = 77
print(nums)            // [99, 20, 77]

val grid = [[1, 2], [3, 4]]
grid[1][0] = 88        // 중첩 리스트도 됩니다
```
- 여러 줄에 걸쳐 리스트를 써도 됩니다:
```
val big = [1, 2,
           3, 4]
```
- 두 리스트는 `+` 로 이을 수 있습니다: `[1, 2] + [3, 4]` → `[1, 2, 3, 4]`
- **슬라이스** `리스트[시작:끝]` 로 일부만 잘라 **새 리스트**를 만듭니다 (끝 번호는 포함 안 함). 양쪽 번호는 생략할 수 있고 음수도 됩니다. 문자열에도 됩니다.
```
val nums = [10, 20, 30, 40, 50]
print(nums[1:3])       // [20, 30]
print(nums[:2])        // [10, 20]   (처음부터)
print(nums[3:])        // [40, 50]   (끝까지)
print(nums[-2:])       // [40, 50]   (뒤에서 2개)
print(nums[:])         // 통째로 복사

print("Hello"[1:4])    // ell
```
- 리스트를 고치는 함수 — **끝**은 `push(리스트, 값)` (끝에 추가), `pop(리스트)` (끝에서 빼기)
```
val stack = []
push(stack, "a")
push(stack, "b")
print(stack)           // ["a", "b"]
print(pop(stack))      // b
```
- **가운데**는 `insert(자리, 값, 리스트)` (끼워 넣기), `erase([자리,] 리스트)` (지우기) → 자세히는 [리스트 고치기](#리스트-고치기-insert--erase)
```
val xs = ["가", "다"]
insert(1, "나", xs)
print(xs)              // ["가", "나", "다"]
erase(0, xs)
print(xs)              // ["나", "다"]
```
> 이 네 함수는 **원래 리스트를 바로 고칩니다.** `sort` 처럼 새 값을 돌려주는 함수들과 반대예요.
- 순서대로 줄 세우기: `sort(리스트)` → 정렬된 **새 리스트** (자세히는 [정렬](#정렬-sort))
- 무작위로 섞기: `shuffle(리스트)` → 섞인 **새 리스트** (자세히는 [뒤섞기](#뒤섞기-shuffle))

### 딕셔너리(키-값)
이름표(키)에 값을 짝지어 담는 자료형입니다. 중괄호 `{ }` 로 만들고 `키: 값` 으로 씁니다.
```
val person = {"name": "Alex", "age": 20}
print(person["name"])      // Alex   (키로 값 꺼내기)
person["age"] = 21         // 기존 키의 값 바꾸기
person["city"] = "Seoul"   // 새 키 추가하기
print(len(person))         // 3
```
- 키는 **문자열·숫자·참거짓** 을 쓸 수 있어요 (리스트나 딕셔너리는 키가 못 됩니다).
- `for` 로 돌리면 **키**를 하나씩 꺼냅니다:
```
val fruit = {"a": 1, "b": 2}
for k in fruit:
    print(k, "=>", fruit[k])
```
- 딕셔너리 함수:
  - `keys(딕셔너리)` : 키들을 리스트로
  - `values(딕셔너리)` : 값들을 리스트로
  - `inpair([모양,] 딕셔너리)` : `[키, 값]` 짝들을 (모양: `list`·`tuple`·`dict`)
  - `has(딕셔너리, 키)` : 그 키가 있으면 `true`, 없으면 `false`
  - `del(딕셔너리[, 키])` : 그 키를 **지우기** → 자세히는 [딕셔너리 고치기](#딕셔너리-고치기-del)
```
print(keys(fruit))         // ["a", "b"]
print(values(fruit))       // [1, 2]
print(has(fruit, "a"))     // true
del(fruit, "a")            // 키 "a" 를 지움
print(fruit)               // {"b": 2}
```

#### 키가 같다는 것
키가 같다는 것은 **값도 같고 종류도 같다**는 뜻입니다.
```
print({2: "a", 2.0: "b"})        // {2: "b"}   숫자끼리는 값으로만 봅니다
val d = {1: "정수", true: "참"}
print(len(d))                    // 2          1 과 true 는 다른 키입니다
print(d[1], d[true])             // 정수 참
print(1 == true)                 // true       값을 견주는 == 는 그대로입니다
```
- 파이썬은 `{1: "a", true: "b"}` 를 한 칸으로 뭉갭니다. Lumi 는 일부러 가릅니다 — 참·거짓을 키로 쓰다가 조용히 덮어쓰는 일이 없도록요.
- 이 규칙은 **키에만** 씁니다. `1 == true` 는 여전히 참입니다.

### 시퀀스 자료형 (seq)
**리스트·튜플·문자열**은 "**시퀀스(sequence)**" 라는 한 가족입니다. 순서가 있고, 다음 공통 기능을 똑같이 씁니다.

| 공통 기능 | 예시 |
|---|---|
| 인덱싱 `[i]` (음수도) | `"abc"[0]` → `a`, `[10,20][-1]` → `20` |
| 슬라이싱 `[a:b]` | `"hello"[1:4]` → `ell`, `[1,2,3][:2]` → `[1, 2]` |
| 길이 `len()` | `len((1,2,3))` → `3` |
| 순회 `for x in ...` | `for ch in "hi": ...` |
| 잇기 `+` (같은 종류끼리) | `[1,2] + [3,4]`, `"ab" + "cd"` |
| 반복 `*` (정수 배) | `"ab" * 3` → `ababab`, `[0] * 3` → `[0, 0, 0]` |
| 멤버십 `in` / `not in` | `"e" in "hello"` → `true`, `20 in [10,20]` → `true` |
| 개수 `incount(것, x)` | `incount("l", "hello")` → `2`, `incount(1, [1,1])` → `2` |
| 자리 `infind(것, x)` | `infind("l", "hello")` → `[2, 3]`, `infind(1, [1,1])` → `[0, 1]` |
| 짝짓기 `inpair([모양,] x)` | `inpair("hi")` → `[[0,"h"], [1,"i"]]`, `inpair(dict, "hi")` → `{0:"h", 1:"i"}` |
| 정렬 `sort([방향,] x)` | `sort([3,1,2])` → `[1, 2, 3]`, `sort(false, "abc")` → `cba` |
| 기준 자리 정렬 `insor(자리, x)` | `insor(0, ["bo","am"])` → `["am", "bo"]` |
| 뒤섞기 `shuffle(x)` | `shuffle([1,2,3])` → `[3, 1, 2]` (부를 때마다 다름) |
| 값마다 함수 `each(f, x)` | `each(func v: v * 2, [1,2])` → `[2, 4]` (늘 리스트) |
| 끼워 넣기 `insert(자리, 값, 리스트)` | `insert(1, "b", ["a","c"])` → `["a","b","c"]` (리스트만) |
| 지우기 `erase([자리,] 리스트)` | `erase(0, xs)`, `erase(1:3, xs)`, `erase(xs)` (리스트만) |

바이트 객체(`encode(...)`)도 같은 가족이라 위 기능이 그대로 됩니다 → [글자와 바이트](#글자와-바이트-encode--decode)

`seq` 로 선언하면 **리스트·튜플·문자열·바이트 어느 것이든** 담을 수 있는 변수가 됩니다 (시퀀스가 아닌 값은 오류).
```
seq a = [1, 2, 3]
seq b = "hello"
seq c = (10, 20)
a = "다른 시퀀스로 바꿔도 OK"    // 시퀀스끼리는 자유롭게 재할당
seq empty                       // 값 없이 선언하면 빈 시퀀스 []
```
- `type()` 은 여전히 실제 종류(`list`/`tuple`/`str`/`bytes`)를 그대로 알려줍니다. `seq` 는 "시퀀스 중 하나"를 담겠다는 **선언용 이름**입니다.
- 멤버십: 문자열에서는 `in` 이 **부분 문자열** 검사입니다 (`"ell" in "hello"` → `true`). 딕셔너리에 쓰면 **키**가 있는지 검사합니다.

```
if "a" in "banana":
    print("found")
print(5 not in [1, 2, 3])      // true
```

### 함수
```
func add(a, b):
    return a + b

print(add(3, 4))   // 7
```

#### 기본값 인자 · 이름 붙인 인자
매개변수에 `= 값` 을 붙이면 **안 줘도 되는 인자**가 됩니다. 옵션이 여럿인 함수를 하나로 둘 수 있어요.
```
func 인사(이름, 말 = "안녕", 느낌표 = false):
    val s = 말 + ", " + 이름
    if 느낌표:
        return s + "!"
    return s

print(인사("루미"))                 // 안녕, 루미
print(인사("루미", "반가워"))        // 반가워, 루미
print(인사("루미", 느낌표 = true))   // 안녕, 루미!   <- 가운데를 건너뛰기
print(인사(말 = "하이", 이름 = "영희"))   // 이름을 붙이면 순서도 자유
```
- **기본값이 붙은 것은 뒤에** 모아 둡니다. `func g(a = 1, b):` 는 오류입니다.
- 부를 때도 **이름 붙인 것을 뒤에** 둡니다. `f(1, b = 2)` 는 되고 `f(a = 1, 2)` 는 오류입니다.
- **기본값은 부를 때마다 새로 셈합니다.** 그래서 `func 담기(값, 통 = [])` 는 부를 때마다 새 리스트를 받습니다 (파이썬에서 자주 걸리는 함정이 없어요). 기본값 안에서 바깥 변수도 쓸 수 있습니다.
- 클래스의 `init` 과 메서드에서도 똑같이 됩니다: `상자(이름 = "배", 무게 = 3.0)`.
- 이름 붙인 인자는 **내가 만든 함수·클래스에만** 씁니다. 내장 함수(`len`, `sort` …)는 값을 순서대로 받습니다.
- 이름 없는 한 줄 함수(`func x: ...`)에는 기본값을 못 답니다.
> 전체 예시는 `examples/defaults.lumi` 를 참고하세요.

#### 자료형 적어 두기 (골라 쓰는 정적 타입)
매개변수 앞과 `)` 뒤에 자료형 이름을 적어 둘 수 있습니다. **적고 싶은 곳에만** 적으면 되고, 하나도 안 적으면 예전과 똑같습니다.

```
func 나이말하기(str 이름, int 살) str:
    return 이름 + "은(는) " + str(살) + "살"

print(나이말하기("루미", 3))     // 루미은(는) 3살
print(나이말하기(3, 3))          // Error: str needs text (a string) but got 3
```

- 쓸 수 있는 이름 12 개: `int` `float` `num` `char` `str` `text` `seq` `bytes` (값을 다듬음) · `bool` `list` `tuple` `dict` (맞는지 보기만)
- 검사 규칙은 `int x = 5` **선언과 똑같습니다** — 같은 코드가 봅니다. `int` 자리의 `3.9` 는 `3` 이 되고, 글자는 아스키 코드가 됩니다.
- 매개변수는 **부를 때**(기본값 포함), 돌려주는 값은 **끝날 때** 봅니다. 돌려주는 자료형을 적으면 `return` 없이 끝나는 길도 오류입니다.
- 뒤의 넷은 **선언에도** 쓸 수 있습니다: `bool 켜짐 = true`, `dict 표 = {}`.
- **`lumi lint` 가 돌리기 전에 잡아 줍니다** — 그 자리에 글자로 적어 둔 값만 봅니다 (변수는 실행할 때 검사).
> 전체 예시는 [`examples/types.lumi`](examples/types.lumi).

### 이름 없는 한 줄 함수
`func 인자, 인자: 식` 을 한 줄로 적으면 이름 없는 함수 **값**이 됩니다. `return` 없이 `:` 뒤의 식이 결과입니다.
```
val 두배 = func x: x * 2
print(두배(5))                   // 10

val 더하기 = func a, b: a + b
print(더하기(3, 4))               // 7

val 인사 = func : "안녕"          // 인자 없이도 OK
print(인사())                     // 안녕

func 적용(f, v):
    return f(v)
print(적용(func x: x * x, 7))     // 49
```
- 식 하나만 됩니다(문장·여러 줄은 `func 이름(...)`), 재귀는 이름이 없어 안 됩니다.

### 값을 바로 부르기 (함수는 값입니다)
함수가 담긴 **자리를 그 자리에서 바로** 부를 수 있습니다. 변수에 옮겨 담지 않아도 됩니다.
```
val 목록 = [func x: x * 2]
print(목록[0](5))                 // 10   리스트 안의 함수

func 바깥(n):
    func 안(x):
        return x + n
    return 안
print(바깥(5)(3))                 // 8    돌려받은 함수를 바로

val 표 = {"두배": func x: x * 2, "제곱": func x: x * x}
print(표["제곱"](7))              // 49   디스패치 표(콜백 테이블)

class 단추:
    func init(누르면):
        this.누르면 = 누르면
val b = 단추(func x: "눌림 " + str(x))
print(b.누르면(9))                // 눌림 9   객체 칸에 담아 둔 함수
```
- 함수가 아닌 값을 부르면 `'this value' is not a function` 오류가 납니다.

### 오류가 났을 때 — 어디서 왔는지 보기
잡히지 않은 오류는 메시지 아래에 **어느 파일 몇째 줄을 거쳐 왔는지**를 안쪽부터 차례로 보여 줍니다.
```
Error: [line 2] Index out of range
  at fetch (lib_helper.lumi:2)      <- 오류가 난 자리
  at get_user (app.lumi:4)
  at handle (app.lumi:8)
  at <main> (app.lumi:11)           <- 파일 본문
```
`<main>` 은 함수 안이 아니라 파일 본문이라는 뜻입니다. 재귀가 깊으면 가운데는 `... N more frame(s) ...` 로 접어서 보여 줍니다. `try` 로 잡은 오류에는 나오지 않습니다(프로그램이 멈추지 않았으니까요).

### 입력 받기
`input()` 은 사용자에게 한 줄을 입력받아 **문자열**로 돌려줍니다.
숫자로 쓰려면 `num()` 으로 바꿉니다. (IDE에서는 입력 팝업이 떠요.)
```
val name = input("이름이 뭐예요? ")
print("반가워요,", name)

val age = num(input("나이: "))
print("내년엔", age + 1, "살")
```
- 입력이 닫혀 더 읽을 줄이 없으면 (`lumi.exe 파일.lumi < 입력.txt` 로 준 줄을 다 쓴 경우 등) **줄 번호가 붙은 루미 오류**로 알려 줍니다.

### 형 변환 함수
**자료형 이름을 그대로 함수처럼 부르면** 그 자료형으로 바뀝니다. 자료형마다 하나씩 있어요.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `int(값)` | **정수**로 (소수점 아래 버림) | `int(3.9)` → `3`, `int("42")` → `42`, `int("A")` → `65` |
| `float(값)` | **실수**로 | `float("3.5")` → `3.5`, `float(7)` → `7.0` |
| `num(값)` | **숫자**로 (정수/실수 자동) | `num("42")` → `42`, `num("3.5")` → `3.5` |
| `char(값)` | **글자 하나**로 | `char(65)` → `"A"`, `char("hello")` → `"h"` |
| `str(값)` | **문자열**로 | `str(3.14)` → `"3.14"`, `str(true)` → `"true"` |
| `text(값)` | **문자열**로 (`str` 과 동일) | `text(12)` → `"12"` |
| `bool(값)` | **참/거짓**으로 | `bool(0)` → `false`, `bool("a")` → `true` |
| `list(값)` | **리스트**로 | `list("abc")` → `["a","b","c"]` |
| `tuple(값)` | **튜플**로 | `tuple([1,2])` → `(1, 2)` |
| `dict(값)` | **딕셔너리**로 ([키,값] 짝 리스트에서) | `dict([["a",1]])` → `{"a": 1}` |
| `seq(값)` | **시퀀스**로 (리스트/튜플/문자열/바이트는 그대로) | `seq({"a":1})` → `["a"]` |
| `bytes(값)` | **바이트**로 (0~255 숫자 리스트에서) | `bytes([72,73])` → `bytes[48 49]` |

```
val n = 5
print("점수: " + str(n) + "점")     // 점수: 5점
print(int("3.9"))                  // 3
print(char(int("A") + 1))          // B   (문자 코드로 계산)
print(list("루미"))                 // ["루", "미"]
```
- 바꿀 수 없으면 오류가 납니다. (`int("사과")`, `list(5)` 등)
- 원래 변수는 그대로 두고 **바뀐 값을 새로 돌려줍니다**.
- `int x = 5` 같은 **선언**은 "맞는 자료형인지 검사", `int(5)` 같은 **호출**은 "그 자료형으로 변환" 입니다.

### 문자열 검사 (알파벳·숫자 확인)
문자열이 **어떤 글자로 되어 있는지** 확인해서 `true`/`false` 를 줍니다. 값을 바꾸지 않고 **확인만** 해요.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `isalpha(문자열)` | **전부** 알파벳이면 `true` | `isalpha("Lumi")` → `true` |
| `isdigit(문자열)` | **전부** 숫자면 `true` | `isdigit("2026")` → `true` |
| `isalgit(문자열)` | **전부** 알파벳 또는 숫자면 `true` | `isalgit("Lumi2026")` → `true` |
| `hasalpha(문자열)` | 알파벳이 **하나라도** 있으면 `true` | `hasalpha("a1")` → `true` |
| `hasdigit(문자열)` | 숫자가 **하나라도** 있으면 `true` | `hasdigit("a1")` → `true` |
| `hasalgit(문자열)` | 알파벳이나 숫자가 **하나라도** 있으면 `true` | `hasalgit("!a!")` → `true` |

이름은 **묻는 방법(`is`/`has`) + 글자 종류(`alpha`/`digit`/`algit`)** 로 만들어집니다. `algit` 은 `al`pha + di`git`, 즉 "알파벳이거나 숫자"예요.

```
print(isalpha("Lumi"))      // true
print(isalpha("Lumi2"))     // false   숫자가 섞였음
print(isdigit("2026"))      // true
print(isdigit("20.26"))     // false   점은 숫자가 아님
print(isalgit("Lumi2026"))  // true    영어와 숫자만
print(isalgit("Lumi 26"))   // false   공백이 섞임
print(hasalpha("a1b2"))     // true
print(hasdigit("abcd"))     // false

val age = input("나이: ")
if isdigit(age):
    print("내년엔", num(age) + 1, "살")
else:
    print("숫자만 적어 주세요")
```
- `is...` 는 "**모두** 그런가?", `has...` 는 "**하나라도** 있나?" 를 묻습니다.
- 알파벳은 영어 `A`~`Z`, `a`~`z` 만, 숫자는 `0`~`9` 만입니다. 한글·공백·기호는 어느 쪽도 아니에요.
- 그래서 `isalgit` 은 **영어와 숫자만으로 되어 있는지** 보는 검사입니다. 아이디 검사에 딱 좋아요.
- 빈 문자열 `""` 은 확인할 글자가 없어서 모두 `false` 입니다.
- 문자열만 넣을 수 있습니다. `isdigit(123)` 은 오류이니 `isdigit(str(123))` 처럼 바꿔서 넣으세요.

### 찾기 (개수 세기 · 자리 찾기)
`in` 은 "있나 없나"만 알려줍니다. **몇 개인지**, **어디에 있는지**까지 알고 싶을 때 쓰는 함수예요.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `incount(찾을 것, 시퀀스)` | **몇 개** 있는지 (없으면 `0`) | `incount("l", "hello")` → `2` |
| `infind(찾을 것, 시퀀스)` | **어디에** 있는지, 자리 번호 리스트 (없으면 `[]`) | `infind("l", "hello")` → `[2, 3]` |

둘 다 **찾을 것을 먼저, 시퀀스를 나중에** 적습니다. `값 in 시퀀스` 와 읽는 순서가 같아요.

```
val s = "hello world"
print(incount("l", s))              // 3
print(infind("l", s))               // [2, 3, 9]
print(incount("z", s))              // 0        없으면 0
print(infind("z", s))               // []       없으면 빈 리스트

print(incount(10, [10, 20, 10]))    // 2        리스트도
print(infind("수", ("월","화","수"))) // [2]      튜플도
print(incount("도레", "도레미 도레")) // 2        여러 글자도

for i in infind("l", s):            // 찾은 자리를 하나씩
    print(i, "->", s[i])
```
- 자리 번호는 인덱싱과 똑같이 **`0` 부터** 셉니다. `s[자리]` 로 다시 꺼낼 수 있어요.
- 첫 번째 자리만 필요하면 `infind(...)[0]`. 꺼내기 전에 **비어 있지 않은지 확인**하세요.
- 문자열에서는 찾은 것끼리 **겹치지 않습니다**: `incount("aa", "aaa")` → `1`.
- 그래서 `len(infind(x, s))` 는 **언제나** `incount(x, s)` 와 같습니다.
- 문자열·리스트·튜플에만 씁니다. **딕셔너리는 자리 번호가 없어서** `has(딕셔너리, 키)` 를 쓰세요.

### 자리와 값 짝짓기 (`inpair`)
`for-in` 은 **값**만, `infind` 는 **자리**만 줍니다. **둘 다** 필요하면 `inpair` 를 쓰세요.
`inpair([모양,] 시퀀스)` 는 모든 원소를 **`[자리, 값]` 짝**으로 만들어 돌려줍니다.

```
print(inpair(["가", "나", "다"]))   // [[0, "가"], [1, "나"], [2, "다"]]
print(inpair("hi"))                 // [[0, "h"], [1, "i"]]

for 짝 in inpair(["가", "나", "다"]):
    print(짝[0], "번째는", 짝[1])

val 나이 = {"민수": 12, "영희": 13}
print(inpair(나이))                 // [["민수", 12], ["영희", 13]]   딕셔너리는 [키, 값]
```

**모양 고르기** — 앞에 `list` · `tuple` · `dict` 중 하나를 **따옴표 없이** 적습니다 (생략하면 `list`).

| 모양 | 돌려주는 것 | `["가", "나"]` 를 넣으면 |
|------|-------------|--------------------------|
| 생략 · `list` | 리스트의 리스트 (기본) | `[[0, "가"], [1, "나"]]` |
| `tuple` | 튜플의 튜플 (못 바꿈) | `((0, "가"), (1, "나"))` |
| `dict` | 딕셔너리 — 자리가 키 | `{0: "가", 1: "나"}` |

```
val xs = ["가", "나"]
print(inpair(tuple, xs))   // ((0, "가"), (1, "나"))
print(inpair(dict, xs))    // {0: "가", 1: "나"}
print(inpair(dict, 나이))  // {"민수": 12, "영희": 13}   딕셔너리 복사본
```
- 모양은 **바깥과 짝 둘 다**에 걸립니다. `tuple` 이면 짝도 튜플이라 아무것도 못 바꿉니다.
- 문자열·리스트·튜플·바이트 무엇을 넣어도 **나오는 모양은 고른 것**을 따릅니다.
- 짝의 개수는 **언제나 `len(시퀀스)`**. 빈 시퀀스는 빈 결과(`[]`·`()`·`{}`)입니다.
- **딕셔너리**는 자리가 없어서 `[키, 값]` 짝을 줍니다. `inpair(dict, d)` 는 딕셔너리 **복사본**이에요.
- `insor` 와 잘 어울립니다: `insor(false, 1, inpair([10,30,20]))` → `[[1,30], [2,20], [0,10]]` (원래 자리를 잃지 않고 값 기준 정렬).

### 대소문자 바꾸기 (`upper` · `lower`)
검사 함수가 "확인만" 한다면, 이 둘은 실제로 **바꿔서 새 문자열을 돌려줍니다**.
**바꿀 자리를 앞에, 문자열을 마지막에** 적고, **앞자리를 안 적으면 전체**를 바꿉니다.

| 적는 법 | 뜻 | 예시 |
|---------|-----|------|
| `upper(문자열)` | **전체** | `upper("hello")` → `"HELLO"` |
| `upper(자리, 문자열)` | 그 **한 글자** | `upper(0, "hello")` → `"Hello"` |
| `upper(시작:끝, 문자열)` | 그 **구간** (슬라이싱과 같음) | `upper(0:2, "hello")` → `"HEllo"` |
| `upper([자리, 자리], 문자열)` | 적어 준 **자리들** | `upper([0, 4], "hello")` → `"HellO"` |

`lower` 도 같은 네 가지 방법으로 쓰며 방향만 반대입니다.

```
val s = "hello world"
print(upper(s))             // HELLO WORLD   전체
print(lower("LUMI"))        // lumi

print(upper(0, s))          // Hello world   그 자리 한 글자
print(upper(-1, s))         // hello worlD   음수는 뒤에서부터

print(upper(0:5, s))        // HELLO world   0 이상 5 미만
print(upper(:5, s))         // HELLO world   시작 생략 = 처음부터
print(upper(6:, s))         // hello WORLD   끝 생략 = 마지막까지

print(upper([0, 6], s))     // Hello World   떨어져 있는 자리들
print(upper(infind("o", s), s))  // hellO wOrld   찾은 자리만!

print(s)                    // hello world   원본은 그대로
```
- 자리 번호는 인덱싱과 똑같이 **`0` 부터**, 음수면 뒤에서부터입니다.
- 구간은 슬라이스와 같은 규칙이라 **끝 자리는 포함하지 않고**, 범위를 넘겨도 있는 데까지만 바꿉니다. 반대로 **자리 번호**가 문자열 밖이면 오류입니다.
- 숫자·기호·한글처럼 **대소문자가 없는 글자**는 그대로 지나갑니다.
- **원본은 바뀌지 않습니다.** 남기려면 `s = upper(s)` 처럼 다시 넣으세요.
- 구간 `0:3` 은 **`upper`/`lower`/`strip` 안에서만** 쓰는 적는 법입니다(`val r = 0:3` 은 오류). 잘라내기 `s[0:3]` 과 헷갈리지 마세요 — `s[0:3]` 은 **그 부분만**, `upper(0:3, s)` 는 **바꾼 문장 전체**를 줍니다.

### 공백 지우기 (`strip`)
적는 법은 `upper`/`lower` 와 **완전히 같습니다.** 앞자리를 안 적으면 **전체**의 공백을 지웁니다.

| 적는 법 | 뜻 | 예시 |
|---------|-----|------|
| `strip(문자열)` | **전체**의 공백 | `strip("a b c")` → `"abc"` |
| `strip(자리, 문자열)` | 그 자리가 공백이면 | `strip(1, "a b c")` → `"ab c"` |
| `strip(시작:끝, 문자열)` | 그 **구간 안의** 공백 | `strip(0:3, "a b c")` → `"ab c"` |
| `strip([자리, 자리], 문자열)` | 적어 준 **자리의** 공백 | `strip([1, 3], "a b c")` → `"abc"` |

```
val s = "a b c d"        // 자리: 0=a 1=공백 2=b 3=공백 4=c 5=공백 6=d

print(strip(s))          // abcd     전체
print(strip(1, s))       // ab c d   1번이 공백이라 지워짐
print(strip(0, s))       // a b c d  0번은 'a' 라서 그대로
print(strip(3:, s))      // a bcd    3번부터 끝까지
print(strip([1, 5], s))  // ab cd    적어 준 자리들만

val 답 = input("숫자: ")   // " 42 " 라고 쳐도
print(num(strip(답)) + 1) // 43       공백을 털고 숫자로
```
- **지우는 것은 공백뿐**입니다. 자리를 적어도 그 자리가 공백이 아니면 그 글자는 그대로 남습니다.
- 공백은 **스페이스 · 탭 · 줄바꿈 · 전각 공백**(한글 상태에서 친 공백)을 뜻합니다.
- **자리 번호는 "지우기 전" 기준**이라, 지워서 길이가 줄어도 번호가 밀리지 않습니다.
- 나머지 규칙(0부터, 음수, 구간, 원본 보존)은 `upper`/`lower` 와 같습니다.
- 파이썬의 `strip` 은 **앞뒤만** 털지만 루미의 `strip` 은 **적어 준 자리**를 지웁니다(안 적으면 전부). 앞뒤만 털려면 `strip(0:2, strip(-2:, s))` 처럼 구간으로 적으세요.

### 나누기 · 합치기 · 바꾸기 · 서식 맞추기 (`split` · `join` · `replace` · `format`)
`split` 은 문자열을 **잘라서 리스트로**, `join` 은 리스트를 **이어서 문자열로** 만드는 **반대 짝**입니다. `format` 은 값들을 **서식에 맞춰 문자열로** 채워 만듭니다.
여기서도 **채울 값/인자나 대상이 앞, 서식 문자열/대상이 마지막**입니다.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `split(구분자, 문자열)` | 잘라서 **리스트로** | `split(",", "a,b,c")` → `["a","b","c"]` |
| `split(문자열)` | 생략하면 **공백 기준** | `split("a b  c")` → `["a","b","c"]` |
| `join(이을 것, 시퀀스)` | 이어서 **문자열로** | `join("-", ["a","b"])` → `"a-b"` |
| `join(시퀀스)` | 생략하면 **그냥 붙임** | `join(["a","b"])` → `"ab"` |
| `replace(바꿀 것, 새 것, 문자열)` | 찾은 것을 **모두** 바꾸기 | `replace(" ", "-", "a b")` → `"a-b"` |
| `format(값들..., 서식문자열)` | 서식의 `{}` 에 **값을 채워 문자열로** | `format("홍길동", 20, "이름: {}, 나이: {}")` → `"이름: 홍길동, 나이: 20"` |

```
val 줄 = "김,이,박"
print(split(",", 줄))              // ["김", "이", "박"]
for 이름 in split(",", 줄):
    print("-", 이름)

print(split("  앞뒤   공백  "))     // ["앞뒤", "공백"]   여러 칸도 한 번으로
print(join(", ", [1, 2, 3]))       // 1, 2, 3          숫자도 알아서 글자로
print(join(",", split(",", 줄)))   // 김,이,박          되돌아옴 (서로 반대)

print(replace(",", ", ", "1,2,3"))     // 1, 2, 3
print(replace("-", "", "010-1234-5678")) // 01012345678   새 것이 "" 면 지우기
print(join("-", split(",", strip(" 3, 1, 2 "))))  // 3-1-2

print(format("안녕", "세계", "{1}, {0}!"))              // 안녕, 세계!
print(format(["사과", 3], "품목: {}, 개수: {}개"))       // 리스트도 묶어서 전달 가능
print(format({"name": "Alex"}, "Hello {name}"))         // 딕셔너리로 이름 지정 가능
print(format(3.14159, "소수점: {:.2f}"))                 // 소수점: 3.14
```
- 조각 개수는 언제나 **`incount(구분자, 문자열) + 1`**. 구분자 자체는 조각에 남지 않습니다.
- 구분자로 **빈 문자열은 못 씁니다** — 글자 하나씩은 `list("abc")`.
- `join` 의 **조각**은 숫자여도 되지만(`+` 처럼 글자로 바뀜), **사이에 끼우는 것은 문자열**이어야 합니다.
- `replace` 는 못 찾아도 오류가 아니라 **원래 문자열 그대로**입니다. 원본은 넷 다 바뀌지 않습니다.
- `format` 은 위치 인자(`{}`/`{0}`), 리스트/튜플 묶음, 딕셔너리 키(`{name}`), 소수점/진법/정렬 서식 지정자(`{:.2f}`, `{:04d}`, `{:x}`, `{:b}`, `{:>10}`)를 모두 지원합니다. `{` 자체를 출력할 땐 `{{` 로 씁니다.

### 정렬 (`sort`)
시퀀스를 **순서대로 줄 세운 새 시퀀스**를 돌려줍니다. 여기서도 **방향이 앞, 시퀀스가 마지막**이에요.
방향은 **참/거짓**으로 적습니다 — **`true` 면 오름차순, `false` 면 내림차순**이에요.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `sort(시퀀스)` | **작은 것부터** (오름차순) | `sort([3,1,2])` → `[1, 2, 3]` |
| `sort(true, 시퀀스)` | 작은 것부터 (위와 같음) | `sort(true, "cab")` → `"abc"` |
| `sort(false, 시퀀스)` | **큰 것부터** (내림차순) | `sort(false, [3,1,2])` → `[3, 2, 1]` |

```
print(sort([3, 1, 2]))                  // [1, 2, 3]
print(sort(false, [3, 1, 2]))           // [3, 2, 1]
print(sort(["다", "가", "나"]))           // ["가", "나", "다"]   한글은 가나다 순
print(sort("banana"))                   // aaabnn            문자열은 '글자'가 줄을 섬
print(sort((3, 1)))                     // (1, 3)            넣은 것과 같은 종류로

val xs = [3, 1, 2]
print(sort(xs), xs)                     // [1, 2, 3] [3, 1, 2]   원본은 그대로!
xs = sort(xs)                           // 남기려면 다시 넣기

print(sort(split("바나나 사과 포도")))     // ["바나나", "사과", "포도"]  단어 정렬
print(sort(false, [78, 92, 85])[0])     // 92                 가장 큰 값
```
- 나온 것은 **넣은 것과 같은 종류**입니다: 리스트→리스트, 튜플→튜플, 문자열→글자를 줄 세운 문자열, 바이트→바이트.
- 원본은 **바뀌지 않습니다**. 남기려면 `xs = sort(xs)`.
- 크고 작음을 견줄 수 있어야 하므로 원소는 **모두 같은 갈래**(숫자끼리·글자끼리·리스트끼리·튜플끼리·바이트끼리)여야 합니다. `sort([1, "a"])` 는 오류예요. `true`/`false` 는 숫자와 같은 갈래입니다.
- **리스트·튜플·바이트도 정렬됩니다.** `<` 처럼 **앞에서부터 차례로** 견주기 때문이에요: `sort([[2,1],[1,9]])` → `[[1, 9], [2, 1]]`.
- 방향 자리에는 **`true`/`false` 만** 올 수 있어요(`sort("down", xs)` 는 오류). 참/거짓이 나오는 변수나 식은 그대로 넣어도 됩니다.
- 글자는 **문자 코드 순**이라 대문자가 먼저입니다. 대소문자를 무시하려면 `sort(lower(s))`. 딕셔너리는 `sort(keys(d))` 처럼 꺼내서 정렬합니다.

#### 기준 자리로 정렬 (`insor`)
`sort` 가 **값 전체**를 견준다면, `insor` 는 **속에 있는 한 자리**만 기준으로 삼습니다. 이름·점수처럼 여러 칸이 묶인 표를 "점수 순"으로 세울 때 써요. (`in`(안의) + `sor`t)

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `insor(기준자리, 시퀀스)` | 그 자리 값이 **작은 것부터** | `insor(1, 표)` |
| `insor(방향, 기준자리, 시퀀스)` | 방향까지 정해서 | `insor(false, 1, 표)` |

```
val 표 = [["김", 90], ["박", 70], ["이", 80]]

print(insor(1, 표))          // [["박", 70], ["이", 80], ["김", 90]]   점수 낮은 순
print(insor(false, 1, 표))   // [["김", 90], ["이", 80], ["박", 70]]   점수 높은 순
print(insor(0, 표))          // 이름 가나다 순
print(insor(0, ["bob", "amy"]))   // ["amy", "bob"]   문자열이면 글자 하나가 기준
```
- 자리 번호는 **0부터**, 음수면 **뒤에서부터**. 모든 항목에 그 자리가 있어야 합니다.
- **기준 자리 값만** 견주므로 나머지 칸에는 어떤 값이 있어도 됩니다.
- 기준값이 같으면 **원래 순서를 그대로** 지켜서, `insor(1, insor(0, 표))` 처럼 이어 쓰면 1순위·2순위 정렬이 됩니다.
- 여기서도 원본은 그대로이고, 넣은 것과 같은 종류(리스트→리스트, 튜플→튜플)로 나옵니다.

### 뒤섞기 (`shuffle`)
`sort` 가 순서대로 줄을 세우는 것이라면, `shuffle` 은 **반대로 순서를 무작위로 흩어 놓습니다**. 제비뽑기·카드 섞기·문제 순서 바꾸기에 써요.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `shuffle(시퀀스)` | 순서를 **무작위로** 바꾼 새 시퀀스 | `shuffle([1,2,3])` → `[3, 1, 2]` |

```
print(shuffle([1, 2, 3, 4, 5]))    // [3, 5, 1, 4, 2]   부를 때마다 달라집니다
print(shuffle("abcdef"))           // dbfaec            글자도 섞입니다
print(shuffle((1, 2, 3)))          // (2, 3, 1)         넣은 것과 같은 종류로

val 메뉴 = ["김밥", "라면", "돈까스"]
print(shuffle(메뉴)[0])              // 아무거나 하나 고르기
print(shuffle(메뉴)[0:2])            // 겹치지 않게 두 개 뽑기

val xs = [1, 2, 3]
print(shuffle(xs), xs)             // [2, 1, 3] [1, 2, 3]   원본은 그대로!
xs = shuffle(xs)                   // 남기려면 다시 넣기
```
- 나온 것은 `sort` 와 마찬가지로 **넣은 것과 같은 종류**입니다: 리스트→리스트, 튜플→튜플, 문자열→글자를 섞은 문자열, 바이트→바이트.
- 원본은 **바뀌지 않습니다**. 남기려면 `xs = shuffle(xs)`.
- **자리만 바꿀 뿐** 값을 더하거나 빼지 않아요. 그래서 `len` 은 그대로이고, `sort(shuffle(xs)) == sort(xs)` 는 언제나 참입니다.
- 크고 작음을 견주지 않으므로 `sort` 와 달리 **갈래가 섞여 있어도** 됩니다: `shuffle([1, "가", true])` 도 잘 됩니다.
- 딕셔너리는 받지 않습니다. `shuffle(keys(d))` 처럼 꺼내서 섞으세요.
- **부를 때마다 결과가 다릅니다.** 같은 결과를 두 번 쓰려면 변수에 담아 두세요 (`val 차례 = shuffle(이름들)`).

### 값마다 함수 부르기 (`each`)
같은 일을 시퀀스의 값마다 되풀이하고 싶을 때 씁니다. **인자만 바꿔 가며 함수를 한 번씩 부르고**, 나온 결과를 리스트로 모아 줍니다 (파이썬의 `map`).
여기서도 **다룰 시퀀스가 마지막**, 어떤 함수로 할지는 그 앞입니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `each(함수, 시퀀스)` | 값마다 `함수(값)` 을 부름 | 결과를 모은 **리스트** |

```
val 두배 = func x: x * 2
print(each(두배, [1, 2, 3]))            // [2, 4, 6]
print(each(func x: x + 1, [1, 2, 3]))   // [2, 3, 4]   그 자리에서 만들어 넘기기

func 제곱(x):                            // 이름 있는 함수도 그대로
    return x * x
print(each(제곱, (1, 2, 3)))             // [1, 4, 9]

print(each(func c: upper(c), "abc"))    // ["A", "B", "C"]   글자는 한 글자씩
print(join("", each(func c: upper(c), "abc")))   // ABC   글자로 되돌리려면 join
```
- 나오는 것은 **언제나 리스트**입니다. 함수가 무엇을 돌려줄지 모르니 `sort`·`shuffle` 처럼 넣은 종류를 지키지 않아요.
- **길이는 늘 같습니다**: `len(each(f, xs)) == len(xs)`. 원본은 바뀌지 않습니다.
- 함수는 인자를 **하나**만 받습니다 (값 하나 → 결과 하나).
- 함수는 **값**이어야 해서, `upper` 같은 내장 함수는 이름만으로 넘길 수 없습니다. `each(func s: upper(s), xs)` 처럼 한 줄 함수로 감싸세요.
- 딕셔너리는 반쪽이 둘이라 받지 않습니다. `each(f, keys(d))` 나 `each(f, values(d))` 로 쓰세요.
- 클래스도 부를 수 있는 것이라 `each(사람, 이름들)` 처럼 값마다 객체를 만들 수 있습니다.
- 골라내기(파이썬의 `filter`)는 아직 없습니다. `for` 문에 `if` 를 넣어 쓰세요.

### 리스트 고치기 (`insert` · `erase`)
`push`·`pop` 이 리스트의 **끝**만 손본다면, `insert`·`erase` 는 **가운데**를 손봅니다.
여기서도 **다룰 리스트가 마지막**, 어떻게 할지는 그 앞입니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `insert(자리, 값, 리스트)` | 그 자리에 **끼워 넣기** (뒤의 값들은 한 칸씩 밀림) | 새 길이 |
| `erase([자리,] 리스트)` | 그 자리를 **지우기** | 지운 개수 |

> **원래 리스트를 바로 고칩니다.** `sort`·`strip`·`replace` 는 원본을 두고 새 값을 돌려주지만, `push`·`pop`·`insert`·`erase` 는 리스트 자체를 고쳐요. 그래서 `xs = insert(...)` 라고 쓰면 안 됩니다(길이가 들어갑니다).

```
val xs = ["가", "다", "라"]
insert(1, "나", xs)
print(xs)                  // ["가", "나", "다", "라"]
print(insert(0, "!", xs))  // 5    새 길이
```
- `insert` 의 자리는 **0부터 길이까지**. **길이**를 적으면 맨 뒤라서 `push` 와 같고, 음수는 뒤에서부터 세어 `-1` 은 "마지막 값 바로 앞"입니다.

`erase` 의 자리는 `upper`·`strip` 과 **똑같이 네 가지**로 적습니다 (`xs = [10, 20, 30, 40, 50]` 기준).

| 적는 법 | 뜻 | 예시 |
|---|---|---|
| 안 적음 | **전부** 지우기(비우기) | `erase(xs)` → `[]` |
| 자리 하나 | 그 자리만 | `erase(0, xs)` → `[20, 30, 40, 50]` |
| 구간 `시작:끝` | 그 구간 (슬라이싱과 같은 방식) | `erase(1:3, xs)` → `[10, 40, 50]` |
| 자리 리스트 | 적어 준 자리들 | `erase([0, -1], xs)` → `[20, 30, 40]` |

- 자리 번호는 **지우기 전** 기준이라, 여러 자리를 한 번에 적어도 번호가 밀리지 않습니다.
- 값으로 지우려면 `infind` 로 자리를 찾아 넘기세요: `erase(infind(0, 점수), 점수)`.
- 지운 **값**이 필요하면 지우기 전에 `xs[i]` 로 먼저 꺼내 두세요 (`erase` 는 개수를 줍니다).
- 문자열·튜플은 못 고치는 값이라 받지 않습니다. 슬라이싱으로 새로 만드세요: `글[0:2] + 글[3:]`.
- 딕셔너리는 자리 번호가 없어서 받지 않습니다. 키로 지우는 `del` 을 쓰세요 → [딕셔너리 고치기](#딕셔너리-고치기-del)

### 딕셔너리 고치기 (`del`)
리스트를 `erase` 로 **자리 번호**로 지운다면, 딕셔너리는 `del` 로 **키**로 지웁니다.
`keys`·`values`·`has` 와 똑같이 **딕셔너리를 먼저** 적습니다 (딕셔너리에는 앞에 적을 "어디를"이 없기 때문이에요).

| 적는 법 | 뜻 | 예 (`d = {"a": 1, "b": 2, "c": 3}`) |
|---|---|---|
| `del(딕셔너리)` | **전부** 지우기(비우기) | `del(d)` → `{}` |
| `del(딕셔너리, 키)` | 그 키 하나 | `del(d, "a")` → `{"b": 2, "c": 3}` |
| `del(딕셔너리, 키리스트)` | 적어 준 키들 | `del(d, ["a", "c"])` → `{"b": 2}` |

```
val 나이 = {"김": 20, "박": 21, "이": 22}
print(del(나이, "박"))     // 1     돌려주는 값은 지운 개수
print(나이)                // {"김": 20, "이": 22}
print(del(나이))           // 2     전부 지우기
print(나이)                // {}
```
> **원래 딕셔너리를 바로 고칩니다.** `push`·`pop`·`insert`·`erase` 와 같은 가족이라, `d = del(d, "a")` 라고 쓰면 안 됩니다(개수가 들어갑니다).

- **없는 키**를 적으면 `d["없는키"]` 를 읽을 때처럼 오류입니다. 있을 때만 지우려면 `has` 로 먼저 물어보세요: `if has(d, k): del(d, k)`.
- 지운 **값**이 필요하면 지우기 전에 `d[키]` 로 먼저 꺼내 두세요 (`del` 은 개수를 줍니다).
- 값으로 지우려면 `for` 로 키를 모아 한 번에 넘기세요 — `del(d, 빈것)`.
- 같은 키를 두 번 적어도 **한 번만** 셉니다 (`del(d, ["a", "a"])` → `1`).

### 글자와 바이트 (`encode` · `decode`)
컴퓨터는 글자를 그대로 담지 못하고 **0~255 사이의 숫자**만 담습니다.
어떤 글자를 어떤 숫자로 적을지 정해 둔 약속이 **인코딩**이고, 그렇게 바뀐 숫자들의 묶음이 **바이트 객체**입니다.
여기서도 **어떻게 할지가 앞, 대상이 마지막**이며, 앞을 생략하면 `"utf-8"` 입니다.

| 함수 | 하는 일 | 예시 |
|------|---------|------|
| `encode(문자열)` | 글자를 **바이트로** (기본 utf-8) | `encode("hi")` → `bytes[68 69]` |
| `encode(인코딩, 문자열)` | 약속을 정해서 바이트로 | `encode("cp949", "가")` → `bytes[b0 a1]` |
| `decode(바이트)` | 바이트를 **글자로** (기본 utf-8) | `decode(encode("hi"))` → `"hi"` |
| `decode(인코딩, 바이트)` | 그 약속으로 읽어서 글자로 | `decode("cp949", b)` → `"가"` |

```
val b = encode("hi")
print(b)                        // bytes[68 69]   16진수 두 글자가 한 바이트
print(type(b), len(b))          // bytes 2
print(decode(b))                // hi

print(len("한글"))               // 2   글자 수
print(len(encode("한글")))       // 6   utf-8 에서는 한 글자가 3바이트
print(encode("cp949", "가"))     // bytes[b0 a1]   약속이 다르면 숫자도 다름

print(b[0])                     // 104            자리 하나 -> 숫자
print(b[0:1])                   // bytes[68]      자르면 -> 바이트 객체
print(list(b))                  // [104, 105]
print(decode(bytes([72, 73])))  // HI

bytes box = encode("ok")        // 바이트만 담는 변수
```
- **담을 때 쓴 인코딩으로 읽어야** 원래 글자가 돌아옵니다. 다르면 오류가 나거나 글자가 깨집니다.
- 쓸 수 있는 인코딩: `"utf-8"`(기본) `"utf-16"` `"ascii"` `"cp949"` `"euc-kr"` `"latin-1"` (대소문자 무관)
- 바이트도 **시퀀스 가족**이라 `len`·인덱싱·슬라이싱·`for-in`·`in`·`incount`/`infind`·`+`·`*` 가 됩니다. 꺼낸 값 하나는 **글자가 아니라 숫자**예요.
- 바이트는 **읽기 전용**이고, `+` 는 **바이트끼리만** 됩니다. `str(b)` 는 보이는 모양일 뿐 되돌리기는 **`decode`** 입니다.
- 글자를 바이트로 만들 때는 인코딩을 정해야 하므로 `bytes("hi")` 는 일부러 오류입니다 → `encode("hi")`.

### 진수 (2진수 · 10진수 · 16진수)
진법은 수를 **적는 방법**일 뿐, 값은 하나입니다. `0b1011`, `11`, `0xb` 는 **모두 같은 수**예요.

| 진법 | 적는 법 | 표기로 바꾸기 | 되돌리기 |
|------|--------|--------------|---------|
| 2진수 | `0b1011` | `bin(11)` → `"0b1011"` | `num("0b1011")` → `11` |
| 10진수 | `11` | (그냥 숫자) | `num("11")` → `11` |
| 16진수 | `0xff` | `hex(255)` → `"0xff"` | `num("0xff")` → `255` |

```
print(0b1011)               // 11     0b 로 시작하면 2진수
print(0xff)                 // 255    0x 로 시작하면 16진수 (0xFF 도 OK)
print(0b1011 == 11)         // true   같은 값, 다른 표기

print(bin(11))              // 0b1011
print(hex(255))             // 0xff
print(num("0b1011"))        // 11     어떤 표기든 10진수 숫자로
print(int("0xff"))          // 255    int() / float() 도 읽습니다

val n = 200
print(num(bin(n)) == n)     // true   적었다가 되돌리면 그대로

print(0b1010 + 0b0101)      // 15     계산은 평소와 똑같이
print(bin(0b1100 & 0b1010)) // 0b1000  비트연산 확인용으로 딱
```
- **`bin()`·`hex()` 의 결과는 문자열**(적어 놓은 글자)이고, **`num()` 의 결과는 숫자**(계산할 수 있는 값)입니다.
- 정수만 이렇게 적고 바꿀 수 있습니다. `0b1010.5`, `bin(3.5)` 는 오류입니다.
- 진법에 없는 글자를 쓰면 알려줍니다: `0b1012`, `0xzz` → 오류.

### 글자로 적은 식 실행 (`eval`)
`eval(문자열)` 은 글자로 적어 둔 **식**을 그 자리에서 계산해서 값으로 돌려줍니다.

```
print(eval("1 + 2 * 3"))        // 7

val x = 10
print(eval("x ** 2"))           // 100     지금 있는 변수를 그대로 씁니다

func twice(n):
    return n * 2
print(eval("twice(21)"))        // 42      내가 만든 함수도 부릅니다

val 코드 = "x" + " + " + "5"
print(eval(코드))               // 15      식을 만들어서 실행

val 식 = input("계산할 식: ")    // 예: 3 * (4 + 5)
print(eval(식))                 // 27      간단한 계산기
```
- **부른 자리의 변수·함수를 그대로** 봅니다. 함수 안에서 부르면 그 함수의 지역 변수도 보입니다.
- **값이 되는 식 하나**만 됩니다. `eval("x = 1")` 같은 문장이나 빈 글자(`eval("")`)는 오류입니다.
- 안에서 난 오류의 줄 번호는 **그 문자열 안에서 센 번호**(보통 `1`)입니다.
- 남이 적어 준 글자를 그대로 `eval` 하면 **그 사람의 코드가 그대로 돌아갑니다**. 알고 쓰세요.

### 텍스트 파일 (`read` · `write` · `append` · `isfile` · `isdir`)
프로그램이 끝나도 남는 곳이 **파일**입니다. 여기서도 **'어떻게'(인코딩)가 앞, 다룰 파일이 마지막**입니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `read([인코딩,] 파일)` | 파일을 통째로 읽기 | 그 안의 **글자** |
| `write([인코딩,] 내용, 파일)` | 새로 쓰기 (있던 내용은 지워짐) | 쓴 **글자 수** |
| `append([인코딩,] 내용, 파일)` | 파일 **뒤에 이어** 쓰기 | 쓴 **글자 수** |
| `isfile(파일)` | 그런 파일이 있는지 | `true` / `false` |
| `isdir(폴더)` | 그런 폴더가 있는지 | `true` / `false` |
| `newfile(파일)` | **빈 파일 만들기** (있으면 그대로 둠) | 새로 만들었으면 `true` |
| `newdir(폴더)` | **빈 폴더 만들기** (있으면 그대로 둠) | 새로 만들었으면 `true` |
| `deldir(폴더)` | **폴더 지우기** (없으면 오류, 속 내용도 지움) | 지웠으면 `true` |
| `delfile(파일)` | **파일 지우기** (없으면 오류) | 지웠으면 `true` |
| `renamefile(새 이름, 파일)` | **이름 바꾸기** (없으면 오류) | 바꿨으면 `true` |
| `movefile(폴더, 파일)` | **다른 폴더로 옮기기** (이름은 그대로) | 옮겼으면 `true` |

```
write("첫 줄\n둘째 줄\n", 'note.txt')   // 9   <- 쓴 글자 수
append("셋째 줄\n", 'note.txt')
print(read('note.txt'))                 // 세 줄이 그대로

newfile('기록.txt')                     // 없으면 만들고, 있으면 그대로 둡니다
append("한 줄 더\n", '기록.txt')         // 그 뒤에 쌓기
delfile('기록.txt')                     // 다 썼으면 지우기

val 줄들 = split("\n", read('note.txt'))  // 읽은 것은 그냥 문자열
write(join("\n", 이름들), '이름표.txt')    // 리스트를 줄 단위로 저장

write("cp949", "한글 파일\n", 'note.txt') // 인코딩은 encode/decode 와 같은 규칙
print(read("cp949", 'note.txt'))
```
- 파일 이름 자리에는 **경로도** 적을 수 있어서 다른 폴더의 파일도 그대로 다룹니다 — `'자료/오늘.txt'`(아래 폴더), `'../옆폴더/오늘.txt'`(위로 갔다 옆으로), `'C:/일기/오늘.txt'`(통째 경로). 이름만 또는 상대 경로는 **실행 중인 `.lumi` 파일이 있는 폴더** 기준입니다 (`bring` 과 같아요).
- **주소는 작은따옴표로 적습니다** — `read('C:\notes\a.txt')`. 붙여넣은 그대로 쓰이니 손댈 것이 없습니다. 폴더 구분은 `/` `\` 둘 다 됩니다. 큰따옴표에서는 `\n`·`\t`·`\u` 처럼 특수문자로 시작하는 폴더 이름(`"C:\notes"`, `"C:\users"`)이 특수문자에 지는데, 그 경우엔 조용히 넘어가지 않고 오류로 알려 줍니다 (`/` 나 `\\` 로 고쳐도 됩니다).
- 파일이 없으면 `write`·`append` 는 **새로 만듭니다.** 없는 파일을 `read` 하면 오류이니 `isfile` 로 먼저 물어보세요.
- 인코딩을 안 적으면 `"utf-8"` 입니다. **담을 때 쓴 인코딩으로 읽어야** 글자가 안 깨집니다.
- 이름을 주면 **한 번에 읽고 한 번에 쓰고 바로 닫습니다.** 줄 단위 작업은 `split("\n", read(이름))`.
- 윈도우 줄바꿈(`\r\n`)은 **읽을 때 `\n` 하나로** 맞춰 줍니다. 쓸 때는 적어 준 그대로.
- `newfile` 은 **있는 파일을 건드리지 않습니다** (내용을 날리는 `write("", 이름)` 과 다릅니다). 새로 만들었으면 `true`, 원래 있었으면 `false`.
- `delfile` 은 **없는 파일이면 오류**입니다. 모를 때는 `if isfile(이름): delfile(이름)`. 지운 파일은 **휴지통에 가지 않습니다.**
- `renamefile` 은 **새 이름이 앞, 바꿀 파일이 뒤**입니다. 없는 파일이면 오류이고, **새 이름의 파일이 이미 있어도 오류**입니다 (덮어쓰지 않습니다).
- `movefile` 은 **폴더가 앞, 옮길 파일이 뒤**입니다 — `movefile("보관", '일기.txt')`. 이름은 그대로 두고 자리만 옮깁니다. 없는 파일·없는 폴더·같은 이름이 이미 있으면 오류이고, 다른 드라이브(`C:`→`D:`)로도 옮겨집니다.
- 폴더는 만들지도 지우지도 못합니다. **파일만** 다룹니다.

### 파일 열어 두고 쓰기 (`open` · `close` · `readline` · `isend`)
큰 파일을 **한 줄씩** 읽거나 여러 번에 나눠 쓸 때는 파일을 열어 둡니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `open([인코딩,] [어떻게,] 파일)` | 파일 열기 — 어떻게: `read`(기본)/`write`/`append`, **따옴표 없이** | 열어 둔 파일 |
| `readline(파일)` | 다음 **한 줄** 읽기 (줄바꿈은 떼고) | 그 줄의 글자 |
| `writeline([인코딩,] 내용, 파일)` | **한 줄** 쓰기 (줄바꿈을 붙여서) | 쓴 글자 수 |
| `isend(파일)` | 더 읽을 게 없으면 `true` | `true` / `false` |
| `close(파일)` | 닫기 (두 번 닫아도 오류 아님) | 닫았으면 `true` |

```
val f = open(write, '일기.txt')
writeline("첫 줄", f)           // 줄바꿈은 writeline 이 붙여 줍니다
writeline("둘째 줄", f)
close(f)

val g = open('일기.txt')        // 안 적으면 읽기로 열립니다
while not isend(g):
    print(readline(g))          // 줄바꿈이 빠진 '그 줄의 내용'
close(g)

val 안 = open('일기.txt')        // 한 줄 읽어 한 줄 쓰기 (둘은 정확히 반대)
val 밖 = open("cp949", write, '복사.txt')
while not isend(안):
    writeline(readline(안), 밖)
close(안)
close(밖)
```
- 열어 둔 파일도 **같은 이름**을 씁니다: `read(f)` = 남은 것 전부, `write(내용, f)` = 그 자리에 이어 쓰기(줄바꿈 안 붙임). 마지막 자리가 이름이면 통째로, 열어 둔 파일이면 그 자리에서입니다.
- **읽기와 쓰기는 섞이지 않습니다.** 읽으려고 연 파일에 쓰면 오류이니 닫고 다시 여세요.
- 빈 줄도 `""` 라서 **끝인지는 `isend` 로** 물어봅니다. `readline` 은 열어 둔 파일에만 — 이름으로 줄들을 얻으려면 `split("\n", read(이름))`.
- 닫는 것을 잊어도 프로그램이 끝날 때 저절로 닫힙니다. 그래도 바로 닫는 습관을 — 또는 아래 `use` 를 쓰세요.

### 열고 저절로 닫기 (`use`)
`use` 는 파일을 열어 두는 **제어문**입니다. 블록이 끝나면 **저절로 `close`** 합니다.

```
use f = open(write, '일기.txt'):
    writeline("첫 줄", f)
    writeline("둘째 줄", f)
                                // 여기서 이미 닫혔습니다 (close 를 안 적습니다)

use f = open('일기.txt'):        // 안 적으면 읽기로 열립니다
    while not isend(f):
        print(readline(f))

for 이름 in 이름들:               // break 로 나가도, return 으로 나가도 닫습니다
    use f = open(이름):
        if isend(f):
            break
        print(readline(f))
```
- 모양은 `use 이름 = 식:` 입니다. 식은 **`open(...)` 이 준 열린 파일**이어야 합니다.
- 그 이름은 **블록 안에서만** 삽니다. 밖에서 쓰면 "not defined" 오류이니, 닫힌 파일을 잘못 쓸 일이 없습니다.
- 블록을 **어떻게 나가든** 닫습니다: 끝까지 갔을 때, `break`·`continue`·`return` 으로 나갈 때 모두.
- 다만 **오류가 나면** 그 자리에서 프로그램이 멈추므로 `use` 를 거치지 않습니다. 그때는 프로그램이 끝나면서 닫힙니다.
- 열어 둔 파일이 여럿이면 `use` 를 겹쳐 쓰세요 (안쪽이 먼저 닫힙니다).

### 파일 안에서 자리 옮기기 (`move` · `where` · `len`)
열어 둔 파일은 "지금 어디까지 읽었는지"를 기억합니다. 그 자리를 **마음대로 옮길** 수 있습니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `move(자리, 파일)` | 그 자리로 옮기기 | 옮긴 **자리** |
| `where(파일)` | 지금 자리 묻기 | 지금 **자리** |
| `len(파일)` | 파일 전체 크기 | **바이트 수** |

```
use f = open('note.txt'):
    val 첫줄 = readline(f)
    val 표시 = where(f)         // 여기를 기억해 둡니다
    print(readline(f))
    move(표시, f)               // 되돌아가서
    print(readline(f))          // 같은 줄을 다시 읽습니다

    move(0, f)                  // 맨 앞으로
    move(-10, f)                // 뒤에서 10바이트 앞으로 (리스트처럼 음수)
    move(len(f), f)             // 맨 끝으로 (isend(f) 가 true 가 됩니다)

use f = open(write, 'note.txt'):   // 쓰는 자리도 옮깁니다 (덮어씁니다)
    write("hello world", f)
    move(0, f)
    write("HELLO", f)              // -> "HELLO world"
```
- 자리는 **글자 수가 아니라 바이트 수**입니다. 한글 한 글자는 utf-8 에서 3바이트예요. 그래서 **`where` 로 기억했다가 `move` 로 되돌아가는** 쓰임이 가장 안전합니다.
- 음수는 리스트처럼 **끝에서부터** 셉니다 (`-1` = 마지막 바이트). `0` 부터 `len(f)` 까지 쓸 수 있고, `len(f)` 는 맨 끝입니다. 그 밖은 오류.
- `open(append, 이름)` 으로 연 파일은 **늘 끝에 쓰므로** `move` 가 오류입니다. 자리를 고르려면 `open(write, 이름)` 으로 여세요.
- `len(f)` 는 열어 둔 파일에만 씁니다. 파일 **이름**의 크기는 `len(read(이름))` (이건 글자 수).

### 객체지향 (클래스)
값(필드)과 동작(메서드)을 하나로 묶은 **설계도**가 클래스이고, 그 설계도로 만든 것이 **객체**입니다.

```
class Animal:
    func init(name):        // init = 생성자, 객체를 만들 때 자동 실행
        this.name = name    // this = 지금 이 객체

    func speak():
        print(this.name + " makes a sound")

class Dog from Animal:      // from = 상속 (Animal 을 물려받음)
    func init(name, age):
        super.init(name)    // super = 부모 것 부르기
        this.age = age

    func speak():           // 같은 이름으로 다시 정의 = 재정의(덮어쓰기)
        print(this.name + " barks")

    func text():            // print(객체) 할 때 어떻게 보일지
        return "Dog(" + this.name + ")"

val d = Dog("보리", 3)
d.speak()                   // 보리 barks
print(d.name, d.age)        // 보리 3
print(d)                    // Dog(보리)
print(type(d))              // Dog
print(isa(d, Animal))       // true

// 다형성 — 같은 이름을 불러도 종류에 따라 다르게 움직입니다
for a in [Animal("나비"), Dog("보리", 3)]:
    a.speak()
```

- `class 이름:` 로 클래스를, `class 자식 from 부모:` 로 상속을 만듭니다.
- 클래스 블록 안에는 **메서드(`func`)와 공유 값(`val`)** 만 씁니다.
- `이름(값들)` 로 객체를 만들고(`new` 같은 키워드 없음), `init` 메서드가 자동 실행됩니다.
- `this` 는 자기 자신, `super` 는 부모입니다. 둘 다 **메서드 안에서만** 쓸 수 있어요.
- 필드는 `this.이름 = 값` 으로 만들고 `객체.이름` 으로 읽습니다. (`객체.이름 += 1`, `객체.이름++` 도 됩니다)
- 이름을 **`_` 로 시작하면 비공개**라서 그 클래스 메서드 안에서만 쓸 수 있습니다 (캡슐화).
- 특별한 메서드 (연산자 재정의): `text()`는 출력 모양, `equals(o)`는 `==`, `add(o)`/`sub(o)`/`mul(o)`/`div(o)`/`mod(o)`는 사칙연산(`+ - * / %`), `lt(o)`/`gt(o)`는 크기 비교(`< >`), `get(i)`/`set(i, val)`은 색인 연산(`obj[i]`, `obj[i] = val`)의 동작을 정의합니다.
- 추상 메서드 (`abstract func`): `abstract func area()` 처럼 틀만 만들고, 자식 클래스에서 반드시 구현하도록 강제합니다. (미구현 시 객체 생성 불가)
- `type(객체)` 는 클래스 이름을, `isa(객체, 클래스)` 는 그 종류가 맞는지 알려줍니다.
- 클래스도 `bring` 으로 라이브러리에서 가져올 수 있어요.

자세한 내용은 `Lumi 사용설명서.md` 15장을 보세요.

### 주석
```
// 이 줄은 무시됩니다

/. 이렇게 열고 닫는 주석도 있어요.
   여러 줄을 한 번에 묶습니다. ./
print(1 /. 줄 가운데도 됩니다 ./ + 2)
```

### 라이브러리 (bring)
다른 `.lumi` 파일의 함수를 가져와 씁니다. 가져오는 방식에 따라 **부르는 법이 달라집니다.**

```
bring math              // 네임스페이스로 가져오기
print(math.sqrt(16))    // 4    -> "라이브러리명.함수()" 로 부릅니다

bring math up pow       // 함수만 콕 집어 가져오기
print(pow(2, 10))       // 1024 -> 함수 이름만으로 바로 부릅니다
```
- **`bring 라이브러리`** : 라이브러리를 통째로 가져오되, 반드시 **`라이브러리명.함수()`** 형태로 씁니다 (`math.sqrt(16)`). 이름이 겹칠 걱정이 없어요.
- **`bring 라이브러리 up 함수1, 함수2`** : 나열한 함수만 가져와 **이름만으로 바로** 씁니다 (`pow(2, 10)`).
- 맨이름으로 적었을 때 찾는 위치 (순서대로):
  1. **실행 중인 파일과 같은 폴더** 의 `이름.lumi`
  2. `lumi_packages/` 와 `~/.lumi/packages/` (패키지 매니저가 설치한 것)
  3. `lumi.exe` 옆의 **`libraries/`** 폴더의 `이름.lumi`
- `.lumi` 파일만 가져올 수 있어요.
- 기본 제공 라이브러리: `math` (`sqrt`, `pow`, `abs`, `max`, `min`) · `csv` (아래 참고)
- 나만의 라이브러리를 만들려면 `libraries/` 폴더(또는 같은 폴더)에 `.lumi` 파일을 만들고 그 안에 `func` 함수와 `class` 클래스를 정의하면 돼요.

#### 폴더로 나눈 프로그램 — 경로로 가져오기
파일이 늘어나면 폴더로 나누게 됩니다. 그럴 때는 **경로를 따옴표 안에** 적습니다. 파일 함수(`read`·`write`)에 경로를 적는 방법과 똑같습니다.

```
project/
  main.lumi
  models/user.lumi
  services/auth.lumi
```
```
bring "models/user"           // 하위 폴더  -> user.만들기(...)
bring "../shared/util"        // 상위 폴더
bring "models/user.lumi"      // .lumi 는 붙여도 되고 빼도 됩니다
bring u = "models/user"       // 담을 이름 고르기 -> u.만들기(...)
bring "models/user" up 보기    // 골라 오기 -> 보기(...)
```
- **기준은 그 `bring` 을 적은 파일이 있는 폴더**입니다. `services/auth.lumi` 안에서는 `bring "../models/user"` 가 됩니다. 어느 폴더에서 `lumi` 를 실행하든 결과가 같습니다.
- 이름을 안 고르면 **파일 이름**으로 담깁니다 (`"models/user"` → `user`).
- `C:\...` 나 `/...` 로 시작하면 그 자리를 그대로 씁니다.
- **라이브러리 안에서도 `bring` 을 쓸 수 있습니다.** 위 예의 `services/auth.lumi` 가 자기 안에서 `models/user` 를 가져다 씁니다.
- 경로에 `\` 를 쓸 때는 `\\` 로 적거나 `'...'` 작은따옴표를 쓰세요 (`\` 는 이스케이프 문자입니다).
> 전체 예시는 `examples/project/` 폴더를 통째로 보세요.

### 웹으로 주고받기 (`bring http`)

```
bring http

val 답 = http.get("https://api.example.com/users")
if 답["ok"]:
    val 자료 = json.parse(답["본문"])
```

| 함수 | 하는 일 |
|------|---------|
| `http.get(주소[, 머리말])` | 가져오기 |
| `http.post(본문, 주소[, 머리말])` | 보내기 (`write(내용, 파일)` 과 같은 차례) |
| `http.put` · `http.delete` | 같은 모양 |
| `http.getjson` · `http.postjson` | JSON 머리말을 알아서 붙이고 답도 `["자료"]` 로 풀어 줌 |
| `http.download(주소, 파일이름)` | 파일로 곧장 받기 (큰 파일도 OK) |
| `http.serve(포트, 처리함수[, 몇번])` | 작은 웹 서버 열기 |

**답의 모양**: `{ "ok": true, "코드": 200, "본문": "...", "머리말": {...} }`

**규칙 두 가지 — 이것만 기억하면 됩니다**
- **`404`·`500` 은 오류가 아닙니다.** 서버가 제대로 답한 것이라 `["코드"]` 로 옵니다. `ok` 는 200~299 일 때만 `true`.
- **주소를 못 찾거나 연결이 끊기면 `NetworkError` 를 냅니다.** `catch NetworkError e:` 로 잡으세요.

#### 작은 웹 서버
```
func 처리(요청):
    // 요청 = {"방법":"GET", "길":"/인사", "물음":{"이름":"루미"}, "머리말":{...}, "본문":"..."}
    if 요청["길"] == "/인사":
        return {"코드": 200, "본문": "안녕 " + 요청["물음"]["이름"]}
    return {"코드": 404, "본문": "없는 길"}

http.serve(8080, 처리)        // 멈출 때까지 (Ctrl+C)
http.serve(8080, 처리, 1)     // 딱 한 번만 받기 (시험할 때)
```
- **길은 `%EC%95%88` 같은 적기를 풀어서** 줍니다 — 한글 주소가 그대로 옵니다.
- **물음표 뒤는 이름=값 짝을 갈라 딕셔너리로** 줍니다.
- **처리 함수가 터져도 서버는 죽지 않습니다.** 500 을 보내고 다음 손님을 받습니다.
- 한 번에 손님 하나씩입니다 — 사내 도구·웹훅 받기·만들어 보기용.
- 서버 쪽은 `https` 를 하지 않습니다 (앞에 nginx 같은 것을 두세요).

#### 어떻게 만들어졌나 (알아 두면 좋은 것)
- **클라이언트는 `curl` 을 불러서 씁니다.** 윈도우 10·맥·대부분 리눅스에 기본으로 있습니다. 없으면 `NetworkError` 로 분명히 알려 줍니다.
  - 이렇게 한 까닭: `https` 를 직접 하려면 TLS 와 **루트 인증서 저장소**가 필요한데, 그건 운영체제마다 제각각이라 조금만 틀려도 *동작은 하는데 안전하지 않은* 상태가 됩니다. 바깥 라이브러리를 끌어오면 "그냥 컴파일되는 몇 개 파일"이라는 성질도 잃습니다.
  - 주소는 셸에 넘기지 않고 **curl 설정 파일**로 건넵니다. 주소에 이상한 글자가 있어도 셸이 명령으로 잘못 읽지 않습니다.
  - 요청마다 프로세스를 하나 띄우므로 20~50ms 가 듭니다. 초당 수백 건을 보내는 일에는 맞지 않습니다.
- **서버는 운영체제 소켓만** 써서 인터프리터 안에 직접 들어 있습니다 (의존성 0).
- `libraries/http.lumi` 는 전부 Lumi 로 적혀 있으니 열어 보고 고쳐 쓰셔도 됩니다.
> 전체 예시는 `examples/web.lumi` 를 참고하세요.

### 표 데이터 (`bring csv`)
엑셀에서 "CSV로 내보내기" 한 파일을 읽고 고쳐서 다시 저장합니다.

```
bring csv

// 한국 엑셀이 만든 CSV 는 거의 "cp949", 웹/프로그램이 만든 것은 "utf-8"
val 표 = csv.load("cp949", "매출.csv")

for 줄 in csv.head(표):          // 첫 줄을 이름표로 삼습니다
    print(줄["이름"], num(줄["금액"]))

csv.save("cp949", 표, "사본.csv")
```

| 함수 | 하는 일 |
|------|---------|
| `csv.parse(글)` / `csv.build(행들)` | CSV 텍스트 ↔ 행 리스트 |
| `csv.line(값들)` | 값 리스트 하나를 CSV 한 줄로 |
| `csv.load(인코딩, 파일)` / `csv.save(인코딩, 행들, 파일)` | 파일로 읽고 쓰기 |
| `csv.head(행들)` / `csv.unhead(이름표, 딕셔너리들)` | 첫 줄을 이름표로 ↔ 되돌리기 |

- **인코딩은 반드시 적습니다** — CSV 사고의 대부분이 여기서 납니다.
- 칸 안의 쉼표·큰따옴표·줄바꿈은 CSV 규격대로 감싸고 풉니다. 읽어 낸 값은 **모두 문자열**이라 숫자는 `num()` 으로 바꿔 쓰세요.
- 예제: `examples/csv_demo.lumi`

### 시간 다루기 (`time` · `now` · `date` · `sleep` 외)
이 가족의 기본 단위는 **타임스탬프**입니다 — 1970년 1월 1일부터 몇 초가 흘렀는지를 담은 실수 하나. 사람이 읽는 글자로 바꿀 때는 `now`/`timeformat` 을, 반대로 사람이 적은 글자를 되돌릴 때는 `timeparse` 를 씁니다.

| 함수 | 하는 일 |
|------|---------|
| `time()` | 지금의 타임스탬프 (초, 실수) |
| `now([서식])` | 지금을 사람이 읽는 글자로 (기본 `"%Y-%m-%d %H:%M:%S"`) |
| `date([타임스탬프])` | 연·월·일·시·분·초를 딕셔너리로 (`year`/`month`/`day`/`hour`/`minute`/`second`/`weekday`/`yearday`/`timestamp`/`formatted`) |
| `timeformat(서식, [타임스탬프])` | 타임스탬프를 글자로 |
| `timeparse(날짜글자, 서식)` | 글자를 타임스탬프로 (`timeformat` 의 반대) |
| `timezone()` | 이 컴퓨터의 시간대 (`{"offset": 초, "name": ...}`) |
| `dateadd(타임스탬프, 얼마, 단위)` | 날짜 더하기·빼기 (단위: `seconds`/`minutes`/`hours`/`days`/`weeks`/`months`/`years`) |
| `datediff(시작, 끝, 단위)` | 두 시각의 차이 (단위 위와 같음, `months` 제외) |
| `isleap(연도)` | 윤년인가? |
| `sleep(초)` | 잠깐 멈추기 |
| `clock()` | '뒤로 가지 않는 시계' — 두 번 재서 빼면 걸린 시간 |
| `timed(함수, 인자...)` | 함수를 부르고 `[결과, 걸린 시간]` 으로 돌려줌 |
| `uptime()` / `runtime()` | 프로그램이 시작된 뒤 흐른 시간 (같은 함수) |

```
val t = time()
print(t)                              // 1754000000.123 같은 실수
print(now())                          // "2026-08-02 00:48:36"
print(now("%Y년 %m월 %d일"))            // "2026년 08월 02일"

val d = date()
print(d["year"], "년", d["month"], "월", d["day"], "일")   // 2026 년 8 월 2 일
print(d["formatted"])                 // "2026-08-02 00:48:36"

val 세일뒤 = dateadd(t, 3, "days")       // 3일 뒤 (음수를 주면 빼기)
print(datediff(t, 세일뒤, "days"))       // 3
print(isleap(2028))                   // true

func 느린일(x):                          // timed() 로 걸린 시간 재기
    sleep(0.05)
    return x * 2
val 측정 = timed(느린일, 21)
print(측정[0], 측정[1])                  // 42  0.0501...
```
> 전체 예시는 `examples/time.lumi` 를 참고하세요.

### 알짜 유틸 (`rand` · `filter` · `sum` · `clamp` · `env`)
자주 쓰지만 직접 짜기는 번거로운 것들만 모았습니다.

| 함수 | 하는 일 | 돌려주는 값 |
|------|---------|------|
| `rand()` | 0.0 ~ 1.0 사이 실수 | 실수 |
| `rand(n)` | 1 ~ n 사이 정수 (주사위) | 정수 |
| `rand(a, b)` | a ~ b 사이 정수 | 정수 |
| `rand(시퀀스)` | 그중 하나 뽑기 (제비뽑기) | 원소 하나 |
| `filter(함수, 시퀀스)` | 조건에 맞는 것만 남기기 | 담은 모양 그대로 (리스트→리스트, 튜플→튜플, 문자열→문자열) |
| `sum(시퀀스)` | 숫자를 모두 더하기 | 정수만 있으면 정수, 실수 섞이면 실수 |
| `clamp(값, 최소, 최대)` | 값을 범위 안으로 붙들어 두기 | 넘치면 최대, 모자라면 최소 |
| `env(이름)` | 운영체제 환경 변수 읽기 (`getenv` 도 같은 함수) | 없는 이름이면 `none` |
| `args()` | 터미널에서 넘어온 인자 전부 | 문자열 리스트 |
| `args(자리)` | 그 자리 하나만 (음수는 뒤에서부터) | 문자열 |

```
print(rand(6))                                 // 1~6 사이 정수
print(rand(["사과", "바나나", "포도"]))           // 그중 하나

func 짝수인가(x):
    return x % 2 == 0
print(filter(짝수인가, [1, 2, 3, 4, 5, 6]))     // [2, 4, 6]
print(filter(func c: c in "aeiou", "sequence")) // "euee" (문자열은 문자열로)

print(sum([10, 20, 30, 40]))                   // 100
print(clamp(120, 0, 100), clamp(-5, 0, 100))   // 100  0
print(env("USERNAME"))                         // 환경 변수 (없으면 none)
```
> 전체 예시는 `examples/util.lumi` 를 참고하세요.

#### 터미널에서 넘어온 인자 (`args`)
`lumi 프로그램.lumi --port 8080` 처럼 뒤에 붙여 준 것들을 읽습니다. **`args(0)` 은 실행한 파일 이름**입니다(파이썬 `sys.argv` 와 같은 규칙). 이것만 있으면 CLI 도구를 만들 수 있습니다.

```
// lumi app.lumi --port 3000 --debug
print(args())        // ["app.lumi", "--port", "3000", "--debug"]
print(len(args()))   // 4
print(args(0))       // app.lumi
print(args(-1))      // --debug

// --이름 값 꼴 읽기
func 옵션(이름, 없을때):
    val 목록 = args()
    for val i = 0, i < len(목록) - 1, i++:
        if 목록[i] == 이름:
            return 목록[i + 1]
    return 없을때
print(옵션("--port", "8080"))     // 3000

// 그냥 있는지만 보는 깃발
print(incount("--debug", args()) > 0)   // true
```
- 없는 자리를 대면 오류입니다. 꺼내기 전에 `len(args())` 로 세어 보세요.
- 인자 없이 그냥 돌려도 파일 이름 하나는 늘 들어 있습니다.
> 전체 예시는 `examples/args.lumi` 를 참고하세요.

### 폴더 훑기와 경로 (`listdir` · `filename` · `foldername` · `pathjoin` · `fullpath`)
폴더 안에 뭐가 있는지 보고, 경로를 조각내거나 이어 붙입니다.

| 함수 | 하는 일 |
|------|---------|
| `listdir(폴더)` | 폴더 안의 이름들 (가나다순) |
| `listdir(file, 폴더)` | 그중 **파일만** (따옴표 없이 `file`) |
| `listdir(dir, 폴더)` | 그중 **폴더만** (따옴표 없이 `dir`) |
| `filename(경로)` | 마지막 조각 — `"자료/개.png"` → `"개.png"` |
| `foldername(경로)` | 앞의 폴더 — `"자료/개.png"` → `"자료"` |
| `pathjoin(조각들)` | 이 운영체제의 구분자로 잇기 (리스트로 줘도 됩니다) |
| `fullpath(경로)` | 절대 경로로 펴기 |

```
for 이름 in listdir(file, "자료"):
    if hasmatch("\.txt$", 이름):
        print(이름, read(pathjoin("자료", 이름)))
```
- `listdir` 은 **바로 아래**만 봅니다 (하위 폴더 속까지 들어가지 않습니다).
- 없는 폴더를 주면 `FileNotFound` 오류입니다. 미리 `isdir(폴더)` 로 확인하세요.

### 지문과 옮겨 적기 (`sha256` · `md5` · `base64` · `unbase64`)
파일이 그대로인지 보기, 캐시 이름표 만들기, 웹으로 주고받기에 씁니다.

| 함수 | 하는 일 |
|------|---------|
| `sha256(글자나 바이트)` | 64 글자 16진수 지문 |
| `md5(글자나 바이트)` | 32 글자 16진수 지문 |
| `base64(글자나 바이트)` | base64 글자로 |
| `unbase64(글자)` | 다시 **바이트**로 |

```
print(sha256("hello"))                       // 2cf24dba5fb0a30e...
print(base64("Lumi 안녕"))                    // THVtaSDslYjrhZU=
print(decode(unbase64(base64("Lumi"))))      // Lumi

val 전 = sha256(read("보고서.txt"))
// ... 뭔가 한 뒤 ...
print("바뀌었나:", 전 != sha256(read("보고서.txt")))
```
- 지문(`sha256`·`md5`)은 **되돌릴 수 없습니다.** 같은 내용이면 같은 지문이 나오는지 견주는 용도입니다.
- `base64` 는 **되돌릴 수 있는** 옮겨 적기입니다. 숨기는 것이 아니니 비밀에 쓰면 안 됩니다.
- **`md5` 는 안전이 걸린 곳에 쓰지 마세요.** 일부러 같은 지문을 만들어 낼 수 있습니다. 옛 자료와 맞춰 볼 때만 쓰고, 그 밖에는 `sha256` 을 쓰세요.

### 무늬 찾기 — 정규표현식 (`ismatch` · `hasmatch` · `findmatch` …)
글자 속에서 **모양으로** 찾습니다. 로그 파싱, 입력 검사, 골라 바꾸기에 씁니다.
집 규칙대로 **무늬가 앞, 글자가 뒤**입니다 (`replace` 와 같은 차례).

| 함수 | 하는 일 |
|------|---------|
| `ismatch(무늬, 글자)` | 글자 **전체**가 무늬와 같은가 |
| `hasmatch(무늬, 글자)` | **어딘가에** 무늬가 있는가 |
| `findmatch(무늬, 글자)` | 처음 맞은 조각 (없으면 `none`) |
| `findmatches(무늬, 글자)` | 맞은 조각 **전부** (없으면 `[]`) |
| `findgroups(무늬, 글자)` | **괄호로 묶은** 부분들 (없으면 `none`) |
| `replacematch(무늬, 새것, 글자)` | 맞은 곳을 모두 바꾸기 |
| `splitmatch(무늬, 글자)` | 무늬가 나오는 자리마다 자르기 |

```
print(hasmatch("ERROR", 줄))                       // true / false
print(findmatch("\d+", "주문 1234 번"))             // 1234
print(findmatches("\d+", "a1 bb22"))               // ["1", "22"]
print(findgroups("(\d+)-(\d+)", "010-1234"))       // ["010", "1234"]
print(replacematch("\d", "#", "a1b22"))            // a#b##
print(replacematch("(\w+)@(\w+)", "\2/\1", "kim@mail"))   // mail/kim
print(splitmatch(",\s*", "가, 나,다"))              // ["가", "나", "다"]
```

무늬에 쓸 수 있는 것:

| 적는 법 | 뜻 |
|---------|-----|
| `가` `a` `1` | 그 글자 그대로 |
| `.` | 아무 글자 하나 (줄바꿈은 빼고) |
| `[abc]` `[^abc]` `[a-z0-9]` `[가-힣]` | 이 중 하나 / 이것 빼고 |
| `\d` `\w` `\s` | 숫자 / 글자·숫자·밑줄 / 공백 |
| `\D` `\W` `\S` | 그 반대 |
| `^` `$` | 줄의 처음 / 끝 |
| `(...)` | 묶어서 담기 (`findgroups` 로 꺼냄) |
| `(?:...)` | 묶기만 하고 **담지 않기** |
| `a\|b` | a 또는 b |
| `*` `+` `?` | 0번 이상 / 1번 이상 / 있어도 없어도 |
| `{3}` `{2,}` `{2,5}` | 딱 3번 / 2번 이상 / 2~5번 |
| `*?` `+?` `??` | 뒤에 `?` 를 붙이면 **게으르게** (가장 짧게) |
| `\.` `\*` `\\` | 무늬 글자를 글자 그대로 |

- **`\` 는 문자열 안에서도 이스케이프**라서, 큰따옴표 안에서는 `"\\d+"` 처럼 두 번 적거나 `'\d+'` 작은따옴표를 쓰세요.
- 글자 하나하나가 **코드포인트** 단위라 `[가-힣]` 이 그대로 됩니다.
- `replacematch` 의 새 글자 안에서 `\1` `\2` 는 그 묶음으로 바뀝니다.
- 되돌아가며 맞춰 보는 방식이라, `(a+)+b` 처럼 되풀이가 겹친 무늬에 긴 글자를 물리면 오래 걸립니다. 한도를 넘으면 **틀린 답 대신 오류로 알려 줍니다.**
> 전체 예시는 `examples/toolbox.lumi` 를 참고하세요.

### 다른 프로그램 부르기 (`shell` · `shellcode`)
```
val 나온것 = shell("git status")     // 그 프로그램이 찍은 글자 (표준오류까지)
val 코드 = shellcode("git diff")     // 종료 코드만 (성공이면 0)
```
- 명령은 **셸에 그대로** 넘어갑니다. 사용자가 적은 글자를 그대로 이어 붙이지 마세요.
- 이름이 `run` 이 아닌 까닭: `run` 은 사람들이 자기 함수 이름으로 아주 흔히 쓰는 낱말이라, 내장으로 삼으면 남의 `run()` 을 조용히 가립니다.

### 벡터 산술 (`add` · `sub` · `mul` · `div` · `mod` · `pow` · `neg`)
`+`/`-`/`*` 는 리스트끼리 쓰면 '이어 붙이기·되풀이'가 됩니다. 리스트 속 숫자를 **자리마다 짝지어** 계산하고 싶을 때 이 가족을 씁니다.

- **숫자 × 숫자** → 그냥 계산
- **시퀀스 × 숫자** → 모든 원소에 그 숫자를 적용
- **시퀀스 × 시퀀스** → 같은 자리끼리 (짧은 쪽 길이에 맞춥니다)

```
print(add(3, 5))                       // 8
print(add([1, 2, 3], 10))              // [11, 12, 13]
print(add([1, 2], [10, 20]))           // [11, 22]

print(sub([10, 20, 30], 5))            // [5, 15, 25]
print(mul([1, 2, 3], 2))               // [2, 4, 6]
print(div([10, 20, 30], 10))           // [1, 2, 3]  (결과는 언제나 실수)
print(mod([10, 11, 12], 3))            // [1, 2, 0]
print(pow([2, 3, 4], 2))               // [4, 9, 16]
print(neg([1, -2, 3]))                 // [-1, 2, -3]

// 점수에 가중치를 곱하고 보너스를 더하기
val 점수 = [80, 90, 70]
val 가중치 = [1.1, 1.0, 1.2]
print(add(mul(점수, 가중치), 5))          // [93, 95, 89]
```
> 전체 예시는 `examples/vecmath.lumi` 를 참고하세요.

---

> 이 외에도 `print` · `input` · `type` · `len` · `eval` 등 기본 함수부터 리스트/딕셔너리/문자열/파일 함수까지 많은 내장 함수가 있습니다. 위쪽 각 단원에서 이미 설명했고, **자세한 설명과 전체 목록은 [Lumi 사용설명서.md](Lumi%20사용설명서.md)** 의 내장 함수 단원을 참고하세요.

---

### 예약된 낱말
아래 낱말은 문법이 쓰므로 변수·함수 이름으로 쓸 수 없습니다.
```
val  global  local  if  elif  else  while  for  in  switch  case  default
func  return  class  from  this  super  abstract  bring  up  use
break  continue  and  or  not  true  false
try  catch  safe  always  error  test
```
내장 함수 이름(`print` `len` `sort` `each` `args` `assert` …)은 예약어는 아니지만 **내장 쪽이 먼저 불립니다.** 같은 이름으로 내 함수를 만들면 그 함수가 안 불리니 다른 이름을 쓰세요.

## 규칙 요약
- 문장 끝에 세미콜론을 쓰지 않습니다.
- 블록은 `:` 뒤에 줄을 바꾸고 **한 단계 들여쓰기** 합니다 (보통 공백 4칸).
- 변수는 쓰기 전에 `val` 으로 먼저 만들어야 합니다.

## 더 만들어볼 것들 (아이디어)
- 문자열 함수 더 늘리기 (`find`, `startswith` 등 — `upper`/`lower`/`strip`/`split`/`join`/`replace`/`format`/`sort`/`encode` 는 이미 있어요)
- 추상 메서드(`abstract func`) · 연산자 재정의(`add`/`sub`/`mul`/`div`/`mod`/`lt`/`gt`/`get`/`set`) · `text`/`equals` 특수 메서드는 **이미 구현되었습니다** — 객체지향 단원을 참고하세요.

