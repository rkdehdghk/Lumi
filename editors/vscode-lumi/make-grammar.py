# syntaxes/lumi.tmLanguage.json 을 c-interpreter/src/lumiwords.h 에서 만들어 냅니다.
#
#   python make-grammar.py
#
# 낱말 표를 손으로 두 번 적지 않으려는 것입니다. 언어에 이름이 늘면
# lumiwords.h 에 한 줄 넣고 이 스크립트를 한 번 돌리면 끝납니다.
import io, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
WORDS = os.path.join(HERE, "..", "..", "c-interpreter", "src", "lumiwords.h")
OUT = os.path.join(HERE, "syntaxes", "lumi.tmLanguage.json")

src = io.open(WORDS, encoding="utf-8").read()
rows = re.findall(r'\{"([^"]+)",\s*(LW_\w+),', src)

keywords, types, funcs = [], [], []
for name, kind in rows:
    if kind == "LW_KEYWORD":
        keywords.append(name)
    elif kind == "LW_TYPE":
        types.append(name)
    elif kind in ("LW_FUNC", "LW_CAST"):
        funcs.append(name)

def uniq(xs):
    seen, out = set(), []
    for x in xs:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out

keywords, types, funcs = uniq(keywords), uniq(types), uniq(funcs)
# 자료형 이름은 형변환 함수로도 쓰이므로 함수 목록에서 뺍니다 (키워드 색이 이깁니다)
funcs = [f for f in funcs if f not in types and f not in keywords]

grammar = {
    "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
    "name": "Lumi",
    "scopeName": "source.lumi",
    "patterns": [
        {"include": "#comment"},
        {"include": "#string"},
        {"include": "#number"},
        {"include": "#keyword"},
        {"include": "#type"},
        {"include": "#builtin"},
        {"include": "#funcname"},
    ],
    "repository": {
        "comment": {
            "patterns": [
                {"name": "comment.line.double-slash.lumi", "match": "//.*$"},
                {"name": "comment.block.lumi", "begin": "/\\.", "end": "\\./"},
            ]
        },
        "string": {
            "patterns": [
                {"name": "string.quoted.double.lumi", "begin": "\"", "end": "\"",
                 "patterns": [{"name": "constant.character.escape.lumi",
                               "match": "\\\\(u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|x[0-9a-fA-F]{2}|.)"}]},
                {"name": "string.quoted.single.lumi", "begin": "'", "end": "'"},
            ]
        },
        "number": {
            "name": "constant.numeric.lumi",
            "match": "\\b(0[bB][01]+|0[xX][0-9a-fA-F]+|\\d+\\.\\d+|\\d+)\\b"
        },
        "keyword": {
            "name": "keyword.control.lumi",
            "match": "\\b(" + "|".join(sorted(keywords)) + ")\\b"
        },
        "type": {
            "name": "storage.type.lumi",
            "match": "\\b(" + "|".join(sorted(types)) + ")\\b"
        },
        "builtin": {
            "name": "support.function.lumi",
            "match": "\\b(" + "|".join(sorted(funcs, key=len, reverse=True)) + ")\\b"
        },
        "funcname": {
            "match": "\\b(func)\\s+([^\\s(:,]+)",
            "captures": {"1": {"name": "keyword.control.lumi"},
                         "2": {"name": "entity.name.function.lumi"}}
        },
    },
}

os.makedirs(os.path.dirname(OUT), exist_ok=True)
io.open(OUT, "w", encoding="utf-8").write(json.dumps(grammar, ensure_ascii=False, indent=2) + "\n")
print("낱말 %d개(문법 %d / 자료형 %d / 함수 %d)로 문법 파일을 만들었습니다"
      % (len(rows), len(keywords), len(types), len(funcs)))
