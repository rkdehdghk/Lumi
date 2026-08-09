# tests/dap/session.txt 를 만듭니다 (한 번 만들어 두면 시험은 파이썬 없이 돕니다).
#   python make_session.py
import io, json, os
HERE = os.path.dirname(os.path.abspath(__file__))

msgs = [
  {"seq":1,"type":"request","command":"initialize","arguments":{"adapterID":"lumi"}},
  {"seq":2,"type":"request","command":"setBreakpoints",
   "arguments":{"source":{"path":"prog.lumi"},"breakpoints":[{"line":5}]}},
  {"seq":3,"type":"request","command":"configurationDone","arguments":{}},
  {"seq":4,"type":"request","command":"launch","arguments":{"program":"prog.lumi"}},
  # 여기서 프로그램이 5 번째 줄에서 멈춥니다
  {"seq":5,"type":"request","command":"threads","arguments":{}},
  {"seq":6,"type":"request","command":"stackTrace","arguments":{"threadId":1}},
  {"seq":7,"type":"request","command":"scopes","arguments":{"frameId":0}},
  {"seq":8,"type":"request","command":"variables","arguments":{"variablesReference":2}},
  {"seq":9,"type":"request","command":"evaluate","arguments":{"expression":"수 + 100","frameId":0}},
  {"seq":10,"type":"request","command":"stepIn","arguments":{"threadId":1}},
  # 이제 두배() 안입니다
  {"seq":11,"type":"request","command":"stackTrace","arguments":{"threadId":1}},
  {"seq":12,"type":"request","command":"scopes","arguments":{"frameId":1}},
  {"seq":13,"type":"request","command":"variables","arguments":{"variablesReference":1}},
  {"seq":14,"type":"request","command":"continue","arguments":{"threadId":1}},
]
out = b""
for m in msgs:
    body = json.dumps(m, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    out += b"Content-Length: %d\r\n\r\n" % len(body) + body
io.open(os.path.join(HERE, "session.txt"), "wb").write(out)
print("session.txt:", len(out), "bytes")
