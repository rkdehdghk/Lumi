# Lumi — VS Code 확장

Lumina IDE 가 아니어도 **VS Code 에서 Lumi 를 쓸 수 있게** 해 주는 얇은 껍데기입니다.
진짜 일은 전부 `lumi lsp`(언어 서버)와 `lumi dap`(디버거)이 하고, 이 확장은 그것을 켜서 이어 주기만 합니다.

## 해 주는 일

| 무엇 | 설명 |
|------|------|
| **문법 오류 표시** | 타이핑하는 대로 렉서·파서를 돌려 틀린 줄에 빨간 밑줄 |
| **자동완성** | 내장 낱말 160개 + 그 파일이 만든 `func`/`class`/`val` |
| **마우스 올렸을 때 설명** | 내장 함수의 쓰는 형식 |
| **파일 안 목록** | `func`/`class` 를 Outline 에 표시 (Ctrl+Shift+O) |
| **정의로 가기** | **F12** — 그 이름을 만든 자리로 (`func`/`class`/`val`/매개변수/`bring ... up`) |
| **쓴 곳 모두 찾기** | **Shift+F12** — 그 파일 안에서 그 이름이 나오는 자리 전부 |
| **이름 바꾸기** | **F2** — 만드는 자리가 딱 하나인 이름만 (여럿이면 거절합니다) |
| **색칠** | 문법 낱말·자료형·내장 함수·문자열·주석·숫자 |
| **디버깅** | 중단점 · 한 줄씩 · 호출 자취 · 변수 보기 (`lumi dap`) |

## 넣는 법

1. **`lumi` 를 PATH 에 두거나**, 설정에서 자리를 알려 줍니다.
   - `설정 → 확장 → Lumi → Lumi: Path` 에 `C:\...\c-interpreter\bin\lumi.exe` 처럼 적습니다.
   - 저장소의 `add-path.bat` 을 쓰면 PATH 에 들어갑니다.
2. 이 폴더(`editors/vscode-lumi`)를 통째로 VS Code 확장 폴더에 복사합니다.
   - 윈도우: `%USERPROFILE%\.vscode\extensions\lumi-lang-1.0.0`
   - 리눅스·맥: `~/.vscode/extensions/lumi-lang-1.0.0`
3. VS Code 를 다시 켭니다.
4. `.lumi` 파일을 열면 바로 됩니다. 줄 번호 왼쪽을 눌러 중단점을 찍고 **F5** 를 누르면 디버깅이 시작됩니다 (`launch.json` 없이도 됩니다).

> `npm install` 이 필요 없습니다. 받아 오는 꾸러미가 하나도 없습니다.

## 잘 안 될 때

| 증상 | 볼 곳 |
|------|-------|
| 아무 일도 안 일어남 | `lumi lsp` 가 터미널에서 켜지는지 확인 (Ctrl+C 로 끕니다) |
| "cannot start lumi lsp" | 설정의 `lumi.path` 를 절대 경로로 |
| 색은 나오는데 오류 표시가 없음 | VS Code 의 **출력 → Lumi** 와 개발자 도구 콘솔 확인 |

## 문법 색칠 파일 다시 만들기

낱말 표는 `c-interpreter/src/lumiwords.h` **한 곳에만** 적습니다.
언어에 이름이 늘면 거기에 한 줄 넣고 이걸 한 번 돌리세요.

```bash
python make-grammar.py
```

`syntaxes/lumi.tmLanguage.json` 이 다시 만들어집니다. 손으로 고치지 마세요 — 다음 번에 덮어써집니다.

## 다른 편집기

`lumi lsp` 는 표준 LSP, `lumi dap` 은 표준 DAP 라서 그것을 아는 편집기면 어디서나 붙습니다.

- **Neovim**: `vim.lsp.start({ name = "lumi", cmd = { "lumi", "lsp" } })`
- **Helix** (`languages.toml`):
  ```toml
  [language-server.lumi]
  command = "lumi"
  args = ["lsp"]

  [[language]]
  name = "lumi"
  file-types = ["lumi"]
  language-servers = ["lumi"]
  ```
- **JetBrains**: LSP4IJ 플러그인에 `lumi lsp` 를 등록

디버거 쪽은 어댑터 명령이 `lumi dap`, 시작 설정이
`{ "type": "lumi", "request": "launch", "program": "경로.lumi" }` 입니다.
`stopOnEntry: true` 를 주면 첫 줄에서 멈춥니다.

**한계 두 가지**: 돌고 있는 도중의 '멈춤'(pause)은 안 되고(멈춰 있을 때만 듣습니다),
프로그램의 `input()` 은 EOF 입니다(표준입력이 편집기와 이야기하는 통로라서).
