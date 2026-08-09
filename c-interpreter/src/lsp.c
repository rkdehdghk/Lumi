/* lsp.c — Lumi 언어 서버 (Language Server Protocol)
 * ==========================================================
 *   lumi lsp
 * 로 켜면 표준입출력으로 편집기(VS Code, JetBrains 등)와 이야기합니다.
 * 덕분에 Lumina IDE 가 아니어도 Lumi 를 쓸 수 있습니다.
 *
 * 해 주는 일:
 *   1) 문법 오류 알려 주기  — 파일이 바뀔 때마다 렉서·파서를 돌려 그 자리를 짚어 줍니다
 *   2) 자동완성            — 내장 낱말(lumiwords.h) + 그 파일이 만든 func/class/val
 *   3) 마우스 올렸을 때 설명 — 내장 낱말의 쓰는 형식
 *   4) 파일 안 목록        — func / class 를 훑어 보여 줍니다
 *
 * 주고받는 글은 LSP 규칙대로 "Content-Length: N\r\n\r\n{json}" 입니다.
 * JSON 은 인터프리터가 이미 갖고 있는 것을 그대로 씁니다 (또 만들지 않습니다).
 */
#include "lumi.h"
#include "lumiwords.h"
#include "platform.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ============================================================
 * 1. 열어 둔 파일들
 * ============================================================ */

typedef struct {
    char *uri;
    char *text;
} Doc;

static Doc  *g_docs;
static size_t g_ndocs, g_cdocs;

static Doc *doc_find(const char *uri)
{
    for (size_t i = 0; i < g_ndocs; i++)
        if (strcmp(g_docs[i].uri, uri) == 0) return &g_docs[i];
    return NULL;
}

static void doc_set(const char *uri, const char *text)
{
    Doc *d = doc_find(uri);
    if (d) { free(d->text); d->text = xstrdup(text); return; }
    if (g_ndocs == g_cdocs) {
        g_cdocs = g_cdocs ? g_cdocs * 2 : 8;
        g_docs = (Doc *)xrealloc(g_docs, g_cdocs * sizeof(Doc));
    }
    g_docs[g_ndocs].uri = xstrdup(uri);
    g_docs[g_ndocs].text = xstrdup(text);
    g_ndocs++;
}

static void doc_drop(const char *uri)
{
    for (size_t i = 0; i < g_ndocs; i++) {
        if (strcmp(g_docs[i].uri, uri) != 0) continue;
        free(g_docs[i].uri);
        free(g_docs[i].text);
        g_docs[i] = g_docs[--g_ndocs];
        return;
    }
}

/* ============================================================
 * 2. JSON 드나들기 (인터프리터 것을 빌려 씁니다)
 * ============================================================ */

static Interp *g_in;      /* JSON 을 읽고 쓰려고 하나만 만들어 둡니다 */

static void lsp_out(const char *text) { (void)text; }   /* 인터프리터가 찍는 것은 버립니다 */
static char *lsp_in(const char *prompt) { (void)prompt; return NULL; }

/* Value 딕셔너리에서 이름으로 꺼내기 (없으면 NONE) */
static Value obj_get(Value v, const char *key)
{
    if (v.kind != V_DICT) return NONE_VAL;
    Value k = str_value(key);
    Value *found = dict_find(AS_DICT(v), k);
    release(k);
    return found ? *found : NONE_VAL;
}

static const char *obj_str(Value v, const char *key, char **owner)
{
    Value f = obj_get(v, key);
    if (f.kind != V_STR) { *owner = NULL; return NULL; }
    *owner = str_to_utf8(AS_STR(f));
    return *owner;
}

static long obj_int(Value v, const char *key, long dflt)
{
    Value f = obj_get(v, key);
    if (f.kind == V_INT) return (long)f.as.i;
    if (f.kind == V_FLOAT) return (long)f.as.f;
    return dflt;
}

static void dict_put(Dict *d, const char *key, Value v)
{
    dict_set(d, str_value(key), v);
}

/* 만든 JSON 값을 LSP 봉투에 담아 내보냅니다 */
static void send_value(Value v)
{
    Value txt = lumi_json_stringify_value(g_in, v);
    char *u = str_to_utf8(AS_STR(txt));
    release(txt);
    printf("Content-Length: %zu\r\n\r\n%s", strlen(u), u);
    fflush(stdout);
    free(u);
}

static Value msg_new(void)
{
    Dict *d = dict_new();
    dict_put(d, "jsonrpc", str_value("2.0"));
    return obj_val(d);
}

static void send_result(Value id, Value result)
{
    Value m = msg_new();
    dict_put(AS_DICT(m), "id", retain(id));
    dict_put(AS_DICT(m), "result", result);
    send_value(m);
    release(m);
}

static void send_notify(const char *method, Value params)
{
    Value m = msg_new();
    dict_put(AS_DICT(m), "method", str_value(method));
    dict_put(AS_DICT(m), "params", params);
    send_value(m);
    release(m);
}

/* ============================================================
 * 3. uri <-> 파일 경로
 * ============================================================ */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* file:///c%3A/a/b.lumi -> c:/a/b.lumi  (malloc) */
static char *uri_to_path(const char *uri)
{
    const char *p = uri;
    if (strncmp(p, "file://", 7) == 0) p += 7;
    if (*p == '/' && p[1] && p[2] == ':') p++;      /* /c:/... -> c:/... */
    char *out = (char *)xmalloc(strlen(p) + 1);
    size_t j = 0;
    for (size_t i = 0; p[i]; i++) {
        if (p[i] == '%' && hexval(p[i + 1]) >= 0 && hexval(p[i + 2]) >= 0) {
            out[j++] = (char)(hexval(p[i + 1]) * 16 + hexval(p[i + 2]));
            i += 2;
        } else {
            out[j++] = p[i];
        }
    }
    out[j] = 0;
    return out;
}

/* ============================================================
 * 4. 문법 오류 알려 주기
 * ============================================================ */

static Value range_of(long line, long from, long to)
{
    Dict *a = dict_new(); dict_put(a, "line", int_val(line)); dict_put(a, "character", int_val(from));
    Dict *b = dict_new(); dict_put(b, "line", int_val(line)); dict_put(b, "character", int_val(to));
    Dict *r = dict_new();
    dict_put(r, "start", obj_val(a));
    dict_put(r, "end", obj_val(b));
    return obj_val(r);
}

/* 그 줄이 몇 글자인지 (오류 표시를 줄 끝까지 긋습니다) */
static long line_len(const char *text, long line)
{
    long cur = 0;
    const char *p = text;
    while (cur < line && *p) { if (*p == '\n') cur++; p++; }
    long n = 0;
    while (p[n] && p[n] != '\n') n++;
    return n;
}

/* lint 가 찾은 것 하나를 경고 진단으로 담습니다 */
typedef struct { Seq *list; const char *text; } LintSink;

static void lint_into_list(void *ud, int line, const char *msg)
{
    LintSink *s = (LintSink *)ud;
    Seq *list = s->list;
    long l = line > 0 ? line - 1 : 0;            /* LSP 는 0 부터 셉니다 */
    long n = line_len(s->text, l);
    Dict *d = dict_new();
    dict_put(d, "range", range_of(l, 0, n > 0 ? n : 1));
    dict_put(d, "severity", int_val(2));         /* 2 = Warning */
    dict_put(d, "source", str_value("lumi lint"));
    dict_put(d, "message", str_value(msg));
    seq_push(list, obj_val(d));
}

static void publish_diagnostics(const char *uri, const char *text)
{
    Seq *list = seq_new(V_LIST);

    /* lint 가 렉서·파서를 돌려 줍니다.  -1 이면 문법 오류라 못 읽은 것입니다. */
    LintSink sink = { list, text };
    if (lint_source(text, lint_into_list, &sink) >= 0) {
        /* 문법은 멀쩡 — 담긴 것은 lint 경고뿐입니다 */
    } else {
        long line = lumi_err_line > 0 ? lumi_err_line - 1 : 0;   /* LSP 는 0 부터 셉니다 */
        long n = line_len(text, line);
        Dict *d = dict_new();
        dict_put(d, "range", range_of(line, 0, n > 0 ? n : 1));
        dict_put(d, "severity", int_val(1));                     /* 1 = Error */
        dict_put(d, "source", str_value("lumi"));
        /* 메시지 앞의 "[line N] " 은 편집기가 이미 자리를 아니 떼어 냅니다 */
        const char *msg = lumi_err_msg;
        const char *cut = strstr(msg, "] ");
        if (cut && msg[0] == '[') msg = cut + 2;
        dict_put(d, "message", str_value(msg));
        seq_push(list, obj_val(d));
    }

    Dict *p = dict_new();
    dict_put(p, "uri", str_value(uri));
    dict_put(p, "diagnostics", obj_val(list));
    send_notify("textDocument/publishDiagnostics", obj_val(p));
}

/* ============================================================
 * 5. 자동완성 · 설명 · 파일 안 목록
 * ============================================================ */

/* LSP 의 CompletionItemKind */
static int kind_of(LumiWordKind k)
{
    switch (k) {
    case LW_KEYWORD: return 14;   /* Keyword  */
    case LW_TYPE:    return 7;    /* Class    */
    case LW_CAST:    return 3;    /* Function */
    default:         return 3;    /* Function */
    }
}

static const char *kind_label(LumiWordKind k)
{
    switch (k) {
    case LW_KEYWORD: return "keyword";
    case LW_TYPE:    return "type";
    case LW_CAST:    return "cast";
    default:         return "builtin";
    }
}

/* 그 파일이 만든 이름들(func/class/val)을 훑어 담습니다.
 * 파서를 다시 돌리지 않고 글자만 훑습니다 — 오류가 있는 파일에서도 되게 하려고요. */
static void add_file_symbols(const char *text, Seq *out, bool as_symbol)
{
    const char *p = text;
    long line = 0;
    while (*p) {
        const char *bol = p;
        while (*p == ' ' || *p == '\t') p++;
        const char *kw = NULL;
        int klen = 0;
        if (strncmp(p, "func ", 5) == 0)       { kw = "func";  klen = 5; }
        else if (strncmp(p, "class ", 6) == 0) { kw = "class"; klen = 6; }
        else if (strncmp(p, "val ", 4) == 0 && !as_symbol) { kw = "val"; klen = 4; }
        if (kw) {
            const char *n = p + klen;
            while (*n == ' ') n++;
            const char *e = n;
            while (*e && *e != '(' && *e != ' ' && *e != ':' && *e != '=' && *e != '\n') e++;
            if (e > n) {
                char *nm = (char *)xmalloc((size_t)(e - n) + 1);
                memcpy(nm, n, (size_t)(e - n));
                nm[e - n] = 0;
                Dict *d = dict_new();
                if (as_symbol) {
                    dict_put(d, "name", str_value(nm));
                    dict_put(d, "kind", int_val(strcmp(kw, "class") == 0 ? 5 : 12)); /* Class/Function */
                    Value r = range_of(line, 0, line_len(text, line));
                    dict_put(d, "range", retain(r));
                    dict_put(d, "selectionRange", r);
                } else {
                    dict_put(d, "label", str_value(nm));
                    dict_put(d, "kind", int_val(strcmp(kw, "class") == 0 ? 7 : 3));
                    dict_put(d, "detail", str_value(strcmp(kw, "class") == 0
                                                    ? "class in this file" : "in this file"));
                }
                seq_push(out, obj_val(d));
                free(nm);
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') { p++; line++; }
        if (p == bol) break;
    }
}

static Value make_completions(const char *text)
{
    Seq *items = seq_new(V_LIST);
    for (size_t i = 0; i < LUMI_WORD_COUNT; i++) {
        Dict *d = dict_new();
        dict_put(d, "label", str_value(LUMI_WORDS[i].name));
        dict_put(d, "kind", int_val(kind_of(LUMI_WORDS[i].kind)));
        dict_put(d, "detail", str_value(LUMI_WORDS[i].hint));
        char *doc = xsprintf("%s %s%s", LUMI_WORDS[i].name,
                             kind_label(LUMI_WORDS[i].kind),
                             LUMI_WORDS[i].hint[0] ? "" : "");
        dict_put(d, "documentation", str_value(doc));
        free(doc);
        seq_push(items, obj_val(d));
    }
    if (text) add_file_symbols(text, items, false);
    return obj_val(items);
}

/* 그 자리에 놓인 낱말을 떼어 냅니다 */
static char *word_at(const char *text, long line, long ch)
{
    long cur = 0;
    const char *p = text;
    while (cur < line && *p) { if (*p == '\n') cur++; p++; }
    long n = 0;
    while (p[n] && p[n] != '\n') n++;
    if (ch < 0 || ch > n) return NULL;

    /* 글자·숫자·밑줄·한글이면 이름의 일부로 봅니다 (바이트 단위라 한글도 이어집니다) */
    #define ISNAME(c) (((unsigned char)(c) >= 0x80) || (c) == '_' || \
                       ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || \
                       ((c) >= '0' && (c) <= '9'))
    long a = ch, b = ch;
    while (a > 0 && ISNAME(p[a - 1])) a--;
    while (b < n && ISNAME(p[b])) b++;
    #undef ISNAME
    if (b <= a) return NULL;
    char *w = (char *)xmalloc((size_t)(b - a) + 1);
    memcpy(w, p + a, (size_t)(b - a));
    w[b - a] = 0;
    return w;
}

static Value make_hover(const char *text, long line, long ch)
{
    char *w = word_at(text, line, ch);
    if (!w) return NONE_VAL;
    Value out = NONE_VAL;
    for (size_t i = 0; i < LUMI_WORD_COUNT; i++) {
        if (strcmp(LUMI_WORDS[i].name, w) != 0) continue;
        char *md = xsprintf("**%s** — %s\n\n```\n%s%s\n```",
                            LUMI_WORDS[i].name, kind_label(LUMI_WORDS[i].kind),
                            LUMI_WORDS[i].name, LUMI_WORDS[i].hint);
        Dict *c = dict_new();
        dict_put(c, "kind", str_value("markdown"));
        dict_put(c, "value", str_value(md));
        free(md);
        Dict *h = dict_new();
        dict_put(h, "contents", obj_val(c));
        out = obj_val(h);
        break;
    }
    free(w);
    return out;
}

/* ============================================================
 * 6. 주고받기
 * ============================================================ */

/* 한 덩어리를 읽어 옵니다 (Content-Length 만큼).  끝이면 NULL. */
static char *read_message(void)
{
    char line[512];
    long len = -1;
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) return NULL;
        if (line[0] == '\r' || line[0] == '\n') break;          /* 머리말 끝 */
        if (strncmp(line, "Content-Length:", 15) == 0) len = strtol(line + 15, NULL, 10);
    }
    if (len <= 0) return NULL;
    char *buf = (char *)xmalloc((size_t)len + 1);
    size_t got = fread(buf, 1, (size_t)len, stdin);
    buf[got] = 0;
    return buf;
}

static Value server_capabilities(void)
{
    Dict *sync = dict_new();
    dict_put(sync, "openClose", bool_val(true));
    dict_put(sync, "change", int_val(1));            /* 1 = 통째로 다시 보냄 */

    Seq *trig = seq_new(V_LIST);
    seq_push(trig, str_value("."));
    Dict *comp = dict_new();
    dict_put(comp, "triggerCharacters", obj_val(trig));

    Dict *cap = dict_new();
    dict_put(cap, "textDocumentSync", obj_val(sync));
    dict_put(cap, "completionProvider", obj_val(comp));
    dict_put(cap, "hoverProvider", bool_val(true));
    dict_put(cap, "documentSymbolProvider", bool_val(true));

    Dict *info = dict_new();
    dict_put(info, "name", str_value("lumi-lsp"));
    dict_put(info, "version", str_value(LUMI_VERSION));

    Dict *res = dict_new();
    dict_put(res, "capabilities", obj_val(cap));
    dict_put(res, "serverInfo", obj_val(info));
    return obj_val(res);
}

int run_lsp(void)
{
    /* 편집기와 이야기하는 통로라 인터프리터가 찍는 글은 모두 버립니다 */
    g_in = interp_new(lsp_out, lsp_in, ".");

    for (;;) {
        char *body = read_message();
        if (!body) break;

        /* JSON 이 깨졌으면 그 덩어리만 버리고 이어 갑니다 */
        Value msg = NONE_VAL;
        if (setjmp(lumi_jmp) == 0) {
            Value txt = str_value(body);
            msg = lumi_json_parse_value(g_in, txt);
            release(txt);
        } else {
            free(body);
            continue;
        }
        free(body);

        char *mown = NULL;
        const char *method = obj_str(msg, "method", &mown);
        Value id = obj_get(msg, "id");
        Value params = obj_get(msg, "params");

        if (!method) { free(mown); release(msg); continue; }

        if (strcmp(method, "initialize") == 0) {
            send_result(id, server_capabilities());

        } else if (strcmp(method, "shutdown") == 0) {
            send_result(id, NONE_VAL);

        } else if (strcmp(method, "exit") == 0) {
            free(mown); release(msg);
            break;

        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            Value td = obj_get(params, "textDocument");
            char *uo = NULL, *to = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            const char *text = obj_str(td, "text", &to);
            if (uri && text) { doc_set(uri, text); publish_diagnostics(uri, text); }
            free(uo); free(to);

        } else if (strcmp(method, "textDocument/didChange") == 0) {
            Value td = obj_get(params, "textDocument");
            char *uo = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            Value changes = obj_get(params, "contentChanges");
            if (uri && changes.kind == V_LIST && AS_SEQ(changes)->len > 0) {
                Value last = AS_SEQ(changes)->items[AS_SEQ(changes)->len - 1];
                char *to = NULL;
                const char *text = obj_str(last, "text", &to);
                if (text) { doc_set(uri, text); publish_diagnostics(uri, text); }
                free(to);
            }
            free(uo);

        } else if (strcmp(method, "textDocument/didClose") == 0) {
            Value td = obj_get(params, "textDocument");
            char *uo = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            if (uri) {
                doc_drop(uri);
                Dict *p = dict_new();
                dict_put(p, "uri", str_value(uri));
                dict_put(p, "diagnostics", obj_val(seq_new(V_LIST)));
                send_notify("textDocument/publishDiagnostics", obj_val(p));
            }
            free(uo);

        } else if (strcmp(method, "textDocument/completion") == 0) {
            Value td = obj_get(params, "textDocument");
            char *uo = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            Doc *d = uri ? doc_find(uri) : NULL;
            send_result(id, make_completions(d ? d->text : NULL));
            free(uo);

        } else if (strcmp(method, "textDocument/hover") == 0) {
            Value td = obj_get(params, "textDocument");
            Value pos = obj_get(params, "position");
            char *uo = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            Doc *d = uri ? doc_find(uri) : NULL;
            Value h = d ? make_hover(d->text, obj_int(pos, "line", 0),
                                     obj_int(pos, "character", 0))
                        : NONE_VAL;
            send_result(id, h);
            free(uo);

        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            Value td = obj_get(params, "textDocument");
            char *uo = NULL;
            const char *uri = obj_str(td, "uri", &uo);
            Doc *d = uri ? doc_find(uri) : NULL;
            Seq *out = seq_new(V_LIST);
            if (d) add_file_symbols(d->text, out, true);
            send_result(id, obj_val(out));
            free(uo);

        } else if (id.kind != V_NONE) {
            /* 모르는 물음에는 빈 답을 줍니다 (편집기가 기다리지 않도록) */
            send_result(id, NONE_VAL);
        }

        free(mown);
        release(msg);
    }
    return 0;
}
