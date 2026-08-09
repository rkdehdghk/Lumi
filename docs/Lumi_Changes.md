# Lumi 바뀐 것들 (Changes)

지금은 **0.x** 입니다 — 문법과 시맨틱이 아직 바뀔 수 있습니다. 대신 **깨는 변경은 반드시 여기에 적습니다.** 무엇이 어떻게 바뀌었고, 예전 코드를 어떻게 고치면 되는지까지.

규칙은 [`Lumi_Language_Spec.md` 7 장](Lumi_Language_Spec.md#7-버전-정책-versioning-policy) 에 있습니다.

| 표시 | 뜻 |
|---|---|
| 💥 | **깨는 변경** — 예전 코드를 고쳐야 합니다 |
| ✨ | 새로 생긴 것 |
| 🔧 | 고침 (예전 코드는 그대로 돕니다) |

---

## 0.9.1 — 2026-08-10

### 🔧 고침

**CRLF 로 저장한 `.lumi` 가 안 열리던 것.** 렉서가 빈 줄을 줄바꿈 글자 하나(`\n`)로만 알아봐서, 윈도우 줄 끝(CRLF)으로 저장한 파일에서는 **블록 안의 빈 줄이 '빈 줄'로 안 보였습니다.** 그 줄의 칸 수가 0 으로 읽혀 열려 있던 블록이 통째로 닫히고, 그다음 줄에서 문법 오류가 났습니다.

```
func f():
    val a = 1
                  <- 이 빈 줄이 CRLF 면 여기서 함수가 끝나 버렸습니다
    return a
```

실제로 `libraries/sqlite.lumi` 가 이렇게 깨져 **`bring sqlite` 가 통째로 안 됐습니다.** 예제·라이브러리가 모두 LF 로 저장돼 있어서 시험이 한 번도 못 잡았습니다.

- 고친 곳: `src/lexer.c` 의 빈 줄 판정에 `''` 추가.
- 회귀 시험: `tests/crlf/blankline.lumi` — **일부러 CRLF 로 저장한** 파일이고, `run.bat`·`run.sh` 둘 다 이것이 파싱되는지 봅니다. `.gitattributes` 의 `* -text` 가 그 줄 끝을 지켜 줍니다.
- 예전 코드는 그대로 돕니다. LF 파일의 동작은 한 글자도 안 달라집니다.

### ✨ 새로 생긴 것

- `tests/run.sh` — `run.bat` 의 POSIX 쌍둥이. 여섯 가지를 같은 차례로 검사합니다.
- `.github/workflows/ci.yml` — 인터프리터를 우분투·맥·윈도우에서, IDE 를 우분투(Qt6)에서.

---

## 0.9.0 — 2026-08-09

첫 판 번호입니다. 그전까지는 번호 없이 `1.0.0` 이라고만 적혀 있었는데, 그 번호가 약속하는 하위 호환을 지킬 수 없는 상태였습니다. **0.9.0 으로 내려서 "아직 안 얼었다"를 솔직하게 밝힙니다.** (`1.0` 이 되면 켜지는 약속은 명세 7.3 절에 적어 두었습니다.)

### 💥 깨는 변경

**딕셔너리 키 동일성이 되돌아왔습니다** — `1` 과 `true` 는 다른 키입니다.

```
val d = {1: "정수", true: "참"}
len(d)        // 예전(잘못): 1     지금(맞음): 2
```

원래 정한 규칙이었는데 파이썬→C 다시 쓰기 과정에서 잃어버렸던 것을 되살렸습니다. `1 == true` 는 여전히 참입니다 — **값으로 견주는 것**과 **키로 쓰는 것**은 다른 규칙입니다. 딕셔너리에 숫자와 참/거짓을 섞어 키로 쓰고 있었다면 칸 수가 달라집니다.

**`run` 내장 함수가 `shell` 로 이름이 바뀌었습니다.**

```
run("dir")        ->  shell("dir")
runcode("dir")    ->  shellcode("dir")
```

`run` 은 사람들이 자기 함수 이름으로 흔히 쓰는 낱말이라, 내장으로 두면 `func run()` 이 **말없이 가려집니다** (명세 5.2). 실제로 예제 하나가 그렇게 깨졌습니다. 앞으로 새 내장 이름을 지을 때는 흔한 낱말을 피합니다.

**미완성 바이트코드 VM 을 걷어냈습니다.**

`try`/`catch` 가 VM 경로에서 아예 안 돌고 있었습니다 (오류 처리 스택이 둘로 갈려 있었습니다). 걷어내고 트리워크 하나만 남겼습니다. **속도는 같았습니다** (100 만 번 도는 반복 0.20 초 ↔ 0.20 초). 언어를 쓰는 쪽에서 달라지는 것은 `try`/`catch` 가 이제 **제대로 돈다**는 것뿐입니다.

### ✨ 새로 생긴 것

| 무엇 | 어떻게 |
|---|---|
| 폴더로 나눈 프로그램 | `bring "models/user"` · `bring 짧은이름 = "models/user"` |
| 기본값·이름 붙인 인자 | `func f(a, b = 2)` · `f(1, b = 9)` |
| 오류 값과 종류 갈래 | `e.type` `e.message` `e.line` `e.file` · `catch LookupError e:` |
| 호출 자취 | 안 잡힌 오류에 `파일:줄` 이 붙어 나옵니다 |
| 값 바로 부르기 | `목록[0](10)` · `바깥(5)(3)` |
| 터미널 인자 | `args()` |
| 시험 | `test "이름":` + `assert` / `assertsame` + `lumi test` |
| 클래스 반복자 | `iter()` / `next()` |
| 폴더·경로 다루기 | `listdir` `filename` `foldername` `pathjoin` `fullpath` |
| 지문과 옮겨 적기 | `sha256` `md5` `base64` `unbase64` |
| 무늬 찾기 (정규표현식) | `ismatch` `hasmatch` `findmatch` `findmatches` `findgroups` `replacematch` `splitmatch` |
| 다른 프로그램 부르기 | `shell` `shellcode` |
| 웹으로 주고받기 | `bring http` — `get` `post` `put` `delete` `getjson` `postjson` `download` `serve` |
| 언어 서버 | `lumi lsp` + VS Code 확장 |
| 단독 실행 파일 | `lumi build app.lumi` |
| 패키지 잠금 | `lumi.lock` · `lumi pkg lock` |
| **돌리기 전 훑어보기** | `lumi lint` (규칙 6 개, 편집기에도 노란 밑줄) |
| **모양 다듬기** | `lumi fmt` (`--check` 로 CI 에) |
| **언어 명세** | `docs/Lumi_Language_Spec.md` |
| **남의 C 라이브러리 불러 쓰기** | `cload` `cclose` `chas` `ccall` `cbuf` `cfree` `cget` `cput` `ctext` `cbytes` — ⚠️ 위험합니다 |
| **데이터베이스** | `bring sqlite` — `open` `run` `rows` `one` `value` `tx` … 새 오류 종류 `DatabaseError` |

### 🔧 고침

- **인자가 안 맞을 때 `ArgumentError` 를 냅니다.** 그전에는 갈래 표에 `ArgumentError` 를 두고도 **아무도 안 내고 있었습니다** (명세를 적다가 찾았습니다). `catch Error` 로는 예전과 똑같이 잡히므로 깨지지 않습니다. 이제 `catch ArgumentError` 로 좁혀 잡을 수 있습니다.
- **정수 넘침을 검사합니다.** `+ - * **` 가 64 비트를 넘으면 조용히 감기지 않고 `MathError` 를 냅니다.
- **한글 절대 경로에서 패키지를 못 찾던 것**을 고쳤습니다. `pkg.c` 가 플랫폼 계층을 건너뛰고 맨 `fopen` 을 쓰고 있어서, 경로에 한글이 들어가면 `lumi.json` 을 못 읽었습니다.
- **리눅스·맥에서 빌드됩니다.** 운영체제마다 다른 것은 `src/platform.c` 한 곳에 모았습니다. (IDE 는 아직 윈도우 전용입니다.)
- `shell()` 이 내놓는 글이 운영체제마다 달랐던 것을 맞췄습니다.
- HTTP 서버가 한글 주소를 못 알아듣던 것을 고쳤습니다 (퍼센트 적기를 풀어 줍니다).
- 판 번호를 적는 곳을 **`src/lumi.h` 의 `LUMI_VERSION` 한 곳**으로 모았습니다 (셋으로 흩어져 있었습니다).

---

## 그전 — 번호 없던 때

`docs/Lumi_Practical_Features_Roadmap.md` 와 `Lumi 사용설명서.md` 맨 위의 갱신 기록을 보세요. 그때는 바뀐 것을 한곳에 모아 적지 않았습니다 — 이 파일을 만든 까닭이 그것입니다.
