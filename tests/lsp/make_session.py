# tests/lsp/session.txt 를 만듭니다 (한 번 만들어 두면 시험은 파이썬 없이 돕니다).
#   python make_session.py
# prog.lumi 의 줄을 옮겼으면 아래 자리도 같이 고치고 다시 돌리세요.
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, 'prog.lumi'), encoding='utf-8').read()
URI = 'file:///prog.lumi'

# (설명, 방법, 줄, 칸) — 줄과 칸은 0 부터 셉니다 (LSP 규칙)
PROBES = [
    ('area 를 부른 자리에서 정의로',        'definition',    10, 17),
    ('area 를 쓴 곳 모두',                  'references',    10, 17),
    ('limit(전역) 정의로',                  'definition',    10, 29),
    ('Box(클래스) 정의로',                  'definition',    17, 30),
    ('sqrt(bring up 으로 들여온 것) 정의로', 'definition',    11, 21),
    ('print(내장) 은 이 파일에 없음',        'definition',    17, 2),
    ('size 를 쓴 곳 모두',                  'references',    15, 14),
    ('area 는 이름을 바꿀 수 있다',          'prepareRename', 10, 17),
    ('width 는 만드는 자리가 둘이라 안 됨',  'prepareRename', 7, 12),
    ('area -> rect 로 바꾸기',              'rename',        10, 17),
    ('width 바꾸기는 거절',                 'rename',        7, 12),
]

msgs = [
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
    {"jsonrpc": "2.0", "method": "textDocument/didOpen",
     "params": {"textDocument": {"uri": URI, "text": SRC}}},
]
for i, (_, method, line, ch) in enumerate(PROBES, start=10):
    params = {"textDocument": {"uri": URI}, "position": {"line": line, "character": ch}}
    if method == 'references':
        params["context"] = {"includeDeclaration": True}
    if method == 'rename':
        params["newName"] = "rect"
    msgs.append({"jsonrpc": "2.0", "id": i,
                 "method": "textDocument/" + method, "params": params})
msgs.append({"jsonrpc": "2.0", "id": 99, "method": "shutdown", "params": {}})

out = b""
for m in msgs:
    b = json.dumps(m, ensure_ascii=False).encode('utf-8')
    out += b"Content-Length: " + str(len(b)).encode() + b"\r\n\r\n" + b
open(os.path.join(HERE, 'session.txt'), 'wb').write(out)
print("session.txt written -", len(msgs), "messages")
